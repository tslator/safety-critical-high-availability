#include "safety_crit/shared_memory/shared_region.hpp"

#include <atomic>
#include <cstring>

namespace safety_crit::shared_memory {

bool initialize(SharedRegion& region, std::uint64_t slot_count,
                std::uint32_t slot_bytes) {
    // Validate before mutating anything: the dimensions must match the
    // compiled-in layout, and the platform must support lock-free 64-bit
    // atomics (the ring protocol's assumption; see ring_buffer.hpp).
    if (slot_count != kDefaultSlotCount || slot_bytes != kDefaultSlotBytes) {
        return false;
    }
    std::atomic<std::uint64_t> lock_free_probe{0};
    if (!lock_free_probe.is_lock_free()) {
        return false;
    }

    // Precondition: the storage is freshly mapped and no thread has observed
    // it yet, so relaxed atomic stores below are safe. Plain data and atomics
    // are initialized separately (the region contains std::atomic members, so
    // a single byte fill is not an option without -Wclass-memaccess).
    region.identity.magic = kRegionMagic;
    region.identity.version = kRegionVersion;
    for (std::size_t i = 0; i < kMaxWorkers; ++i) {
        RingBufferHeader& rb = region.ring_buffers[i];
        rb.slot_count = kDefaultSlotCount;
        rb.slot_bytes = static_cast<std::uint32_t>(kDefaultSlotBytes);
        rb.reserved0 = 0;
        rb.reserved1 = 0;
        std::memset(rb.padding, 0, sizeof(rb.padding));

        // RingBuffer::initialize() cannot fail here (lock-freeness was checked
        // above); it sets head_/tail_ to zero and each slot's sequence to its
        // index. Slot payloads are deliberately left untouched: a payload is
        // never read until the slot's sequence marks it committed.
        if (!region.rings[i].initialize()) {
            return false;  // defensive: unreachable when the check above passes
        }
    }
    for (auto& cell : region.worker_status) {
        cell.status.store(0, std::memory_order_relaxed);
    }
    region.global_seq.store(0, std::memory_order_relaxed);
    region.integrity_word.store(0, std::memory_order_relaxed);
    return true;
}

bool verify_identity(const SharedRegion& region) {
    if (region.identity.magic != kRegionMagic || region.identity.version != kRegionVersion) {
        return false;
    }
    for (std::size_t i = 0; i < kMaxWorkers; ++i) {
        const RingBufferHeader& rb = region.ring_buffers[i];
        if (rb.slot_count != kDefaultSlotCount || rb.slot_bytes != kDefaultSlotBytes) {
            return false;
        }
    }
    return true;
}

bool verify_worker_ring(const SharedRegion& region, std::size_t worker_idx) {
    if (worker_idx >= kMaxWorkers) {
        return false;
    }
    // Cross-checks the ring's slot counters against its stored head/tail
    // window (recovery requirement, plan section 3). Quiescence of this
    // worker is the caller's precondition; a spurious false from live traffic
    // means "unverified", not "corrupt".
    return region.rings[worker_idx].verify_consistent();
}

bool verify(const SharedRegion& region) {
    if (!verify_identity(region)) {
        return false;
    }
    for (std::size_t i = 0; i < kMaxWorkers; ++i) {
        if (!verify_worker_ring(region, i)) {
            return false;
        }
    }
    return true;
}

}  // namespace safety_crit::shared_memory

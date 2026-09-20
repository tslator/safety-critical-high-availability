#include "safety_crit/shared_memory/shared_region.hpp"

#include <atomic>
#include <cstring>
#include <limits>

#include "safety_crit/shared_memory/integrity.hpp"

namespace safety_crit::shared_memory {
namespace {

std::uint64_t pack_owner(std::uint32_t physical_owner, std::uint32_t generation) {
    return static_cast<std::uint64_t>(physical_owner) |
           (static_cast<std::uint64_t>(generation) << 32u);
}

std::uint32_t unpack_owner(std::uint64_t word) {
    return static_cast<std::uint32_t>(word & 0xFFFFFFFFu);
}

std::uint32_t unpack_generation(std::uint64_t word) {
    return static_cast<std::uint32_t>(word >> 32u);
}

// The integrity word's definition (DEC-0005 #5): crc32c over the raw
// per-worker ring and ownership header blocks followed by global_seq serialized
// as 8 little-endian bytes (explicit serialization: layout-stable across
// observers and independent of host endianness or in-memory atomic representation).
std::uint32_t region_integrity_at(const SharedRegion& region, std::uint64_t global_seq) {
    std::uint32_t state = crc32c_update(crc32c_init(), region.ring_buffers,
                                        sizeof(region.ring_buffers));
    state = crc32c_update(state, region.ring_ownership, sizeof(region.ring_ownership));
    std::uint8_t seq_bytes[8];
    for (int i = 0; i < 8; ++i) {
        seq_bytes[i] = static_cast<std::uint8_t>(global_seq >> (8 * i));
    }
    state = crc32c_update(state, seq_bytes, sizeof(seq_bytes));
    return crc32c_finalize(state);
}

}  // namespace

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

        RingOwnershipCell& ownership = region.ring_ownership[i];
        ownership.logical_ring = static_cast<std::uint32_t>(i);
        ownership.reserved0 = 0;
        std::memset(ownership.padding, 0, sizeof(ownership.padding));
        ownership.owner_word.store(pack_owner(static_cast<std::uint32_t>(i), 1u),
                                   std::memory_order_relaxed);
        ownership.epoch.store(2u, std::memory_order_relaxed);

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
    // T1.3: seed the integrity word from the just-filled headers and seq 0 so
    // a freshly initialized region is self-consistent before any commit.
    region.integrity_word.store(region_integrity_at(region, 0), std::memory_order_relaxed);
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
        const RingOwnershipCell& ownership = region.ring_ownership[i];
        const std::uint64_t epoch = ownership.epoch.load(std::memory_order_acquire);
        if (ownership.logical_ring != i || epoch == 0u || (epoch & 1u) != 0u) {
            return false;
        }
        const std::uint64_t owner_word = ownership.owner_word.load(std::memory_order_acquire);
        if (unpack_owner(owner_word) >= kMaxWorkers || unpack_generation(owner_word) == 0u) {
            return false;
        }
    }
    return true;
}

bool read_ownership(const SharedRegion& region, std::size_t logical_ring,
                    OwnershipToken& out) {
    if (logical_ring >= kMaxWorkers) {
        return false;
    }
    const RingOwnershipCell& ownership = region.ring_ownership[logical_ring];
    for (;;) {
        const std::uint64_t epoch = ownership.epoch.load(std::memory_order_acquire);
        if ((epoch & 1u) != 0u) {
            return false;
        }
        const std::uint64_t owner_word = ownership.owner_word.load(std::memory_order_acquire);
        if (epoch != ownership.epoch.load(std::memory_order_acquire)) {
            continue;
        }
        const std::uint32_t owner = unpack_owner(owner_word);
        const std::uint32_t generation = unpack_generation(owner_word);
        if (ownership.logical_ring != logical_ring || epoch == 0u || (epoch & 1u) != 0u ||
            owner >= kMaxWorkers ||
            generation == 0u) {
            return false;
        }
        out = OwnershipToken{static_cast<std::uint32_t>(logical_ring), owner, generation, epoch};
        return true;
    }
}

bool ownership_valid(const SharedRegion& region, const OwnershipToken& token) {
    OwnershipToken current;
    return read_ownership(region, token.logical_ring, current) &&
           current.physical_owner == token.physical_owner &&
           current.process_generation == token.process_generation && current.epoch == token.epoch;
}

bool transfer_ownership(SharedRegion& region, std::size_t logical_ring,
                        const OwnershipToken& expected, std::uint32_t new_physical_owner,
                        std::uint32_t new_process_generation, OwnershipToken& replacement) {
    if (logical_ring >= kMaxWorkers || expected.logical_ring != logical_ring ||
        new_physical_owner >= kMaxWorkers || new_process_generation == 0u ||
        (expected.epoch & 1u) != 0u || expected.epoch > std::numeric_limits<std::uint64_t>::max() - 2u) {
        return false;
    }
    RingOwnershipCell& ownership = region.ring_ownership[logical_ring];
    const std::uint64_t expected_word = pack_owner(expected.physical_owner,
                                                   expected.process_generation);
    if (ownership.epoch.load(std::memory_order_acquire) != expected.epoch) {
        return false;
    }
    std::uint64_t epoch = expected.epoch;
    if (!ownership.epoch.compare_exchange_strong(epoch, expected.epoch + 1u,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
        return false;
    }
    std::uint64_t owner_word = expected_word;
    if (!ownership.owner_word.compare_exchange_strong(
            owner_word, pack_owner(new_physical_owner, new_process_generation),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        ownership.epoch.store(expected.epoch, std::memory_order_release);
        return false;
    }
    ownership.epoch.store(expected.epoch + 2u, std::memory_order_release);
    refresh_region_integrity(region);
    replacement = OwnershipToken{static_cast<std::uint32_t>(logical_ring), new_physical_owner,
                                 new_process_generation, expected.epoch + 2u};
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

std::uint32_t compute_region_integrity(const SharedRegion& region) {
    // Observers/tests: recompute from the *observed* seq. Reading the headers
    // as plain words is safe -- they are written once by initialize before the
    // region is shared.
    return region_integrity_at(region, region.global_seq.load(std::memory_order_relaxed));
}

void refresh_region_integrity(SharedRegion& region) {
    // CAS-converging refresh (DEC-0005 #5). The seq is re-read every iteration,
    // and global_seq only increases, so a value can only be stored while the
    // word still holds what was loaded alongside that read -- i.e. the seq
    // argument of the stored word advances monotonically. At quiescence no
    // fetch_add remains, every in-flight updater reads the final seq F and
    // wants exactly H(headers, F); the first such CAS wins and all later
    // iterations take the fast path, so the last store always leaves a
    // consistent word. Under live traffic transient inconsistency is possible;
    // it is one-way (spurious mismatch for a live observer, never masked
    // corruption), mirroring verify_consistent()'s documented direction.
    for (;;) {
        const std::uint32_t expected = region.integrity_word.load(std::memory_order_relaxed);
        const std::uint32_t want =
            region_integrity_at(region, region.global_seq.load(std::memory_order_relaxed));
        if (expected == want) {
            return;  // already consistent: this state is covered
        }
        std::uint32_t seen = expected;
        if (region.integrity_word.compare_exchange_weak(seen, want, std::memory_order_relaxed)) {
            return;
        }
    }
}

}  // namespace safety_crit::shared_memory

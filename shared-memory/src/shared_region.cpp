#include "safety_crit/shared_memory/shared_region.hpp"

#include <cstring>

namespace safety_crit::shared_memory {

void initialize(SharedRegion& region) {
    // Precondition: the storage is freshly mapped and no thread has observed
    // it yet, so relaxed atomic stores below are safe. Plain data and atomics
    // are initialized separately (the region contains std::atomic members, so
    // a single byte fill is not an option without -Wclass-memaccess).
    region.identity.magic = kRegionMagic;
    region.identity.version = kRegionVersion;
    for (auto& rb : region.ring_buffers) {
        rb.slot_count = 0;
        rb.slot_bytes = 0;
        rb.reserved0 = 0;
        rb.committed_seq = 0;
        std::memset(rb.padding, 0, sizeof(rb.padding));
    }
    for (auto& cell : region.worker_status) {
        cell.status.store(0, std::memory_order_relaxed);
    }
    region.global_seq.store(0, std::memory_order_relaxed);
    region.integrity_word.store(0, std::memory_order_relaxed);
}

bool verify(const SharedRegion& region) {
    return region.identity.magic == kRegionMagic &&
           region.identity.version == kRegionVersion;
}

}  // namespace safety_crit::shared_memory

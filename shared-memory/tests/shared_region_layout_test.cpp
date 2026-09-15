#include "test_framework.hpp"

#include <cstddef>
#include <cstdint>

#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace {
using namespace safety_crit::shared_memory;

constexpr std::uintptr_t line_of(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p) / 64u;
}
}  // namespace

SAFETY_CRIT_TEST_CASE(SharedRegionLayout, CompileTimeInvariants) {
    // These static_asserts also live in the header; repeating them here gives
    // CTest an executable witness under both frameworks.
    SAFETY_CRIT_ASSERT(alignof(SharedRegion) == 64);
    SAFETY_CRIT_ASSERT(sizeof(RingBufferHeader) == 64);
    SAFETY_CRIT_ASSERT(sizeof(WorkerStatusCell) == 64);
    SAFETY_CRIT_ASSERT(sizeof(SharedRegion) % 64 == 0);
}

SAFETY_CRIT_TEST_CASE(SharedRegionLayout, NoFalseSharingBetweenWorkers) {
    SharedRegion region;
    for (std::size_t i = 0; i < kMaxWorkers; ++i) {
        SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(&region.ring_buffers[i]) % 64 == 0);
        SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(&region.worker_status[i].status) % 64 == 0);
    }
    // Every worker's header and status cell sits on its own cache line, and
    // none of them shares a line with global_seq.
    for (std::size_t a = 0; a < kMaxWorkers; ++a) {
        for (std::size_t b = a + 1; b < kMaxWorkers; ++b) {
            SAFETY_CRIT_ASSERT(line_of(&region.worker_status[a].status) !=
                               line_of(&region.worker_status[b].status));
            SAFETY_CRIT_ASSERT(line_of(&region.ring_buffers[a]) != line_of(&region.ring_buffers[b]));
        }
        SAFETY_CRIT_ASSERT(line_of(&region.worker_status[a].status) != line_of(&region.global_seq));
        SAFETY_CRIT_ASSERT(line_of(&region.ring_buffers[a]) != line_of(&region.global_seq));
    }
}

SAFETY_CRIT_TEST_CASE(SharedRegion, InitializeAndVerify) {
    SharedRegion region;
    initialize(region);
    SAFETY_CRIT_ASSERT(verify(region));
    SAFETY_CRIT_ASSERT(region.identity.magic == kRegionMagic);
    SAFETY_CRIT_ASSERT(region.identity.version == kRegionVersion);
    SAFETY_CRIT_ASSERT(region.global_seq.load(std::memory_order_acquire) == 0);
    for (std::size_t i = 0; i < kMaxWorkers; ++i) {
        SAFETY_CRIT_ASSERT(region.worker_status[i].status.load(std::memory_order_acquire) == 0);
        SAFETY_CRIT_ASSERT(region.ring_buffers[i].slot_count == 0);
        SAFETY_CRIT_ASSERT(!load_has_flag(region.worker_status[i].status, WorkerStatusFlag::kRunning));
    }
}

SAFETY_CRIT_TEST_CASE(SharedRegion, RejectsBadIdentity) {
    SharedRegion region;
    initialize(region);
    region.identity.magic = 0xDEADBEEFu;
    SAFETY_CRIT_ASSERT(!verify(region));
    initialize(region);
    region.identity.version = kRegionVersion + 1;
    SAFETY_CRIT_ASSERT(!verify(region));
}

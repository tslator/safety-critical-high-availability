#include "test_framework.hpp"

#include <cstdint>

#include "safety_crit/shared_memory/atomic_flags.hpp"

namespace {
using namespace safety_crit::shared_memory;
}  // namespace

SAFETY_CRIT_TEST_CASE(AtomicFlags, SetClearHas) {
    std::atomic<std::uint64_t> status{0};
    SAFETY_CRIT_ASSERT(!load_has_flag(status, WorkerStatusFlag::kRunning));
    set_flag(status, WorkerStatusFlag::kRunning);
    SAFETY_CRIT_ASSERT(load_has_flag(status, WorkerStatusFlag::kRunning));
    clear_flag(status, WorkerStatusFlag::kRunning);
    SAFETY_CRIT_ASSERT(!load_has_flag(status, WorkerStatusFlag::kRunning));
    SAFETY_CRIT_ASSERT(status.load(std::memory_order_acquire) == 0);
}

SAFETY_CRIT_TEST_CASE(AtomicFlags, BitsAreIndependent) {
    std::atomic<std::uint64_t> status{0};
    set_flag(status, WorkerStatusFlag::kRunning);
    set_flag(status, WorkerStatusFlag::kIdle);
    SAFETY_CRIT_ASSERT(load_has_flag(status, WorkerStatusFlag::kRunning));
    clear_flag(status, WorkerStatusFlag::kIdle);
    SAFETY_CRIT_ASSERT(load_has_flag(status, WorkerStatusFlag::kRunning));
    SAFETY_CRIT_ASSERT(!load_has_flag(status, WorkerStatusFlag::kIdle));
    clear_flag(status, WorkerStatusFlag::kRunning);
    set_status(status, to_bits(WorkerStatusFlag::kCrashed) |
                          to_bits(WorkerStatusFlag::kRecovering));
    SAFETY_CRIT_ASSERT(load_has_flag(status, WorkerStatusFlag::kCrashed));
    SAFETY_CRIT_ASSERT(load_has_flag(status, WorkerStatusFlag::kRecovering));
}

SAFETY_CRIT_TEST_CASE(AtomicFlags, WithFlagCombinesWithoutCrossBleed) {
    std::uint64_t s = 0;
    s = with_flag(s, WorkerStatusFlag::kRunning, true);
    s = with_flag(s, WorkerStatusFlag::kCrashed, true);
    SAFETY_CRIT_ASSERT(has_flag(s, WorkerStatusFlag::kRunning));
    SAFETY_CRIT_ASSERT(has_flag(s, WorkerStatusFlag::kCrashed));
    s = with_flag(s, WorkerStatusFlag::kRunning, false);
    SAFETY_CRIT_ASSERT(!has_flag(s, WorkerStatusFlag::kRunning));
    SAFETY_CRIT_ASSERT(has_flag(s, WorkerStatusFlag::kCrashed));
    // A flag we never set stays unset.
    SAFETY_CRIT_ASSERT(!has_flag(s, WorkerStatusFlag::kIdle));
}

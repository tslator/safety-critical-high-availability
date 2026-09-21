#include "test_framework.hpp"

#include <sched.h>

#include "safety_crit/runtime/scheduling.hpp"

namespace {

using safety_crit::runtime::ProcessRole;
using safety_crit::runtime::priority_for;

}  // namespace

SAFETY_CRIT_TEST_CASE(RuntimeScheduling, PriorityOrdering) {
    SAFETY_CRIT_ASSERT(priority_for(ProcessRole::kMonitor) <
                       priority_for(ProcessRole::kSupervisor));
    SAFETY_CRIT_ASSERT(priority_for(ProcessRole::kSupervisor) <
                       priority_for(ProcessRole::kWorker));
}

SAFETY_CRIT_TEST_CASE(RuntimeScheduling, FallbackIsExplicit) {
    const auto result = safety_crit::runtime::apply_scheduling(ProcessRole::kSupervisor);
    SAFETY_CRIT_ASSERT(result.requested_priority == priority_for(ProcessRole::kSupervisor));
    SAFETY_CRIT_ASSERT(result.applied != result.fallback);
    if (result.fallback) {
        SAFETY_CRIT_ASSERT(result.error_number != 0);
    } else {
        SAFETY_CRIT_ASSERT(result.error_number == 0);
        sched_param parameters{};
        SAFETY_CRIT_ASSERT(::sched_getscheduler(0) == SCHED_FIFO);
        SAFETY_CRIT_ASSERT(::sched_getparam(0, &parameters) == 0);
        SAFETY_CRIT_ASSERT(parameters.sched_priority == result.requested_priority);
    }
}

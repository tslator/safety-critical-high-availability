#include "safety_crit/runtime/scheduling.hpp"

#include <cerrno>
#include <sched.h>

namespace safety_crit::runtime {

SchedulingResult apply_scheduling(ProcessRole role) {
    SchedulingResult result{};
    result.requested_priority = priority_for(role);
    sched_param parameters{};
    parameters.sched_priority = result.requested_priority;
    if (::sched_setscheduler(0, SCHED_FIFO, &parameters) == 0) {
        result.applied = true;
        return result;
    }
    result.error_number = errno;
    result.fallback = true;
    return result;
}

}  // namespace safety_crit::runtime

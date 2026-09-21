#pragma once

#include <cstdint>

namespace safety_crit::runtime {

enum class ProcessRole : std::uint8_t {
    kMonitor,
    kSupervisor,
    kWorker,
};

struct SchedulingResult {
    int requested_priority{0};
    int error_number{0};
    bool applied{false};
    bool fallback{false};
};

constexpr int priority_for(ProcessRole role) {
    switch (role) {
    case ProcessRole::kMonitor:
        return 10;
    case ProcessRole::kSupervisor:
        return 20;
    case ProcessRole::kWorker:
        return 30;
    }
    return 0;
}

// Applies SCHED_FIFO once at process startup. Failure is intentionally
// non-fatal: the process keeps the inherited/default scheduler and the result
// records the deterministic fallback for observability and tests.
SchedulingResult apply_scheduling(ProcessRole role);

}  // namespace safety_crit::runtime

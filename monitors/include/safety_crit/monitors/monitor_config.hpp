#pragma once

#include <chrono>
#include <cstdint>

namespace safety_crit::monitors {

// Immutable configuration of one monitor process (T3.1, DEC-0010 #7).
// Durations use std::chrono types; the poll loop (T3.2) wakes every
// poll_interval and a worker whose ring counters are unchanged for longer
// than stall_threshold while it is RUNNING is STALLED.
struct MonitorConfig {
    std::chrono::milliseconds poll_interval{10};
    std::chrono::milliseconds stall_threshold{100};
};

// Validation boundary (CLI parsing in T3.2, tests here): both durations are
// strictly positive and the stall threshold is at least one poll interval
// (a sub-interval threshold could never be observed). Returns false
// otherwise and leaves `config` untouched.
bool validate_config(const MonitorConfig& config);

}  // namespace safety_crit::monitors

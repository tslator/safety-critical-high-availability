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
    // T-0032 (failover handoff blind spot): a fresh, promoted, or restarted
    // owner is announced RUNNING before it can have committed anything on its
    // logical ring, and its first commit is timing bound (18-96 ms locally,
    // more on a loaded CI runner). Arming `stall_threshold` at that edge
    // reports a stall that never happened, so until the first observed tail
    // advance of the episode the bound is `handoff_grace` instead -- it must
    // exceed the failover SLA (100 ms, scripts/phase4-failover-timing.sh) with
    // room for a loaded runner, because the stall detector cannot otherwise
    // tell "promoted, not committed yet" from "wedged".
    std::chrono::milliseconds handoff_grace{750};
};

// Validation boundary (CLI parsing in T3.2, tests here): all three durations
// are strictly positive, the stall threshold is at least one poll interval
// (a sub-interval threshold could never be observed), and the handoff grace
// is at least one stall threshold (a shorter grace would re-introduce the
// handoff blind spot). Returns false otherwise and leaves `config` untouched.
bool validate_config(const MonitorConfig& config);

}  // namespace safety_crit::monitors

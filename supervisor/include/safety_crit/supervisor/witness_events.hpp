// T-0034 (DEC-0014 §4): supervisor witness event vocabulary and sink.
//
// Published vocabulary (additions are additive; renames/removals require a
// decision, DEC-0010 rule carried into DEC-0014 maintenance rules):
//
//   failover_started    error  "worker":<physical>            reaped crash
//   failover_recovered  info   "latency_ms":<N>               first post-failover record
//   ring_degraded       error  "ring":<N>,"reason":"standby_exhausted"
//   stall_recovered     info   "worker":<N>,"epoch":<N>
//   stall_escalated     error  "worker":<N>,"epoch":<N>
//   shutdown_summary    info   "state","a_records","a_corruptions",
//                              "a_first_post_failover","b_records",
//                              "b_corruptions","b_first_post_failover",
//                              "failover_timing_emitted"
//
// Field order is part of the pinned format (supervisor/tests pin it): the
// first extra field follows "event" immediately. Emission is dual: JSON on
// stdout (replacing the Phase 5 plain-text witness lines) plus an append to
// the consolidated event log with component "supervisor".
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "safety_crit/observability/event_log.hpp"

namespace safety_crit::supervisor {

inline constexpr const char* kEventFailoverStarted = "failover_started";
inline constexpr const char* kEventFailoverRecovered = "failover_recovered";
inline constexpr const char* kEventRingDegraded = "ring_degraded";
inline constexpr const char* kEventStallRecovered = "stall_recovered";
inline constexpr const char* kEventStallEscalated = "stall_escalated";
inline constexpr const char* kEventShutdownSummary = "shutdown_summary";

// Extra-field builders (pinned field sets and orders).
std::string failover_started_fields(std::uint32_t physical_worker);
std::string failover_recovered_fields(std::uint64_t latency_ms);
std::string ring_degraded_fields(std::uint32_t ring);
std::string stall_recovered_fields(std::uint32_t physical_worker, std::uint64_t epoch);
std::string stall_escalated_fields(std::uint32_t physical_worker, std::uint64_t epoch);
std::string shutdown_summary_fields(int state, std::uint64_t a_records,
                                    std::uint64_t a_corruptions, bool a_first_post_failover,
                                    std::uint64_t b_records, std::uint64_t b_corruptions,
                                    bool b_first_post_failover, bool failover_timing_emitted);

// Dual-write sink (stdout JSON + event log append). Null-sink pattern at
// call sites: `if (witness != nullptr) witness->emit(...)` keeps the plain
// Phase 5 path untouched when `--event-log` is absent.
class WitnessSink {
public:
    // Opens the consolidated log (component "supervisor"). A failed open is
    // reported to the caller via `ec` but is not fatal: stdout JSON emission
    // still works, so a broken volume mount degrades persistence only.
    bool open(const std::string& path, std::error_code& ec);

    // Emits one witness event: JSON line on stdout (flushed; witnesses must
    // be visible immediately) and an event-log record when the log is open.
    // Appending failures are swallowed by design -- the stdout witness stays
    // authoritative and the reader's gap detection surfaces persistence
    // loss (DEC-0014 §3 control-plane rule).
    void emit(observability::LogLevel level, std::string_view event,
              std::string_view extra_fields);

    bool log_open() const { return log_.is_open(); }

private:
    observability::EventLogWriter log_{};
};

}  // namespace safety_crit::supervisor

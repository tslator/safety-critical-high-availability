#pragma once

#include <cstdint>
#include <string>

#include "safety_crit/monitors/health.hpp"
#include "safety_crit/monitors/monitor_loop.hpp"

namespace safety_crit::monitors {

// Structured JSON log lines (T3.2, DEC-0010 #3): one event per line on
// stdout, built with std::format (no dependencies). Field order is fixed
// and strings are fixed-charset (worker names are indices, states/alerts
// are the published vocabulary), so no escaping is required. Phase 6 wraps
// this event stream in the HTTP/Prometheus surface; the Phase 4 supervisor
// and Phase 5 harness parse these lines directly.

// Alert severity for the log envelope (not part of the event vocabulary).
constexpr const char* level_for(AlertKind kind) {
    switch (kind) {
        case AlertKind::kWorkerCrashed:
            return "error";
        case AlertKind::kWorkerStalled:
        case AlertKind::kWorkerOverrun:
            return "warn";
        default:
            return "info";
    }
}

// {"ts":<unix-ns>,"level":"...","component":"monitor","event":"<kind>",
//  "worker":<idx>}
std::string format_alert_line(std::int64_t ts_ns, const Alert& alert);

// Shutdown summary (DEC-0010 #8): polls, per-kind alert counts, and the
// final per-worker classification snapshot.
// {"ts":...,"level":"info","component":"monitor","event":"monitor_report",
//  "polls":N,"alerts":{...},"workers":["..."]}
std::string format_report_line(std::int64_t ts_ns, const MonitorStats& stats);

}  // namespace safety_crit::monitors

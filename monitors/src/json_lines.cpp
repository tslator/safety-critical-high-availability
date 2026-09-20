#include "safety_crit/monitors/json_lines.hpp"

#include <cinttypes>
#include <cstdio>

namespace safety_crit::monitors {

// Toolchain note (recorded as Phase 3 deviation #1): the plan sketch uses
// std::format, but the pinned clang-14 verification pipeline links
// libstdc++ 12, which predates <format> (implemented in libstdc++ 13 -- the
// same wall the Phase 2 ranges guard records). These strings are small with
// a fixed, escape-free shape, so fixed-format snprintf is the portable
// choice; a toolchain update (libstdc++ >= 13) may retire snprintf for
// std::format in an intentional toolchain change.

std::string format_alert_line(std::int64_t ts_ns, const Alert& alert) {
    char buf[192];
    const int n = std::snprintf(buf, sizeof(buf),
                                "{\"ts\":%" PRId64
                                ",\"level\":\"%s\",\"component\":\"monitor\","
                                "\"event\":\"%s\",\"worker\":%zu}",
                                ts_ns, level_for(alert.kind), to_string(alert.kind),
                                alert.worker);
    return std::string(buf, static_cast<std::size_t>(n < 0 ? 0 : n));
}

std::string format_report_line(std::int64_t ts_ns, const MonitorStats& stats) {
    char head[192];
    char alerts[256];
    const int hn =
        std::snprintf(head, sizeof(head),
                      "{\"ts\":%" PRId64
                      ",\"level\":\"info\",\"component\":\"monitor\","
                      "\"event\":\"monitor_report\",\"polls\":%zu,",
                      ts_ns, stats.polls);
    const int an =
        std::snprintf(alerts, sizeof(alerts),
                      "\"alerts\":{\"worker_crashed\":%" PRIu64 ",\"worker_stalled\":%" PRIu64
                      ",\"worker_recovered\":%" PRIu64 ",\"worker_overrun\":%" PRIu64
                      ",\"worker_idle\":%" PRIu64 ",\"worker_running\":%" PRIu64 "},",
                      stats.alerts_by_kind[to_index(AlertKind::kWorkerCrashed)],
                      stats.alerts_by_kind[to_index(AlertKind::kWorkerStalled)],
                      stats.alerts_by_kind[to_index(AlertKind::kWorkerRecovered)],
                      stats.alerts_by_kind[to_index(AlertKind::kWorkerOverrun)],
                      stats.alerts_by_kind[to_index(AlertKind::kWorkerIdle)],
                      stats.alerts_by_kind[to_index(AlertKind::kWorkerRunning)]);
    std::string line(head, static_cast<std::size_t>(hn < 0 ? 0 : hn));
    line.append(alerts, static_cast<std::size_t>(an < 0 ? 0 : an));
    line += "\"workers\":[";
    for (std::size_t i = 0; i < stats.worker_states.size(); ++i) {
        if (i != 0) {
            line += ",";
        }
        line += "\"";
        line += to_string(stats.worker_states[i]);
        line += "\"";
    }
    line += "]}";
    return line;
}

}  // namespace safety_crit::monitors

#pragma once

#include <cstdint>

#include "safety_crit/monitors/monitor_config.hpp"

namespace safety_crit::monitors {

// Run one monitor process to completion (T3.2, DEC-0010 #1/#3/#6). Attaches
// to the shared region via SharedRegionHandle::create_or_open and rejects a
// stale/incompatible mapping with verify_identity (a metadata-only check,
// safe against live workers). Writes one JSON alert line per alert
// (stdout, flushed immediately), then a final monitor_report line, and
// returns 0. SIGTERM/SIGINT stop the loop (bounded latency: one poll).
// `pid_dir` is the directory the workers' pidfiles are expected in; it must
// match the workers' `--pid-dir` (DEC-0010 #2). `max_polls` 0 means run
// until stopped. Never writes to the shared region (review blocker per
// DEC-0010 maintenance rules).
int run_monitor(const MonitorConfig& cfg, const char* region_name, const char* pid_dir,
                std::uint64_t max_polls = 0);

}  // namespace safety_crit::monitors

// Observability daemon assembly (T-0038, DEC-0014 §9/#10).
//
// Wires the Phase 6 pieces into the long-lived daemon process:
//   - attaches the shared region read-only (DEC-0014 #1 observer path;
//     never initializes, never writes, never signals),
//   - collects region metrics (T-0041) and event-log metrics (T-0042) on a
//     bounded poll cadence driven by the HttpServer tick hook,
//   - reports data_loss_events_total exclusively from supervisor
//     `data_loss_observed` witness records (the event-log *content* can
//     never move the counter otherwise; DEC-0014 §8, CORE property #1),
//   - serves /health /metrics /status (T-0037) until SIGTERM/SIGINT.
//
// Region-loss posture (DEC-0014 §2): a verify failure at startup is a
// startup error (nonzero exit); loss of the region *mid-run* flips /health
// to "degraded" and the daemon keeps serving -- visibility outranks a
// restart, so the daemon never exits for region loss. Mid-run liveness is
// probed by re-checking the backing file and re-verifying identity each
// poll; a vanished region swaps the endpoint context to a zeroed sentinel
// image (verify fails -> degraded verdict, handlers never touch a stale
// mapping).
//
// CLI (exact accepted grammar; unknown options are errors, exit code 2):
//   safety-critical-ha observability [--listen HOST:PORT] [--region NAME]
//       [--event-log PATH] [--poll-interval-ms MS] [--ticks N] [--once]
// `--once` prints exactly one status snapshot (the /status body) to stdout
// and exits without binding a socket; region attach failure is exit 1.

#pragma once

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "safety_crit/observability/http_server.hpp"

namespace safety_crit::observability {

inline constexpr const char* kDefaultDaemonRegionName = "/safety_crit_region";
inline constexpr std::chrono::milliseconds kDefaultDaemonPollInterval{250};
inline constexpr std::chrono::milliseconds kMaxDaemonPollInterval{3600000};

struct DaemonConfig {
    // "HOST:PORT", numeric IPv4 (validated by parse_listen_address).
    std::string listen_address{kDefaultListenAddress};
    // shm_open name (absolute path style, e.g. "/safety_crit_region").
    std::string region_name{kDefaultDaemonRegionName};
    // Consolidated event log to tail for event-derived metrics; empty
    // disables the event side (metrics render structural defaults).
    std::string event_log_path{};
    std::chrono::milliseconds poll_interval{kDefaultDaemonPollInterval};
    // Metric poll cycles to run before exiting; 0 means run until stopped.
    std::uint64_t ticks{0};
    bool once{false};
};

// Parses argv after the subcommand name (argv[0] is skipped). Strict:
// unknown options, missing values, and out-of-range values are errors
// (description in `error`). Defaults apply to absent options.
bool parse_daemon_args(int argc, const char* const* argv, DaemonConfig& cfg,
                       std::string& error);

// Runs the daemon. `--once`: single attach + collect + snapshot to `out`,
// exit 0 (exit 1 when the region cannot be attached/verified). Server
// mode: blocks until SIGTERM/SIGINT (HttpServer stop flag) or the `ticks`
// budget elapses; returns 0 on clean shutdown, 1 on startup failure
// (details on `err`). Uses the process-wide signal handlers and stop flag
// from HttpServer (install_signal_handlers/request_stop).
int run_daemon(const DaemonConfig& cfg, std::ostream& out, std::ostream& err);

}  // namespace safety_crit::observability

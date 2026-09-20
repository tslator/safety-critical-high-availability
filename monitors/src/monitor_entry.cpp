#include "safety_crit/monitors/monitor_entry.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <stop_token>

#include "safety_crit/monitors/json_lines.hpp"
#include "safety_crit/monitors/monitor_loop.hpp"
#include "safety_crit/monitors/pidfile_liveness.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/workers/signals.hpp"

namespace safety_crit::monitors {

int run_monitor(const MonitorConfig& cfg, const char* region_name, const char* pid_dir,
                std::uint64_t max_polls) {
    if (!validate_config(cfg)) {
        std::fprintf(stderr, "monitor: invalid configuration\n");
        return 2;
    }

    shared_memory::SharedRegionHandle handle =
        shared_memory::SharedRegionHandle::create_or_open(region_name);
    if (!handle.ok()) {
        std::fprintf(stderr, "monitor: attach to '%s' failed: %s (errno %d)\n", region_name,
                     shared_memory::to_string(handle.error()), handle.errnum());
        return 1;
    }
    // Metadata-only identity check: a stale or foreign region must never be
    // observed as health data (verify_identity reads no ring state, so live
    // workers cannot affect it).
    if (!shared_memory::verify_identity(*handle.get())) {
        std::fprintf(stderr, "monitor: region '%s' is stale or incompatible\n", region_name);
        handle.detach();
        return 1;
    }

    // Reuses the T2.2 signal wiring (DEC-0009 #3): handler stores a
    // sig_atomic_t only; the SIGUSR1 crash hook also gives Phase 5 a
    // fault-injection surface for the monitor itself.
    workers::signals::SignalState signal_state;
    if (!workers::signals::install(signal_state)) {
        std::fprintf(stderr, "monitor: signal handler installation failed\n");
        handle.detach();
        return 1;
    }

    const std::filesystem::path pid_path_dir{pid_dir};
    auto emit_fn = [](const Alert& alert) {
        const auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        const std::string line = format_alert_line(static_cast<std::int64_t>(ts), alert);
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);  // alerts must be visible immediately
    };
    auto alive_fn = [&](std::size_t worker_idx) {
        return worker_process_alive(pid_path_dir, worker_idx);
    };

    std::stop_source src;  // never requested; stop rides the signal flag
    const MonitorStats stats = run_monitor_loop(
        cfg, *handle.get(), src.get_token(), signal_state.stop_requested, max_polls, emit_fn,
        alive_fn, [&] { interval_pacer(cfg.poll_interval, signal_state.stop_requested); });

    const auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    const std::string report = format_report_line(static_cast<std::int64_t>(ts), stats);
    std::fwrite(report.data(), 1, report.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);

    workers::signals::uninstall();
    handle.detach();
    return 0;
}

}  // namespace safety_crit::monitors

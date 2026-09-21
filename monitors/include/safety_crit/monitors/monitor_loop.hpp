#pragma once

#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iterator>
#include <stop_token>
#include <thread>
#include <vector>

#include "safety_crit/monitors/health.hpp"
#include "safety_crit/monitors/monitor_config.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace safety_crit::monitors {

// Monitor poll loop (T3.2, DEC-0010 #1/#3/#5). One iteration ("poll"):
// read every worker's status cell + ring tail (acquire loads, the
// external-observer path), liveness-check its process, run the health
// algorithm once per worker with a single shared timestamp, and emit the
// resulting alerts. Between polls the pacer sleeps one interval; stops
// (stop_token or the sig_atomic_t flag) are observed before pacing, so
// stop latency is bounded by one poll.
//
// Region-agnostic for testability: liveness is `alive_fn(worker_idx)`
// (pidfile in production, scripted in tests), the clock is a template
// parameter, and the pacer is injected. This code is outside the ring hot
// path: allocation here happens once per poll, never per ring operation.

struct MonitorStats {
    std::uint64_t polls{0};
    std::uint64_t alerts_by_kind[kAlertKindCount] = {};
    std::array<HealthState, safety_crit::shared_memory::kMaxWorkers> worker_states{};
    std::array<std::uint64_t, safety_crit::shared_memory::kMaxWorkers> last_tail{};
};

// Runs at most `max_polls` polls (0 = until stopped). `emit_fn(const Alert&)`
// is called once per alert, in worker order. Returns the accumulated
// statistics, including the final per-worker classification snapshot.
template <class ClockT = std::chrono::steady_clock, class EmitFn, class AliveFn, class PacerFn>
MonitorStats run_monitor_loop(const MonitorConfig& cfg,
                              const safety_crit::shared_memory::SharedRegion& region,
                              std::stop_token stoken,
                              const volatile std::sig_atomic_t& signal_requested,
                              std::uint64_t max_polls, EmitFn&& emit_fn, AliveFn&& alive_fn,
                              PacerFn&& pacer_fn) {
    const auto stop_wanted = [&] {
        return stoken.stop_requested() || signal_requested != 0;
    };

    std::array<WorkerTrack<typename ClockT::time_point>, safety_crit::shared_memory::kMaxWorkers>
        tracks;
    // Alerts carry the physical worker index (T-0021: supervisor and Compose
    // smoke both key off `alert.worker` to distinguish failover participants).
    for (std::size_t i = 0; i < safety_crit::shared_memory::kMaxWorkers; ++i) {
        tracks[i].worker = i;
    }
    MonitorStats stats;
    std::vector<Alert> alerts;

    for (std::uint64_t poll = 0; max_polls == 0 || poll < max_polls; ++poll) {
        if (stop_wanted()) {
            break;
        }
        const auto now = ClockT::now();
        ++stats.polls;
        alerts.clear();
        for (std::size_t i = 0; i < safety_crit::shared_memory::kMaxWorkers; ++i) {
            const WorkerObservation obs = poll_worker(region, i, alive_fn(i));
            stats.last_tail[i] = obs.ring_tail;
            observe_worker(tracks[i], cfg, obs, now, std::back_inserter(alerts));
        }
        for (const Alert& alert : alerts) {
            ++stats.alerts_by_kind[to_index(alert.kind)];
            emit_fn(alert);
        }
        for (std::size_t i = 0; i < safety_crit::shared_memory::kMaxWorkers; ++i) {
            stats.worker_states[i] = tracks[i].state;
        }
        if (stop_wanted()) {
            break;
        }
        if (max_polls != 0 && poll + 1 >= max_polls) {
            break;  // no next poll can run: skip the final interval pacing
        }
        pacer_fn();
    }
    return stats;
}

// Convenience pacer for production: sleep one poll interval, in 1 ms slices
// so a stop request is observed promptly (worker_entry precedent; sleeping
// is legal here -- strictly between polls, outside the ring hot path).
inline void interval_pacer(std::chrono::milliseconds interval,
                           const volatile std::sig_atomic_t& stop_flag) {
    auto remaining = interval;
    while (remaining > std::chrono::milliseconds::zero() && stop_flag == 0) {
        const auto slice =
            remaining < std::chrono::milliseconds(1) ? remaining : std::chrono::milliseconds(1);
        std::this_thread::sleep_for(slice);
        remaining -= slice;
    }
}

}  // namespace safety_crit::monitors

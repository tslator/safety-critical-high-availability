#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "safety_crit/monitors/monitor_config.hpp"
#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace safety_crit::monitors {

// Health algorithm (T3.1, DEC-0010 #2/#3/#5). Region-agnostic for
// testability: one poll of one worker is reduced to a plain
// `WorkerObservation` (status word, ring tail counter, process liveness),
// and the monitor binds these to the shared region and pidfile liveness
// (T3.2). Clock injection is a template parameter: production uses
// std::chrono::steady_clock, tests use a fake clock so thresholds and
// alert edges are deterministic (the DEC-0009 #2 pattern).
//
// This code is NOT on the ring hot path: allocation and the STL are
// permitted here (they occur once per poll, never per ring operation).

// Alert event vocabulary (DEC-0010 #3). Published names are published to
// consumers (Phase 4 supervisor, Phase 5 harness, Phase 6 API); renames or
// removals require a new decision, additions are additive.
enum class AlertKind : std::uint8_t {
    kWorkerCrashed,    // RUNNING (or CRASHED) observed with a dead process
    kWorkerStalled,    // RUNNING, ring tail unchanged beyond threshold
    kWorkerRecovered,  // stalled worker's ring tail advanced again
    kWorkerOverrun,    // OVERRUN status bit observed (once per assertion)
    kWorkerIdle,       // worker quiesced (IDLE observed; includes clean exit)
    kWorkerRunning,    // first RUNNING observation of an episode
};

constexpr const char* to_string(AlertKind kind) {
    switch (kind) {
        case AlertKind::kWorkerCrashed:
            return "worker_crashed";
        case AlertKind::kWorkerStalled:
            return "worker_stalled";
        case AlertKind::kWorkerRecovered:
            return "worker_recovered";
        case AlertKind::kWorkerOverrun:
            return "worker_overrun";
        case AlertKind::kWorkerIdle:
            return "worker_idle";
        case AlertKind::kWorkerRunning:
            return "worker_running";
    }
    return "unknown";
}

inline constexpr std::size_t kAlertKindCount = 6;

// Dense index for per-kind counters (MonitorStats, json_lines).
constexpr std::size_t to_index(AlertKind kind) {
    return static_cast<std::size_t>(kind);
}

// Current classification of one worker, tracked between polls. Reported in
// status/metrics events (T3.2); not part of the alert vocabulary contract.
enum class HealthState : std::uint8_t {
    kUnknown,   // never observed RUNNING (word zero) — worker not started
    kRunning,
    kStalled,
    kCrashed,
    kIdle,
};

constexpr const char* to_string(HealthState state) {
    switch (state) {
        case HealthState::kUnknown:
            return "unknown";
        case HealthState::kRunning:
            return "running";
        case HealthState::kStalled:
            return "stalled";
        case HealthState::kCrashed:
            return "crashed";
        case HealthState::kIdle:
            return "idle";
    }
    return "unknown";
}

struct Alert {
    std::size_t worker{0};
    AlertKind kind{AlertKind::kWorkerRunning};
};

// What one poll of one worker observes. `process_alive` is the pidfile
// pid's liveness (kill(pid, 0)); stale or missing pidfiles are dead
// (DEC-0010 #2). The monitor never writes the region, so these values
// never race with a writer of the same words.
struct WorkerObservation {
    std::uint64_t status{0};
    std::uint64_t ring_tail{0};
    bool process_alive{false};
};

// Per-worker state the health algorithm carries between polls. The caller
// owns one track per worker for the monitor's lifetime.
template <class TimePoint>
struct WorkerTrack {
    std::size_t worker{0};
    HealthState state{HealthState::kUnknown};
    TimePoint last_advance{};       // last poll at which the tail advanced
    std::uint64_t last_tail{0};
    bool running_announced{false};  // episode's kWorkerRunning emitted
    bool idle_announced{false};     // idle edge emitted since running
    bool stalled_alerted{false};    // stall latched until tail advances
    bool crash_alerted{false};      // crash latched until process returns
    bool overrun_reported{false};   // OVERRUN bit seen since last clear
};

// Advances one worker's track by one observation, appending zero or one
// alert to `out` (at most one per poll; the priority is: crashed, then the
// running episode edge, then recovered/stalled, then the overrun edge -- a
// lower-priority edge is not lost, it fires on the next poll).
//
// Rules (DEC-0010 #2/#3, tested exhaustively in monitors_health_test):
//  - Dead process with RUNNING (or CRASHED) in its status word => CRASHED,
//    alerted once per crash episode (process gone stays gone silently; a
//    returning process re-arms, and dying again alerts again).
//  - Alive + RUNNING: first observation of the episode announces
//    kWorkerRunning; tail advance resets the stall window (and clears a
//    latched stall with kWorkerRecovered); no advance for strictly more
//    than cfg.stall_threshold latches kWorkerStalled.
//  - Alive + IDLE (or transition to not-RUNNING): kWorkerIdle once. A
//    clean exit (IDLE + dead) is an idle edge, never a crash.
//  - OVERRUN bit observed: kWorkerOverrun once until the bit is cleared
//    (workers never clear it -- DEC-0009 #4 -- so in practice once per
//    episode).
template <class TimePoint, class OutputIt>
void observe_worker(WorkerTrack<TimePoint>& track, const MonitorConfig& cfg,
                    const WorkerObservation& obs, TimePoint now, OutputIt out) {
    namespace shm = safety_crit::shared_memory;
    const bool running = shm::has_flag(obs.status, shm::WorkerStatusFlag::kRunning);
    const bool crashed_bit = shm::has_flag(obs.status, shm::WorkerStatusFlag::kCrashed);
    const bool idle = shm::has_flag(obs.status, shm::WorkerStatusFlag::kIdle);

    if (!obs.process_alive && (running || crashed_bit)) {
        // Crash (or already-reported-crash) dominates every other edge.
        track.stalled_alerted = false;
        if (!track.crash_alerted) {
            track.crash_alerted = true;
            track.running_announced = false;
            track.overrun_reported = false;
            track.state = HealthState::kCrashed;
            *out++ = Alert{track.worker, AlertKind::kWorkerCrashed};
        }
        return;
    }

    if (track.crash_alerted) {
        // Process observed alive again after a crash report: reset the
        // latches and fall through so the running episode edge (if RUNNING)
        // announces fresh state.
        track.crash_alerted = false;
    }

    if (!running) {
        if (idle && !track.idle_announced) {
            track.idle_announced = true;
            track.running_announced = false;
            track.stalled_alerted = false;
            track.state = HealthState::kIdle;
            *out++ = Alert{track.worker, AlertKind::kWorkerIdle};
        }
        return;
    }

    if (!track.running_announced) {
        track.running_announced = true;
        track.idle_announced = false;
        track.stalled_alerted = false;
        track.last_advance = now;
        track.last_tail = obs.ring_tail;
        track.state = HealthState::kRunning;
        *out++ = Alert{track.worker, AlertKind::kWorkerRunning};
        return;
    }

    if (obs.ring_tail != track.last_tail) {
        track.last_tail = obs.ring_tail;
        track.last_advance = now;
        if (track.stalled_alerted) {
            track.stalled_alerted = false;
            track.state = HealthState::kRunning;
            *out++ = Alert{track.worker, AlertKind::kWorkerRecovered};
            return;
        }
    } else if (!track.stalled_alerted && now - track.last_advance > cfg.stall_threshold) {
        track.stalled_alerted = true;
        track.state = HealthState::kStalled;
        *out++ = Alert{track.worker, AlertKind::kWorkerStalled};
        return;
    }

    if (shm::has_flag(obs.status, shm::WorkerStatusFlag::kOverrun)) {
        if (!track.overrun_reported) {
            track.overrun_reported = true;
            *out++ = Alert{track.worker, AlertKind::kWorkerOverrun};
        }
    } else {
        track.overrun_reported = false;  // observed clear re-arms the edge
    }
}

// Look up the logical ring currently owned by a physical worker (T-0023,
// Phase 5 tail fix). Returns the logical-ring index when the physical
// worker owns a ring for the current epoch; nullopt when the worker is a
// standby (owns no ring). The caller falls back to the physical home ring
// in the standby case, which is safe because standby status is IDLE and
// the stall rule only applies to RUNNING.
inline std::optional<std::size_t> owned_logical_ring(
    const safety_crit::shared_memory::SharedRegion& region, std::size_t physical_idx) {
    for (std::size_t logical = 0; logical < safety_crit::shared_memory::kMaxWorkers; ++logical) {
        safety_crit::shared_memory::OwnershipToken token;
        if (safety_crit::shared_memory::read_ownership(region, logical, token) &&
            token.physical_owner == physical_idx) {
            return logical;
        }
    }
    return std::nullopt;
}

// Poll one worker: reads its status cell and ring tail (acquire loads only
// -- the external-observer path, DEC-0010 #1). `process_alive` is the
// caller's pidfile liveness verdict (the liveness check itself lives with
// the pidfile module, T3.2). Ring tail is read from the logical ring the
// physical worker currently owns (T-0023), not the physical home ring, so
// a promoted standby (physical C owning logical A) is observed on its
// output ring. Standby workers own no ring and fall back to their home
// index (their status word is IDLE and the stall rule never fires).
// Region access lives here, in exactly one place.
inline WorkerObservation poll_worker(const safety_crit::shared_memory::SharedRegion& region,
                                     std::size_t worker_idx, bool process_alive) {
    WorkerObservation obs;
    obs.status = region.worker_status[worker_idx].status.load(std::memory_order_acquire);
    const auto owned = owned_logical_ring(region, worker_idx);
    const std::size_t tail_index = owned.value_or(worker_idx);
    obs.ring_tail = region.rings[tail_index].tail_.load(std::memory_order_acquire);
    obs.process_alive = process_alive;
    return obs;
}

}  // namespace safety_crit::monitors

// Phase 3, T3.1: monitor core -- config validation, alert vocabulary, and
// the exhaustive health classification matrix (running/stalled/crashed/
// idle/overrun edges, single-alert latching, recovery). DEC-0010 #2/#3/#7;
// plan Phase 3 tests #1/#2 with fake clock + injected liveness.

#include "test_framework.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include "safety_crit/monitors/health.hpp"
#include "safety_crit/monitors/monitor_config.hpp"
#include "safety_crit/shared_memory/atomic_flags.hpp"

namespace {

using namespace safety_crit::monitors;
using safety_crit::shared_memory::WorkerStatusFlag;
using safety_crit::shared_memory::to_bits;

// Fake clock: time advances only when the test advances it, making stall
// thresholds and alert edges deterministic (DEC-0009 #2 pattern).
struct FakeClock {
    using duration = std::chrono::milliseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<FakeClock>;
    static inline time_point current{};
    static time_point now() { return current; }
    static void advance(std::chrono::milliseconds d) { current += d; }
    static void reset() { current = time_point{}; }
};

using TimePoint = FakeClock::time_point;
using Alerts = std::vector<Alert>;

MonitorConfig make_cfg() {
    MonitorConfig c{};
    c.poll_interval = std::chrono::milliseconds(10);
    c.stall_threshold = std::chrono::milliseconds(100);
    return c;
}

// One poll of one worker; returns the alerts (0 or 1) it produced.
Alerts poll(WorkerTrack<TimePoint>& track, const MonitorConfig& cfg, std::uint64_t status,
            std::uint64_t tail, bool alive) {
    Alerts out;
    observe_worker(track, cfg, WorkerObservation{status, tail, alive}, FakeClock::now(),
                   std::back_inserter(out));
    return out;
}

// T-0032 variant: same poll with an explicit logical-ring identity, so tests
// can model a failover handoff (borrowed ring / changed epoch).
Alerts poll(WorkerTrack<TimePoint>& track, const MonitorConfig& cfg, std::uint64_t status,
            std::uint64_t tail, bool alive, std::uint64_t ring_epoch) {
    Alerts out;
    observe_worker(track, cfg, WorkerObservation{status, tail, alive, ring_epoch},
                   FakeClock::now(), std::back_inserter(out));
    return out;
}

void expect_alert(const Alerts& alerts, AlertKind kind) {
    SAFETY_CRIT_ASSERT(alerts.size() == 1u);
    SAFETY_CRIT_ASSERT(alerts[0].kind == kind);
}

void expect_quiet(const Alerts& alerts) { SAFETY_CRIT_ASSERT(alerts.empty()); }

}  // namespace

SAFETY_CRIT_TEST_CASE(MonitorsConfig, ConfigValidation) {
    SAFETY_CRIT_ASSERT(validate_config(make_cfg()));

    // Defaults per DEC-0010 #7: 10 ms poll, 100 ms stall threshold.
    MonitorConfig defaults{};
    SAFETY_CRIT_ASSERT(defaults.poll_interval == std::chrono::milliseconds(10));
    SAFETY_CRIT_ASSERT(defaults.stall_threshold == std::chrono::milliseconds(100));
    SAFETY_CRIT_ASSERT(validate_config(defaults));

    MonitorConfig bad{};
    bad.poll_interval = std::chrono::milliseconds::zero();
    bad.stall_threshold = std::chrono::milliseconds(100);
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    bad.poll_interval = std::chrono::milliseconds(10);
    bad.stall_threshold = std::chrono::milliseconds::zero();
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    // Sub-interval threshold could never be observed.
    bad.stall_threshold = std::chrono::milliseconds(5);
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    // Threshold == interval is observable on the first interval boundary.
    bad.stall_threshold = std::chrono::milliseconds(10);
    SAFETY_CRIT_ASSERT(validate_config(bad));

    // T-0032: the handoff grace defaults above the failover SLA and must not
    // be shorter than the stall threshold it temporarily replaces.
    MonitorConfig handoff{};
    SAFETY_CRIT_ASSERT(handoff.handoff_grace == std::chrono::milliseconds(750));
    SAFETY_CRIT_ASSERT(handoff.handoff_grace > handoff.stall_threshold);
    handoff.handoff_grace = std::chrono::milliseconds::zero();
    SAFETY_CRIT_ASSERT(!validate_config(handoff));
    handoff.handoff_grace = std::chrono::milliseconds(-5);
    SAFETY_CRIT_ASSERT(!validate_config(handoff));
    handoff.handoff_grace = std::chrono::milliseconds(99);
    SAFETY_CRIT_ASSERT(!validate_config(handoff));
    handoff.handoff_grace = std::chrono::milliseconds(100);
    SAFETY_CRIT_ASSERT(validate_config(handoff));
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, AlertVocabulary) {
    // Published names (DEC-0010 #3): Phase 4/5/6 consumers depend on them.
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(AlertKind::kWorkerCrashed), "worker_crashed") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(AlertKind::kWorkerStalled), "worker_stalled") == 0);
    SAFETY_CRIT_ASSERT(
        std::strcmp(to_string(AlertKind::kWorkerRecovered), "worker_recovered") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(AlertKind::kWorkerOverrun), "worker_overrun") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(AlertKind::kWorkerIdle), "worker_idle") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(AlertKind::kWorkerRunning), "worker_running") == 0);

    SAFETY_CRIT_ASSERT(std::strcmp(to_string(HealthState::kUnknown), "unknown") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(HealthState::kRunning), "running") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(HealthState::kStalled), "stalled") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(HealthState::kCrashed), "crashed") == 0);
    SAFETY_CRIT_ASSERT(std::strcmp(to_string(HealthState::kIdle), "idle") == 0);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, RunningStallRecoverMatrix) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    track.worker = 1;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);

    // First RUNNING observation announces the episode.
    expect_alert(poll(track, cfg, run, 0, true), AlertKind::kWorkerRunning);
    SAFETY_CRIT_ASSERT(track.state == HealthState::kRunning);

    // Advancing tail keeps quiet, resets the stall window every poll.
    for (std::uint64_t tail = 1; tail <= 9; ++tail) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, run, tail, true));
    }

    // Tail frozen at t=90: last advance seen at t=90; stall is strictly
    // beyond the 100 ms threshold, so t=190 (delta 100) is still quiet.
    FakeClock::advance(std::chrono::milliseconds(100));  // t=190
    expect_quiet(poll(track, cfg, run, 9, true));
    FakeClock::advance(std::chrono::milliseconds(10));  // t=200, delta 110
    expect_alert(poll(track, cfg, run, 9, true), AlertKind::kWorkerStalled);
    SAFETY_CRIT_ASSERT(track.state == HealthState::kStalled);

    // Latched: continued stalling is silent.
    for (int i = 0; i < 5; ++i) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, run, 9, true));
    }

    // Tail advances again: exactly one recovery, re-arming the stall logic.
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_alert(poll(track, cfg, run, 10, true), AlertKind::kWorkerRecovered);
    SAFETY_CRIT_ASSERT(track.state == HealthState::kRunning);
    expect_quiet(poll(track, cfg, run, 11, true));
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, CrashDetectionAndEpisodeRearm) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    track.worker = 2;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);

    expect_alert(poll(track, cfg, run, 100, true), AlertKind::kWorkerRunning);

    // Process dies mid-run: status word keeps RUNNING (a dead process
    // cannot clear it); the liveness verdict is what detects the crash.
    expect_alert(poll(track, cfg, run, 100, false), AlertKind::kWorkerCrashed);
    SAFETY_CRIT_ASSERT(track.state == HealthState::kCrashed);

    // Gone stays gone silently.
    for (int i = 0; i < 4; ++i) {
        expect_quiet(poll(track, cfg, run, 100, false));
    }

    // Process returns (supervisor restarted it): episode edge announces
    // running again, and dying again alerts again.
    expect_alert(poll(track, cfg, run, 130, true), AlertKind::kWorkerRunning);
    expect_alert(poll(track, cfg, run, 130, false), AlertKind::kWorkerCrashed);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, CrashBitInStatusWord) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    const std::uint64_t crashed = to_bits(WorkerStatusFlag::kCrashed);

    // CRASHED advertised (future supervisor surface) + dead process:
    // crash observed even though RUNNING was never announced.
    expect_alert(poll(track, cfg, crashed, 5, false), AlertKind::kWorkerCrashed);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, OverrunEdgeOncePerAssertion) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);
    const std::uint64_t over = run | to_bits(WorkerStatusFlag::kOverrun);

    expect_alert(poll(track, cfg, run, 1, true), AlertKind::kWorkerRunning);
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_alert(poll(track, cfg, over, 2, true), AlertKind::kWorkerOverrun);

    // Latched while the bit stays set (workers never clear it -- DEC-0009 #4).
    for (int i = 0; i < 3; ++i) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, over, 3 + static_cast<std::uint64_t>(i), true));
    }

    // Bit cleared and set again: re-armed.
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_quiet(poll(track, cfg, run, 6, true));
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_alert(poll(track, cfg, over, 7, true), AlertKind::kWorkerOverrun);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, IdleEdgeAndCleanExit) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);
    const std::uint64_t idle = to_bits(WorkerStatusFlag::kIdle);

    expect_alert(poll(track, cfg, run, 42, true), AlertKind::kWorkerRunning);
    expect_alert(poll(track, cfg, idle, 42, true), AlertKind::kWorkerIdle);
    SAFETY_CRIT_ASSERT(track.state == HealthState::kIdle);

    // Latched while idle.
    expect_quiet(poll(track, cfg, idle, 42, true));

    // Clean exit (IDLE + dead) is an idle edge, never a crash: fresh track,
    // monitor first sees the worker after it already exited.
    WorkerTrack<TimePoint> fresh;
    expect_alert(poll(fresh, cfg, idle, 42, false), AlertKind::kWorkerIdle);
    expect_quiet(poll(fresh, cfg, idle, 42, false));

    // Never-started (zero word) dead or alive: no events, stays unknown.
    WorkerTrack<TimePoint> ghost;
    expect_quiet(poll(ghost, cfg, 0, 0, false));
    expect_quiet(poll(ghost, cfg, 0, 0, true));
    SAFETY_CRIT_ASSERT(ghost.state == HealthState::kUnknown);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, CrashDominatesLowerPriorityEdges) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);
    const std::uint64_t over = run | to_bits(WorkerStatusFlag::kOverrun);

    // First-ever observation is RUNNING+OVERRUN+DEAD: crash wins the poll,
    // and the overrun edge is NOT lost -- the episode re-arms on return.
    expect_alert(poll(track, cfg, over, 10, false), AlertKind::kWorkerCrashed);
    expect_alert(poll(track, cfg, over, 10, true), AlertKind::kWorkerRunning);
    expect_alert(poll(track, cfg, over, 11, true), AlertKind::kWorkerOverrun);
}

// T-0032: failover handoff blind spot. A promoted owner is announced RUNNING
// at ownership transfer -- before it can commit on its new logical ring. The
// first post-failover commit is timing bound (18-96 ms measured locally, more
// on a loaded CI runner), so arming stall_threshold at the handoff edge
// reported a stall that never happened (CI run 35865376613: the false stall
// reached the supervisor, whose bounded SIGCONT path then logged
// "stall recovered" and, when the grace expired, SIGKILLed the worker that
// was in the middle of recovering the ring).
SAFETY_CRIT_TEST_CASE(MonitorsHealth, PromotedOwnerHandoffIsNotReportedAsStall) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();  // stall 100 ms, handoff grace 750 ms
    WorkerTrack<TimePoint> track;
    track.worker = 2;  // the standby physical worker
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);
    const std::uint64_t idle = to_bits(WorkerStatusFlag::kIdle);

    // Boot: standby, IDLE on its home index.
    expect_alert(poll(track, cfg, idle, 0, true, 1), AlertKind::kWorkerIdle);

    // The hot worker dies and the supervisor transfers logical ring 0 (epoch
    // 5, a borrowed ring) onto this worker: RUNNING at the transfer edge,
    // tail still frozen at the pre-crash value.
    expect_alert(poll(track, cfg, run, 41, true, 5), AlertKind::kWorkerRunning);

    // Nothing committed yet: every poll beyond stall_threshold stays quiet
    // until the handoff grace expires.
    for (int ms = 10; ms <= 500; ms += 10) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, run, 41, true, 5));
    }

    // First commit lands: the handoff is over and the ordinary threshold
    // resumes from this point.
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_quiet(poll(track, cfg, run, 42, true, 5));
    for (std::uint64_t tail = 43; tail <= 51; ++tail) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, run, tail, true, 5));
    }

    // A genuine stall after the handoff is still reported on stall_threshold:
    // the grace is a window, not a permanent exemption.
    for (int i = 0; i < 10; ++i) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, run, 51, true, 5));
    }
    FakeClock::advance(std::chrono::milliseconds(10));  // delta 110 ms
    expect_alert(poll(track, cfg, run, 51, true, 5), AlertKind::kWorkerStalled);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, OwnershipEpochChangeRebaselinesStallWindow) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    track.worker = 0;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);

    // Home ring, epoch 3, committing.
    expect_alert(poll(track, cfg, run, 10, true, 3), AlertKind::kWorkerRunning);
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_quiet(poll(track, cfg, run, 11, true, 3));

    // Frozen 90 ms: inside stall_threshold, quiet.
    FakeClock::advance(std::chrono::milliseconds(90));
    expect_quiet(poll(track, cfg, run, 11, true, 3));

    // Epoch bump (transfer_ownership bumps the epoch on every handoff): the
    // window is re-based on the handoff edge, silently -- no alert is emitted
    // for the handoff itself.
    FakeClock::advance(std::chrono::milliseconds(10));  // t=110
    expect_quiet(poll(track, cfg, run, 11, true, 9));
    for (int i = 0; i < 3; ++i) {
        FakeClock::advance(std::chrono::milliseconds(10));  // delta 140 from the freeze
        expect_quiet(poll(track, cfg, run, 11, true, 9));
    }

    // Commit lands, then a real stall fires on the ordinary threshold.
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_quiet(poll(track, cfg, run, 12, true, 9));
    for (int i = 0; i < 10; ++i) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, run, 12, true, 9));
    }
    FakeClock::advance(std::chrono::milliseconds(10));
    expect_alert(poll(track, cfg, run, 12, true, 9), AlertKind::kWorkerStalled);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, PromotedOwnerThatNeverCommitsStillAlerts) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    track.worker = 2;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);

    // Promoted onto a borrowed ring and never produces a record: detection is
    // delayed to the handoff bound, not lost (bounded detection, AGENTS.md #5).
    expect_alert(poll(track, cfg, run, 7, true, 5), AlertKind::kWorkerRunning);
    for (int i = 0; i < 75; ++i) {
        FakeClock::advance(std::chrono::milliseconds(10));
        expect_quiet(poll(track, cfg, run, 7, true, 5));
    }
    FakeClock::advance(std::chrono::milliseconds(10));  // 760 ms since the handoff edge
    expect_alert(poll(track, cfg, run, 7, true, 5), AlertKind::kWorkerStalled);
}

SAFETY_CRIT_TEST_CASE(MonitorsHealth, CrashRearmsHandoffBoundForNextOwner) {
    FakeClock::reset();
    const MonitorConfig cfg = make_cfg();
    WorkerTrack<TimePoint> track;
    track.worker = 0;
    const std::uint64_t run = to_bits(WorkerStatusFlag::kRunning);

    // Hot on its home ring, then SIGSEGV: status word keeps RUNNING, liveness
    // detects the death.
    expect_alert(poll(track, cfg, run, 10, true, 3), AlertKind::kWorkerRunning);
    expect_alert(poll(track, cfg, run, 10, false, 3), AlertKind::kWorkerCrashed);

    // The replacement process answers on the same status slot (it stores its
    // own status word, which is why the crash edge can be masked in the field
    // -- T-0032). Its first episode runs on the handoff bound even though it
    // owns its home ring, because the ring moved over to it.
    expect_alert(poll(track, cfg, run, 10, true, 3), AlertKind::kWorkerRunning);
    for (int i = 0; i < 11; ++i) {
        FakeClock::advance(std::chrono::milliseconds(10));  // delta 110 ms only
        expect_quiet(poll(track, cfg, run, 10, true, 3));
    }
    FakeClock::advance(std::chrono::milliseconds(650));  // beyond the handoff grace
    expect_alert(poll(track, cfg, run, 10, true, 3), AlertKind::kWorkerStalled);
}

// Phase 3, T3.2: monitor poll loop -- exact poll accounting, stall/recovery
// driven by real ring counters, crash detection through the injected
// liveness verdict, clean-exit idle classification, bounded stop latency,
// and the exact JSON line formats. DEC-0010 #1/#3/#5.

#include "test_framework.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <string>
#include <vector>

#include "safety_crit/monitors/json_lines.hpp"
#include "safety_crit/monitors/monitor_loop.hpp"
#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace {

using namespace safety_crit::monitors;
namespace shm = safety_crit::shared_memory;
using shm::WorkerStatusFlag;
using shm::to_bits;

// Fake clock: advances only inside the injected pacer, so thresholds and
// alert edges are deterministic (DEC-0009 #2 pattern).
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

const volatile std::sig_atomic_t kNoSignal = 0;

void set_status(shm::SharedRegion& region, std::size_t idx, std::uint64_t bits) {
    region.worker_status[idx].status.store(bits, std::memory_order_release);
}

MonitorConfig make_cfg(std::chrono::milliseconds interval, std::chrono::milliseconds threshold) {
    MonitorConfig c{};
    c.poll_interval = interval;
    c.stall_threshold = threshold;
    return c;
}

}  // namespace

SAFETY_CRIT_TEST_CASE(MonitorsLoop, ExactPollAccountingAndRunningAnnounce) {
    FakeClock::reset();
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    set_status(region, 0, to_bits(WorkerStatusFlag::kRunning));

    const MonitorConfig cfg = make_cfg(std::chrono::milliseconds(10),
                                       std::chrono::milliseconds(100));
    std::vector<Alert> emitted;
    std::uint64_t pushed = 0;

    auto stats = run_monitor_loop<FakeClock>(
        cfg, region, std::stop_token(), kNoSignal, 7,
        [&](const Alert& a) { emitted.push_back(a); },
        [](std::size_t) { return true; },
        [&]() {
            FakeClock::advance(std::chrono::milliseconds(10));
            ++pushed;
            SAFETY_CRIT_ASSERT(shm::push(region, 0, pushed));  // keep the ring alive
        });

    SAFETY_CRIT_ASSERT(stats.polls == 7u);
    SAFETY_CRIT_ASSERT(pushed == 6u);  // pacer runs between polls only
    SAFETY_CRIT_ASSERT(stats.last_tail[0] == 6u);
    SAFETY_CRIT_ASSERT(emitted.size() == 1u);  // exactly one episode announce
    SAFETY_CRIT_ASSERT(emitted[0].kind == AlertKind::kWorkerRunning);
    SAFETY_CRIT_ASSERT(emitted[0].worker == 0u);
    SAFETY_CRIT_ASSERT(stats.worker_states[0] == HealthState::kRunning);
    SAFETY_CRIT_ASSERT(stats.worker_states[1] == HealthState::kUnknown);
    SAFETY_CRIT_ASSERT(stats.alerts_by_kind[to_index(AlertKind::kWorkerStalled)] == 0u);
}

SAFETY_CRIT_TEST_CASE(MonitorsLoop, StallAndRecoveryViaRingCounters) {
    FakeClock::reset();
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    set_status(region, 0, to_bits(WorkerStatusFlag::kRunning));

    // threshold 30ms at a 10ms poll: freezing must latch exactly one stall
    // and resuming exactly one recovery.
    const MonitorConfig cfg = make_cfg(std::chrono::milliseconds(10),
                                       std::chrono::milliseconds(30));
    std::vector<AlertKind> kinds;
    std::uint64_t pushed = 0;

    // Polls: 0..5 push (advance), 6..14 frozen, 15 onward push again.
    // Inside the pacer, `poll_number` (post-increment) IS the index of the
    // poll about to run, so it scripts the next poll's ring state.
    int poll_number = 0;
    auto stats = run_monitor_loop<FakeClock>(
        cfg, region, std::stop_token(), kNoSignal, 20,
        [&](const Alert& a) { kinds.push_back(a.kind); },
        [](std::size_t) { return true; },
        [&]() {
            FakeClock::advance(std::chrono::milliseconds(10));
            ++poll_number;
            if (poll_number < 6 || poll_number >= 15) {
                ++pushed;
                SAFETY_CRIT_ASSERT(shm::push(region, 0, pushed));
            }
        });

    std::size_t stalled = 0;
    std::size_t recovered = 0;
    for (AlertKind k : kinds) {
        if (k == AlertKind::kWorkerStalled) {
            ++stalled;
        }
        if (k == AlertKind::kWorkerRecovered) {
            ++recovered;
        }
    }
    SAFETY_CRIT_ASSERT(stalled == 1u);
    SAFETY_CRIT_ASSERT(recovered == 1u);
    SAFETY_CRIT_ASSERT(stats.alerts_by_kind[to_index(AlertKind::kWorkerStalled)] == 1u);
    SAFETY_CRIT_ASSERT(stats.alerts_by_kind[to_index(AlertKind::kWorkerRecovered)] == 1u);
    SAFETY_CRIT_ASSERT(stats.worker_states[0] == HealthState::kRunning);
}

SAFETY_CRIT_TEST_CASE(MonitorsLoop, CrashDetectionViaLivenessVerdict) {
    FakeClock::reset();
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    set_status(region, 0, to_bits(WorkerStatusFlag::kRunning));

    const MonitorConfig cfg = make_cfg(std::chrono::milliseconds(10),
                                       std::chrono::milliseconds(100));
    // Liveness: alive for polls 0..2, dead from poll 3 on (status word keeps
    // RUNNING -- a dead process cannot clear it).
    int poll_number = 0;
    std::vector<Alert> emitted;

    auto stats = run_monitor_loop<FakeClock>(
        cfg, region, std::stop_token(), kNoSignal, 8,
        [&](const Alert& a) { emitted.push_back(a); },
        [&](std::size_t) { return poll_number < 3; },
        [&]() {
            FakeClock::advance(std::chrono::milliseconds(10));
            ++poll_number;
        });

    SAFETY_CRIT_ASSERT(stats.polls == 8u);
    SAFETY_CRIT_ASSERT(emitted.size() == 2u);  // announce + exactly one crash
    SAFETY_CRIT_ASSERT(emitted[0].kind == AlertKind::kWorkerRunning);
    SAFETY_CRIT_ASSERT(emitted[1].kind == AlertKind::kWorkerCrashed);
    SAFETY_CRIT_ASSERT(stats.alerts_by_kind[to_index(AlertKind::kWorkerCrashed)] == 1u);
    SAFETY_CRIT_ASSERT(stats.worker_states[0] == HealthState::kCrashed);
}

SAFETY_CRIT_TEST_CASE(MonitorsLoop, CleanExitIsIdleNeverCrash) {
    FakeClock::reset();
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    set_status(region, 0, to_bits(WorkerStatusFlag::kRunning));

    const MonitorConfig cfg = make_cfg(std::chrono::milliseconds(10),
                                       std::chrono::milliseconds(10000));
    int poll_number = 0;
    std::vector<AlertKind> kinds;

    auto stats = run_monitor_loop<FakeClock>(
        cfg, region, std::stop_token(), kNoSignal, 6,
        [&](const Alert& a) { kinds.push_back(a.kind); },
        [&](std::size_t) { return poll_number < 3; },  // process exits after poll 2
        [&]() {
            FakeClock::advance(std::chrono::milliseconds(10));
            ++poll_number;
            if (poll_number == 3) {
                // Clean exit path: IDLE published, then process gone.
                set_status(region, 0, to_bits(WorkerStatusFlag::kIdle));
            }
        });

    SAFETY_CRIT_ASSERT(stats.alerts_by_kind[to_index(AlertKind::kWorkerCrashed)] == 0u);
    SAFETY_CRIT_ASSERT(stats.alerts_by_kind[to_index(AlertKind::kWorkerIdle)] == 1u);
    SAFETY_CRIT_ASSERT(stats.worker_states[0] == HealthState::kIdle);
}

SAFETY_CRIT_TEST_CASE(MonitorsLoop, BoundedStopLatency) {
    FakeClock::reset();
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));

    const MonitorConfig cfg = make_cfg(std::chrono::milliseconds(10),
                                       std::chrono::milliseconds(100));
    volatile std::sig_atomic_t stop_flag = 0;
    int poll_number = 0;

    // The flag fires after the third poll's pacing starts -- the loop must
    // observe it before the fourth poll runs: stop latency <= one poll.
    auto stats = run_monitor_loop<FakeClock>(
        cfg, region, std::stop_token(), stop_flag, 0, [](const Alert&) {},
        [](std::size_t) { return true; },
        [&]() {
            FakeClock::advance(std::chrono::milliseconds(10));
            ++poll_number;
            if (poll_number == 3) {
                stop_flag = 1;
            }
        });

    SAFETY_CRIT_ASSERT(stats.polls == 3u);
    SAFETY_CRIT_ASSERT(poll_number == 3u);
}

SAFETY_CRIT_TEST_CASE(MonitorsLoop, AlertsCarryPhysicalWorkerIndex) {
    FakeClock::reset();
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    // Physical 0 idle, 1 running, 2 running: each emits one edge in worker
    // order on the first poll and the reported index must match the poll
    // slot (regression: T-0021 caught alerts all attributing to worker 0
    // because the per-worker tracks were never seeded with their index).
    set_status(region, 0, to_bits(WorkerStatusFlag::kIdle));
    set_status(region, 1, to_bits(WorkerStatusFlag::kRunning));
    set_status(region, 2, to_bits(WorkerStatusFlag::kRunning));

    const MonitorConfig cfg = make_cfg(std::chrono::milliseconds(10),
                                       std::chrono::milliseconds(100));
    std::vector<Alert> emitted;
    (void)run_monitor_loop<FakeClock>(
        cfg, region, std::stop_token(), kNoSignal, 1,
        [&](const Alert& a) { emitted.push_back(a); },
        [](std::size_t) { return true; }, []() {});

    SAFETY_CRIT_ASSERT(emitted.size() == 3u);
    SAFETY_CRIT_ASSERT(emitted[0].worker == 0u);
    SAFETY_CRIT_ASSERT(emitted[0].kind == AlertKind::kWorkerIdle);
    SAFETY_CRIT_ASSERT(emitted[1].worker == 1u);
    SAFETY_CRIT_ASSERT(emitted[1].kind == AlertKind::kWorkerRunning);
    SAFETY_CRIT_ASSERT(emitted[2].worker == 2u);
    SAFETY_CRIT_ASSERT(emitted[2].kind == AlertKind::kWorkerRunning);
}

SAFETY_CRIT_TEST_CASE(MonitorsJson, AlertLineExactFormat) {
    Alert alert;
    alert.worker = 2;
    alert.kind = AlertKind::kWorkerStalled;
    SAFETY_CRIT_ASSERT(
        format_alert_line(1700000000000000000LL, alert) ==
        R"({"ts":1700000000000000000,"level":"warn","component":"monitor","event":"worker_stalled","worker":2})");

    alert.kind = AlertKind::kWorkerCrashed;
    SAFETY_CRIT_ASSERT(format_alert_line(1, alert).find(R"("level":"error")") !=
                       std::string::npos);
    alert.kind = AlertKind::kWorkerRunning;
    SAFETY_CRIT_ASSERT(format_alert_line(1, alert).find(R"("level":"info")") !=
                       std::string::npos);
}

SAFETY_CRIT_TEST_CASE(MonitorsJson, ReportLineExactFormat) {
    MonitorStats stats;
    stats.polls = 42;
    stats.alerts_by_kind[to_index(AlertKind::kWorkerCrashed)] = 1;
    stats.alerts_by_kind[to_index(AlertKind::kWorkerStalled)] = 2;
    stats.worker_states[0] = HealthState::kRunning;
    stats.worker_states[1] = HealthState::kStalled;
    stats.worker_states[2] = HealthState::kIdle;

    SAFETY_CRIT_ASSERT(
        format_report_line(5, stats) ==
        R"({"ts":5,"level":"info","component":"monitor","event":"monitor_report","polls":42,)"
        R"("alerts":{"worker_crashed":1,"worker_stalled":2,"worker_recovered":0,)"
        R"("worker_overrun":0,"worker_idle":0,"worker_running":0},)"
        R"("workers":["running","stalled","idle"]})");
}

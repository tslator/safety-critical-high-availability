// Phase 2, T2.2: work loop tests -- exact tick accounting, stop-token and
// signal-flag cancellation (plan test #3 analogue), deterministic budget
// overrun under a fake clock (plan test #2), push backpressure retry, and
// end-to-end determinism through the loop.

#include "test_framework.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <vector>

#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/workers/signals.hpp"
#include "safety_crit/workers/work_loop.hpp"
#include "safety_crit/workers/workload.hpp"

namespace {
using namespace safety_crit::workers;

constexpr std::uint64_t kTestSeed = 0xFEEDFACECAFEBEEFULL;
constexpr std::sig_atomic_t kNoSignal = 0;

struct NoopPacer {
    void operator()() const {}
};

WorkerConfig make_cfg(std::uint64_t ticks) {
    WorkerConfig c{};
    c.worker_idx = 0;
    c.ticks = ticks;
    c.cpu_budget = std::chrono::microseconds(1000);
    return c;
}

// Fake clock: time advances only when the test advances it (inside tick_fn
// or push_fn), making budget witnesses deterministic.
struct FakeClock {
    using duration = std::chrono::microseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<FakeClock>;
    static inline time_point current{};
    static time_point now() { return current; }
    static void advance(std::chrono::microseconds d) { current += d; }
    static void reset() { current = time_point{}; }
};
}  // namespace

SAFETY_CRIT_TEST_CASE(WorkersLoop, ExactTickAccounting) {
    WorkerConfig cfg = make_cfg(100);
    std::vector<std::uint64_t> seen;
    std::stop_source src;
    auto result = run_work_loop(
        cfg, nullptr, src.get_token(), kNoSignal,
        [&seen](const std::uint64_t& payload) {
            seen.push_back(payload);
            return true;
        },
        [](std::uint64_t t) { return t; }, NoopPacer{});
    SAFETY_CRIT_ASSERT(result.ticks_completed == 100u);
    SAFETY_CRIT_ASSERT(result.overruns == 0u);
    SAFETY_CRIT_ASSERT(!result.stopped_before_push);
    SAFETY_CRIT_ASSERT(!result.stopped_during_push);
    SAFETY_CRIT_ASSERT(seen.size() == 100u);
    for (std::uint64_t i = 0; i < seen.size(); ++i) {
        SAFETY_CRIT_ASSERT(seen[i] == i);  // in-order, no duplication
    }
}

SAFETY_CRIT_TEST_CASE(WorkersLoop, StopTokenObservedBetweenTicks) {
    WorkerConfig cfg = make_cfg(1000);
    std::stop_source src;
    std::uint64_t completed_pushes = 0;
    auto result = run_work_loop(
        cfg, nullptr, src.get_token(), kNoSignal,
        [&](const std::uint64_t&) {
            ++completed_pushes;
            if (completed_pushes == 50u) {
                src.request_stop();  // cancellation during tick 49
            }
            return true;
        },
        [](std::uint64_t t) { return t; }, NoopPacer{});
    // The stop lands during push of tick 49; the loop must finish that push,
    // then observe the stop before tick 50 (bounded latency of one tick).
    SAFETY_CRIT_ASSERT(result.ticks_completed == 50u);
    SAFETY_CRIT_ASSERT(result.stopped_before_push);
    SAFETY_CRIT_ASSERT(!result.stopped_during_push);
    SAFETY_CRIT_ASSERT(result.pending_tick == 50u);
    SAFETY_CRIT_ASSERT(completed_pushes == 50u);
}

SAFETY_CRIT_TEST_CASE(WorkersLoop, SignalFlagStopsLoop) {
    WorkerConfig cfg = make_cfg(1000);
    volatile std::sig_atomic_t flag{0};
    std::stop_source src;  // never requested
    std::uint64_t pushes = 0;
    auto result = run_work_loop(
        cfg, nullptr, src.get_token(), flag,
        [&](const std::uint64_t&) {
            ++pushes;
            if (pushes == 31u) {
                flag = 1;  // simulate the handler having fired
            }
            return true;
        },
        [](std::uint64_t t) { return t; }, NoopPacer{});
    SAFETY_CRIT_ASSERT(result.ticks_completed == 31u);
    SAFETY_CRIT_ASSERT(result.stopped_before_push);
    SAFETY_CRIT_ASSERT(result.pending_tick == 31u);
}

SAFETY_CRIT_TEST_CASE(WorkersLoop, BudgetOverrunWitnessedWithFakeClock) {
    // Plan test #2: per-tick budget enforcement. tick_fn advances the fake
    // clock past the budget on every tick: every tick must count an overrun
    // and set the OVERRUN status bit.
    FakeClock::reset();
    WorkerConfig cfg = make_cfg(25);
    cfg.cpu_budget = std::chrono::microseconds(100);
    std::atomic<std::uint64_t> status{0};
    std::stop_source src;
    auto result = run_work_loop<FakeClock>(
        cfg, &status, src.get_token(), kNoSignal,
        [](const std::uint64_t&) { return true; },
        [](std::uint64_t t) {
            FakeClock::advance(std::chrono::microseconds(250));
            return t;
        },
        NoopPacer{});
    SAFETY_CRIT_ASSERT(result.ticks_completed == 25u);
    SAFETY_CRIT_ASSERT(result.overruns == 25u);
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::load_has_flag(
        status, safety_crit::shared_memory::WorkerStatusFlag::kOverrun));
}

SAFETY_CRIT_TEST_CASE(WorkersLoop, BudgetRespectedNoOverrun) {
    FakeClock::reset();
    WorkerConfig cfg = make_cfg(25);
    cfg.cpu_budget = std::chrono::microseconds(100);
    std::atomic<std::uint64_t> status{0};
    std::stop_source src;
    auto result = run_work_loop<FakeClock>(
        cfg, &status, src.get_token(), kNoSignal,
        [](const std::uint64_t&) {
            FakeClock::advance(std::chrono::microseconds(10));
            return true;
        },
        [](std::uint64_t t) { return t; }, NoopPacer{});
    SAFETY_CRIT_ASSERT(result.ticks_completed == 25u);
    SAFETY_CRIT_ASSERT(result.overruns == 0u);
    SAFETY_CRIT_ASSERT(!safety_crit::shared_memory::load_has_flag(
        status, safety_crit::shared_memory::WorkerStatusFlag::kOverrun));
}

SAFETY_CRIT_TEST_CASE(WorkersLoop, PushBackpressureRetriesUntilStop) {
    // Ring-full semantics: push_fn always refuses; the loop must retry
    // (yielding) and only exit when a stop is requested mid-push.
    WorkerConfig cfg = make_cfg(100);
    std::stop_source src;
    std::uint64_t attempts = 0;
    auto result = run_work_loop(
        cfg, nullptr, src.get_token(), kNoSignal,
        [&](const std::uint64_t&) {
            ++attempts;
            if (attempts >= 50u) {
                src.request_stop();
            }
            return false;  // always full
        },
        [](std::uint64_t t) { return t; }, NoopPacer{});
    SAFETY_CRIT_ASSERT(result.ticks_completed == 0u);
    SAFETY_CRIT_ASSERT(result.stopped_during_push);
    SAFETY_CRIT_ASSERT(!result.stopped_before_push);
    SAFETY_CRIT_ASSERT(result.pending_tick == 0u);
    SAFETY_CRIT_ASSERT(attempts >= 50u);
}

SAFETY_CRIT_TEST_CASE(WorkersLoop, DeterministicThroughTheLoop) {
    // End-to-end determinism: same (seed, worker, tick) run through the loop
    // produces identical payloads across repeated runs and matches the
    // direct workload computation (plan test #1 through T2.2 plumbing).
    const WorkerConfig cfg = make_cfg(50);
    std::stop_source src;

    auto run_once = [&]() {
        std::vector<ProcessedData> out;
        auto r = run_work_loop(
            cfg, nullptr, src.get_token(), kNoSignal,
            [&out](const ProcessedData& p) {
                out.push_back(p);
                return true;
            },
            [](std::uint64_t t) {
                return process_sensor_data(generate_raw_tick(kTestSeed, 0, t));
            },
            NoopPacer{});
        SAFETY_CRIT_ASSERT(r.ticks_completed == 50u);
        return out;
    };

    const std::vector<ProcessedData> first = run_once();
    const std::vector<ProcessedData> second = run_once();
    SAFETY_CRIT_ASSERT(first.size() == 50u);
    for (std::uint64_t t = 0; t < 50; ++t) {
        SAFETY_CRIT_ASSERT(processed_equal(first[t], second[t]));
        const ProcessedData direct =
            process_sensor_data(generate_raw_tick(kTestSeed, 0, t));
        SAFETY_CRIT_ASSERT(processed_equal(first[t], direct));
    }
}

SAFETY_CRIT_TEST_CASE(WorkersLoop, SignalInstallAndQuery) {
    // Installs real dispositions, verifies they are custom, restores
    // defaults. The SIGUSR1 _exit path itself is exercised in the fork-based
    // integration tests (T2.3) -- never in-process.
    signals::SignalState state;
    SAFETY_CRIT_ASSERT(signals::install(state));
    const signals::HandlerSnapshot snap = signals::query_handlers();
    SAFETY_CRIT_ASSERT(snap.stop_custom);
    SAFETY_CRIT_ASSERT(snap.crash_custom);
    signals::uninstall();
    const signals::HandlerSnapshot restored = signals::query_handlers();
    SAFETY_CRIT_ASSERT(!restored.stop_custom);
    SAFETY_CRIT_ASSERT(!restored.crash_custom);
}

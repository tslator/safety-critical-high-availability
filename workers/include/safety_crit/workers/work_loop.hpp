#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <thread>

#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/workers/worker_config.hpp"

namespace safety_crit::workers {

// Worker work loop (T2.2, DEC-0009 #2/#3/#4/#7). Region-agnostic for
// testability: output publication is a callback (`push_fn`), workload
// synthesis is a callback (`tick_fn`), and inter-tick pacing is a callback
// (`pacer_fn`). The worker entry (T2.3) binds these to the shared region,
// the workload, and a clock sleep respectively.
//
// Per tick:
//   1. Stop check (stop_token OR the sig_atomic_t flag set by the signal
//      handler). Stops are observed between ticks -- bounded latency of one
//      tick, per DEC-0009 #3. A pending output is NOT committed after a
//      stop was observed; it is reported in `pending_tick` so the caller can
//      account for it.
//   2. Deadline window: clock::now() before/after tick_fn + push_fn. If the
//      elapsed time exceeds cfg.cpu_budget, the OVERRUN flag is set on the
//      (optional) status word and counted. Callbacks do the actual budget
//      consumption; the loop only measures.
//   3. push_fn retries with yield while the ring is full (backpressure);
//      a stop request mid-push aborts the wait (pending_tick reported).
//
// Hot-path policy (POLICIES): this function itself performs no allocation,
// no exceptions, no mutexes. `pacer_fn` may sleep (a syscall) because it
// runs strictly between ticks, outside the ring-operation hot path.
//
// Clock injection is a template parameter (ClockT::now()); production uses
// std::chrono::steady_clock, tests use a fake clock to witness budget
// overruns deterministically (plan Phase 2 test #2).
struct LoopResult {
    std::uint64_t ticks_completed{0};
    std::uint64_t overruns{0};
    std::uint64_t pending_tick{0};      // meaningful when stopped_* is true
    bool stopped_before_push{false};    // stop observed between ticks
    bool stopped_during_push{false};    // stop observed while ring full
};

template <class ClockT = std::chrono::steady_clock, class PushFn, class TickFn, class PacerFn>
LoopResult run_work_loop(const WorkerConfig& cfg, std::atomic<std::uint64_t>* status,
                         std::stop_token stoken,
                         const volatile std::sig_atomic_t& signal_requested, PushFn&& push_fn,
                         TickFn&& tick_fn, PacerFn&& pacer_fn) {
    const auto stop_wanted = [&] {
        return stoken.stop_requested() || signal_requested != 0;
    };

    LoopResult r;
    for (std::uint64_t t = 0; t < cfg.ticks; ++t) {
        if (stop_wanted()) {
            r.stopped_before_push = true;
            r.pending_tick = t;
            return r;
        }
        const auto start = ClockT::now();
        auto payload = tick_fn(t);
        while (!push_fn(payload)) {
            if (stop_wanted()) {
                r.stopped_during_push = true;
                r.pending_tick = t;
                return r;
            }
            std::this_thread::yield();  // ring full: back off, retry
        }
        const auto elapsed = ClockT::now() - start;
        if (elapsed > cfg.cpu_budget) {
            ++r.overruns;
            if (status != nullptr) {
                shared_memory::set_flag(*status, shared_memory::WorkerStatusFlag::kOverrun);
            }
        }
        ++r.ticks_completed;
        if (t + 1 < cfg.ticks) {
            pacer_fn();
        }
    }
    return r;
}

// Convenience pacer for production: sleep one tick interval between ticks
// (outside the ring hot path, so sleeping is allowed).
inline void sleep_pacer(std::chrono::milliseconds interval) {
    std::this_thread::sleep_for(interval);
}

}  // namespace safety_crit::workers

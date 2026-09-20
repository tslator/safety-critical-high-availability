#include "safety_crit/workers/worker_entry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stop_token>
#include <thread>

#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/workers/signals.hpp"
#include "safety_crit/workers/work_loop.hpp"
#include "safety_crit/workers/workload.hpp"

namespace safety_crit::workers {

static_assert(sizeof(ProcessedData) <= shared_memory::kDefaultSlotBytes,
              "worker payload must fit one region slot");

namespace {

// Sleep `total` in 1 ms slices, aborting early when the stop flag fires.
// Keeps SIGTERM stop latency well under one tick without SA_RESTART tricks;
// sleeping is legal here because this runs strictly between ticks (outside
// the ring hot path).
void sleep_slice_slice(std::chrono::milliseconds total,
                       const volatile std::sig_atomic_t& stop_flag) {
    auto remaining = total;
    while (remaining > std::chrono::milliseconds::zero() && stop_flag == 0) {
        const auto slice =
            remaining < std::chrono::milliseconds(1) ? remaining : std::chrono::milliseconds(1);
        std::this_thread::sleep_for(slice);
        remaining -= slice;
    }
}

int run_hot(shared_memory::SharedRegion& region, const WorkerConfig& cfg,
            signals::SignalState& signal_state) {
    std::atomic<std::uint64_t>& status = region.worker_status[cfg.worker_idx].status;
    shared_memory::set_status(status, shared_memory::to_bits(shared_memory::WorkerStatusFlag::kRunning));

    std::stop_source src;  // never requested; stop rides the signal flag
    auto result = run_work_loop(
        cfg, &status, src.get_token(), signal_state.stop_requested,
        [&region, &cfg](const ProcessedData& payload) {
            return shared_memory::push(region, cfg.worker_idx, payload);
        },
        [&cfg](std::uint64_t tick) {
            return process_sensor_data(generate_raw_tick(cfg.seed_base, cfg.worker_idx, tick));
        },
        [&cfg, &signal_state] { sleep_slice_slice(cfg.tick_interval, signal_state.stop_requested); });

    // Leaving RUNNING behind would fake a live worker for the Phase 3/4
    // monitor and supervisor; overwrite with the exact IDLE word (also
    // clears any OVERRUN bit from this run -- the flag means "since last
    // clear").
    shared_memory::set_status(status, shared_memory::to_bits(shared_memory::WorkerStatusFlag::kIdle));

    std::printf("worker %u (hot): %llu ticks, %llu overruns\n", cfg.worker_idx,
                static_cast<unsigned long long>(result.ticks_completed),
                static_cast<unsigned long long>(result.overruns));
    return 0;
}

int run_standby(shared_memory::SharedRegion& region, const WorkerConfig& cfg,
                signals::SignalState& signal_state) {
    std::atomic<std::uint64_t>& status = region.worker_status[cfg.worker_idx].status;
    shared_memory::set_status(status, shared_memory::to_bits(shared_memory::WorkerStatusFlag::kIdle));

    // Warm standby: observe sibling status flags, push nothing (DEC-0009 #8).
    // Takeover *decisions* are Phase 4 supervisor scope; the standby merely
    // stays responsive so that phase can promote it.
    std::uint64_t polls = 0;
    while (signal_state.stop_requested == 0) {
        std::uint64_t active_siblings = 0;
        for (std::size_t i = 0; i < shared_memory::kMaxWorkers; ++i) {
            if (i == cfg.worker_idx) {
                continue;
            }
            if (shared_memory::load_has_flag(region.worker_status[i].status,
                                             shared_memory::WorkerStatusFlag::kRunning)) {
                ++active_siblings;
            }
        }
        (void)active_siblings;  // observable for a debugger/Phase 3; no writes
        ++polls;
        sleep_slice_slice(std::chrono::milliseconds(100), signal_state.stop_requested);
    }
    std::printf("worker %u (standby): %llu status polls\n", cfg.worker_idx,
                static_cast<unsigned long long>(polls));
    return 0;
}
}  // namespace

int run_worker(const WorkerConfig& cfg, const char* region_name) {
    if (!validate_config(cfg)) {
        std::fprintf(stderr, "worker: invalid configuration\n");
        return 2;
    }

    shared_memory::SharedRegionHandle handle =
        shared_memory::SharedRegionHandle::create_or_open(region_name);
    if (!handle.ok()) {
        std::fprintf(stderr, "worker: attach to '%s' failed: %s (errno %d)\n", region_name,
                     shared_memory::to_string(handle.error()), handle.errnum());
        return 1;
    }

    signals::SignalState signal_state;
    if (!signals::install(signal_state)) {
        std::fprintf(stderr, "worker: signal handler installation failed\n");
        return 1;
    }

    int rc = 0;
    if (cfg.role == WorkerRole::kHot) {
        rc = run_hot(*handle.get(), cfg, signal_state);
    } else {
        rc = run_standby(*handle.get(), cfg, signal_state);
    }

    signals::uninstall();
    handle.detach();
    return rc;
}

}  // namespace safety_crit::workers

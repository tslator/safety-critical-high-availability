#include "safety_crit/workers/worker_entry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stop_token>
#include <thread>

#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/runtime/scheduling.hpp"
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
    shared_memory::OwnershipToken ownership;
    if (!acknowledge_ownership(region, cfg, ownership)) {
        std::fprintf(stderr, "worker %u: ownership acknowledgement failed\n", cfg.worker_idx);
        return 3;
    }
    std::atomic<std::uint64_t>& status = region.worker_status[cfg.worker_idx].status;
    shared_memory::set_status(status, shared_memory::to_bits(shared_memory::WorkerStatusFlag::kRunning));

    std::stop_source src;  // never requested; stop rides the signal flag
    auto result = run_work_loop(
        cfg, &status, src.get_token(), signal_state.stop_requested,
        [&region, &cfg, &ownership, &src, &signal_state](const ProcessedData& payload) {
            // A process that loses its generation must stop retrying rather
            // than publish to a ring now owned by its replacement.
            if (!shared_memory::ownership_valid(region, ownership)) {
                src.request_stop();
                return false;
            }
            // T-0027 (DEC-0012 #5): the poison-next-slot flag is checked and
            // consumed here, between the loop's ticks and outside the ring
            // hot path; one flag, one poisoned push, then normal operation.
            if (signal_state.poison_next_slot != 0) {
                signal_state.poison_next_slot = 0;
                return shared_memory::push_with_bad_crc(region, ownership.logical_ring,
                                                        payload);
            }
            return shared_memory::push(region, ownership.logical_ring, payload);
        },
        [&cfg, &ownership](std::uint64_t tick) {
            return process_sensor_data(
                generate_raw_tick(cfg.seed_base, ownership.logical_ring, tick));
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

    // Warm standby: observe ownership records, push nothing until the
    // supervisor has transferred one logical ring to this process.
    std::uint64_t polls = 0;
    while (signal_state.stop_requested == 0) {
        for (std::size_t logical_ring = 0; logical_ring < shared_memory::kMaxWorkers;
             ++logical_ring) {
            if (cfg.logical_ring != shared_memory::kUnassignedPhysicalOwner &&
                cfg.logical_ring != logical_ring) {
                continue;
            }
            if (cfg.logical_ring == shared_memory::kUnassignedPhysicalOwner &&
                logical_ring == cfg.worker_idx) {
                continue;  // the replacement remains standby until assigned
            }
            shared_memory::OwnershipToken token;
            WorkerConfig promoted = cfg;
            promoted.logical_ring = static_cast<std::uint32_t>(logical_ring);
            if (acknowledge_ownership(region, promoted, token)) {
                promoted.process_generation = token.process_generation;
                return run_hot(region, promoted, signal_state);
            }
        }
        ++polls;
        sleep_slice_slice(std::chrono::milliseconds(100), signal_state.stop_requested);
    }
    std::printf("worker %u (standby): %llu status polls\n", cfg.worker_idx,
                static_cast<unsigned long long>(polls));
    return 0;
}
}  // namespace

bool acknowledge_ownership(const shared_memory::SharedRegion& region,
                           const WorkerConfig& cfg, shared_memory::OwnershipToken& token) {
    if (cfg.logical_ring == shared_memory::kUnassignedPhysicalOwner ||
        !shared_memory::read_ownership(region, cfg.logical_ring, token)) {
        return false;
    }
    return token.physical_owner == cfg.worker_idx &&
           token.process_generation == cfg.process_generation;
}

int run_worker(const WorkerConfig& cfg, const char* region_name, const char* pid_dir) {
    const runtime::SchedulingResult scheduling =
        runtime::apply_scheduling(runtime::ProcessRole::kWorker);
    if (scheduling.fallback) {
        std::fprintf(stderr, "worker: scheduling fallback (errno %d)\n",
                     scheduling.error_number);
    }
    if (!validate_config(cfg)) {
        std::fprintf(stderr, "worker: invalid configuration\n");
        return 2;
    }

    WorkerConfig effective_cfg = cfg;
    if (effective_cfg.role == WorkerRole::kHot &&
        effective_cfg.logical_ring == shared_memory::kUnassignedPhysicalOwner) {
        effective_cfg.logical_ring = effective_cfg.worker_idx;
    }

    shared_memory::SharedRegionHandle handle =
        shared_memory::SharedRegionHandle::create_or_open(region_name);
    if (!handle.ok()) {
        std::fprintf(stderr, "worker: attach to '%s' failed: %s (errno %d)\n", region_name,
                     shared_memory::to_string(handle.error()), handle.errnum());
        return 1;
    }

    // Fatal on failure: a live worker without a pidfile is indistinguishable
    // from a crashed one for the monitor (DEC-0010 #2).
    if (!write_worker_pidfile(pid_dir, cfg.worker_idx)) {
        std::fprintf(stderr, "worker: cannot write pidfile in '%s'\n", pid_dir);
        handle.detach();
        return 1;
    }

    signals::SignalState signal_state;
    if (!signals::install(signal_state)) {
        std::fprintf(stderr, "worker: signal handler installation failed\n");
        remove_worker_pidfile(pid_dir, cfg.worker_idx);
        handle.detach();
        return 1;
    }
    // T-0027 (DEC-0012 #5): the SIGUSR2 corruption hook is test surface only;
    // the production default installs nothing extra (hot-path policy
    // DEC-0009 #4 stays in force).
    if (corruption_hook_opt_in(cfg.corrupt_hook, std::getenv("SAFETY_CRIT_CORRUPT_HOOK"))) {
        if (!signals::install_corruption_hook(signal_state)) {
            std::fprintf(stderr, "worker: corruption hook installation failed\n");
            remove_worker_pidfile(pid_dir, cfg.worker_idx);
            handle.detach();
            return 1;
        }
    }

    int rc = 0;
    if (effective_cfg.role == WorkerRole::kHot) {
        rc = run_hot(*handle.get(), effective_cfg, signal_state);
    } else {
        rc = run_standby(*handle.get(), effective_cfg, signal_state);
    }

    // Clean exit only: the IDLE status published above plus pidfile removal
    // is the "gone quietly" signature the monitor classifies as idle, never
    // as a crash (a crash never reaches this line).
    remove_worker_pidfile(pid_dir, cfg.worker_idx);
    signals::uninstall();
    handle.detach();
    return rc;
}

}  // namespace safety_crit::workers

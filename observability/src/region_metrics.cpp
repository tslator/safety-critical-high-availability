// Read-only region metrics collector implementation (T-0041, DEC-0014 §8/§11).

#include "safety_crit/observability/region_metrics.hpp"

#include <atomic>
#include <cerrno>
#include <string>

#include "safety_crit/shared_memory/atomic_flags.hpp"

namespace safety_crit::observability {

namespace shm = safety_crit::shared_memory;

double worker_status_metric_value(std::uint64_t status_word) {
    // Precedence DEGRADED > RECOVERING > CRASHED > RUNNING > IDLE; the zero
    // word (never started) falls through to 0.
    if (shm::has_flag(status_word, shm::WorkerStatusFlag::kDegraded)) {
        return 4.0;
    }
    if (shm::has_flag(status_word, shm::WorkerStatusFlag::kRecovering)) {
        return 3.0;
    }
    if (shm::has_flag(status_word, shm::WorkerStatusFlag::kCrashed)) {
        return 2.0;
    }
    if (shm::has_flag(status_word, shm::WorkerStatusFlag::kRunning)) {
        return 1.0;
    }
    return 0.0;
}

bool declare_region_metrics(MetricsRegistry& registry, std::error_code& ec) {
    if (!registry.declare_family("worker_status", "Worker state: "
                                                  "0=IDLE,1=RUNNING,2=CRASHED,3=RECOVERING,4=DEGRADED",
                                 MetricType::kGauge, "worker", ec)) {
        return false;
    }
    if (!registry.declare_family("ring_buffer_sequence", "Committed sequence per logical ring",
                                 MetricType::kGauge, "worker", ec)) {
        return false;
    }
    if (!registry.declare_family("ring_corruptions_total", "Corrupt slots skipped per logical ring",
                                 MetricType::kCounter, "worker", ec)) {
        return false;
    }
    if (!registry.declare_family("ownership_epoch", "Ownership token epoch per logical ring",
                                 MetricType::kGauge, "ring", ec)) {
        return false;
    }
    return true;
}

bool collect_region_metrics(const shm::SharedRegion& region, MetricsRegistry& registry,
                            std::error_code& ec) {
    for (std::size_t i = 0; i < shm::kMaxWorkers; ++i) {
        const std::string worker = std::to_string(i);

        const std::uint64_t status =
            region.worker_status[i].status.load(std::memory_order_acquire);
        if (!registry.set("worker_status", worker, worker_status_metric_value(status), ec)) {
            return false;
        }

        const double sequence =
            static_cast<double>(region.rings[i].pushed());
        if (!registry.set("ring_buffer_sequence", worker, sequence, ec)) {
            return false;
        }

        const double corruptions =
            static_cast<double>(region.rings[i].corruption_count());
        if (!registry.set("ring_corruptions_total", worker, corruptions, ec)) {
            return false;
        }

        shm::OwnershipToken token{};
        // read_ownership() is the sanctioned epoch reader. It fails for rings
        // that are unassigned (epoch 0) or mid-transfer; the collector then
        // keeps the last reported epoch (absent until the first successful
        // read) instead of fabricating a value.
        if (shm::read_ownership(region, i, token) &&
            !registry.set("ownership_epoch", worker,
                          static_cast<double>(token.epoch), ec)) {
            return false;
        }
    }
    ec.clear();
    return true;
}

}  // namespace safety_crit::observability

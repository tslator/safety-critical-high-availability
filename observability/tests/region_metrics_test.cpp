// T-0041 unit tests for the read-only region metrics collector: the pinned
// DEC-0014 §8 worker_status mapping, family declarations, gauge values equal
// region state at render time, visibility of status flips / pushes /
// corruptions / ownership transfers across collect cycles, and the read-only
// proof (verify_identity + integrity word unchanged after collection).
#include "test_framework.hpp"

#include <cstdint>
#include <string>

#include "safety_crit/observability/metrics.hpp"
#include "safety_crit/observability/region_metrics.hpp"
#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace {

using namespace safety_crit::observability;
namespace shm = safety_crit::shared_memory;

std::string render(MetricsRegistry& registry) {
    std::string out;
    registry.render_prometheus(out);
    return out;
}

// True when `text` contains `line` as a full exposition line.
bool has_line(const std::string& text, const std::string& line) {
    const std::string padded = "\n" + text;
    return padded.find("\n" + line + "\n") != std::string::npos;
}

}  // namespace

SAFETY_CRIT_TEST_CASE(RegionMetrics, WorkerStatusMappingPinned) {
    using shm::WorkerStatusFlag;
    SAFETY_CRIT_ASSERT(worker_status_metric_value(0) == 0.0);
    SAFETY_CRIT_ASSERT(worker_status_metric_value(shm::to_bits(WorkerStatusFlag::kIdle)) == 0.0);
    SAFETY_CRIT_ASSERT(
        worker_status_metric_value(shm::to_bits(WorkerStatusFlag::kRunning)) == 1.0);
    SAFETY_CRIT_ASSERT(
        worker_status_metric_value(shm::to_bits(WorkerStatusFlag::kCrashed)) == 2.0);
    SAFETY_CRIT_ASSERT(
        worker_status_metric_value(shm::to_bits(WorkerStatusFlag::kRecovering)) == 3.0);
    SAFETY_CRIT_ASSERT(
        worker_status_metric_value(shm::to_bits(WorkerStatusFlag::kDegraded)) == 4.0);
    // Precedence DEGRADED > RECOVERING > CRASHED > RUNNING > IDLE.
    const std::uint64_t running = shm::to_bits(WorkerStatusFlag::kRunning);
    SAFETY_CRIT_ASSERT(worker_status_metric_value(
                           running | shm::to_bits(WorkerStatusFlag::kDegraded)) == 4.0);
    SAFETY_CRIT_ASSERT(worker_status_metric_value(
                           shm::to_bits(WorkerStatusFlag::kCrashed) |
                           shm::to_bits(WorkerStatusFlag::kRecovering)) == 3.0);
    SAFETY_CRIT_ASSERT(worker_status_metric_value(
                           running | shm::to_bits(WorkerStatusFlag::kCrashed)) == 2.0);
    SAFETY_CRIT_ASSERT(worker_status_metric_value(
                           running | shm::to_bits(WorkerStatusFlag::kIdle)) == 1.0);
    // kOverrun is not a state: it never changes the classification.
    SAFETY_CRIT_ASSERT(worker_status_metric_value(
                           running | shm::to_bits(WorkerStatusFlag::kOverrun)) == 1.0);
    SAFETY_CRIT_ASSERT(
        worker_status_metric_value(shm::to_bits(WorkerStatusFlag::kOverrun)) == 0.0);
}

SAFETY_CRIT_TEST_CASE(RegionMetrics, DeclarationCreatesAllRegionFamilies) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_region_metrics(registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::error_code{});
    SAFETY_CRIT_ASSERT(registry.family_count() == 4);
    SAFETY_CRIT_ASSERT(registry.has_family("worker_status"));
    SAFETY_CRIT_ASSERT(registry.has_family("ring_buffer_sequence"));
    SAFETY_CRIT_ASSERT(registry.has_family("ring_corruptions_total"));
    SAFETY_CRIT_ASSERT(registry.has_family("ownership_epoch"));
    // Duplicate declaration rejected with EEXIST.
    SAFETY_CRIT_ASSERT(!declare_region_metrics(registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::errc::file_exists);
}

SAFETY_CRIT_TEST_CASE(RegionMetrics, CollectReflectsInitializedRegion) {
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_region_metrics(registry, ec));
    SAFETY_CRIT_ASSERT(collect_region_metrics(region, registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::error_code{});
    const std::string text = render(registry);
    for (std::size_t i = 0; i < shm::kMaxWorkers; ++i) {
        const std::string idx = std::to_string(i);
        SAFETY_CRIT_ASSERT(has_line(text, "worker_status{worker=\"" + idx + "\"} 0"));
        SAFETY_CRIT_ASSERT(has_line(text, "ring_buffer_sequence{worker=\"" + idx + "\"} 0"));
        SAFETY_CRIT_ASSERT(has_line(text, "ring_corruptions_total{worker=\"" + idx + "\"} 0"));
        // Region initialization hands ring i to physical i at epoch 2.
        SAFETY_CRIT_ASSERT(has_line(text, "ownership_epoch{ring=\"" + idx + "\"} 2"));
    }
    SAFETY_CRIT_ASSERT(registry.sample_count("worker_status") == shm::kMaxWorkers);
}

SAFETY_CRIT_TEST_CASE(RegionMetrics, StateChangesVisibleNextCycle) {
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_region_metrics(registry, ec));
    SAFETY_CRIT_ASSERT(collect_region_metrics(region, registry, ec));

    // Fixture-side state changes (the collector itself never writes).
    shm::set_status(region.worker_status[1].status, shm::to_bits(shm::WorkerStatusFlag::kRunning));
    const std::uint64_t value = 0x0102030405060708ull;
    SAFETY_CRIT_ASSERT(shm::push(region, 1, value));
    SAFETY_CRIT_ASSERT(shm::push_with_bad_crc(region, 2, value));
    std::uint64_t drained = 0;
    SAFETY_CRIT_ASSERT(!region.rings[2].try_pop(drained));  // skip-and-count path
    shm::OwnershipToken expected{};
    SAFETY_CRIT_ASSERT(shm::read_ownership(region, 0, expected));
    shm::OwnershipToken replacement{};
    SAFETY_CRIT_ASSERT(shm::transfer_ownership(region, 0, expected, 2u, 2u, replacement));

    SAFETY_CRIT_ASSERT(collect_region_metrics(region, registry, ec));
    const std::string text = render(registry);
    SAFETY_CRIT_ASSERT(has_line(text, "worker_status{worker=\"0\"} 0"));
    SAFETY_CRIT_ASSERT(has_line(text, "worker_status{worker=\"1\"} 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "ring_buffer_sequence{worker=\"0\"} 0"));
    SAFETY_CRIT_ASSERT(has_line(text, "ring_buffer_sequence{worker=\"1\"} 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "ring_corruptions_total{worker=\"1\"} 0"));
    SAFETY_CRIT_ASSERT(has_line(text, "ring_corruptions_total{worker=\"2\"} 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "ownership_epoch{ring=\"0\"} 4"));
    SAFETY_CRIT_ASSERT(has_line(text, "ownership_epoch{ring=\"1\"} 2"));
}

SAFETY_CRIT_TEST_CASE(RegionMetrics, StatusPrecedenceThroughRegion) {
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_region_metrics(registry, ec));
    // DEGRADED wins over RUNNING on the same status cell.
    shm::set_status(region.worker_status[0].status,
                    shm::to_bits(shm::WorkerStatusFlag::kRunning) |
                        shm::to_bits(shm::WorkerStatusFlag::kDegraded));
    SAFETY_CRIT_ASSERT(collect_region_metrics(region, registry, ec));
    SAFETY_CRIT_ASSERT(has_line(render(registry), "worker_status{worker=\"0\"} 4"));
}

SAFETY_CRIT_TEST_CASE(RegionMetrics, CollectionIsReadOnly) {
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    const std::uint64_t value = 0xfeedfacecafebeefull;
    SAFETY_CRIT_ASSERT(shm::push(region, 0, value));
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_region_metrics(registry, ec));
    SAFETY_CRIT_ASSERT(collect_region_metrics(region, registry, ec));
    SAFETY_CRIT_ASSERT(collect_region_metrics(region, registry, ec));
    // Read-only proof (DEC-0014 §11): identity and region-integrity state are
    // untouched by collection; the second collect renders byte-identically.
    SAFETY_CRIT_ASSERT(shm::verify_identity(region));
    SAFETY_CRIT_ASSERT(region.integrity_word.load(std::memory_order_relaxed) ==
                       shm::compute_region_integrity(region));
    SAFETY_CRIT_ASSERT(shm::verify_worker_ring(region, 0));
    std::string after;
    registry.render_prometheus(after);
    SAFETY_CRIT_ASSERT(render(registry) == after);
}

SAFETY_CRIT_TEST_CASE(RegionMetrics, CollectRequiresDeclaredFamilies) {
    shm::SharedRegion region;
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(!collect_region_metrics(region, registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::errc::no_such_file_or_directory);
    SAFETY_CRIT_ASSERT(registry.family_count() == 0);
}

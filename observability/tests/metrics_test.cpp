// T-0040 unit tests for the metrics registry and Prometheus text encoder:
// type/help rendering, label-value escaping, deterministic ordering,
// empty-registry output, declaration validation, atomic update visibility,
// and a concurrent scrape-vs-update exercise (TSan leg).
#include "test_framework.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>

#include "safety_crit/observability/metrics.hpp"

namespace {

using namespace safety_crit::observability;

std::string render(MetricsRegistry& registry) {
    std::string out;
    registry.render_prometheus(out);
    return out;
}

bool declare(MetricsRegistry& registry, std::string_view name, MetricType type,
             std::string_view label_key, std::error_code& ec) {
    return registry.declare_family(name, "help text", type, label_key, ec);
}

}  // namespace

SAFETY_CRIT_TEST_CASE(MetricsRegistry, EmptyRegistryRendersEmpty) {
    MetricsRegistry registry;
    SAFETY_CRIT_ASSERT(render(registry).empty());
    SAFETY_CRIT_ASSERT(registry.family_count() == 0);
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, TypeHelpAndSampleRendering) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare(registry, "observability_up", MetricType::kGauge, "", ec));
    SAFETY_CRIT_ASSERT(registry.set("observability_up", "", 1.0, ec));
    SAFETY_CRIT_ASSERT(ec == std::error_code{});
    SAFETY_CRIT_ASSERT(render(registry) ==
                       "# HELP observability_up help text\n"
                       "# TYPE observability_up gauge\n"
                       "observability_up 1\n");
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, CounterFamilyWithLabelSamples) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(
        declare(registry, "ring_corruptions_total", MetricType::kCounter, "worker", ec));
    SAFETY_CRIT_ASSERT(registry.add("ring_corruptions_total", "1", 3.0, ec));
    SAFETY_CRIT_ASSERT(registry.add("ring_corruptions_total", "0", 1.0, ec));
    SAFETY_CRIT_ASSERT(registry.add("ring_corruptions_total", "0", 2.0, ec));
    SAFETY_CRIT_ASSERT(render(registry) ==
                       "# HELP ring_corruptions_total help text\n"
                       "# TYPE ring_corruptions_total counter\n"
                       "ring_corruptions_total{worker=\"0\"} 3\n"
                       "ring_corruptions_total{worker=\"1\"} 3\n");
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, LabelValueEscaping) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare(registry, "perturbation_count_total", MetricType::kCounter,
                               "type", ec));
    SAFETY_CRIT_ASSERT(registry.set("perturbation_count_total", "a\\b\"c\nd", 1.0, ec));
    SAFETY_CRIT_ASSERT(registry.set("perturbation_count_total", "plain", 2.0, ec));
    SAFETY_CRIT_ASSERT(render(registry) ==
                       "# HELP perturbation_count_total help text\n"
                       "# TYPE perturbation_count_total counter\n"
                       "perturbation_count_total{type=\"a\\\\b\\\"c\\nd\"} 1\n"
                       "perturbation_count_total{type=\"plain\"} 2\n");
    std::string escaped;
    prometheus_escape_label_value("back\\\\slash", escaped);
    SAFETY_CRIT_ASSERT(escaped == "back\\\\\\\\slash");
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, DeterministicOrdering) {
    MetricsRegistry registry;
    std::error_code ec;
    // Declare out of alphabetical order; samples created out of order too.
    SAFETY_CRIT_ASSERT(declare(registry, "worker_status", MetricType::kGauge, "worker", ec));
    SAFETY_CRIT_ASSERT(registry.set("worker_status", "2", 2.0, ec));
    SAFETY_CRIT_ASSERT(registry.set("worker_status", "0", 0.0, ec));
    SAFETY_CRIT_ASSERT(registry.set("worker_status", "1", 1.0, ec));
    SAFETY_CRIT_ASSERT(declare(registry, "ownership_epoch", MetricType::kGauge, "ring", ec));
    SAFETY_CRIT_ASSERT(registry.set("ownership_epoch", "0", 7.0, ec));
    SAFETY_CRIT_ASSERT(registry.set("observability_up", "", 1.0, ec) == false);

    const std::string expected =
        "# HELP ownership_epoch help text\n"
        "# TYPE ownership_epoch gauge\n"
        "ownership_epoch{ring=\"0\"} 7\n"
        "# HELP worker_status help text\n"
        "# TYPE worker_status gauge\n"
        "worker_status{worker=\"0\"} 0\n"
        "worker_status{worker=\"1\"} 1\n"
        "worker_status{worker=\"2\"} 2\n";
    SAFETY_CRIT_ASSERT(render(registry) == expected);
    // Rendering twice is byte-identical.
    SAFETY_CRIT_ASSERT(render(registry) == expected);
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, DeclaredFamilyWithoutSamples) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare(registry, "data_loss_events_total", MetricType::kCounter, "", ec));
    SAFETY_CRIT_ASSERT(render(registry) ==
                       "# HELP data_loss_events_total help text\n"
                       "# TYPE data_loss_events_total counter\n");
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, DeclarationValidation) {
    MetricsRegistry registry;
    std::error_code ec;
    // Invalid metric names.
    SAFETY_CRIT_ASSERT(!declare(registry, "", MetricType::kGauge, "", ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::invalid_argument));
    SAFETY_CRIT_ASSERT(!declare(registry, "9lives", MetricType::kGauge, "", ec));
    SAFETY_CRIT_ASSERT(!declare(registry, "bad name", MetricType::kGauge, "", ec));
    SAFETY_CRIT_ASSERT(!declare(registry, "bad-dash", MetricType::kGauge, "", ec));
    // Colons are legal in metric names but not in label names.
    SAFETY_CRIT_ASSERT(declare(registry, "scrape_:requests", MetricType::kCounter, "", ec));
    SAFETY_CRIT_ASSERT(!declare(registry, "colon_label", MetricType::kCounter, "bad:key", ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::invalid_argument));
    // Duplicate declarations are rejected.
    SAFETY_CRIT_ASSERT(!declare(registry, "scrape_:requests", MetricType::kCounter, "", ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::file_exists));
    SAFETY_CRIT_ASSERT(registry.family_count() == 1);
    SAFETY_CRIT_ASSERT(registry.has_family("scrape_:requests"));
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, LookupAndLabelContract) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare(registry, "failover_duration_seconds", MetricType::kGauge, "", ec));
    SAFETY_CRIT_ASSERT(declare(registry, "event_log_gaps_total", MetricType::kCounter,
                               "component", ec));
    // Unknown family.
    SAFETY_CRIT_ASSERT(!registry.set("nope", "", 1.0, ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::no_such_file_or_directory));
    // Label on an unlabeled family is a contract violation.
    SAFETY_CRIT_ASSERT(!registry.set("failover_duration_seconds", "x", 1.0, ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::invalid_argument));
    // value() creates at 0 on first touch and returns a stable reference.
    std::atomic<double>* sample = nullptr;
    SAFETY_CRIT_ASSERT(registry.value("failover_duration_seconds", "", sample, ec));
    SAFETY_CRIT_ASSERT(sample->load() == 0.0);
    sample->store(0.084, std::memory_order_relaxed);
    std::atomic<double>* again = nullptr;
    SAFETY_CRIT_ASSERT(registry.value("failover_duration_seconds", "", again, ec));
    SAFETY_CRIT_ASSERT(again == sample);
    SAFETY_CRIT_ASSERT(again->load() == 0.084);
    SAFETY_CRIT_ASSERT(registry.sample_count("failover_duration_seconds") == 1);
    SAFETY_CRIT_ASSERT(registry.sample_count("event_log_gaps_total") == 0);
    SAFETY_CRIT_ASSERT(registry.sample_count("missing") == 0);
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, ValueFormatting) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare(registry, "gauges", MetricType::kGauge, "which", ec));
    SAFETY_CRIT_ASSERT(registry.set("gauges", "i", 42.0, ec));
    SAFETY_CRIT_ASSERT(registry.set("gauges", "f", 1.5, ec));
    SAFETY_CRIT_ASSERT(registry.set("gauges", "big", 1e20, ec));
    SAFETY_CRIT_ASSERT(registry.set("gauges", "nan", std::nan(""), ec));
    SAFETY_CRIT_ASSERT(registry.set("gauges", "inf", -std::numeric_limits<double>::infinity(), ec));
    const std::string out = render(registry);
    SAFETY_CRIT_ASSERT(out.find("gauges{which=\"big\"} 1e+20") != std::string::npos);
    SAFETY_CRIT_ASSERT(out.find("gauges{which=\"f\"} 1.5") != std::string::npos);
    SAFETY_CRIT_ASSERT(out.find("gauges{which=\"i\"} 42") != std::string::npos);
    SAFETY_CRIT_ASSERT(out.find("gauges{which=\"inf\"} -Inf") != std::string::npos);
    SAFETY_CRIT_ASSERT(out.find("gauges{which=\"nan\"} NaN") != std::string::npos);
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, AtomicUpdatesVisibleOnNextRender) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare(registry, "ring_buffer_sequence", MetricType::kGauge, "worker", ec));
    std::atomic<double>* sample = nullptr;
    SAFETY_CRIT_ASSERT(registry.value("ring_buffer_sequence", "0", sample, ec));
    for (const char* v : {"1", "2", "3"}) {
        sample->store(std::strtod(v, nullptr), std::memory_order_relaxed);
        SAFETY_CRIT_ASSERT(render(registry).find(std::string("ring_buffer_sequence{worker=\"0\"} ") +
                                                 v) != std::string::npos);
    }
}

SAFETY_CRIT_TEST_CASE(MetricsRegistry, ConcurrentScrapeVersusUpdate) {
    // Structure is single-writer (this thread); the updater thread only
    // stores through the atomic handle — legal from any thread and the
    // exact pattern exercised under TSan in CI.
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare(registry, "scrape_updates_total", MetricType::kCounter, "", ec));
    std::atomic<double>* sample = nullptr;
    SAFETY_CRIT_ASSERT(registry.value("scrape_updates_total", "", sample, ec));

    std::atomic<bool> stop{false};
    std::thread updater([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            sample->fetch_add(1.0, std::memory_order_relaxed);
        }
    });
    std::size_t scrapes = 0;
    double last = -1.0;
    while (scrapes < 100) {
        std::string out;
        registry.render_prometheus(out);
        SAFETY_CRIT_ASSERT(out.find("# TYPE scrape_updates_total counter") != std::string::npos);
        const std::size_t pos = out.rfind("\nscrape_updates_total ");  // sample line, not # TYPE
        SAFETY_CRIT_ASSERT(pos != std::string::npos);
        const double value = std::strtod(out.c_str() + pos + 21, nullptr);
        SAFETY_CRIT_ASSERT(value >= last);  // counter never decreases
        last = value;
        ++scrapes;
    }
    stop.store(true, std::memory_order_relaxed);
    updater.join();
    SAFETY_CRIT_ASSERT(last > 0.0);
}

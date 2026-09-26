// T-0042 unit tests for the event-log-tail metrics collector: exact counter
// values from replayed synthetic event-log fixtures (perturbation counts by
// type, failover gauge last-write-wins, per-component gap counters, uptime
// from an injected clock), the structural independence of
// data_loss_events_total from event-log content, and torn-trailing-line
// retry semantics inherited from EventLogReader.
#include "test_framework.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <unistd.h>

#include "safety_crit/observability/event_log.hpp"
#include "safety_crit/observability/event_metrics.hpp"
#include "safety_crit/observability/metrics.hpp"

namespace {

using namespace safety_crit::observability;

std::string temp_path(const char* tag) {
    return "/tmp/safety_crit_ha_eventmetrics_" + std::to_string(static_cast<long>(::getpid())) +
           "_" + tag + ".jsonl";
}

std::string render(MetricsRegistry& registry) {
    std::string out;
    registry.render_prometheus(out);
    return out;
}

bool has_line(const std::string& text, const std::string& line) {
    const std::string padded = "\n" + text;
    return padded.find("\n" + line + "\n") != std::string::npos;
}

// Appends one schema v1 line verbatim (for fixtures the writer API cannot
// produce, e.g. deliberate sequence gaps).
void write_line(const std::string& path, const std::string& line, bool truncate) {
    std::ofstream out(path, truncate ? std::ios::trunc : std::ios::app);
    out << line << "\n";
    SAFETY_CRIT_ASSERT(out.good());
}

}  // namespace

SAFETY_CRIT_TEST_CASE(EventMetrics, DeclarationCreatesAllEventFamilies) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::error_code{});
    SAFETY_CRIT_ASSERT(registry.family_count() == 6);
    SAFETY_CRIT_ASSERT(registry.has_family("perturbation_count_total"));
    SAFETY_CRIT_ASSERT(registry.has_family("failover_duration_seconds"));
    SAFETY_CRIT_ASSERT(registry.has_family("data_loss_events_total"));
    SAFETY_CRIT_ASSERT(registry.has_family("event_log_gaps_total"));
    SAFETY_CRIT_ASSERT(registry.has_family("observability_up"));
    SAFETY_CRIT_ASSERT(registry.has_family("observability_uptime_seconds"));
    SAFETY_CRIT_ASSERT(!declare_event_metrics(registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::errc::file_exists);
}

SAFETY_CRIT_TEST_CASE(EventMetrics, ReplayFixtureExactCounters) {
    const std::string path = temp_path("replay");
    std::error_code ec;
    EventLogWriterOptions options{};
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(path, "perturb", options, ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "perturbation", "\"category\":\"crash\"", ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "perturbation", "\"category\":\"crash\"", ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "perturbation", "\"category\":\"stall\"", ec));
    SAFETY_CRIT_ASSERT(
        writer.append(LogLevel::kInfo, "perturbation", "\"category\":\"corrupt\"", ec));
    SAFETY_CRIT_ASSERT(
        writer.append(LogLevel::kInfo, "perturbation", "\"category\":\"double-fault\"", ec));
    // Unknown categories are ignored, never an error.
    SAFETY_CRIT_ASSERT(
        writer.append(LogLevel::kInfo, "perturbation", "\"category\":\"bogus\"", ec));
    SAFETY_CRIT_ASSERT(
        writer.append(LogLevel::kWarn, "failover_recovered", "\"latency_ms\":1234", ec));
    writer.close();

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(path, ec));
    MetricsRegistry registry;
    SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
    EventMetricsCollector collector{};
    SAFETY_CRIT_ASSERT(collector.poll(reader, registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::error_code{});
    const std::string text = render(registry);
    SAFETY_CRIT_ASSERT(has_line(text, "perturbation_count_total{type=\"crash\"} 2"));
    SAFETY_CRIT_ASSERT(has_line(text, "perturbation_count_total{type=\"stall\"} 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "perturbation_count_total{type=\"corrupt\"} 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "perturbation_count_total{type=\"double-fault\"} 1"));
    SAFETY_CRIT_ASSERT(!has_line(text, "perturbation_count_total{type=\"bogus\"} 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "failover_duration_seconds 1.234"));
    SAFETY_CRIT_ASSERT(has_line(text, "observability_up 1"));
    ::remove(path.c_str());
}

SAFETY_CRIT_TEST_CASE(EventMetrics, FailoverGaugeLastWriteWins) {
    const std::string path = temp_path("failover");
    std::error_code ec;
    EventLogWriterOptions options{};
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(path, "supervisor", options, ec));
    SAFETY_CRIT_ASSERT(
        writer.append(LogLevel::kWarn, "failover_recovered", "\"latency_ms\":100", ec));
    SAFETY_CRIT_ASSERT(
        writer.append(LogLevel::kWarn, "failover_recovered", "\"latency_ms\":250", ec));
    writer.close();

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(path, ec));
    MetricsRegistry registry;
    SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
    EventMetricsCollector collector{};
    SAFETY_CRIT_ASSERT(collector.poll(reader, registry, ec));
    SAFETY_CRIT_ASSERT(has_line(render(registry), "failover_duration_seconds 0.25"));
    ::remove(path.c_str());
}

SAFETY_CRIT_TEST_CASE(EventMetrics, LogGapsNeverTouchDataLoss) {
    // Fixture the writer API cannot produce: a deliberate seq jump (1, 3).
    const std::string path = temp_path("gaps");
    write_line(path,
               "{\"schema\":1,\"ts\":1,\"level\":\"info\",\"component\":\"supervisor\","
               "\"seq\":1,\"event\":\"a\"}",
               true);
    write_line(path,
               "{\"schema\":1,\"ts\":2,\"level\":\"info\",\"component\":\"supervisor\","
               "\"seq\":3,\"event\":\"b\"}",
               false);

    std::error_code ec;
    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(path, ec));
    MetricsRegistry registry;
    SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
    EventMetricsCollector collector{};
    SAFETY_CRIT_ASSERT(collector.poll(reader, registry, ec));
    std::string text = render(registry);
    SAFETY_CRIT_ASSERT(has_line(text, "event_log_gaps_total{component=\"supervisor\"} 1"));
    // Data plane untouched: the family exists but has no sample.
    SAFETY_CRIT_ASSERT(registry.sample_count("data_loss_events_total") == 0);

    // observe_data_loss moves only the data-loss counter.
    SAFETY_CRIT_ASSERT(collector.observe_data_loss(3, registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::error_code{});
    text = render(registry);
    SAFETY_CRIT_ASSERT(has_line(text, "data_loss_events_total 3"));
    SAFETY_CRIT_ASSERT(has_line(text, "event_log_gaps_total{component=\"supervisor\"} 1"));
    SAFETY_CRIT_ASSERT(registry.sample_count("data_loss_events_total") == 1);
    ::remove(path.c_str());
}

SAFETY_CRIT_TEST_CASE(EventMetrics, TornTrailingLineRetriedNextPoll) {
    const std::string path = temp_path("torn");
    std::error_code ec;
    EventLogWriterOptions options{};
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(path, "perturb", options, ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "perturbation", "\"category\":\"crash\"", ec));
    writer.close();

    // Half a record: no newline terminator, will be completed later.
    std::ofstream partial(path, std::ios::app);
    partial << "{\"schema\":1,\"ts\":2,\"level\":\"info\",\"component\":\"perturb\",\"seq\":2,"
            << "\"event\":\"perturb\",\"cate";
    SAFETY_CRIT_ASSERT(partial.good());
    partial.close();

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(path, ec));
    MetricsRegistry registry;
    SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
    EventMetricsCollector collector{};
    SAFETY_CRIT_ASSERT(collector.poll(reader, registry, ec));
    std::string text = render(registry);
    SAFETY_CRIT_ASSERT(has_line(text, "perturbation_count_total{type=\"crash\"} 1"));
    SAFETY_CRIT_ASSERT(registry.sample_count("perturbation_count_total") == 1);

    // Complete the torn line and re-poll from the persisted watermark (the
    // documented tail-follow resume): counted exactly once, on the second
    // cycle.
    std::ofstream rest(path, std::ios::app);
    rest << "gory\":\"stall\"}\n";
    SAFETY_CRIT_ASSERT(rest.good());
    rest.close();
    SAFETY_CRIT_ASSERT(reader.seek(reader.watermark(), ec));
    SAFETY_CRIT_ASSERT(collector.poll(reader, registry, ec));
    text = render(registry);
    SAFETY_CRIT_ASSERT(has_line(text, "perturbation_count_total{type=\"crash\"} 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "perturbation_count_total{type=\"stall\"} 1"));
    SAFETY_CRIT_ASSERT(registry.sample_count("perturbation_count_total") == 2);
    ::remove(path.c_str());
}

SAFETY_CRIT_TEST_CASE(EventMetrics, UptimeFromInjectableClock) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
    std::uint64_t fake_now = 1'000'000'000ULL;
    EventMetricsCollector collector([&fake_now]() { return fake_now; });
    // Up is reported from the first poll; before that no samples exist.
    SAFETY_CRIT_ASSERT(registry.sample_count("observability_up") == 0);
    const std::string path = temp_path("uptime");
    write_line(path,
               "{\"schema\":1,\"ts\":1,\"level\":\"info\",\"component\":\"supervisor\","
               "\"seq\":1,\"event\":\"a\"}",
               true);
    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(path, ec));
    SAFETY_CRIT_ASSERT(collector.poll(reader, registry, ec));
    SAFETY_CRIT_ASSERT(has_line(render(registry), "observability_uptime_seconds 0"));
    fake_now += 5'000'000'000ULL;
    SAFETY_CRIT_ASSERT(collector.poll(reader, registry, ec));
    std::string text = render(registry);
    SAFETY_CRIT_ASSERT(has_line(text, "observability_up 1"));
    SAFETY_CRIT_ASSERT(has_line(text, "observability_uptime_seconds 5"));
    ::remove(path.c_str());
}

SAFETY_CRIT_TEST_CASE(EventMetrics, CollectRequiresDeclaredFamilies) {
    const std::string path = temp_path("undeclared");
    write_line(path,
               "{\"schema\":1,\"ts\":1,\"level\":\"info\",\"component\":\"supervisor\","
               "\"seq\":1,\"event\":\"a\"}",
               true);
    std::error_code ec;
    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(path, ec));
    MetricsRegistry registry;
    EventMetricsCollector collector{};
    SAFETY_CRIT_ASSERT(!collector.poll(reader, registry, ec));
    SAFETY_CRIT_ASSERT(ec == std::errc::no_such_file_or_directory);
    SAFETY_CRIT_ASSERT(registry.family_count() == 0);
    ::remove(path.c_str());
}

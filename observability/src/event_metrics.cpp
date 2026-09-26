// Event-log-tail metrics collector implementation (T-0042, DEC-0014 §8).

#include "safety_crit/observability/event_metrics.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace safety_crit::observability {

namespace {

// Pinned perturbation categories (DEC-0014 §8); anything else the harness
// might log later is ignored, never an error.
constexpr std::array<std::string_view, 4> kPerturbationCategories = {
    "crash", "stall", "corrupt", "double-fault",
};

bool known_category(std::string_view category) {
    for (const std::string_view known : kPerturbationCategories) {
        if (category == known) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool find_json_string_field(const std::string& raw, std::string_view key, std::string& out) {
    const std::string needle = "\"" + std::string(key) + "\":\"";
    const std::size_t start = raw.find(needle);
    if (start == std::string::npos) {
        return false;
    }
    std::string result;
    for (std::size_t i = start + needle.size(); i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            const char next = raw[++i];
            switch (next) {
                case 'n': result.push_back('\n'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(next); break;
            }
            continue;
        }
        if (c == '"') {
            out = std::move(result);
            return true;
        }
        result.push_back(c);
    }
    return false;  // unterminated string
}

bool find_json_number_field(const std::string& raw, std::string_view key, double& out) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const std::size_t start = raw.find(needle);
    if (start == std::string::npos) {
        return false;
    }
    const char* begin = raw.c_str() + start + needle.size();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return false;
    }
    out = value;
    return true;
}

bool declare_event_metrics(MetricsRegistry& registry, std::error_code& ec) {
    if (!registry.declare_family("perturbation_count_total",
                                 "Perturbations by category (crash, stall, corrupt, "
                                 "double-fault)",
                                 MetricType::kCounter, "type", ec)) {
        return false;
    }
    if (!registry.declare_family("failover_duration_seconds",
                                 "Latest failover recovery latency", MetricType::kGauge, "", ec)) {
        return false;
    }
    if (!registry.declare_family("data_loss_events_total",
                                 "Data-plane drain-witness sequence gaps (CORE property #1)",
                                 MetricType::kCounter, "", ec)) {
        return false;
    }
    if (!registry.declare_family("event_log_gaps_total",
                                 "Control-plane event-log read gaps per sequence key",
                                 MetricType::kCounter, "component", ec)) {
        return false;
    }
    if (!registry.declare_family("observability_up", "Observability daemon liveness",
                                 MetricType::kGauge, "", ec)) {
        return false;
    }
    if (!registry.declare_family("observability_uptime_seconds",
                                 "Seconds since collector start", MetricType::kGauge, "", ec)) {
        return false;
    }
    return true;
}

EventMetricsCollector::EventMetricsCollector(std::function<std::uint64_t()> now)
    : now_(std::move(now)), start_ns_(now_()) {}

bool EventMetricsCollector::poll(EventLogReader& reader, MetricsRegistry& registry,
                                 std::error_code& ec) {
    EventRecord record{};
    std::string error{};
    for (;;) {
        const ReadStatus status = reader.read_next(record, error);
        if (status == ReadStatus::kEnd || status == ReadStatus::kIncomplete) {
            break;  // torn tail never advances counters past the watermark
        }
        if (status == ReadStatus::kError) {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
        if (record.component == "perturb") {
            std::string category{};
            if (find_json_string_field(record.raw, "category", category) &&
                known_category(category) &&
                !registry.add("perturbation_count_total", category, 1.0, ec)) {
                return false;
            }
        }
        if (record.event == "failover_recovered") {
            double latency_ms = 0.0;
            if (find_json_number_field(record.raw, "latency_ms", latency_ms) &&
                !registry.set("failover_duration_seconds", "", latency_ms / 1000.0, ec)) {
                return false;
            }
        }
    }

    // Reader continuity state is cumulative; set (not add) keeps re-polls
    // and watermark resumes idempotent.
    for (const auto& [key, state] : reader.states()) {
        if (!registry.set("event_log_gaps_total", key,
                          static_cast<double>(state.gaps), ec)) {
            return false;
        }
    }

    up_ = true;
    if (!registry.set("observability_up", "", 1.0, ec)) {
        return false;
    }
    const double uptime = static_cast<double>(now_() - start_ns_) / 1e9;
    if (!registry.set("observability_uptime_seconds", "", uptime, ec)) {
        return false;
    }
    ec.clear();
    return true;
}

bool EventMetricsCollector::observe_data_loss(std::uint64_t n, MetricsRegistry& registry,
                                              std::error_code& ec) {
    return registry.add("data_loss_events_total", "", static_cast<double>(n), ec);
}

}  // namespace safety_crit::observability

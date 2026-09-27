#include "safety_crit/observability/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace safety_crit::observability {
namespace {

bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_alnum(char c) {
    return is_alpha(c) || (c >= '0' && c <= '9');
}

std::error_code err(std::errc code) {
    return std::make_error_code(code);
}

}  // namespace

bool metric_type_to_name(MetricType type, const char*& name) {
    switch (type) {
        case MetricType::kGauge:
            name = "gauge";
            return true;
        case MetricType::kCounter:
            name = "counter";
            return true;
    }
    return false;
}

bool is_valid_metric_name(std::string_view name, bool allow_colon) {
    if (name.empty()) {
        return false;
    }
    if (!is_alpha(name.front()) && !(allow_colon && name.front() == ':')) {
        return false;
    }
    for (const char c : name) {
        if (!is_alnum(c) && !(allow_colon && c == ':')) {
            return false;
        }
    }
    return true;
}

void prometheus_escape_label_value(std::string_view value, std::string& out) {
    for (const char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                out += c;
                break;
        }
    }
}

void prometheus_format_value(double value, std::string& out) {
    if (std::isnan(value)) {
        out += "NaN";
        return;
    }
    if (std::isinf(value)) {
        out += value > 0 ? "+Inf" : "-Inf";
        return;
    }
    char buffer[64];
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        const int written = std::snprintf(buffer, sizeof(buffer), "%lld",
                                          static_cast<long long>(value));
        if (written > 0) {
            out.append(buffer, static_cast<std::size_t>(written));
            return;
        }
    }
    const int written = std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    if (written > 0) {
        out.append(buffer, static_cast<std::size_t>(written));
    }
}

bool MetricsRegistry::declare_family(std::string_view name, std::string_view help,
                                     MetricType type, std::string_view label_key,
                                     std::error_code& ec) {
    ec.clear();
    const char* type_name = nullptr;
    if (!metric_type_to_name(type, type_name)) {
        ec = err(std::errc::invalid_argument);
        return false;
    }
    if (!is_valid_metric_name(name, true)) {
        ec = err(std::errc::invalid_argument);
        return false;
    }
    if (!label_key.empty() && !is_valid_metric_name(label_key, false)) {
        ec = err(std::errc::invalid_argument);
        return false;
    }
    if (find_family(name) != nullptr) {
        ec = err(std::errc::file_exists);
        return false;
    }
    families_.push_back(Family{std::string(name), std::string(help), std::string(label_key),
                               type, {}});
    return true;
}

bool MetricsRegistry::value(std::string_view name, std::string_view label,
                            std::atomic<double>*& out, std::error_code& ec) {
    ec.clear();
    Family* family = find_family(name);
    if (family == nullptr) {
        ec = err(std::errc::no_such_file_or_directory);
        return false;
    }
    if (family->label_key.empty() && !label.empty()) {
        ec = err(std::errc::invalid_argument);
        return false;
    }
    Sample* sample = find_sample(*family, label);
    if (sample == nullptr) {
        family->samples.push_back(std::make_unique<Sample>(std::string(label), 0.0));
        sample = family->samples.back().get();
    }
    out = &sample->value;
    return true;
}

bool MetricsRegistry::set(std::string_view name, std::string_view label, double value,
                          std::error_code& ec) {
    std::atomic<double>* sample = nullptr;
    if (!this->value(name, label, sample, ec)) {
        return false;
    }
    sample->store(value, std::memory_order_relaxed);
    return true;
}

bool MetricsRegistry::add(std::string_view name, std::string_view label, double delta,
                          std::error_code& ec) {
    std::atomic<double>* sample = nullptr;
    if (!this->value(name, label, sample, ec)) {
        return false;
    }
    sample->fetch_add(delta, std::memory_order_relaxed);
    return true;
}

bool MetricsRegistry::get(std::string_view name, std::string_view label, double& out,
                          std::error_code& ec) const {
    ec.clear();
    const Family* family = find_family(name);
    if (family == nullptr) {
        ec = err(std::errc::no_such_file_or_directory);
        return false;
    }
    if (family->label_key.empty() && !label.empty()) {
        ec = err(std::errc::invalid_argument);
        return false;
    }
    const Sample* sample = find_sample(*family, label);
    if (sample == nullptr) {
        ec = err(std::errc::no_such_file_or_directory);
        return false;
    }
    out = sample->value.load(std::memory_order_relaxed);
    return true;
}

bool MetricsRegistry::has_family(std::string_view name) const {
    return find_family(name) != nullptr;
}

std::size_t MetricsRegistry::sample_count(std::string_view name) const {
    const Family* family = find_family(name);
    return family == nullptr ? 0u : family->samples.size();
}

void MetricsRegistry::render_prometheus(std::string& out) const {
    out.clear();
    std::vector<const Family*> sorted;
    sorted.reserve(families_.size());
    for (const Family& family : families_) {
        sorted.push_back(&family);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Family* lhs, const Family* rhs) { return lhs->name < rhs->name; });

    for (const Family* family : sorted) {
        out += "# HELP ";
        out += family->name;
        out += ' ';
        prometheus_escape_label_value(family->help, out);  // HELP uses the same escapes
        out += '\n';
        out += "# TYPE ";
        out += family->name;
        out += ' ';
        const char* type_name = "gauge";
        metric_type_to_name(family->type, type_name);
        out += type_name;
        out += '\n';

        std::vector<const Sample*> samples;
        samples.reserve(family->samples.size());
        for (const std::unique_ptr<Sample>& sample : family->samples) {
            samples.push_back(sample.get());
        }
        std::sort(samples.begin(), samples.end(), [](const Sample* lhs, const Sample* rhs) {
            return lhs->label < rhs->label;
        });
        for (const Sample* sample : samples) {
            out += family->name;
            if (!family->label_key.empty()) {
                out += '{';
                out += family->label_key;
                out += "=\"";
                prometheus_escape_label_value(sample->label, out);
                out += "\"}";
            }
            out += ' ';
            prometheus_format_value(sample->value.load(std::memory_order_relaxed), out);
            out += '\n';
        }
    }
}

MetricsRegistry::Family* MetricsRegistry::find_family(std::string_view name) {
    for (Family& family : families_) {
        if (family.name == name) {
            return &family;
        }
    }
    return nullptr;
}

const MetricsRegistry::Family* MetricsRegistry::find_family(std::string_view name) const {
    for (const Family& family : families_) {
        if (family.name == name) {
            return &family;
        }
    }
    return nullptr;
}

MetricsRegistry::Sample* MetricsRegistry::find_sample(Family& family, std::string_view label) {
    for (const std::unique_ptr<Sample>& sample : family.samples) {
        if (sample->label == label) {
            return sample.get();
        }
    }
    return nullptr;
}

const MetricsRegistry::Sample* MetricsRegistry::find_sample(const Family& family,
                                                            std::string_view label) {
    for (const std::unique_ptr<Sample>& sample : family.samples) {
        if (sample->label == label) {
            return sample.get();
        }
    }
    return nullptr;
}

}  // namespace safety_crit::observability

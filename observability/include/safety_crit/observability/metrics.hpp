// Metrics registry and Prometheus text encoder (T-0040, DEC-0014 §6/§8).
//
// Pure in-memory metrics layer for the Phase 6 observability daemon: atomically
// updated gauges/counters with name+label identity, rendered in Prometheus
// text exposition format v0.0.4 (`# HELP`, `# TYPE`), hand-rolled with the
// snprintf idiom. No region access, no event-log access, no sockets — the
// region collector (T-0041) and event collector (T-0042) feed a registry
// through this layer, and the daemon serves `render_prometheus()` bytes.
//
// Model:
//   - A *family* is a declared metric: name, help text, type (gauge|counter),
//     and an optional single label key. Every metric in the DEC-0014 §8 table
//     carries zero or one labels, so the label model stays a plain string.
//   - A *sample* is one (family, label) pair holding a std::atomic<double>.
//     Samples are created at 0 on first touch (collector setup or first
//     observed observation); steady-state updates are atomic stores on a
//     handle obtained from `value()`, and the scrape path reads the same
//     atomics. No locks anywhere: the family/sample *structure* is mutated
//     only from the single thread that owns the registry (the daemon main
//     loop), value updates flow through atomics and are legal from any
//     thread. Declaring or creating samples while rendering on another
//     thread is out of contract.
//
// Determinism: render output sorts families by name and samples by label
// value, so a scrape of an unchanged registry is byte-identical regardless
// of declaration or sample-creation order.
//
// Error handling follows the standing idiom: bool fn(..., std::error_code&
// ec) with generic-category codes (EINVAL invalid input, EEXIST duplicate
// declaration, ENOENT unknown family).

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace safety_crit::observability {

enum class MetricType : std::uint8_t {
    kGauge,
    kCounter,
};

// Canonical Prometheus type name ("gauge"|"counter"); false if `type` is not
// a valid enumerant.
bool metric_type_to_name(MetricType type, const char*& name);

// True when `name` is a valid Prometheus metric or label name:
// [a-zA-Z_:][a-zA-Z0-9_:]* for metrics; label names (checked with
// allow_colon=false) are [a-zA-Z_][a-zA-Z0-9_]*. Hand-rolled to keep the
// dependency allowlist (DEC-0014 §6).
bool is_valid_metric_name(std::string_view name, bool allow_colon = true);

// Appends the Prometheus label-value escaping of `value` to `out`:
// backslash -> \\, double quote -> \", newline -> \n (backslash first).
void prometheus_escape_label_value(std::string_view value, std::string& out);

// Appends `value` in Prometheus sample syntax: integers without a decimal
// point, NaN as "NaN", everything else via "%.10g".
void prometheus_format_value(double value, std::string& out);

class MetricsRegistry {
public:
    MetricsRegistry() = default;
    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;

    // Declares family `name` (validated; unique) with help text, type, and an
    // optional single label key (empty for unlabeled metrics; validated when
    // present). False with EEXIST/EINVAL on violations; `ec` cleared on
    // success.
    bool declare_family(std::string_view name, std::string_view help, MetricType type,
                        std::string_view label_key, std::error_code& ec);

    // Looks up the sample (name, label), creating it at 0 on first touch.
    // `label` must be empty for unlabeled families (EINVAL otherwise) and
    // may be any string for labeled families (escaping happens at render).
    // On success `out` references the sample's atomic; the reference stays
    // valid for the registry's lifetime (samples are never removed).
    bool value(std::string_view name, std::string_view label, std::atomic<double>*& out,
               std::error_code& ec);

    // Convenience wrappers over value(): false on unknown family or label
    // mismatch, otherwise a relaxed atomic store / fetch_add (single writer
    // per sample is not required; add is an atomic RMW).
    bool set(std::string_view name, std::string_view label, double value, std::error_code& ec);
    bool add(std::string_view name, std::string_view label, double delta, std::error_code& ec);

    bool has_family(std::string_view name) const;
    std::size_t family_count() const { return families_.size(); }
    std::size_t sample_count(std::string_view name) const;

    // Renders the full exposition body into `out` (replacing its previous
    // contents): per family `# HELP name help` / `# TYPE name type` lines
    // followed by `name{label="value"} sample` lines (or bare `name sample`
    // for unlabeled families). Families sorted by name, samples sorted by
    // label value. A declared family without samples renders its comment
    // lines only; an empty registry renders an empty string.
    void render_prometheus(std::string& out) const;

private:
    struct Sample {
        // atomic => non-copyable; heap-allocated so containers stay movable
        // and sample addresses are stable forever.
        std::string label{};
        std::atomic<double> value{0.0};
        Sample(std::string label_, double initial) : label(std::move(label_)), value(initial) {}
        Sample(const Sample&) = delete;
        Sample& operator=(const Sample&) = delete;
    };
    struct Family {
        std::string name{};
        std::string help{};
        std::string label_key{};  // empty = unlabeled
        MetricType type = MetricType::kGauge;
        std::vector<std::unique_ptr<Sample>> samples{};
    };

    Family* find_family(std::string_view name);
    const Family* find_family(std::string_view name) const;
    static const Sample* find_sample(const Family& family, std::string_view label);
    static Sample* find_sample(Family& family, std::string_view label);

    std::vector<Family> families_{};
};

}  // namespace safety_crit::observability

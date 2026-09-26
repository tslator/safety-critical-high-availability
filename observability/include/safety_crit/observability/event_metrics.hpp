// Event-log-tail metrics collector (T-0042, DEC-0014 §8).
//
// Bridges the control-plane event log to a MetricsRegistry: declares the
// DEC-0014 §8 event-derived families and updates them by draining records
// through an EventLogReader (tail-follow semantics inherited: torn trailing
// lines never advance counters past the watermark; the consumer resumes
// with reader.seek(reader.watermark()) between polls, as for any
// EventLogReader).
//
// Sources (DEC-0014 §8):
//   - perturbation_count_total{type}: records with component "perturb",
//     labeled by the "category" extra field (crash, stall, corrupt,
//     double-fault). Unknown categories and records without a category are
//     ignored, never an error.
//   - failover_duration_seconds: latest "failover_recovered" record's
//     latency_ms / 1000 (last write wins).
//   - event_log_gaps_total{component}: per-key gap counts from the reader's
//     continuity state, control plane. The label is the reader's continuity
//     key ("component" or "component/<instance>"); values are set (not
//     added) from the cumulative reader state, so re-polls and watermark
//     resumes never double count.
//   - observability_up / observability_uptime_seconds: daemon self-liveness;
//     `up` becomes 1 on the first poll (the collector is live), uptime is
//     derived from an injectable nanosecond clock (default CLOCK_REALTIME).
//
// data_loss_events_total (data plane, CORE property #1) is structurally
// decoupled from this file's read path: it is NEVER derived from event-log
// content. The embedding process (supervisor drain-witness gap source, wired
// at daemon assembly in T-0038) reports it through observe_data_loss(); the
// registry renders whatever its current value is.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <system_error>

#include "safety_crit/observability/event_log.hpp"
#include "safety_crit/observability/metrics.hpp"

namespace safety_crit::observability {

// Extracts a flat extra-field JSON string value from a record's raw line:
// the unescaped value of `"key":"..."`. False when the field is absent.
// Hand-rolled to keep the dependency allowlist (DEC-0014 §6); matches the
// writer's flat-field format only (schema v1 guarantees it).
bool find_json_string_field(const std::string& raw, std::string_view key, std::string& out);

// Extracts a flat extra-field JSON number value from a record's raw line:
// the value of `"key":<number>`. False when absent or not numeric.
bool find_json_number_field(const std::string& raw, std::string_view key, double& out);

// Declares the event-derived families on `registry` (DEC-0014 §8):
// perturbation_count_total{type} counter, failover_duration_seconds gauge,
// data_loss_events_total counter, event_log_gaps_total{component} counter,
// observability_up gauge, observability_uptime_seconds gauge. False with the
// registry's error on declaration violations.
bool declare_event_metrics(MetricsRegistry& registry, std::error_code& ec);

class EventMetricsCollector {
public:
    // Nanosecond clock (CLOCK_REALTIME default); uptime is measured from
    // construction to the clock value observed at each poll.
    explicit EventMetricsCollector(std::function<std::uint64_t()> now = &now_unix_ns);

    // Drains `reader` until kEnd or kIncomplete (torn tail), updating the
    // registry per the source table above; kError fails the call (ec = EIO)
    // after the records already counted are kept. Safe to call repeatedly;
    // the reader's watermark and continuity state provide resume.
    bool poll(EventLogReader& reader, MetricsRegistry& registry, std::error_code& ec);

    // Data-plane counter hook (see header comment): adds `n` to
    // data_loss_events_total. Never called from poll(); the event-log read
    // path cannot move it.
    bool observe_data_loss(std::uint64_t n, MetricsRegistry& registry, std::error_code& ec);

private:
    std::function<std::uint64_t()> now_{};
    std::uint64_t start_ns_{0};
    bool up_ = false;
};

}  // namespace safety_crit::observability

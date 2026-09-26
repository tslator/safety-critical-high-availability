# T-0036: Metrics Registry and Prometheus Encoder

- Status: In Progress (decomposed 2026-09-26 into T-0040–T-0042; this card
  is the phase-level roll-up and stays open until all subtasks are complete)
- Owner: AI agent (opencode)
- Priority: High
- Depends on: T-0033, T-0035
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Decomposition

Executed as three independently reviewable/committable subtasks, each green
in the full local test matrix before its commit:

1. [T-0040](T-0040-metrics-registry-encoder.md) — metrics registry +
   Prometheus text encoder (pure in-memory layer). **Complete 2026-09-26.**
2. [T-0041](T-0041-region-metrics-collector.md) — read-only region
   collector.
3. [T-0042](T-0042-event-metrics-collector.md) — event-log-tail collector.

## Scope

Implement the metrics collection and Prometheus text-exposition layer per
DEC-0014 §8 in `safety_crit::observability`:

- Metrics registry: atomically-updated gauges/counters with name+label
  identity, rendered in Prometheus text format v0.0.4 (`# HELP`, `# TYPE`)
  with label-value escaping, hand-rolled (`snprintf` idiom).
- Region collector (read-only): `worker_status{worker}` (IDLE=0, RUNNING=1,
  CRASHED=2, RECOVERING=3, DEGRADED=4), `ring_buffer_sequence{worker}`,
  `ring_corruptions_total{worker}`, `ownership_epoch{ring}` — acquire loads
  via the DEC-0010 #1 observer path only.
- Event collector (log-tail driven): `perturbation_count_total{type}`,
  `failover_duration_seconds` (latest `failover_recovered.latency_ms/1000`),
  `data_loss_events_total` (supervisor drain-witness sequence gaps; data
  plane only), `event_log_gaps_total{component}`, `observability_up`,
  `observability_uptime_seconds`.

## Acceptance Criteria

- Registry/encoder unit tests in both frameworks: type/help rendering,
  label escaping, deterministic output ordering, empty-registry output.
- Region-collector tests against a created test region verify gauge values
  equal region state at render time (state changes visible on next scrape).
- Event-collector tests: replayed synthetic event-log fixture yields exact
  expected counter values; `data_loss_events_total` and `event_log_gaps_total`
  are independently sourced (log gap does not move data-loss counter).
- Sanitizer legs green; region access remains strictly read-only (`git diff`
  shows zero `shared-memory/` changes).

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

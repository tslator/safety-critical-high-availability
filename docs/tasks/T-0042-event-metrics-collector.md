# T-0042: Event-Log Metrics Collector

- Status: Complete (2026-09-26)
- Owner: Unassigned
- Priority: High
- Depends on: T-0033, T-0040 (subtask 3/3 of T-0036)
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Third slice of the T-0036 decomposition: the log-tail-driven collector in
`safety_crit::observability` (`event_metrics.hpp` + `src/event_metrics.cpp`,
built on `EventLogReader`).

- `declare_event_metrics(registry)` registers the DEC-0014 §8 event-derived
  families: `perturbation_count_total{type}` (counter),
  `failover_duration_seconds` (gauge), `data_loss_events_total` (counter),
  `event_log_gaps_total{component}` (counter), `observability_up` (gauge),
  `observability_uptime_seconds` (gauge).
- `EventMetricsCollector::poll(reader)` drains records until end/torn line
  and advances:
  - `perturbation_count_total{type}`: records with `component=perturb`,
    labeled by the `"category"` extra field (`crash`, `stall`, `corrupt`,
    `double-fault`; unknown categories ignored, never an error).
  - `failover_duration_seconds`: latest supervisor `failover_recovered`
    record's `latency_ms / 1000` (last write wins).
  - `event_log_gaps_total{component}`: per-key gap totals from the reader's
    continuity state (control plane).
  - `observability_up` = 1 once the collector is live;
    `observability_uptime_seconds` from an injectable clock.
- `data_loss_events_total` (data plane) is structurally decoupled: it is
  never derived from event-log content. The collector exposes
  `observe_data_loss(n)` for the embedding process (supervisor drain-witness
  gap source, wired at daemon assembly in T-0038); the daemon renders it
  whatever its current value is.

Known upstream gap (surfaced by this task, not fixed here): the perturb
harness does not yet append to the consolidated event log, so
`perturbation_count_total` has no producer until the `--event-log` append is
added to the harness (tracked for T-0038/T-0039).

## Acceptance Criteria

- Event-collector tests: replayed synthetic event-log fixtures yield exact
  expected counter values (perturbation counts by type, failover gauge,
  per-component gap counters, uptime).
- `data_loss_events_total` and `event_log_gaps_total` are independently
  sourced: a fixture with injected log gaps leaves the data-loss counter
  untouched; `observe_data_loss(n)` moves only the data-loss counter.
- Torn trailing line does not advance counters past the watermark (retry
  semantics inherited from `EventLogReader`).
- Sanitizer legs green; `git diff` shows zero `shared-memory/` changes.

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

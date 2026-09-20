# T-0012: Phase 3 T3.1 — Monitor Core

- Status: Complete
- Owner: AI agent (opencode)
- Priority: High
- Depends on: Phase 2 exit (G2.1–G2.3)
- Phase: Phase 3
- Phase label: T3.1
- Related decision: [DEC-0010](../decisions/0010-phase3-monitor-daemon.md)

> Canonical narrative and gate live in
> [Phase 3 plan T3.1](../phases/PHASE_3_MONITOR_DAEMON.md).

## Scope

Create the `safety_crit::monitors` library: monitor config, per-worker
health tracking (status word + ring-counter snapshots), and the health
algorithm classifying workers (healthy / stalled / crashed / idle /
overrun-observed) and emitting alert events — with tests. No pidfile I/O,
no CLI, no stdout output in this task (T3.2).

## Deliverables

- `monitors/` static library wired into the build (warnings, sanitizers,
  test adapter); ownership rows in `ARCHITECTURE.md` /
  `ARCHITECTURE_RULES.md`.
- `monitor_config.hpp` (poll interval, stall threshold; validation).
- `health.hpp`: per-worker observation state (last advance sequence +
  timestamp, last status word), health algorithm over injected liveness
  predicates and fake clock, alert event struct + vocabulary constants.

## Acceptance Criteria

- Health tests green in GoogleTest, Catch2, and ASan+UBSan; zero warnings.
- Classification matrix deterministic with fake clock + injected liveness:
  RUNNING+advancing → healthy; RUNNING+stale → STALLED at threshold, single
  alert until recovery; RUNNING+dead → CRASHED, single alert; IDLE/zero →
  idle transitions; OVERRUN bit observed → overrun event once per
  assertion.
- Monitor code performs acquire loads only on status cells and
  `derive_state()` on rings — never writes the region.

## Validation

Gate G3.1 (see phase plan). Record in `NOTES.md`.

## Completion Notes

Implemented 2026-09-20 (`monitors/` library: `monitor_config.hpp/.cpp`,
`health.hpp`; `monitors_health_test` 8 cases). Local G3.1 green: 71/71
GoogleTest, 71/71 Catch2, ASan+UBSan clean, TSan pass (setarch harness,
G2.3 precedent) — zero first-party warnings. Hosted CI pending before
closure.

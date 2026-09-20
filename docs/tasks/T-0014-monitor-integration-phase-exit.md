# T-0014: Phase 3 T3.3 — Monitor Integration and Phase Exit

- Status: Complete
- Owner: AI agent (opencode)
- Priority: High
- Depends on: T-0013
- Phase: Phase 3
- Phase label: T3.3
- Related decision: [DEC-0010](../decisions/0010-phase3-monitor-daemon.md)

> Canonical narrative and gate live in
> [Phase 3 plan T3.3](../phases/PHASE_3_MONITOR_DAEMON.md).

## Scope

Prove the monitor end-to-end across real processes and record the Phase 3
exit evidence.

## Deliverables

- `monitors_integration_test` (fork-based, plain build only; sanitizer
  skip-pass): (a) SIGKILL worker → `worker_crashed` exactly once, report
  count 1, stale pidfile left and rejected via liveness; (b) clean worker
  exit → `worker_idle`, no crash event, pidfile removed; (c) SIGSTOP →
  `worker_stalled` (never crashed), SIGCONT → `worker_recovered`;
  (d) standby → idle while alive, rings untouched.
- README monitor command docs; `NOTES.md` Phase 3 evidence section;
  task/registry/STATUS updates.

## Acceptance Criteria

- Integration scenarios green in the plain build under both frameworks,
  stable across repeated runs.
- Full local matrix green (both frameworks × plain/ASan+UBSan/TSan) plus
  clang-verify.
- Hosted CI green on the Phase 3 exit commit (Phase Exit Gate).

## Validation

Gate G3.3 (see phase plan). Record in `NOTES.md`.

## Completion Notes

Implemented 2026-09-20: 4 scenarios passed 5 consecutive full-configuration
local runs; full matrix green (83/83 both frameworks, ASan+UBSan 77/77,
TSan setarch sweep clean, clang-verify 83/83). README monitor docs added.
Hosted exit run pending — Phase 3 exit gate is NOT satisfied until it is
green.

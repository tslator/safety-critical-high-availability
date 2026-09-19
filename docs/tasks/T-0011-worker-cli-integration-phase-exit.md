# T-0011: Phase 2 T2.3 — Worker CLI, Region Lifecycle, Integration, Phase Exit

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: [T-0010](T-0010-work-loop-signals-deadlines.md)
- Phase: Phase 2
- Phase label: T2.3
- Related decision: [DEC-0009](../decisions/0009-phase2-worker-runtime.md)

> Canonical narrative and gate live in
> [Phase 2 plan T2.3](../phases/PHASE_2_WORKERS.md).

## Scope

Wire the `safety-critical-ha worker` subcommand, implement region
create-or-open lifecycle and hot/standby roles, add fork-based multi-process
integration tests, and record the Phase 2 exit evidence.

## Deliverables

- CLI subcommand
  `safety-critical-ha worker --id <a|b|c> [--role hot|standby] [--ticks N]
  [--tick-interval-ms MS] [--budget-us US]` (hand-rolled parsing; `--version`
  untouched).
- Worker entry: `SharedRegionHandle::create_or_open`, status lifecycle
  (RUNNING/IDLE), standby passivity.
- `workers_integration_test` (plain build): ring drain with sequence
  continuity + pipeline-correct payloads; SIGTERM clean stop with
  `verify_worker_ring` witness; SIGUSR1 crash leaves ring verifiable;
  standby makes no pushes.
- README worker command docs; `NOTES.md` Phase 2 exit section.

## Acceptance Criteria

- Integration tests green in the plain build, both frameworks.
- Full local matrix green (both frameworks × plain/ASan+UBSan/TSan).
- Hosted CI green on the Phase 2 exit commit (Phase Exit Gate).

## Validation

Gate G2.3 + exit table in the phase plan. Record in `NOTES.md`.

## Completion Notes

(To be recorded at completion.)

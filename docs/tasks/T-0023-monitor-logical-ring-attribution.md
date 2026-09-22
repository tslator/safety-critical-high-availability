# T-0023: Phase 5 Monitor Logical-Ring Attribution

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: Phase 4 exit (no upstream task)
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Fix the Phase 4 tail defect recorded in [Phase 4 evidence](../evidence/phase-4.md)
"Follow-ups surfaced by T-0021": `poll_worker` reads
`region.rings[physical_idx].tail_`, so after a promotion physical C's home
ring (index 2) never advances and the monitor emits a spurious
`worker_stalled` for the promoted worker. The observation should read the
ring owned by the physical worker (via its ownership cell), falling back to
the home ring only when the worker owns no logical ring.

P0 blocker for Phase 5 stall and double-fault scenarios: without this fix,
every stall scenario after a promotion produces false positives.

## Acceptance Criteria

- Monitor `poll_worker` looks up the physical worker's owned logical ring
  via `read_ownership` and reads that ring's tail; when the worker owns no
  ring (standby), falls back to the home ring index.
- Post-promotion integration scenario: physical C produces on logical A after
  takeover; monitor emits exactly one `worker_running` for physical 2 and no
  `worker_stalled` within the stall threshold window.
- Both GoogleTest and Catch2 green; ASan+UBSan and TSan green in applicable
  configurations.
- Existing Phase 3 monitor integration cases still pass unchanged.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

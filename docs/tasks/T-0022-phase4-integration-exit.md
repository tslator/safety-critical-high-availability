# T-0022: Phase 4 Integration, Evidence, and Exit

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0018, T-0019, T-0020, T-0021
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Run the complete Phase 4 matrix, repeated crash recovery tests, hosted CI, and
phase exit documentation.

## Acceptance Criteria

- G4.1 through G4.6 in the Phase 4 plan pass.
- GoogleTest, Catch2, ASan+UBSan, TSan, Clang, Docker, and Compose gates pass
  as applicable.
- Timing, continuity, priority, shutdown, and residual-risk evidence is
  recorded in the Phase 4 evidence record.
- Status, task, and phase indexes accurately report completion.

## Evidence

Close only with the hosted CI run and [Phase 4 evidence](../evidence/phase-4.md) linked.

# T-0031: Phase 5 Integration, Evidence, and Exit

- Status: In Progress
- Owner: AI agent (opencode)
- Priority: High
- Depends on: T-0023, T-0024, T-0025, T-0026, T-0027, T-0028, T-0029, T-0030
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Run the complete Phase 5 matrix, repeated scenario runs, hosted CI, and
phase exit documentation.

## Acceptance Criteria

- G5.1 through G5.6 in the Phase 5 plan pass.
- GoogleTest, Catch2, ASan+UBSan, TSan, Clang, Docker, and Compose gates
  pass as applicable.
- Compose scenarios S1, S2, S3, S5, S6 green five consecutive times each
  on the reference host.
- Bounded recovery timings recorded per DEC-0012 #10 budgets.
- Replay determinism contract observed for S1.
- `docs/STATUS.md`, task registry, and phase index accurately report
  completion.

## Evidence

Close only with the hosted CI run and [Phase 5 evidence](../evidence/phase-5.md)
linked.

# T-0022: Phase 4 Integration, Evidence, and Exit

- Status: Complete
- Owner: AI agent (opencode)
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

Full Phase 4 exit matrix recorded in [Phase 4 evidence](../evidence/phase-4.md)
under "Phase 4 Exit". Summary of what closed the phase:

- Supervisor stdout witness events (`first post-failover record observed in Nms`
  and `shutdown state=... a_records=... a_corruptions=... a_first_post_failover=...`)
  enable fault-to-first-output timing and continuity observation without a
  second consumer on the ring.
- `scripts/phase4-failover-timing.sh` runs N supervisor iterations with
  SIGKILL-injected crashes and reports min/median/max/avg and over-threshold
  count against the DEC-0011 #5 `<100 ms` target.
- Local matrix green: GoogleTest 95/95, Catch2 95/95, ASan+UBSan (both
  frameworks, 87/87 each — 8 fork integration cases skip under sanitizers),
  TSan x2 (87/87 each, `setarch --addr-no-randomize`), clang-verify (pinned
  image, 95/95), Docker build, Compose smoke + in-container failover (5
  consecutive passes).
- Repeated crash recovery timing (10 iterations on the reference host):
  min 84 ms, median 84 ms, max 91 ms, avg 85 ms — every iteration within the
  `<100 ms` target with zero corruptions across the drain witness.
- Hosted CI run recorded in the evidence file.

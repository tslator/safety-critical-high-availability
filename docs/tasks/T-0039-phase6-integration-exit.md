# T-0039: Phase 6 Integration, Evidence, and Phase Exit

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0033, T-0034, T-0035, T-0036, T-0037, T-0038
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Run the full Phase 6 verification matrix on the CI reference host, collect
evidence against gates G6.1–G6.5 in the
[Phase 6 plan](../phases/PHASE_6_OBSERVABILITY.md), write
`docs/evidence/phase-6.md`, update `docs/STATUS.md`, and close the phase.

Matrix: GoogleTest + Catch2 (all suites incl. `observability_*`), ASan+UBSan
×2, TSan ×2, clang-verify, `docker-build`, `docker-compose-smoke`, S1/S2/S3/
S5/S6/S1-R scenarios 5× each, metrics/health smoke 5×, hosted CI run link.

## Acceptance Criteria

- Gates G6.1–G6.5 of the Phase 6 plan satisfied with recorded output
  (test counts, scenario results, scrape fixtures, timing for
  `failover_duration_seconds` within the `<100 ms` crash-recovery budget
  restated as an observation, not a new SLA).
- `data_loss_events_total == 0` across every green scenario run;
  `event_log_gaps_total` zero in non-crash scenarios and explained in
  crash scenarios (buffered info records dropped by design).
- `git diff` confirms zero `shared-memory/` changes for the whole phase.
- Hosted CI run green (all jobs); evidence file links run ID, commit SHA,
  and per-gate artifacts.
- Task registry, `docs/STATUS.md`, and phase plan updated to closed state.

## Evidence

[Phase 6 evidence](../evidence/phase-6.md).

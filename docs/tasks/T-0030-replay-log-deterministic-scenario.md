# T-0030: Phase 5 Replay Log and Deterministic Scenario

- Status: Complete
- Owner: Unassigned
- Priority: Medium
- Depends on: T-0028, T-0029
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Define the JSON-lines replay log schema and implement
`safety-critical-ha replay <logfile>` that re-issues signals at recorded
relative offsets. Per DEC-0012 #8, Phase 5 scope is a single deterministic
scenario (S1 crash); the broader `PerturbationEngine::record/replay` surface
moves to Phase 5b.

Determinism contract (DEC-0012 #8): identical event categories in identical
order, identical final ownership, identical committed record count per
logical ring. Wall-clock timestamps and process pids are excluded from
comparison.

## Acceptance Criteria

- Replay log schema documented in
  `perturb/include/safety_crit/perturb/replay_log.hpp`; schema versioned
  with a `"schema":1` header record.
- `safety-critical-ha replay <logfile> [--target-remap <from>=<pid>]`
  CLI subcommand wired through `app/src/main.cpp`; hand-rolled parsing.
- S1 replay scenario runs the original S1 crash, records the log, then
  replays it, and compares:
  - sequence of monitor event categories (order + kind),
  - final `read_ownership(0)` physical_owner and epoch,
  - committed record count from supervisor's shutdown summary per ring.
- Comparison helper lives under `perturb/tests/` and is exercised by both
  frameworks.
- Both frameworks green; ASan+UBSan and TSan green in applicable configs
  (replay test skip-pass under sanitizers if timing-sensitive, following
  Phase 3 precedent).

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

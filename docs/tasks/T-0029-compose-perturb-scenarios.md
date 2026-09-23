# T-0029: Phase 5 Compose Perturb Service and Scenario Scripts

- Status: Complete
- Owner: Unassigned
- Priority: High
- Depends on: T-0028
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Populate the Compose `perturb` profile service with the perturb binary
(replacing the Phase 0 `sleep infinity` stub) and add host-side scenario
scripts for S1 (crash), S2 (stall), S3 (memory corruption), S5 (double
fault), and S6 (supervisor loss). Scenarios are driven from the host against
the running supervisor container, iterated by the CI `docker-compose-smoke`
job.

Per DEC-0012 #9 the T-0021 supervisor-only model and `failover-smoke.sh`
stay green unchanged.

## Acceptance Criteria

- `containers/compose/scenarios/s1_crash.sh`, `s2_stall.sh`, `s3_corrupt.sh`,
  `s5_double_fault.sh`, `s6_supervisor_kill.sh` under a new
  `containers/compose/scenarios/` directory. Each script exits 0 on success
  with a per-scenario assertion bundle (log patterns matched, ownership
  observed, timing budget met).
- `docker-compose.yml` `perturb` service uses the perturb binary; the
  profile remains opt-in (`docker compose --profile perturb up`).
- Compose `docker-compose-smoke` CI job runs each scenario once as an
  extension of the existing failover smoke; T-0031 runs them 5× each for
  phase exit.
- Scenario scripts emit JSON-lines records from `perturb --out` for use by
  T-0030 replay.
- `containers/compose/failover-smoke.sh` unchanged (regression check in CI).
- Existing `docker compose config --quiet`, `build`, `up --wait`, and `down`
  stay green.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

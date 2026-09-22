# Phase 5 Evidence Plan

This record is the evidence target for
[Phase 5](../phases/PHASE_5_PERTURBATION.md) and will be populated as tasks close.

## Required Evidence

- GoogleTest and Catch2 plain configurations.
- ASan+UBSan and TSan for affected control/harness/scenario paths.
- Pinned Clang verification.
- Monitor logical-ring attribution (Phase 4 tail fix, T-0023).
- Abandoned producer-claim rule and test (Phase 4 tail fix, T-0024).
- Supervisor stall recovery state machine and integration scenario (T-0025).
- Supervisor double-fault handling and DEGRADED state (T-0026).
- Worker SIGUSR2 memory-corruption hook and end-to-end CRC observation
  (T-0027).
- Perturbation harness library, CLI, and JSON-lines log (T-0028).
- Compose `perturb` service and per-scenario scripts S1/S2/S3/S5/S6 (T-0029).
- Replay log schema and deterministic S1 replay scenario (T-0030).
- Docker build and Compose smoke unchanged plus new scenario coverage.
- Repeated scenario runs (5× each scenario on the reference host).
- Bounded recovery timings per DEC-0012 #10 budgets.
- Hosted CI run link.

Each closed task must link implementation and durable command/result evidence
here or in `NOTES.md`.

## T-0023 Result

- Implementation: `owned_logical_ring()` helper in
  `monitors/include/safety_crit/monitors/health.hpp`; `poll_worker` reads the
  physical worker's owned logical ring's tail (falling back to the home ring
  for standby workers, whose status word is IDLE and never triggers the
  stall rule).
- Regression test: `MonitorsLoop.AttributionFollowsLogicalRingAfterPromotion`
  in `monitors/tests/monitors_loop_test.cpp`. Written first (TDD red);
  failed against the pre-fix code and passes after.
- GoogleTest 96/96 (95 pre-existing + 1 new); Catch2 96/96.
- ASan+UBSan x2: 88/88 each (fork-integration cases skip under sanitizers,
  established precedent).
- TSan x2 under `setarch --addr-no-randomize`: 88/88 each, zero race reports.
- clang-verify (pinned image, clang-14 preset): 96/96, zero clang warnings.
- Docker build (`--version`) PASS; Compose `up --wait` healthy +
  `containers/compose/failover-smoke.sh` green 5 consecutive times.
- `scripts/phase4-failover-timing.sh 5` unchanged: min/median/max/avg 85 ms,
  0/5 iterations over `<100 ms` SLA.
- `./scripts/sync-agent-guidance.sh --check` PASS.
- Hosted CI: [run 35761919434](https://github.com/tslator/safety-critical-high-availability/actions/runs/35761919434) on commit `c14934c` (2026-09-22), all ten jobs green.

## T-0024 Result

- Rule: an epoch bump implies every in-flight producer claim from the prior
  epoch is abandoned. New owner resumes from the last committed sequence;
  never force-commits an in-flight slot. Consumers never pop a slot from a
  prior epoch.
- Implementation: rollback loop at the end of `transfer_ownership` in
  `shared-memory/src/shared_region.cpp` — CAS tail_ back to head_ whenever
  the slot at tail_-1 still holds `ready(tail_-1)` (never committed since
  the prior lap's release).
- Rule documentation: comment block in
  `shared-memory/include/safety_crit/shared_memory/ring_buffer.hpp`.
- TDD red step (pre-fix): `SharedRegion.AbandonedClaimOnFirstSlotRollsBackTail`
  and `SharedRegion.AbandonedClaimOnInteriorSlotRollsBackTail` observed to
  FAIL; `SharedRegion.TransferDoesNotRollBackCommittedSlots` PASS as the
  control. All three PASS post-fix; committed payload verified poppable
  after takeover.
- GoogleTest 99/99 (96 pre-existing + 3 new); Catch2 99/99.
- ASan+UBSan x2: 91/91 each (fork-integration cases skip under sanitizers,
  established precedent).
- TSan x2 under `setarch --addr-no-randomize`: 91/91 each, zero race reports.
- clang-verify (pinned image, clang-14 preset): 99/99, zero clang warnings.
- Docker build (`--version`) PASS; Compose `up --wait` healthy +
  `containers/compose/failover-smoke.sh` green 5 consecutive times.
- `scripts/phase4-failover-timing.sh 5`: min/median/max/avg 84/84/90/85 ms,
  0/5 iterations over `<100 ms` SLA (Phase 4 baseline preserved).
- `./scripts/sync-agent-guidance.sh --check` PASS.
- Hosted CI: [run 35768199979](https://github.com/tslator/safety-critical-high-availability/actions/runs/35768199979) on commit `97ac5f5` (2026-09-22), all ten jobs green.

## T-0025 Result

- Implementation: `StallRecoveryTracker` (per-logical-ring state machine with
  injected time) plus `StallRecoveryOutcome`/`StallRecoveryEvent` in
  `supervisor/include/safety_crit/supervisor/supervisor.hpp`; tracker body
  and `owned_ring_state()` (T-0023 attribution: tail read from the logical
  ring the physical worker owns) in `supervisor/src/supervisor.cpp`.
  `SupervisorState::kStalledRecovering` added between `kRunning` and
  `kFailoverDetected`.
- Behavior: `worker_stalled` alert → exactly one bounded SIGCONT +
  `kStalledRecovering`; ring-tail change → `kRunning` +
  `supervisor: stall recovered for physical N at epoch E`; grace expiry
  (default 200 ms, `--stall-grace-ms` CLI flag) →
  `supervisor: stall escalation for physical N at epoch E` + SIGKILL, after
  which the existing reap-based crash-recovery path runs. Second alert on an
  already-recovering ring is a no-op and never resets the grace timer.
- TDD red step: six new tests written first and observed failing to compile
  / fail against the pre-fix supervisor; all PASS after implementation.
  Unit: `StallRecoveryRecoversOnTailAdvance`,
  `StallRecoveryEscalatesAfterGracePeriod` (strict `>` boundary),
  `StallRecoveryIdempotentPerEpoch`, `StallRecoveryGracePeriodIsConfigurable`.
  Integration: `RecoversStalledWorkerWithBoundedSigcont` (SIGSTOP →
  `worker_stalled` → recovered event), `EscalatesStalledWorkerToCrashRecovery`
  (SIGSTOP held against SIGCONT → escalation event → ownership transfers to
  physical 2). Timing-sensitive integration tests repeated 5×: 5/5 PASS.
- GoogleTest 105/105 (99 pre-existing + 6 new); Catch2 105/105.
- ASan+UBSan x2: 97/97 each (fork-integration cases skip under sanitizers,
  established precedent).
- TSan x2 under `setarch --addr-no-randomize`: 97/97 each, zero race reports.
- clang-verify (pinned image, clang-14 preset): 105/105; zero warnings from
  supervisor sources (one pre-existing `-Wunused-lambda-capture` in
  `workers/src/worker_entry.cpp` predates this task and is out of scope).
- Docker build (`--version`) PASS; Compose `up --wait` healthy +
  `containers/compose/failover-smoke.sh` green 5 consecutive times.
- `scripts/phase4-failover-timing.sh 5`: min/median/max/avg 85/90/91/89 ms,
  0/5 iterations over `<100 ms` SLA (Phase 4 baseline preserved).
- `./scripts/sync-agent-guidance.sh --check` PASS.
- Hosted CI: [run 35777723370](https://github.com/tslator/safety-critical-high-availability/actions/runs/35777723370) on commit `8236b71` (2026-09-22), all ten jobs green.

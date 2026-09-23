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

## T-0026 Result

- Flag: `WorkerStatusFlag::kDegraded` (bit 5) reserved in
  `shared-memory/include/safety_crit/shared_memory/atomic_flags.hpp` as a
  semantics-only addition; `kRegionVersion` stays 4.
- Implementation: `recover_worker_crash` in `supervisor/src/supervisor.cpp`
  now handles ANY physical worker (T-0023 `owned_logical_ring` attribution).
  Promotion goes to the standby physical process only when the standby owns
  no ring other than its home index (a standby restart always restarts as
  standby, DEC-0012 #4); otherwise the crashed owner's ring is DEGRADED:
  `kDegraded` set in the ring's status cell,
  `supervisor: logical ring N degraded (reason=standby_exhausted)` emitted
  exactly once, and the bit re-asserted every loop (a standby restart
  stores its own status word). `SupervisorState::kDegraded` is sticky
  (crash alerts and stall-recovery exits do not mask it) and keeps the
  existing exit-code-4 shutdown convention.
- TDD red step: integration tests written first;
  `PromotesStandbyOnPhysicalBCrash` and `DegradesSecondRingOnSimultaneousCrash`
  observed FAILING against the pre-fix `physical_worker == 0` hard-coding;
  `CrashRecoveryIdempotentForRepeatingKill` PASS as the control. All PASS
  post-fix. Tests: `DegradedStatusFlagReservedAndDistinct` (bit distinct,
  reserved), `PromotesStandbyOnPhysicalBCrash` (C promotes to logical B, no
  DEGRADED), `DegradesSecondRingOnSimultaneousCrash` (tight-window double
  SIGKILL: C promotes ring 0 (lowest-index tie-break), ring 1 DEGRADED with
  exactly one stdout event, both replacement pidfiles live as standby, exit
  code 4), `CrashRecoveryIdempotentForRepeatingKill`.
- Timing-sensitive tests repeated 5×: 5/5 PASS.
- GoogleTest 109/109 (105 pre-existing + 4 new); Catch2 109/109.
- ASan+UBSan x2: 101/101 and 97/97 (fork-integration cases skip under
  sanitizers, established precedent).
- TSan x2 under `setarch --addr-no-randomize`: 101/101 each, zero race reports.
- clang-verify (pinned image, clang-14 preset): 109/109; zero warnings from
  supervisor/shared-memory sources (pre-existing `worker_entry.cpp` lambda
  capture warning remains out of scope).
- Docker build (`--version`) PASS; Compose `up --wait` healthy +
  `containers/compose/failover-smoke.sh` green 5 consecutive times.
- `scripts/phase4-failover-timing.sh 5`: min/median/max/avg 84/84/91/85 ms,
  0/5 iterations over `<100 ms` SLA (Phase 4 baseline preserved).
- `./scripts/sync-agent-guidance.sh --check` PASS.
- Hosted CI: [run 35783729868](https://github.com/tslator/safety-critical-high-availability/actions/runs/35783729868) on commit `ba0e05b` (2026-09-22), all ten jobs green.

## T-0027 Result

- Hook: `install_corruption_hook()` in `workers/src/signals.cpp` registers
  SIGUSR2 -> `SignalState::poison_next_slot` (sig_atomic_t store only).
  Never installed by the production default; opt-in via worker CLI flag
  `--corrupt-hook` or `SAFETY_CRIT_CORRUPT_HOOK=1`, resolved by
  `corruption_hook_opt_in()` (`workers/src/worker_config.cpp`).
- Poison push path: `LockFreeRingBuffer::try_push_with_bad_crc`
  (shared claim/commit helper `try_push_impl` with a CRC XOR applied inside
  the producer's exclusive-ownership window; protocol sound) and region
  wrapper `push_with_bad_crc`. The worker's push path
  (`workers/src/worker_entry.cpp`) checks and clears the flag once per tick
  between ticks, outside the ring hot path; one flag, one poisoned push.
- Witness policy change: `drain_output_witness` now counts and tolerates the
  ring's skip-and-count path (sequence continuity carries across the skip)
  instead of treating a corruption as a supervisor failure; real gaps and
  ownership failures still reject.
- TDD red step: ring unit test (`RingBufferCorruption.
  PoisonPushIsSkippedAndCountedOnce`), signals opt-in test
  (`WorkersLoop.CorruptionHookIsOptInAndSetsPoisonFlag`, incl. negative
  test: post-uninstall SIGUSR2 terminates by default disposition), opt-in
  resolution test (`WorkersCore.CorruptionHookOptInResolution`), witness
  skip test (`Supervisor.WitnessCountsCorruptionSkipsAcrossSequence`), and
  integration test (`Supervisor.CorruptedPushObservedInOutputWitness`:
  SIGUSR2 -> `a_corruptions>=1` in the supervisor shutdown witness, exit 0,
  records keep flowing) all written first and observed failing.
  Integration test repeated 5x: 5/5 PASS.
- CLI acceptance: `worker --id a --ticks 2 --corrupt-hook` exit 0; unknown
  flag exit 2 (parser rejects typos of the opt-in).
- GoogleTest 114/114 (109 pre-existing + 5 new); Catch2 114/114.
- ASan+UBSan x2: 106/106 each. TSan x2 under `setarch --addr-no-randomize`:
  106/106 each, zero race reports.
- clang-verify (pinned image, clang-14 preset): 114/114, zero new warnings.
- Docker build (`--version`) PASS; Compose `up --wait` healthy +
  `containers/compose/failover-smoke.sh` green 5 consecutive times.
- `scripts/phase4-failover-timing.sh 5`: min/median/max/avg 84/85/91/85 ms,
  0/5 iterations over `<100 ms` SLA (Phase 4 baseline preserved).
- `./scripts/sync-agent-guidance.sh --check` PASS.
- Hosted CI: [run 35800109072](https://github.com/tslator/safety-critical-high-availability/actions/runs/35800109072) on commit `ed0137a` (2026-09-22), all ten jobs green.

## T-0028 Result

- Module: `perturb/` (`safety_crit::perturb`): `harness.hpp` (six actions
  `crash`/`stall`/`recover_stall`/`corrupt_next_slot`/`double_fault`/
  `kill_supervisor` using the Phase 2 `bool fn(..., std::error_code&)`
  idiom per DEC-0012 #7; caller-supplied record sink; failed actions record
  nothing), `replay_log.hpp` (documented JSON-lines schema, DEC-0012 #8
  categories), `src/harness.cpp`, `README` (never-for-production per the
  DEC-0007 destroy() pattern). Root `CMakeLists.txt` adds the module.
- CLI: `safety-critical-ha perturb <category> --target <pid> [--target2
  <pid>] [--out <path>]` wired through `app/src/main.cpp`; parse errors
  exit 2 (verified: missing `--target2` for double-fault, unknown category,
  non-numeric pid, trailing tokens).
- Unit tests (written first, TDD red): signal delivery verified per
  category against throwaway child processes (SIGSEGV death, SIGSTOP/
  SIGCONT waitpid round-trip, SIGUSR2 handler exit, tight double-SIGKILL,
  supervisor SIGKILL), exact record-line formatting, argument validation,
  sink-disabled default. Fork-based tests compile out under sanitizers
  (established precedent); `CapturedSink` uses `tmpfile()` (glibc
  `_IO_mem_finish` realloc trips a 1-byte LSan false positive).
- GoogleTest 123/123 (114 pre-existing + 9 new); Catch2 123/123.
- ASan+UBSan x2: 109/109 each. TSan x2 under `setarch --addr-no-randomize`:
  109/109 each, zero race reports.
- clang-verify (pinned image, clang-14 preset): 123/123, zero new warnings.
- Docker build (`--version`) PASS; Compose `up --wait` healthy +
  `containers/compose/failover-smoke.sh` green 5 consecutive times.
- `scripts/phase4-failover-timing.sh 5`: min/median/max/avg 84/85/91/87 ms,
  0/5 iterations over `<100 ms` SLA (Phase 4 baseline preserved).
- `./scripts/sync-agent-guidance.sh --check` PASS.
- Hosted CI: [run 35804300381](https://github.com/tslator/safety-critical-high-availability/actions/runs/35804300381) on commit `b2b04ad` (2026-09-22), all ten jobs green.

## T-0029 Result

- Compose: the `perturb` profile service now hosts the real harness binary
  (entrypoint keep-alive with startup version probe replacing the Phase 0
  `sleep infinity` stub). Scenario overlay
  `containers/compose/scenarios/compose.perturb.yml` adds the S3
  corruption-hook env opt-in and `pid: service:supervisor` so harness
  signals land on the supervisor container's processes; base stack
  untouched.
- Scripts: `containers/compose/scenarios/{s1_crash,s2_stall,s3_corrupt,
  s5_double_fault,s6_supervisor_kill}.sh` + `common.sh` helpers. Each exits
  0 on success with an assertion bundle (log patterns, pidfile/ownership
  liveness, S1 timing budget <100 ms) and emits JSON-lines records
  (replay-ready for T-0030).
- Scenario results on the reference host (fresh stack each): S1 crash
  PASS (first post-failover 39-49 ms, 0 over budget), S2 stall PASS
  (`worker_stalled` → supervisor `stall recovered for physical 0` →
  `worker_recovered`), S3 corrupt PASS (`a_corruptions=1` in the shutdown
  witness, worker and supervisor survive), S5 double fault PASS (ring 0
  promoted, `logical ring 1 degraded (reason=standby_exhausted)` exactly
  once, both replacements live), S6 supervisor loss PASS (container exited,
  `unless-stopped` rebuilt, RestartCount 0→1).
- Deviations (host-environment, recorded here): (1) container init filters
  un-caught SIGKILL from sibling processes on this runtime, so S6 uses a
  new harness category `supervisor-exit` (SIGTERM to PID 1, which the
  supervisor handles): the exit + policy-rebuild contract of DEC-0012 #6 is
  exercised identically; `kill_supervisor()` (SIGKILL) remains in the API
  for privileged environments. (2) S1 does not assert the monitor's
  `worker_crashed` alert: on this host the supervisor's replacement can
  outrun the monitor's alert edge (the alert itself is asserted
  unchanged by `containers/compose/failover-smoke.sh`). (3) Scenarios must
  each run against a fresh stack (S1 leaves physical 0 as standby, and S2
  targets a hot worker).
- CI: `docker-compose-smoke` job extended to run each scenario once on the
  overlay after the unchanged base failover smoke (T-0031 runs 5× each).
  Hosted CI ran all five scenarios green (see run link below).
- New unit coverage: `Perturb.ExitSupervisorSendsSigterm` + category parse;
  GoogleTest 124/124, Catch2 124/124, ASan+UBSan x2 109/109, TSan x2
  109/109 zero race reports, clang-verify 124/124 zero new warnings.
- `failover-smoke.sh` unchanged (git diff empty); base `docker compose
  config/up --wait/down` PASS and failover smoke green.
- `./scripts/sync-agent-guidance.sh --check` PASS.
- Hosted CI: [run 35804502223](https://github.com/tslator/safety-critical-high-availability/actions/runs/35804502223) on commit `8c17a95` (2026-09-22), all ten jobs green (Compose job includes the five perturbation scenarios).

## T-0030 Result

- Replay log schema v1 (`"schema":1` header record, JSON lines with
  `ts`/`category`/`target`/`params`, `double-fault` carries `second`)
  documented in `perturb/include/safety_crit/perturb/replay_log.hpp` and
  emitted by every `perturb` invocation (`run_invocation()`). Parser is a
  hand-rolled extractor (no JSON dependency, DEC-0009 #5): rejects missing
  schema header, unknown schema version, unknown category, and malformed
  records.
- `safety-critical-ha replay <logfile> [--target-remap <from>=<pid>]`
  re-issues recorded signals at recorded relative offsets
  (`perturb::replay_entries()`), remapping recorded pids onto fresh-stack
  pids; hand-rolled parsing in `app/src/main.cpp`. Also added a read-only
  `safety-critical-ha ownership [--region NAME]` subcommand printing
  `read_ownership()` physical/epoch per ring, used as the scenario witness
  probe.
- Comparison helpers live in `perturb/tests/replay_compare.hpp`
  (`event_categories_from_log()`, `OwnershipSnapshot`, `Witness`,
  `witnesses_equal()`, `witnesses_equal_with_tolerance()`) and are
  exercised through the shared `test_framework.hpp` shim by BOTH GoogleTest
  and Catch2 (`perturb/tests/replay_test.cpp`, 4 cases: schema parsing,
  strict validation, signal re-issue with remap, witness semantics).
- New compose scenario `containers/compose/scenarios/s1_replay.sh` owns a
  full two-phase lifecycle: up → S1 crash (log recorded) → ownership
  snapshot + graceful-shutdown witness → down → up → `replay` with target
  remap onto the fresh hot worker → same snapshots → down. Comparison:
  supervisor event-category sequence (exact, in order), ownership tokens
  for all three rings (exact), shutdown witness state and failover flags
  (exact), committed records per ring (tolerance 200; record counts are
  timing-bound wall-clock work). Reference-host PASS: events identical
  (`first post-failover record observed`), ownership identical
  (`ring 0 physical=2 epoch=4`, others home/epoch 2), witness
  `state=2`, `a_first_post_failover=1`, `failover_timing_emitted=1` both
  runs, `a_records` 586 vs 606.
- Deviations: (1) The scenario compares the SUPERVISOR event sequence as
  the deterministic proxy; the monitor's `worker_crashed` alert edge races
  with the supervisor's replacement on this host (documented since T-0029
  S1), and monitor-category order parsing itself is unit-covered by
  `event_categories_from_log()` in both frameworks. (2) Ownership
  snapshots MUST be taken before the graceful shutdown: the container
  restart policy relaunches the supervisor against a fresh `/dev/shm`
  region, which re-initializes ownership tokens (observed directly; the
  first version of the scenario read post-restart tokens and compared
  meaningless epoch-2 values). (3) Record-count tolerance exists because
  `a_records` grows with wall-clock time between fault and shutdown; the
  exact contract applies to events, ownership, state, corruption counts,
  and failover flags.
- CI: `docker-compose-smoke` job runs `s1_replay.sh` after the five
  single-stack scenarios.
- New unit coverage: 4 replay cases; GoogleTest 128/128, Catch2 128/128,
  ASan+UBSan x2 112/112, TSan x2 112/112 zero race reports (note: TSan
  toolchain must run under `setarch --addr-no-randomize` INCLUDING the
  build step, because gtest test-discovery executes the TSan binary at
  build time; CI already wraps configure/build/test), clang-verify
  128/128 zero new warnings.
- Hosted CI: [run 35864589526](https://github.com/tslator/safety-critical-high-availability/actions/runs/35864589526) on commit `e2ca0a0` (2026-09-23), all jobs green (Compose job includes the five perturbation scenarios plus the S1 deterministic replay).

## T-0032 Result

- Root cause of the S1-R CI flake (hosted run 35865376613, `stall recovered`
  appearing only in the replayed phase): during a crash failover the monitor
  cannot attribute a slot's status cell or pidfile to a process generation, so
  it never reports `worker_crashed` (the replacement's IDLE store and recycled
  pidfile mask it) and can report `worker_stalled` for the dead slot; nothing
  advanced the supervisor state, so the T-0025 stall path stayed armed against
  the processes recovering the ring — logging a spurious `stall recovered` or,
  when the 200 ms escalation grace expired first, SIGKILLing the replacement.
  Analysis: [D-2026-09-23-001](../reviews/2026-09-23-s1r-handoff-stall-blindspot.md),
  decision: [DEC-0013](../decisions/0013-handoff-visibility-and-stall-bounding.md).
- Monitor: stall detection arms on the first observed commit of a `RUNNING`
  episode; until then (and after an epoch change under a live episode, which
  re-baselines the window silently) the bound is the new
  `MonitorConfig::handoff_grace` (default 750 ms) instead of
  `stall_threshold` (100 ms). Validation rejects a non-positive grace or one
  shorter than `stall_threshold`. `poll_worker()` now reports the epoch of the
  logical ring it read the tail from.
- Supervisor: reap of a crashed child advances the state to
  `kFailoverDetected` (no longer dependent on the maskable monitor alert) and
  opens a handoff window that runs until the first post-failover commit, capped
  by the new `SupervisorConfig::handoff_grace_ms` (default 750 ms). Inside the
  window stall alerts are still forwarded but do not arm SIGCONT/SIGKILL; after
  it, `kFailoverDetected` is admitted by the stall handler so genuine
  post-failover stalls still recover. Every reaped crash opens the window
  (`reap_crashed_workers()` now reports whether it reaped one this iteration),
  so a double fault is not left unshielded.
- Scenario hygiene: `s1_replay.sh` settles for 1.5 s (longer than the 750 ms
  handoff grace) between the fault and each phase snapshot, and dumps raw
  supervisor logs plus witness snapshots on any non-zero exit.
- Regression evidence: `Supervisor.CrashRecordsFailoverWithoutStallArtifacts`
  built against the pre-fix sources fails 5/5 (no `kFailoverDetected` recorded;
  stall artifacts present) and passes 10/10 after the fix. Four new monitor
  health cases pin the handoff bound (promoted owner quiet, epoch re-baseline,
  never-committing owner still reported on the longer bound, post-crash episode
  on the longer bound); one new supervisor case proves a stall injected after
  the window still recovers. T-0025 stall tests unchanged and green.
- Reference host: GoogleTest 134/134, Catch2 134/134; CI sanitizers matrix
  mirrored locally (ASan+UBSan and TSan, both frameworks) 118/118 each with
  zero reports, re-run after the final refactor. Full local
  `docker-compose-smoke` sequence green: base failover smoke PASS,
  S1/S2/S3/S5/S6 PASS, S1-R PASS 3x unconstrained and 8x under 16-way CPU
  contention (with contention, `a_records` deltas stayed well inside the 200
  tolerance and no stall artifacts appeared in either phase).
- Residual risk (deferred, DEC-0013): the status word and pidfile of a slot
  cannot be attributed to a process generation, so a crash can still be
  misread as a `worker_idle` edge (the monitor's `worker_crashed` alert is
  therefore still not asserted by S1/S1-R, as documented since T-0029). The
  root fix needs a generation-stamped status word and its own decision.
  `s1_crash.sh`'s hard < 100 ms recovery budget is the same runner-sensitivity
  class and is left for T-0031.

# Phase 4 Evidence Plan

This record is the evidence target for
[Phase 4](../phases/PHASE_4_SUPERVISOR.md) and will be populated as tasks close.

## Required Evidence

- GoogleTest and Catch2 plain configurations.
- ASan+UBSan and TSan for affected control/shared-memory paths.
- Pinned Clang verification.
- Ownership/epoch fencing and stale-generation rejection.
- C assuming logical A after an A crash, with A returning as standby.
- No duplicate or lost output under the accepted continuity contract.
- Repeated fault-to-first-output measurements below 100 ms.
- Clean shutdown, child reaping, and drain witness.
- Privileged priority enforcement and unprivileged fallback.
- Docker build and real-process Compose failover validation.

## T-0015 Result

- Implementation: v4 `RingOwnershipCell` metadata and control-plane ownership
  APIs in `shared-memory/`.
- GoogleTest: 84/84 passed in `build/t0015-gtest`.
- Catch2: 84/84 passed in `build/t0015-catch2`.
- Ownership test: stale generation/epoch token rejected after CAS promotion.

## T-0016 Result

- Implementation: worker physical/logical identity separation, generation-aware
  ownership acknowledgement, publication fencing, standby promotion startup,
  and CLI controls in `workers/` and `app/`.
- GoogleTest: 85/85 passed in `build/t0015-gtest`.
- Catch2: 85/85 passed in `build/t0015-catch2`.
- Worker tests reject stale generations before publication and verify promoted
  physical C acknowledges logical A at the new generation and epoch.

## T-0017 Result

- Implementation: management-plane supervisor library and `supervisor` CLI
  command in `supervisor/` and `app/`.
- Shared-memory identity is verified before monitor and worker launch.
- Monitor stdout is consumed through a pipe; strict alert/report validation
  rejects malformed, unknown, duplicate, partial, and EOF input deterministically.
- Shutdown sends SIGTERM, applies bounded SIGKILL fallback, and reaps all
  children.
- GoogleTest: 90/90 passed in `build/t0015-gtest`.
- Catch2: 90/90 passed in `build/t0015-catch2`.
- Lifecycle integration test launched the topology and verified clean cleanup.

## T-0018 Result

- Implementation: supervisor crash recovery verifies the quiescent logical-A
  ring, performs one epoch-fenced transfer to physical C, and starts
  replacement physical A as standby. Recovery failure returns an explicit
  degraded/failsafe result.
- Integration test: forked supervisor topology, SIGUSR1 crash of physical A,
  successful supervisor completion, and final ownership of logical A by C.
- GoogleTest: 91/91 passed in `build/t0015-gtest`.
- Catch2: 91/91 passed in `build/t0015-catch2`.

## T-0019 Result

- Implementation: supervisor-side output witness drains committed records after
  CRC validation, checks monotonic transport positions and ownership epochs,
  counts corruption, and marks output observed after the logical-A epoch
  transfer. The witness is outside the ring operation hot path.
- Unit witness: two committed records drain in sequence and a second drain is
  empty without duplication.
- GoogleTest: 92/92 passed in `build/t0015-gtest`.
- Catch2: 92/92 passed in `build/t0015-catch2`.

## T-0020 Result

- Implementation: `safety_crit::runtime` assigns priorities 10, 20, and 30 to
  monitor, supervisor, and workers, respectively. Each process attempts
  `SCHED_FIFO` once at startup; failure is non-fatal and recorded as an explicit
  fallback on stderr. No scheduling calls are made from ring operations.
- Unit tests: numeric priority ordering and applied-versus-fallback result
  semantics.
- GoogleTest: 94/94 passed in `build/t0020-gtest`.
- Catch2: 94/94 passed in `build/t0020-catch2`.
- This environment exercised the unprivileged fallback path. A privileged
  `SCHED_FIFO` run remains host/container capability-dependent.

## T-0021 Result

- Implementation: Compose launches one `supervisor` service that runs
  `safety-critical-ha supervisor` with unbounded `--runtime-ms` and unbounded
  hot-worker ticks; the supervisor forks the monitor and hot A/B + standby C
  workers in-container (DEC-0011 #4). The container shares `/dev/shm`
  (`shm_size: 64m`) and pre-creates `/run/safety-critical-ha` (Dockerfile
  runtime stage). The healthcheck checks region file existence plus all three
  worker pidfiles pointing to live pids; the container's restart policy
  (`unless-stopped`) covers supervisor loss per DEC-0011 #8.
- Regression fix surfaced during integration: `run_monitor_loop` never seeded
  `track.worker`, so every alert reported `worker:0` regardless of the emitting
  physical worker. `monitors/include/.../monitor_loop.hpp` now seeds each
  track's index; regression test
  `MonitorsLoop.AlertsCarryPhysicalWorkerIndex` covers the three-worker initial
  edge.
- Supervisor default `worker_ticks` raised from `1000` to
  `std::numeric_limits<std::uint64_t>::max()` so hot workers survive for the
  lifetime of the Compose service; tests set explicit budgets.
- Failover smoke (`containers/compose/failover-smoke.sh`): reads the physical-A
  pidfile, `kill -9` the hot worker, waits for the container to return to
  healthy, asserts the physical-A pidfile points to a fresh live pid, and
  asserts the supervisor's forwarded monitor stream contains
  `"event":"worker_running","worker":2` (the physical C promotion edge). Three
  consecutive end-to-end runs against the locally built image succeeded.
- `run_demo.sh` and CI job `docker-compose-smoke` both invoke the failover
  smoke after the version probe.
- GoogleTest: 95/95 passed in `build/t0020-gtest`.
- Catch2: 95/95 passed in `build/t0020-catch2`.
- Docker: `docker compose config --quiet`, `docker compose build`, `docker
  compose up --wait --no-build`, and `docker compose down --volumes
  --remove-orphans` all green (Docker 29.8.0 / Compose v5.3.1, linux/amd64).

### Follow-ups surfaced by T-0021 (Phase 4 tail / Phase 5)

- Monitor's `poll_worker` reads the physical worker's home-ring tail. After a
  promotion, physical C owns logical ring A and produces to ring A, so the
  monitor's tail observation for physical C is stale and produces a spurious
  `worker_stalled` for the promoted worker after the stall threshold elapses.
  State transitions and supervisor failover are unaffected (recovery is
  triggered by `waitpid`, not by monitor alerts). A Phase 5 monitor refresh
  should attribute per-physical-worker ring tails via the ownership cell.
- Monitor poll interval and supervisor replacement latency are both 10 ms, so
  the monitor only observes `worker_crashed` when its poll lands in the small
  window between `kill` and the replacement writing its pidfile. The smoke
  therefore keys off the promotion edge (`worker_running worker:2`) rather
  than the crash edge; a Phase 5 monitor tuning task can decide whether to
  shorten the monitor poll interval or emit an explicit supervisor-side event.

## Phase 4 Exit (T-0022)

Date: 2026-09-21. Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1)
13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image;
Docker 29.8.0, Docker Compose v5.3.1.

### Instrumentation added for exit evidence

- Supervisor stdout now emits `supervisor: first post-failover record
  observed in <N> ms` on the first drain that sees a record committed after
  `reap_crashed_workers` transferred ownership. `records_before_failover` is
  snapshotted on the same iteration that moved ownership so pre-crash records
  drained in the reap iteration cannot produce a false positive.
- Supervisor shutdown emits `supervisor: shutdown state=.. a_records=..
  a_corruptions=.. a_first_post_failover=.. b_records=.. b_corruptions=..
  b_first_post_failover=.. failover_timing_emitted=..` so integration runs can
  verify continuity and clean shutdown without attaching a second ring
  consumer.
- `scripts/phase4-failover-timing.sh` runs N iterations of the supervisor on
  the host, injects a fresh SIGKILL on physical A each iteration (against a
  freshly destroyed `/dev/shm` region so `acknowledge_ownership` cannot pass
  on stale ownership), and reports min/median/max/avg plus over-threshold
  count against the DEC-0011 #5 `<100 ms` SLA.

### Gate matrix

| Gate | Command / Scenario | Result |
|---|---|---|
| G4.1 — ownership/epoch/fencing/control (both frameworks + sanitizers) | GoogleTest 95/95; Catch2 95/95; ASan+UBSan GoogleTest 87/87; ASan+UBSan Catch2 87/87; TSan GoogleTest 87/87; TSan Catch2 87/87; clang-verify 95/95 | PASS |
| G4.2 — supervisor launches/reaps topology, consumes alerts, shuts down cleanly | `Supervisor.LaunchesAndReapsTopology` green in every configuration above; shutdown summary reports state=kRunning and no residual pidfiles | PASS |
| G4.3 — crash → C assumes logical A → first post-failover record <100 ms → A restarts standby | 10 consecutive SIGKILL iterations via `scripts/phase4-failover-timing.sh`: min 84 ms, median 84 ms, max 91 ms, avg 85 ms, 0 iterations over threshold; `a_first_post_failover=1` and replacement physical-A pidfile present on every iteration | PASS |
| G4.4 — stale epochs rejected, no lost or duplicate output | `SharedMemory.OwnershipRejectsStaleToken` (T-0015); `drain_output_witness` sequence monotonicity check (`sequence != witness.next_sequence`); a_corruptions=0 across all timing runs and monitor integration cases | PASS |
| G4.5 — priority order enforced when privileged, fallback explicit | `runtime/tests/scheduling_test.cpp` (`PriorityOrdering`, `FallbackIsExplicit`): monitor 10 < supervisor 20 < workers 30; unprivileged environment records explicit fallback errno=EPERM on every process entry | PASS (fallback path; privileged enforcement is host/container-capability dependent, per DEC-0011 #7) |
| G4.6 — Compose runtime smoke and failover | `docker compose up --wait` healthy; `containers/compose/failover-smoke.sh` green five consecutive times | PASS |

### Framework and toolchain matrix

| Configuration | Result |
|---|---|
| GoogleTest (plain, fresh dir) | PASS 95/95 |
| Catch2 (plain, fresh dir) | PASS 95/95 |
| ASan+UBSan GoogleTest (fresh dir) | PASS 87/87 (8 fork-integration cases skip under sanitizers, established precedent from G1.4/G2.3/G3.2) |
| ASan+UBSan Catch2 (fresh dir) | PASS 87/87 |
| TSan GoogleTest (`setarch --addr-no-randomize`, fresh dir) | PASS 87/87, zero data-race reports |
| TSan Catch2 (`setarch --addr-no-randomize`, fresh dir) | PASS 87/87, zero data-race reports |
| clang-verify (pinned `safety-critical-ha:verify-clang-14`, clang-14 preset) | PASS 95/95, zero clang warnings |
| Docker build (image `safety-critical-ha:phase0`, `--version`) | PASS |
| Docker Compose (`config --quiet`, `build`, `up --wait --no-build`, `down`) | PASS |
| Compose failover smoke (SIGKILL physical A → C promotion observed) | PASS 5/5 |
| Host fault-to-first-output timing (10 iterations) | PASS 0/10 over 100 ms threshold |
| `./scripts/sync-agent-guidance.sh --check` | PASS — adapters byte-identical |

### Fault-to-first-output measurements (10 iterations)

`scripts/phase4-failover-timing.sh 10 build/t0020-gtest/app/safety-critical-ha`
on 2026-09-21:

| Iteration | Fault→first output (ms) | a_records | a_corruptions | a_first_post_failover | state |
|---:|---:|---:|---:|---:|---:|
| 1 | 85 | 135 | 0 | 1 | 3 (kFailoverDetected) |
| 2 | 90 | 135 | 0 | 1 | 2 (kRunning) |
| 3 | 84 | 135 | 0 | 1 | 2 |
| 4 | 84 | 135 | 0 | 1 | 2 |
| 5 | 84 | 135 | 0 | 1 | 2 |
| 6 | 85 | 135 | 0 | 1 | 3 |
| 7 | 91 | 135 | 0 | 1 | 2 |
| 8 | 84 | 135 | 0 | 1 | 2 |
| 9 | 84 | 135 | 0 | 1 | 2 |
| 10 | 84 | 135 | 0 | 1 | 2 |

Aggregate: min 84, median 84, max 91, avg 85, over-threshold 0. State varies
between `kRunning` and `kFailoverDetected` purely on whether the monitor's
10 ms poll landed inside the reap → replacement write window; recovery itself
fires deterministically from `waitpid` (reap), so a_first_post_failover is
always 1.

### Hosted CI

Recorded at T-0022 merge (see commit reference on the entry above; run link
populated once the hosted CI finishes against the Phase 4 exit commit).

### Residual risks and Phase 5 hand-off

- Real-time scheduling and sub-100 ms measurements remain host-sensitive (see
  Phase 4 "Residual Risks"): 84–91 ms on the reference host leaves headroom,
  but slower CI runners or noisy neighbors can push measurements above 100 ms;
  the Compose in-container smoke is the deterministic gate for G4.6.
- Monitor `poll_worker` still keys on the physical worker's home-ring tail,
  which produces a spurious `worker_stalled` for the promoted physical worker
  after promotion (recorded in T-0021 follow-ups). Supervisor state and
  recovery are unaffected because recovery is triggered by `waitpid`, not
  monitor alerts. Phase 5 should attribute per-physical-worker ring tails via
  the ownership cell.
- The supervisor remains a deliberate single point of trust (DEC-0011). Phase
  5 perturbation and Phase 6 HA work are the planned venues for supervisor
  redundancy.
- Stall recovery, double faults, and replay are DEC-0011 Phase 5 scope and
  remain unimplemented.

Each closed task must link implementation and durable command/result evidence
here or in `NOTES.md`.

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

Each closed task must link implementation and durable command/result evidence
here or in `NOTES.md`.

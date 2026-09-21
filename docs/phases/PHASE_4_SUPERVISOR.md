# Phase 4 - Supervisor and Crash Failover

## Purpose

Add the supervisor process that owns worker/monitor lifecycle, consumes the
Phase 3 alert stream, transfers logical ownership after a crash, and proves
service restoration without duplicate processing or data loss.

Authoritative decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)
(discussion: [D-2026-09-20-002](../reviews/2026-09-20-phase4-supervisor-architecture.md)).

## Scope

- A/B hot plus C standby topology.
- Supervisor process, child lifecycle, reaping, shutdown, and monitor pipe.
- Logical ownership separate from physical identity.
- Epoch/generation fencing and controlled shared-memory v4 if required.
- Crash-only failover: C assumes logical A; replacement A becomes standby.
- Output drain witness and `<100 ms` fault-to-first-output timing.
- Best-effort scheduling and privileged priority-order tests.
- Real Compose process wiring and shared IPC/runtime directories.

Stall recovery, double faults, replay, HTTP/Prometheus, and supervisor
redundancy remain out of scope.

## Task Sequence

1. [T-0015](../tasks/T-0015-phase4-ownership-epoch.md): ownership and epochs.
   Complete 2026-09-20 (G4.1 green: [evidence](../evidence/phase-4.md)).
2. [T-0016](../tasks/T-0016-worker-promotion-control.md): worker control.
   Complete 2026-09-20 (G4.1 green: [evidence](../evidence/phase-4.md)).
3. [T-0017](../tasks/T-0017-supervisor-lifecycle-monitor-pipe.md): supervisor
   lifecycle and monitor ingestion. Complete 2026-09-20 (G4.2 green:
   [evidence](../evidence/phase-4.md)).
4. [T-0018](../tasks/T-0018-crash-failover-replacement.md): crash recovery.
   Complete 2026-09-20 (G4.3 green: [evidence](../evidence/phase-4.md)).
5. [T-0019](../tasks/T-0019-sequence-continuity-witness.md): continuity and
   output witness. Complete 2026-09-20 (G4.4 green: [evidence](../evidence/phase-4.md)).
6. [T-0020](../tasks/T-0020-scheduling-priority.md): scheduling policy.
   Complete 2026-09-20 (G4.5 green: [evidence](../evidence/phase-4.md)).
7. [T-0021](../tasks/T-0021-compose-runtime-integration.md): Compose runtime.
   Complete 2026-09-21 (G4.6 green: [evidence](../evidence/phase-4.md)).
8. [T-0022](../tasks/T-0022-phase4-integration-exit.md): evidence and exit.
   Complete 2026-09-21 (Phase 4 exit gate satisfied:
   [evidence](../evidence/phase-4.md)).

## State Model

```text
IDLE -> LAUNCHING -> RUNNING -> FAILOVER_DETECTED -> RECOVERING -> RUNNING
                                      |                  |
                                      +------------------+-> DEGRADED/FAILSAFE
```

Crash handling must be idempotent. Already handled epochs cannot trigger a
second promotion. Monitor failure is not worker recovery.

## Verification Gates

- G4.1: ownership, epoch, fencing, and control tests pass in both frameworks
  and applicable sanitizers.
- G4.2: supervisor launches/reaps the topology, consumes validated events, and
  shuts down cleanly.
- G4.3: crashing logical A causes physical C to assume A, produces the first
  validated post-failover record in `<100 ms`, and restarts A as standby.
- G4.4: stale epochs are rejected with no lost or duplicate output.
- G4.5: priority order is enforced when privileged; fallback is explicit.
- G4.6: real Compose runtime smoke and failover pass.
- Exit: framework, sanitizer, Clang, Docker, Compose, repeated integration,
  timing, and hosted CI evidence are recorded in [Phase 4 evidence](../evidence/phase-4.md).

## Residual Risks

- Real-time scheduling and sub-100 ms measurements are host-sensitive.
- Recovery of an abandoned producer claim needs an explicit tested rule.
- The supervisor remains a deliberate single point of trust.

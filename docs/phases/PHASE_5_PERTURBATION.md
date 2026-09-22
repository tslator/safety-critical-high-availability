# Phase 5 - Perturbation and Fault Injection

## Purpose

Prove the bounded-recovery, graceful-degradation, and deterministic-replay
tenets by deliberately injecting faults against the Phase 4 supervisor
topology and observing recovery through the shared-memory contract.

Authoritative decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)
(discussion: [D-2026-09-22-001](../reviews/2026-09-22-phase5-perturbation-architecture.md)).

## Scope

- Tail fixes: monitor `poll_worker` reads the logical ring owned by the
  physical worker, and an explicit tested rule for abandoned producer claims.
- Supervisor stall recovery: SIGCONT then SIGKILL escalation per
  `worker_stalled`, idempotent per epoch.
- Supervisor double-fault handling: one promotion, the other logical ring
  marked DEGRADED in `worker_status` and via a supervisor stdout event.
- Worker SIGUSR2 poison-next-slot hook (opt-in, owner-only write).
- Perturbation harness library (`perturb::`) and `safety-critical-ha perturb`
  subcommand emitting JSON-lines records.
- Compose `perturb` profile service populated with the harness; host-side
  scenario scripts for S1 crash, S2 stall, S3 memory corruption, S5 double
  fault, and S6 supervisor loss.
- Replay log schema and one deterministic replay scenario (S1).

Stall recovery via SIGCONT for the DEGRADED ring, second-standby topology
expansion (region v5), CPU starvation, external `process_vm_writev` memory
corruption, shm truncate, broader replay engine, HTTP/Prometheus, and
supervisor redundancy are Phase 5b or Phase 6 scope.

## Task Sequence

1. [T-0023](../tasks/T-0023-monitor-logical-ring-attribution.md): monitor
   logical-ring attribution (Phase 4 tail fix). P0 blocker.
2. [T-0024](../tasks/T-0024-abandoned-claim-rule.md): abandoned producer
   claim rule and test (Phase 4 tail fix). P0 blocker.
3. [T-0025](../tasks/T-0025-supervisor-stall-recovery.md): supervisor stall
   recovery. Depends on T-0023.
4. [T-0026](../tasks/T-0026-supervisor-double-fault-degraded.md): supervisor
   double-fault handling and DEGRADED state. Depends on T-0023, T-0024,
   T-0025.
5. [T-0027](../tasks/T-0027-worker-corruption-hook.md): worker SIGUSR2
   memory-corruption hook. Independent.
6. [T-0028](../tasks/T-0028-perturb-harness-cli.md): perturbation harness
   library and `perturb` CLI subcommand. Depends on T-0023–T-0027.
7. [T-0029](../tasks/T-0029-compose-perturb-scenarios.md): Compose `perturb`
   service and host-side scenario scripts (S1, S2, S3, S5, S6). Depends on
   T-0028.
8. [T-0030](../tasks/T-0030-replay-log-deterministic-scenario.md): replay
   log schema and one deterministic replay scenario. Depends on T-0028,
   T-0029.
9. [T-0031](../tasks/T-0031-phase5-integration-exit.md): Phase 5
   integration, evidence, and exit.

## State Model

Supervisor state machine (Phase 5 additions in **bold**):

```text
IDLE -> LAUNCHING -> RUNNING -> FAILOVER_DETECTED -> RECOVERING -> RUNNING
         |             |             |                   |
         |             +---> STALLED_RECOVERING <--------+
         |                        |
         |                        +---> (SIGCONT ok) RUNNING
         |                        +---> (escalate) FAILOVER_DETECTED
         |
         +----------------------------- DEGRADED <-- (double fault: second ring)
                                        FAILSAFE <-- (unrecoverable)
```

Crash handling and stall escalation remain idempotent per epoch. A second
`worker_stalled` alert on an already-escalating ring is a no-op; a second
`worker_crashed` on an already-recovering ring is a no-op. DEGRADED is a
terminal state for the affected logical ring within the current supervisor
lifetime.

## Verification Gates

- G5.1: T-0023 and T-0024 green in both frameworks and applicable sanitizers;
  post-promotion no spurious `worker_stalled` for physical C; abandoned
  claims never force-commit a slot.
- G5.2: stall recovery and double-fault state machine pass in both frameworks
  and applicable sanitizers; each state transition has at least one test.
- G5.3: memory-corruption CRC skip path exercised end-to-end in at least one
  integration scenario and covered by unit tests.
- G5.4: Compose scenarios S1 (crash), S2 (stall), S3 (memory corruption), S5
  (double fault), and S6 (supervisor loss) green five consecutive times each.
- G5.5: S1 replay semantically identical to original run (same event
  categories in order, same final ownership, same record count).
- G5.6: bounded recovery time measured per category; `worker_crashed`
  inherits `<100 ms`; stall escalation, memory-corruption, and double-fault
  budgets defined in DEC-0012 #10 recorded in Phase 5 evidence.
- Exit: framework, sanitizer, Clang, Docker, Compose, repeated integration,
  timing, and hosted CI evidence are recorded in [Phase 5 evidence](../evidence/phase-5.md).

## Residual Risks

- Perturbation timing is host-sensitive: the reference host consumes 84–91 ms
  of the `<100 ms` budget on baseline crash recovery (Phase 4 exit evidence);
  stall-detection thresholds and SIGCONT-escalation timing must leave slack.
- The `perturb` binary and worker SIGUSR2 hook are test surfaces. If compiled
  into a production build without opt-in disabled, they widen the signal
  surface; the opt-in CLI/env flag must be exercised by a negative test.
- Deterministic replay is semantic, not bit-identical. Scheduling noise
  inside the supervisor and monitor poll cadence can shuffle events with
  identical timestamps; the comparison strips timestamps for exactly this
  reason.
- DEGRADED state is a terminal state within a supervisor lifetime. Full
  recovery of a DEGRADED logical ring requires either supervisor restart or
  a Phase 5b second-standby topology; neither is in Phase 5 scope.
- The supervisor remains a deliberate single point of trust; S6 confirms
  this rather than mitigates it. Phase 6 HA work is the venue for redundancy.

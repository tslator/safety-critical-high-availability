# DEC-0011: Phase 4 Supervisor Failover

- Status: Accepted
- Date: 2026-09-20
- Related discussion: [D-2026-09-20-002](../reviews/2026-09-20-phase4-supervisor-architecture.md)
- Authorizes: [T-0015](../tasks/T-0015-phase4-ownership-epoch.md)–[T-0022](../tasks/T-0022-phase4-integration-exit.md)

## Context

Phase 3 provides worker liveness detection and a stable JSON alert vocabulary,
but no process supervisor, failover state machine, or role-transfer protocol.

## Decision

1. Phase 4 targets two hot logical workloads, A and B, and one standby physical
   process, C. Recovery is limited to `worker_crashed`; stalls, double faults,
   and replay remain Phase 5 scenarios.
2. Physical process identity is separate from logical workload identity. Before
   failure, physical A owns logical A at epoch 1, physical B owns logical B at
   epoch 1, and C is standby. After A fails, C assumes logical A at epoch 2
   and owns logical ring A. A is restarted as standby with a new generation.
3. Exactly one process may own a logical ring for an epoch. Stale generations
   and stale monitor events are rejected. Any required ownership metadata is a
   controlled, versioned shared-memory v4 change; v3 is not reinterpreted.
4. The supervisor launches and owns the monitor and reads its existing JSON
   stdout through a pipe. Event names and semantics from DEC-0010 remain the
   published contract. Invalid and stale input is handled deterministically.
5. The `<100 ms` target starts at the injected worker fault and ends at the
   supervisor's first validated output record from the promoted workload.
6. A supervisor-side drain/validation path is a test witness, not an added
   production consumer in the ring hot path.
7. The supervisor attempts `SCHED_FIFO` priorities satisfying
   `monitor < supervisor < workers`; unprivileged environments use explicit
   best-effort fallback.
8. Compose launches the real process topology with shared IPC and runtime
   directories. Supervisor loss exits the container and relies on restart
   policy.

## Consequences

Phase 4 changes must preserve CRC, monotonic sequence, CAS, lock-free hot-path,
and sanitizer requirements. Worker startup, reaping, pidfile generations,
ownership acknowledgements, and shutdown become supervisor lifecycle concerns.

## Maintenance Rules

- The monitor remains read-only with respect to shared memory.
- No allocation, syscall, exception, or mutex may enter the ring hot path.
- Accepted decisions are superseded rather than rewritten.

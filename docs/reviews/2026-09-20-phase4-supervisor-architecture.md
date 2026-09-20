# D-2026-09-20-002: Phase 4 Supervisor Architecture

- Date: 2026-09-20
- Status: Accepted
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)
- Resulting tasks: [T-0015](../tasks/T-0015-phase4-ownership-epoch.md)–[T-0022](../tasks/T-0022-phase4-integration-exit.md)

## Question

How should Phase 4 turn the Phase 3 monitor and worker contracts into a
supervised failover path while preserving the shared-memory safety invariants?

## Findings

- The current standby worker is passive and cannot change role in place.
- Physical process identity must be separated from logical workload identity.
- Monitor stdout JSON lines are a sufficient semantic event contract; a
  supervisor-owned pipe provides deterministic delivery.
- Epoch fencing is required to reject stale processes and alerts after a role
  transfer.
- The existing A/B hot plus C standby topology is the smallest useful target.
- The `<100 ms` target must be defined as fault-to-first-validated-output.
- Compose currently starts placeholders rather than the actual process graph.
- Scheduling order is mandatory, but ordinary CI may lack real-time permission.

## Alternatives Considered

- Relaunching the standby as hot was rejected as the primary design because it
  has no explicit logical ownership or epoch fencing.
- Keeping physical and logical identities coupled was rejected because a
  standby cannot safely assume a failed worker's ring.
- A new socket protocol was deferred; it adds transport surface without
  changing alert semantics.
- Stall recovery and double faults remain Phase 5 scope.

## Result

Phase 4 will implement crash-only supervision for A/B hot plus C standby. C
remains the physical process but assumes logical A under a new epoch and owns
logical ring A. A is restarted as standby with a new process generation. The
supervisor owns the monitor child and consumes newline-delimited JSON through a
pipe. A controlled shared-memory v4 change is authorized if required for
logical ownership, epochs, or fencing.

The supervisor will drain and validate output as the first-output SLA witness.
It will attempt `SCHED_FIFO` with `monitor < supervisor < workers` ordering,
falling back explicitly when permissions are unavailable. Compose will run the
real process topology with shared IPC and pid/runtime directories.

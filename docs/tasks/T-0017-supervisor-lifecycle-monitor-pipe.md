# T-0017: Phase 4 Supervisor Lifecycle and Monitor Pipe

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0015
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Implement supervisor process lifecycle, child launch/reaping, monitor ownership,
newline-delimited JSON pipe ingestion, validation, and shutdown.

## Acceptance Criteria

- Shared memory is created/verified before children launch.
- Worker and monitor children are reaped and terminated deterministically.
- Partial, malformed, unknown, duplicate, stale, and EOF input is handled
  without deadlock or unsafe failover.
- Shutdown signals workers, drains the witness path, and exits cleanly.

## Evidence

Record implementation and integration results in [Phase 4 evidence](../evidence/phase-4.md).

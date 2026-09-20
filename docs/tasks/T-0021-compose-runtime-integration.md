# T-0021: Phase 4 Compose Runtime Integration

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0017, T-0018
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Replace Compose placeholders with the real supervisor/monitor/worker topology,
shared IPC, pid/runtime directories, health checks, and failover smoke testing.

## Acceptance Criteria

- Compose services see the required shared-memory namespace.
- Supervisor owns processes and handles signals/children correctly.
- Docker build and Compose startup/cleanup stay green.
- Compose-level crash/failover completes successfully.

## Evidence

Record Docker and Compose results in [Phase 4 evidence](../evidence/phase-4.md).

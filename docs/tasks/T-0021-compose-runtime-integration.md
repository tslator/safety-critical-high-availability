# T-0021: Phase 4 Compose Runtime Integration

- Status: Complete
- Owner: AI agent (opencode)
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

Compose now launches a single `supervisor` service that runs
`safety-critical-ha supervisor` (infinite `--runtime-ms` and unbounded hot
worker ticks), forks the monitor and hot A/B + standby C workers inside one
container, and shares `/dev/shm` plus `/run/safety-critical-ha`. A healthcheck
polls the region file plus all three worker pidfiles and their liveness, and
`containers/compose/failover-smoke.sh` SIGKILLs hot physical A inside the
running container and verifies (a) the supervisor stays up, (b) the physical-A
pidfile points to a fresh live pid, (c) the monitor emits `worker_running` for
physical worker 2 (proving C was promoted to logical A), and (d) the shared
region survived. Recorded in [Phase 4 evidence](../evidence/phase-4.md).

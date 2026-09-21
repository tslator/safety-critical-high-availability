# T-0020: Phase 4 Scheduling Priority Policy

- Status: Complete
- Owner: AI agent (opencode)
- Priority: Medium
- Depends on: T-0017
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Implement best-effort scheduling setup and tests for `monitor < supervisor < workers`.

## Acceptance Criteria

- Privileged tests verify numeric priority ordering.
- Unprivileged startup has deterministic fallback and records failed elevation.
- Priority setup does not block or alter the ring hot path.

## Evidence

The runtime scheduling policy assigns numeric priorities 10/20/30 to monitor,
supervisor, and workers respectively, attempts `SCHED_FIFO` once during process
startup, and records an explicit non-fatal fallback when elevation is denied.
Record validation results in [Phase 4 evidence](../evidence/phase-4.md).

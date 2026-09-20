# T-0020: Phase 4 Scheduling Priority Policy

- Status: Planned
- Owner: Unassigned
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

Record privileged and fallback results in [Phase 4 evidence](../evidence/phase-4.md).

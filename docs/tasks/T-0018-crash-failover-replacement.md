# T-0018: Phase 4 Crash Failover and Replacement

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0016, T-0017
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Implement the idempotent `worker_crashed` recovery state machine for physical
C to assume logical A and physical A to return as standby.

## Acceptance Criteria

- A crash is handled once per epoch.
- Physical C assumes logical A and reaches the required ready state.
- Replacement physical A attaches, starts as standby, and publishes readiness.
- Recovery failures enter an explicit degraded/failsafe state.

## Evidence

Record repeated crash/failover results in [Phase 4 evidence](../evidence/phase-4.md).

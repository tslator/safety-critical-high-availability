# T-0019: Phase 4 Sequence Continuity and Output Witness

- Status: Complete
- Owner: AI agent (opencode)
- Priority: High
- Depends on: T-0015, T-0016, T-0018
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Implement supervisor-side draining and validation for epoch, sequence, CRC,
no-loss, no-duplicate, and first-output timing across takeover.

## Acceptance Criteria

- The first validated post-failover output is observable by the supervisor.
- Fault-to-first-output is measured against the `<100 ms` target.
- Stale records are rejected and committed records are not duplicated or
  silently discarded.
- The witness remains outside the ring operation hot path.

## Evidence

The supervisor now drains each logical ring outside the ring hot path, validates
ownership epoch monotonicity, transport sequence continuity, and CRC failure
counts, and records whether output is observed after logical-A takeover.
Validation is recorded in [Phase 4 evidence](../evidence/phase-4.md).

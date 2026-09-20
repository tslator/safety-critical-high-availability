# T-0015: Phase 4 Ownership and Epoch Model

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0014
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Define logical ownership separately from physical process identity, including
epoch/generation fencing and any controlled shared-memory v4 layout.

## Acceptance Criteria

- Exactly one owner exists per logical ring and stale generations are rejected.
- Any v4 layout change is versioned, documented, and tested in both frameworks.
- CRC, monotonic sequence, CAS, and lock-free hot-path rules remain intact.
- Abandoned producer-claim recovery behavior is specified and tested.

## Evidence

Record implementation links and matrix results in [Phase 4 evidence](../evidence/phase-4.md).

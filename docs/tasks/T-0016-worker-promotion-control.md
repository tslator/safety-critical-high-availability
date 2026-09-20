# T-0016: Phase 4 Worker Promotion Control

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0015
- Phase: Phase 4
- Related decision: [DEC-0011](../decisions/0011-phase4-supervisor-failover.md)

## Scope

Add worker-side promotion, ownership acknowledgement, epoch fencing, and
replacement standby startup control.

## Acceptance Criteria

- Physical C assumes logical A under a new epoch without two active owners.
- Stale workers cannot publish after ownership transfer.
- Replacement physical A starts as standby with a new generation.
- Control transitions are deterministic and sanitizer-clean.

## Evidence

Record implementation and test links in [Phase 4 evidence](../evidence/phase-4.md).

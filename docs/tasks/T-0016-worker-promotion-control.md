# T-0016: Phase 4 Worker Promotion Control

- Status: Complete
- Owner: AI agent (opencode)
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

Implementation:

- `workers/include/safety_crit/workers/worker_config.hpp` separates physical
  worker identity, logical ring assignment, and process generation.
- `workers/src/worker_entry.cpp` acknowledges ownership before hot operation,
  fences every publication with the ownership token, and promotes an assigned
  standby only after the supervisor-visible transfer.
- `app/src/main.cpp` exposes `--logical-id` and `--generation` for controlled
  process startup.

Validation:

- GoogleTest: 85/85 passed in `build/t0015-gtest`.
- Catch2: 85/85 passed in `build/t0015-catch2`.
- Worker integration tests reject stale generations before publication and
  verify promoted physical C acknowledges logical A at the new generation.
- `git diff --check` passed.

See [Phase 4 evidence](../evidence/phase-4.md).

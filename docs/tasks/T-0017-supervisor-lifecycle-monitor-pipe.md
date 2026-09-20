# T-0017: Phase 4 Supervisor Lifecycle and Monitor Pipe

- Status: Complete
- Owner: AI agent (opencode)
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

Implementation:

- `supervisor/` adds the management-plane supervisor library and CLI support.
- Shared memory is created and identity-verified before child launch.
- Monitor stdout is consumed through a pipe with strict newline-delimited JSON
  validation; malformed, unknown, duplicate, partial, and EOF records are
  rejected without triggering recovery actions.
- Workers and monitor receive bounded SIGTERM shutdown, then SIGKILL fallback,
  and are synchronously reaped.

Validation:

- GoogleTest: 90/90 passed in `build/t0015-gtest`.
- Catch2: 90/90 passed in `build/t0015-catch2`.
- Supervisor tests cover alert parsing, malformed/unknown/partial records,
  shutdown reports, and a real launch/pipe/reap lifecycle.
- `git diff --check` passed.

See [Phase 4 evidence](../evidence/phase-4.md).

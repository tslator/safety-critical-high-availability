# T-0025: Phase 5 Supervisor Stall Recovery

- Status: Complete
- Owner: Unassigned
- Priority: High
- Depends on: T-0023
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Extend the supervisor to act on `worker_stalled` alerts consumed from the
monitor pipe. Phase 4 only transitions state; Phase 5 issues bounded recovery.

Per DEC-0012 #3: one bounded `SIGCONT` to the stalled worker, then a
SIGKILL-escalation via the existing crash-recovery path if the ring tail has
not advanced after a documented grace period (default 200 ms; configurable
via a supervisor CLI flag).

## Acceptance Criteria

- Supervisor state machine includes `kStalledRecovering`; enters on
  `worker_stalled` alert and exits to `kRunning` on ring-tail advance or to
  `kFailoverDetected` on SIGKILL escalation.
- Idempotent per epoch: a second `worker_stalled` on an already-recovering
  ring is a no-op; the grace timer is not reset.
- Supervisor stdout emits `supervisor: stall escalation for physical N at
  epoch E` and `supervisor: stall recovered for physical N at epoch E` so
  scenarios can assert recovery timing.
- Unit tests: SIGCONT-only recovery, SIGCONT-then-SIGKILL escalation,
  idempotence, and grace-period expiry.
- Integration test: SIGSTOP a hot worker; verify `worker_stalled`, SIGCONT
  observed, ring advances; SIGSTOP again with no SIGCONT and verify escalation
  triggers crash recovery.
- Both frameworks green; ASan+UBSan and TSan green in applicable configs.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

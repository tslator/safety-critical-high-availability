# T-0026: Phase 5 Supervisor Double-Fault and DEGRADED State

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0023, T-0024, T-0025
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Generalize the supervisor's crash recovery past the current
`physical_worker == 0` hard-coding (see `recover_worker_crash` in
`supervisor/src/supervisor.cpp`). Add explicit DEGRADED handling when a
second hot physical worker crashes while the first is still recovering or
when there are no standby physical processes left to promote.

Per DEC-0012 #4: physical C can own at most one logical ring per epoch. On
simultaneous crash of hot physical A and hot physical B, C promotes to
exactly one logical ring (deterministic tie-break: lowest logical-ring index).
The other logical ring is marked DEGRADED in a `worker_status` bit and via a
supervisor stdout event; the second hot physical process restarts as standby.
Recovery of the DEGRADED ring is out of scope.

## Acceptance Criteria

- `recover_worker_crash` handles any physical worker index, not just 0.
- New `WorkerStatusFlag::kDegraded` bit reserved in `worker_status`; region
  layout `kRegionVersion` stays at 4.
- Supervisor state machine includes `kDegraded`; DEGRADED state is terminal
  for the affected logical ring within a supervisor lifetime.
- Supervisor stdout emits `supervisor: logical ring N degraded (reason=...)`
  exactly once per degraded ring.
- Unit tests: single-crash A, single-crash B, simultaneous A+B (verifies one
  promotion + one DEGRADED), and re-crash of an already-handled physical
  worker (idempotent).
- Integration test: SIGKILL physical A and physical B in a tight window;
  verify C owns one logical ring, the other is DEGRADED, and replacement
  A/B pidfiles are present as standby.
- Both frameworks green; ASan+UBSan and TSan green in applicable configs.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

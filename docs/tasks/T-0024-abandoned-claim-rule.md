# T-0024: Phase 5 Abandoned Producer Claim Rule

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: Phase 4 exit (no upstream task)
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Define and test an explicit rule for an abandoned producer claim (a crash
between claiming a ring slot and committing it). Phase 4 plan "Residual
Risks" listed this as an untested rule; DEC-0012 #2 makes it a P0 Phase 5
blocker because memory-corruption scenarios in T-0027 and T-0029 depend on a
deterministic answer.

Rule (from DEC-0012 #2): an epoch bump implies every in-flight claim from the
prior epoch is abandoned. The new owner resumes from the last committed
sequence; it never force-commits an in-flight slot.

## Acceptance Criteria

- Written rule documented in `shared-memory/include/safety_crit/shared_memory/ring_buffer.hpp`
  (comment block) and in [Phase 5 evidence](../evidence/phase-5.md).
- Unit test: force a producer claim without commit (SIGUSR1 crash hook at a
  deterministic point), bump the epoch via `transfer_ownership`, verify the
  new owner observes the ring quiescent at the last committed sequence and
  never pops a slot from the prior epoch.
- Test covers both a claim on the very first slot and a claim on an interior
  slot.
- Both frameworks green; ASan+UBSan and TSan green in applicable
  configurations; no new data-race reports.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

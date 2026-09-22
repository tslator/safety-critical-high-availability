# T-0024: Phase 5 Abandoned Producer Claim Rule

- Status: Complete
- Owner: AI agent (opencode)
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

Rule implementation: `shared-memory/src/shared_region.cpp` — a rollback loop
at the end of `transfer_ownership` (after the epoch/owner CAS pair succeeds).
Each pass reads `tail_` and `head_`; if `tail_ > head_` and the slot at
`tail_-1` still holds `ready(tail_-1)` (never committed since prior lap's
release), the claim was abandoned and `tail_` is CAS-ed back to `tail_-1`.
The loop breaks on either a successful rollback (single rollback per handoff
suffices under the one-owner-per-epoch rule) or a non-abandoned slot state
(committed at `tail_-1` or released-to-next-lap). Never force-commits.
Ordering: acquire load on the slot sequence synchronizes with the prior
commit release if any; relaxed CAS on `tail_` matches the ring's existing
CAS-relaxed pattern (the epoch release already synchronizes the handoff).

Rule documentation: `shared-memory/include/safety_crit/shared_memory/ring_buffer.hpp`
comment block updated in the same change (see commit).

TDD tests (all in `shared-memory/tests/shared_region_layout_test.cpp`,
written first against pre-fix code, observed to FAIL, then flipped to PASS):

| Test | Scenario | Pre-fix | Post-fix |
|---|---|---|---|
| `SharedRegion.AbandonedClaimOnFirstSlotRollsBackTail` | Claim at position 0, no commit; transfer ownership | FAIL (tail_ stays 1) | PASS (tail_ rolled back to 0) |
| `SharedRegion.AbandonedClaimOnInteriorSlotRollsBackTail` | 3 committed + abandoned claim at position 3; transfer ownership | FAIL (tail_ stays 4) | PASS (tail_ rolled back to 3) |
| `SharedRegion.TransferDoesNotRollBackCommittedSlots` | 1 committed record; transfer ownership | PASS (control case, no rollback expected) | PASS; committed payload also verified poppable after takeover |

Validation matrix:

| Configuration | Result |
|---|---|
| GoogleTest (plain, fresh dir) | PASS 99/99 (96 pre-existing + 3 new) |
| Catch2 (plain, fresh dir) | PASS 99/99 |
| ASan+UBSan GoogleTest (fresh dir) | PASS 91/91 |
| ASan+UBSan Catch2 (fresh dir) | PASS 91/91 |
| TSan GoogleTest (`setarch --addr-no-randomize`, fresh dir) | PASS 91/91, zero race reports |
| TSan Catch2 (`setarch --addr-no-randomize`, fresh dir) | PASS 91/91, zero race reports |
| clang-verify (pinned `safety-critical-ha:verify-clang-14`, clang-14 preset) | PASS 99/99, zero clang warnings |
| Docker build (image `safety-critical-ha:phase0`, `--version`) | PASS |
| Compose `up --wait` healthy + `containers/compose/failover-smoke.sh` | PASS 5 consecutive |
| `scripts/phase4-failover-timing.sh 5` (regression check on supervisor drain timing) | PASS min/median/max/avg 84/84/90/85 ms, 0/5 over `<100 ms` |
| `./scripts/sync-agent-guidance.sh --check` | PASS |

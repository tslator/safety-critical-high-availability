# T-0023: Phase 5 Monitor Logical-Ring Attribution

- Status: Complete
- Owner: AI agent (opencode)
- Priority: High
- Depends on: Phase 4 exit (no upstream task)
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Fix the Phase 4 tail defect recorded in [Phase 4 evidence](../evidence/phase-4.md)
"Follow-ups surfaced by T-0021": `poll_worker` reads
`region.rings[physical_idx].tail_`, so after a promotion physical C's home
ring (index 2) never advances and the monitor emits a spurious
`worker_stalled` for the promoted worker. The observation should read the
ring owned by the physical worker (via its ownership cell), falling back to
the home ring only when the worker owns no logical ring.

P0 blocker for Phase 5 stall and double-fault scenarios: without this fix,
every stall scenario after a promotion produces false positives.

## Acceptance Criteria

- Monitor `poll_worker` looks up the physical worker's owned logical ring
  via `read_ownership` and reads that ring's tail; when the worker owns no
  ring (standby), falls back to the home ring index.
- Post-promotion integration scenario: physical C produces on logical A after
  takeover; monitor emits exactly one `worker_running` for physical 2 and no
  `worker_stalled` within the stall threshold window.
- Both GoogleTest and Catch2 green; ASan+UBSan and TSan green in applicable
  configurations.
- Existing Phase 3 monitor integration cases still pass unchanged.

## Evidence

Implementation: `monitors/include/safety_crit/monitors/health.hpp` — new
`owned_logical_ring(region, physical_idx)` helper iterates logical rings,
returns the one whose `read_ownership` reports this physical worker;
`poll_worker` reads the owned ring's tail, falling back to the physical
home index for standby workers (whose status word is IDLE so the stall rule
never fires). `#include <optional>` added.

Regression test: `MonitorsLoop.AttributionFollowsLogicalRingAfterPromotion`
in `monitors/tests/monitors_loop_test.cpp`. The test transfers ownership of
logical ring 0 to physical worker 2 (simulating standby C promoted to hot
on logical A), sets physical 2 status to RUNNING, advances only ring 0's tail
per pacer tick past the 50 ms stall threshold, and asserts no
`worker_stalled` alert for physical 2 is emitted. The test failed
(`ctest -R AttributionFollowsLogicalRingAfterPromotion` reported FAILED)
before the fix and passes after; TDD red → green transition captured.

Validation matrix:

| Configuration | Result |
|---|---|
| GoogleTest (plain, fresh dir) | PASS 96/96 (95 pre-existing + 1 new) |
| Catch2 (plain, fresh dir) | PASS 96/96 |
| ASan+UBSan GoogleTest (fresh dir) | PASS 88/88 |
| ASan+UBSan Catch2 (fresh dir) | PASS 88/88 |
| TSan GoogleTest (`setarch --addr-no-randomize`, fresh dir) | PASS 88/88, zero race reports |
| TSan Catch2 (`setarch --addr-no-randomize`, fresh dir) | PASS 88/88, zero race reports |
| clang-verify (pinned `safety-critical-ha:verify-clang-14`, clang-14 preset) | PASS 96/96, zero clang warnings |
| Docker build (image `safety-critical-ha:phase0`, `--version`) | PASS |
| Compose `up --wait` healthy + failover smoke | PASS 5/5 consecutive |
| `scripts/phase4-failover-timing.sh 5` (supervisor drain unchanged) | PASS min/median/max/avg 85 ms, 0/5 over `<100 ms` |
| `./scripts/sync-agent-guidance.sh --check` | PASS |

Hosted CI run link: recorded in this file after CI completes.

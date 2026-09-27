# T-0041: Region Metrics Collector

- Status: Complete (2026-09-26)
- Owner: AI agent (opencode)
- Priority: High
- Depends on: T-0040 (subtask 2/3 of T-0036)
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Second slice of the T-0036 decomposition: the read-only region collector in
`safety_crit::observability` (`region_metrics.hpp` + `src/region_metrics.cpp`,
linking `safety_crit::shared_memory`).

- `declare_region_metrics(registry)`: registers the DEC-0014 §8 region
  families: `worker_status{worker}` (gauge), `ring_buffer_sequence{worker}`
  (gauge), `ring_corruptions_total{worker}` (counter), `ownership_epoch{ring}`
  (gauge); worker/ring labels are decimal indices (`0`..`kMaxWorkers-1`).
- `collect_region_metrics(region, registry)`: one acquire-load read per
  value via the sanctioned observer surface only
  (`WorkerStatusCell.status` acquire loads, `RingBuffer::pushed()` /
  `corruption_count()`, `read_ownership()`); no CAS, no store, no pop — a
  region write from this code is a review blocker (DEC-0014 §11).
- `worker_status` mapping (DEC-0014 §8): IDLE=0, RUNNING=1, CRASHED=2,
  RECOVERING=3, DEGRADED=4. Multiple flag bits resolve by precedence
  DEGRADED > RECOVERING > CRASHED > RUNNING > IDLE; a zero word (never
  started) maps to 0. The mapping is documented in the header and pinned
  by tests.

## Acceptance Criteria

- Region-collector tests against a created test region verify gauge values
  equal region state at render time; state changes (status flag flips,
  pushes, ownership transfer in the test fixture) are visible on the next
  collect/render cycle.
- `verify_identity()` still succeeds after collection (read-only proof).
- Sanitizer legs green; `git diff` shows zero `shared-memory/` changes.

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

# T-0009: Phase 2 T2.1 — Worker Core

- Status: In Review (implementation + local validation done; awaiting hosted CI)
- Owner: AI agent (opencode)
- Priority: High
- Depends on: Phase 1 exit (G1.1–G1.5)
- Phase: Phase 2
- Phase label: T2.1
- Related decision: [DEC-0009](../decisions/0009-phase2-worker-runtime.md)

> Canonical narrative and gate live in
> [Phase 2 plan T2.1](../phases/PHASE_2_WORKERS.md).

## Scope

Create the `safety_crit::workers` library: worker config, `OVERRUN` status
bit, deterministic workload simulation, and the C++20 ranges processing
pipeline — with tests.

## Deliverables

- `workers/` static library wired into the build (warnings, sanitizers, test
  adapter); ownership rows in `ARCHITECTURE.md` / `ARCHITECTURE_RULES.md`.
- `worker_config.hpp` (id/role/tick interval/CPU budget/seed derivation).
- `workload.hpp`: splitmix64 sensor sim (per-(worker, tick) determinism) +
  ranges pipeline with manual-loop equivalence witness.
- `OVERRUN` bit in `WorkerStatusFlag` with flag-test coverage.

## Acceptance Criteria

- Core tests green in GoogleTest, Catch2, and ASan+UBSan; zero warnings.
- Determinism: identical (worker, tick) → byte-identical processed output.
- Pipeline output identical to the manual loop over the test corpus.

## Validation

Gate G2.1 (see phase plan). Record in `NOTES.md`.

## Completion Notes

Implemented 2026-09-19 (`workers/` library: `worker_config.hpp/.cpp`,
`workload.hpp`, `workers_core_test.cpp` 6 cases, `OVERRUN` bit +
`AtomicFlags.OverrunBitIsIndependent`). Local validation: 51/51 in
GoogleTest, Catch2, ASan+UBSan, TSan (both frameworks), and clang-verify —
zero warnings on first-party code. Deviation #1 in the phase plan records
the clang-14/libstdc++-12 `<ranges>` breakage and the guard/fallback.
Splitmix64 reference vectors computed from the reference stream
(independently verified); one initially mis-chained vector was caught by the
test itself.

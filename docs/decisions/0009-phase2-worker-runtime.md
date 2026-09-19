# DEC-0009: Phase 2 Worker Runtime

- Status: Accepted
- Date: 2026-09-19
- Related discussion: [D-2026-09-19-002](../reviews/2026-09-19-phase2-worker-architecture.md)
- Authorizes: [T-0009](../tasks/T-0009-worker-core.md),
  [T-0010](../tasks/T-0010-work-loop-signals-deadlines.md),
  [T-0011](../tasks/T-0011-worker-cli-integration-phase-exit.md)

## Context

Phase 2 adds worker processes that attach to the shared region, run a
deadline-monitored work loop, update `worker_status`, and demonstrate hot /
warm-standby roles. The plan sketch predates the implemented baseline and
uses facilities the project cannot adopt; the reconciliation must keep the
frozen Phase 1 contract and the hot-path policies intact.

## Decision

1. **Layout: v3 stays frozen.** A worker is a producer on its own
   `SharedRegion::rings[idx]`, committing through the region-level `push()`
   (which maintains `global_seq` and `integrity_word` exactly as designed in
   T1.3). The plan's "input ring" is realized as a deterministic in-worker
   workload simulation: per-worker splitmix64 PRNG seeded from
   (worker id, tick) so the same tick sequence yields byte-identical outputs
   (the plan's idempotency test). External input injection would require
   layout v4 and is deferred to a future decision only if a later phase
   demonstrates the need.
2. **C++20 ceiling.** No `std::generator` (C++23; absent on the gcc-12
   primary) and no `std::this_thread::pause` (C++26). The work loop is a
   plain loop with `std::stop_token` cooperative cancellation; idle backoff
   uses `__builtin_ia32_pause()` guarded by feature-test macros, falling back
   to `std::this_thread::yield()`. The C++20 ranges pipeline from the sketch
   stays and keeps a manual-loop equivalence test.
3. **Signals.** `sigaction`-based handlers. SIGTERM/SIGINT set a
   `volatile std::sig_atomic_t` flag (async-signal-safe); the loop checks it
   between ticks and converts the request into a stop (bounded stop latency
   of one tick, documented). SIGUSR1 is a test-only forced-crash hook
   (immediate `abort()` semantics, no cleanup) reserved for Phase 5 fault
   injection. No non-trivially-destructible object is touched in a handler.
4. **OVERRUN status bit.** Deadline monitoring adds an `OVERRUN` bit to
   `WorkerStatusFlag` semantics (the status word is a packed bitwise OR; this
   is a semantics addition, not a layout change — `kRegionVersion` stays 3).
   Per tick, elapsed wall time is measured against a configurable budget
   (default per tick plan §Phase 2; injected clock type in tests,
   `std::chrono::steady_clock` in production); overruns set the bit and
   increment an in-region-visible counter observable via status polling.
5. **Delivery: one binary, subcommands.**
   `safety-critical-ha worker --id <a|b|c> [--role hot|standby] [--ticks N]
   [--tick-interval-ms MS] [--budget-us US]`; hand-rolled parsing in `app/`
   (no new dependencies). `--version` behavior is untouched, so image
   healthchecks and the Compose smoke keep working. Compose stays unchanged:
   `/dev/shm` is per-container, the Phase 2 validated topology is multiple
   processes in one container; cross-container topology is a Phase 7 demo
   decision.
6. **New `workers/` component.** Static library `safety_crit::workers` with
   headers under `workers/include/safety_crit/workers/` and tests under
   `workers/tests/` (own suite, standard matrix). `app/` keeps only CLI
   parsing/wiring. `docs/ARCHITECTURE.md` and
   `docs/ai-guidance/ARCHITECTURE_RULES.md` gain the ownership row;
   `CORE.md` is untouched, so the generated adapters stay byte-identical.
7. **Hot path stays clean.** The worker tick performs only: ring `push()`
   (claim/commit), PRNG arithmetic, the pipeline computation, status store,
   and clock reads — no allocation, no syscalls, no exceptions, no mutexes
   (POLICIES). "Logging" is in-region counters only (Phase 6 owns real
   observability).
8. **Roles.** Hot workers run the loop and push outputs. Warm standby polls
   region status flags and stays ready; failover orchestration (deciding and
   executing takeover) is Phase 4 supervisor scope and is NOT built here.

## Consequences

- New files: `workers/` (library + tests), Phase 2 plan
  (`docs/phases/PHASE_2_WORKERS.md`), tasks T-0009..T-0011.
- Modified: `CMakeLists.txt` (`add_subdirectory(workers)`),
  `shared-memory` flags header (OVERRUN bit), `app/src/main.cpp` + `app/`
  parsing (subcommand), `README.md` (worker command), `docs/ARCHITECTURE.md`,
  `docs/ai-guidance/ARCHITECTURE_RULES.md`.
- New CI coverage arrives with the test targets (native ×2, sanitizers ×4,
  clang-verify pick up `workers_*` tests automatically); no workflow edits
  are planned in Phase 2.
- Fork-based integration tests run in the plain build only (fork under
  sanitizers is unreliable — G1.4 precedent); the in-process loop tests carry
  the sanitizer signal.

## Maintenance Rules

- Any future need for external input injection goes through a new decision
  that bumps `kRegionVersion` and updates the verify path — never by
  reinterpreting existing fields.
- Signal-handler code must keep the async-signal-safety discipline
  (`sig_atomic_t` only); violations are review blockers.
- Accepted decisions are superseded, not rewritten.

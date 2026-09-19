# D-2026-09-19-002: Phase 2 Worker Architecture Reconciliation

- Date: 2026-09-19
- Status: Accepted
- Related decision: [DEC-0009](../decisions/0009-phase2-worker-runtime.md)
- Resulting tasks: [T-0009](../tasks/T-0009-worker-core.md),
  [T-0010](../tasks/T-0010-work-loop-signals-deadlines.md),
  [T-0011](../tasks/T-0011-worker-cli-integration-phase-exit.md)

## Question

The Phase 2 plan sketch (`SAFETY_CRITICAL_HA_PLAN.md` §Phase 2) assumes
coroutine-based work loops, C++26 `std::this_thread::pause`, and an
input-ring/output-ring pair per worker. None of these survive contact with the
implemented baseline: the project is C++20 with gcc-12 as the pinned primary
compiler, and the Phase 1 exit froze the shared region at layout v3, which has
exactly one ring per worker. How should Phase 2 workers be built against the
real baseline without breaking it or over-building for later phases?

## Findings

- `std::generator` is C++23 (libstdc++ 13+); the gcc-12 primary cannot even
  compile the sketch. `std::this_thread::pause` is C++26. The sketch's loop
  shape must be re-expressed with C++20 tools; `std::stop_token` /
  `std::stop_source` (C++20) provide the same cooperative-cancellation
  intent, and x86 `pause` is available as a guarded builtin.
- The v3 layout carries one ring per worker, and `shared_region.hpp`'s
  region-level `push()` docblock already designates it as the Phase 2 worker
  commit path. An "input ring" would require layout v4 — a breaking change
  with revalidation cost — for a need the demo does not have yet: the plan's
  own test list (idempotent workload, deadline monitoring, pipeline
  equivalence) is satisfied by a deterministic in-worker workload simulation.
- The Compose stack runs one container per role with `sleep` placeholders;
  `/dev/shm` is per-container, so the plan's shared-memory topology is one
  container running multiple processes. Phase 2 must validate multi-process
  behavior in-container (fork tests, CLI), and cross-container topology is a
  Phase 7 demo decision — Compose must stay untouched so the
  `docker-compose-smoke` gate keeps meaning what it means.
- Signal handling cannot put `stop_source` (not async-signal-safe) in a
  handler; the safe pattern is a `sig_atomic_t` flag flipped by the handler
  and consumed by the loop between ticks. SIGUSR1-as-forced-crash is the
  plan's fault-injection hook for Phase 5 and must not do cleanup.
- The status word is a packed bitwise OR; adding an `OVERRUN` flag bit for
  deadline monitoring is a semantics addition, not a layout change, so no
  `kRegionVersion` bump is needed.
- POLICIES forbid allocation/syscalls/exceptions/mutexes in the hot path;
  the worker tick (ring op + arithmetic + status store) can stay clean, and
  deadline measurement can use an injected clock so tests are deterministic
  and syscall-free.
- No CLI argument-parsing dependency exists in the project; adding one for a
  handful of flags is not warranted — hand-rolled parsing in `app/` suffices.

## Alternatives Considered

- **Build as sketched (coroutines, input+output rings):** rejected —
  uncompilable on the primary (C++23) and a breaking layout change.
- **v4 layout with per-worker input rings now:** rejected for Phase 2 —
  re-validation cost and a frozen-contract change for a need nothing has yet
  demonstrated; kept open as a future decision if Phase 3/4 show a real
  external-injection requirement.
- **Separate `worker` binary:** rejected — the image, healthchecks, and
  Compose all reference the single installed binary; a subcommand keeps all
  of them valid.
- **C++20-reconciled single binary + frozen v3 + in-worker deterministic
  workload (Chosen):** see DEC-0009.

## Result

Build Phase 2 against the real baseline: C++20 loop with stop-token
cancellation, guarded pause builtin, async-signal-safe SIGTERM/SIGINT and
test-only SIGUSR1 crash hook, OVERRUN as a status flag bit, frozen v3 layout
with a deterministic in-worker workload pushing to the worker's own ring, and
an extended `safety-critical-ha worker` subcommand. Rationale in
[DEC-0009](../decisions/0009-phase2-worker-runtime.md).

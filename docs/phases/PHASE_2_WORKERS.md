# Phase 2 - Worker Processes

## Purpose

Add worker processes that attach to the Phase 1 shared region, run a
deadline-monitored work loop producing deterministic processed output onto
their own ring, maintain `worker_status` flags, respond to signals, and
demonstrate hot / warm-standby roles — the execution surface the Phase 3
monitor observes and the Phase 4 supervisor controls.

Authoritative decision: [DEC-0009](../decisions/0009-phase2-worker-runtime.md)
(discussion: [D-2026-09-19-002](../reviews/2026-09-19-phase2-worker-architecture.md)).

## Scope and Non-Goals

**In scope**

- `workers/` library (`safety_crit::workers`): worker config, status
  lifecycle helpers, deterministic workload simulation, C++20 ranges
  processing pipeline (with manual-loop equivalence tests).
- Work loop with `std::stop_token` cancellation, per-tick CPU budget
  (deadline) monitoring, `OVERRUN` status bit, guarded `pause` backoff.
- `safety-critical-ha worker --id …` subcommand; region create-or-open
  attach; hot and warm-standby roles.
- Multi-process (fork-based) integration tests in the plain build; standard
  matrix on all unit tests.

**Out of scope (later phases)**

- Failover orchestration / takeover decisions (Phase 4 supervisor).
- Heartbeat polling, stall detection, metrics collection (Phase 3 monitor).
- Fault injection beyond the SIGUSR1 crash hook (Phase 5).
- Observability logging (Phase 6); in-region counters only here.
- Cross-container shared memory / Compose topology changes (Phase 7).
- Layout changes of any kind (`kRegionVersion` stays 3, per DEC-0009 #1).

## Handoff Contract From Phase 1

- Region layout v3 is frozen: attach via
  `SharedRegionHandle::create_or_open`, commit outputs via region-level
  `push(region, worker_idx, value)`, verify a quiescent worker with
  `verify_worker_ring(region, idx)`; whole-region `verify()` only at
  quiescence (see `docs/ARCHITECTURE.md`).
- `WorkerStatusFlag` bits (`RUNNING`, `IDLE`, `CRASHED`, `RECOVERING`) are
  combined in the per-worker status word; Phase 2 adds `OVERRUN` as a
  semantics bit (DEC-0009 #4).
- Hosted CI green on the Phase 2 exit commit is part of the exit gate
  (pattern established by Phase 1).

## Deviations From the Plan Sketch (recorded decisions)

1. **`std::ranges` pipeline guarded by toolchain (T2.1).** Verification
   finding: clang 14.0.6 against libstdc++ 12.2 cannot instantiate `<ranges>`
   views at all — even `views::iota` fails inside `view_interface` ("no
   member named 'begin'", witnessed in the pinned clang-verify image;
   upstream libstdc++ workarounds for clang landed with GCC 13). The
   `process_sensor_data` ranges composition therefore builds only where it
   compiles (`#if !defined(__clang__) || _GLIBCXX_RELEASE > 12` semantics),
   with a byte-identical manual fallback elsewhere. The plan-test-#4
   equivalence witness runs with the live ranges path on gcc-primary (local)
   and CI GCC 13; the clang job compiles and tests the fallback path.
   Consequence recorded: a future clang-verify toolchain update (>= 16 or
   libstdc++ >= 13) retires the guard in an intentional toolchain change.

(Further deviations recorded during implementation.)

## Task Plan

### Task T2.1 - Worker Core _(3-4 h)_

**Record:** [T-0009](../tasks/T-0009-worker-core.md) ·
[DEC-0009](../decisions/0009-phase2-worker-runtime.md) · Completed
2026-09-19 (G2.1: [evidence](../../NOTES.md); hosted run 35472130840).

**Dependencies:** Phase 1 exit (G1.1–G1.5).

**Implementation steps**

1. Create `workers/` as static library `safety_crit::workers` (C++20,
   warnings, sanitizers; standard test wiring), add to root `CMakeLists.txt`,
   update `docs/ARCHITECTURE.md` + `ARCHITECTURE_RULES.md` ownership rows.
2. Add `OVERRUN` bit to `WorkerStatusFlag` (shared-memory flags header),
   with combine/inspect coverage in the existing flags tests.
3. `workers/include/safety_crit/workers/worker_config.hpp`: worker id
   (role + index), tick interval, CPU budget, PRNG seed derivation.
4. `workers/include/safety_crit/workers/workload.hpp`: splitmix64 sensor
   simulation (per (worker, tick) determinism) + `process_sensor_data`
   C++20 ranges pipeline (filter/transform/take) with fixed-point
   calibration; manual-loop equivalence witness test (plan test #4).

**Deliverables**

- `safety_crit::workers` with config + workload + pipeline; `workers_core_test`.

**Verification gate G2.1**

```bash
for cfg in "GoogleTest" "Catch2"; do
  cmake -S . -B build/t21-$cfg -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=$cfg \
    && cmake --build build/t21-$cfg --parallel \
    && ctest --test-dir build/t21-$cfg --output-on-failure
done
# plus ASan+UBSan configuration
```

Pass when core tests (determinism, pipeline equivalence, OVERRUN flag
combine) pass in both frameworks and under ASan+UBSan, zero warnings.

---

### Task T2.2 - Work Loop, Signals, Deadline Monitoring _(3-4 h)_

**Record:** [T-0010](../tasks/T-0010-work-loop-signals-deadlines.md) ·
[DEC-0009](../decisions/0009-phase2-worker-runtime.md) · Completed
2026-09-19 (G2.2: [evidence](../../NOTES.md); hosted run 35473680814).

**Dependencies:** G2.1.

**Implementation steps**

1. `workers/include/safety_crit/workers/work_loop.hpp`: bounded-iteration
   loop over (input tick → pipeline → `push()` → status update), template
   clock injection; per-tick budget check sets `OVERRUN` + counts overruns;
   stop via `std::stop_token` and a `sig_atomic_t`-backed stop request
   consumed between ticks; idle backoff via guarded `__builtin_ia32_pause`,
   else `yield()`.
2. Signal wiring (install/handler split so the handler is just
   flag-store): SIGTERM/SIGINT → stop request; SIGUSR1 → forced-crash hook
   (test-only, no cleanup).
3. Loop tests: exact tick count, clean stop mid-run (plan test #3 analogue:
   cancellation completeness), budget overrun witnessed with a fake clock
   (plan test #2), no partial outputs on stop.

**Deliverables**

- `work_loop.hpp` + signal module; `workers_loop_test`.

**Verification gate G2.2**

Full matrix (both frameworks × plain, ASan+UBSan, TSan) on the loop tests:
pass when cancellation is clean under sanitizers (no leaks, no data-race
reports), overrun witness is deterministic under the fake clock, and tick
accounting is exact.

---

### Task T2.3 - Worker CLI, Region Lifecycle, Integration, Phase Exit _(4-5 h)_

**Record:** [T-0011](../tasks/T-0011-worker-cli-integration-phase-exit.md) ·
Completed 2026-09-19 ([evidence](../../NOTES.md), section "Gate: G2.3 /
Phase 2 exit"). Hosted exit run 35485369860 on commit d00526b: 10/10 jobs
green — **Phase 2 exit gate satisfied**.

**Dependencies:** G2.2.

**Implementation steps**

1. Extend `app/` CLI: `safety-critical-ha worker --id <a|b|c>
   [--role hot|standby] [--ticks N] [--tick-interval-ms MS] [--budget-us US]`;
   hand-rolled parsing, validation errors to stderr with nonzero exit;
   `--version` untouched.
2. Worker entry: `SharedRegionHandle::create_or_open` (create-or-attach),
   scoped status flag maintenance (RUNNING while looping, IDLE when drained
   and finished), standby role poll loop (status-only, no pushes).
3. Fork-based integration tests (plain build only; G1.4 precedent):
   (a) child worker pushes N ticks, parent drains its ring asserting
   sequence continuity and pipeline-correct payloads; (b) SIGTERM → child
   exits clean (status IDLE quiescent ring passes `verify_worker_ring`);
   (c) SIGUSR1 → child dies, ring still verifies at quiescence; (d) standby
   stays passive (no pushes, status shows READY-equivalent).
4. Record Phase 2 matrix + exit evidence in `NOTES.md`; update task/registry/
   STATUS docs.

**Deliverables**

- `worker` subcommand; `workers_integration_test` (fork-based); README
  command docs; Phase 2 exit evidence.

**Verification gate G2.3**

```bash
cmake -S . -B build/t23 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest \
  && cmake --build build/t23 --parallel \
  && ctest --test-dir build/t23 --output-on-failure
ctest --test-dir build/t23 -R workers_integration --output-on-failure
```

Pass when integration tests are green in the plain build (both frameworks)
and the full unit matrix stays green under sanitizers.

## Phase Exit Gate

Phase 2 is complete only when G2.1–G2.3 pass and the following evidence is
attached in `NOTES.md`:

| Required evidence | Source gate |
|---|---|
| Worker core determinism + pipeline equivalence (both frameworks) | G2.1 |
| Loop cancellation / deadline tests clean under sanitizers | G2.2 |
| Integration: sequence continuity, SIGTERM clean stop, SIGUSR1 crash ring-verify, standby passivity | G2.3 |
| Full local matrix (both frameworks × plain/ASan+UBSan/TSan) green | exit |
| Hosted CI green on the Phase 2 exit commit | exit |

No Phase 3 monitor implementation may be accepted until this exit gate is
satisfied.

## Handoff to Phase 3

Phase 3 adds the monitor daemon: poll `worker_status` cells (including
`OVERRUN`), detect stalled rings via unchanged head/tail counters, and
collect metrics. The worker status lifecycle established in T2.3
(RUNNING/IDLE/CRASHED-detection surface/OVERRUN) is that phase's input
contract; the worker CLI flags are the perturbation surface Phase 5 reuses.

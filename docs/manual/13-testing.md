# Chapter 13 — Testing and Verification

## Motivation

Chapter 02 listed seven safety properties. This chapter is about the machine
that makes them more than opinions: every property maps to tests, every test
builds and runs under **two test frameworks**, and the lock-free core runs
again under **three sanitizers**. The verification stack is itself engineered
— the dual-framework rule exists to catch test-infrastructure bugs (a broken
assertion macro fails *both* legs, a flaky harness surfaces in one), and CI
treats scenario flakes as rerun-and-report, never as silent green.

## The dual-framework switch

Production libraries are framework-free; only `*-tests` targets link a
framework. One CMake variable selects it:

```console
$ cmake -S . -B build/gtest   -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
$ cmake -S . -B build/catch2  -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
$ cmake --build build/gtest && ctest --test-dir build/gtest --output-on-failure
100% tests passed, 0 tests failed out of 217
Total Test time (real) =  35.93 sec
```

CI runs both (`native-gtest`, `native-catch2` jobs,
`.github/workflows/ci.yml:13,28`), plus a clang-14 leg in a pinned container
(`clang-verify`, `ci.yml:109`) — two compilers x two frameworks, so a test
that passes only under one toolchain is a CI failure, not a mystery.

## What gets tested where

| Layer | Style | Examples |
|-------|-------|----------|
| Shared memory core | Property + concurrency tests, both frameworks | multi-producer claim races; CRC corrupt skip-and-count; `transfer_ownership` rollback; `verify_*` scoping |
| Work loop / workload | Fake clock + fake publisher (template injection) | deadline window arithmetic; deterministic byte-identical output (T2.1 idempotency) |
| Health machine | Fake clock | threshold edges; one-alert-per-transition; crash vs stall vs idle disambiguation |
| Supervisor | In-process + subprocess | alert-line validation (T-0032); failover witness; `StallRecoveryTracker` ladder incl. second-alert no-op |
| Event log | Filesystem round-trips | seq recovery after restart; per-key continuity scan; level/fsync policy |
| Daemon | HTTP over loopback | `/health` `/metrics` `/status` bodies; region-loss degraded posture |

The clock-injection pattern (template `ClockT` parameter — production
`steady_clock`, tests a fake) is the single biggest testability win in the
codebase: anything with a threshold (stall detection, stall grace, handoff
grace) becomes deterministic and instant under test
(`monitors/include/safety_crit/monitors/health.hpp:23`, DEC-0009 #2).

## Sanitizers: the lock-free tax audit

The `sanitizers` CI job runs a matrix of sanitizer x framework
(`.github/workflows/ci.yml:43`):

- **ASan+UBSan** — memory errors and undefined behavior (property 4).
- **TSan** — data races on the atomics-adjacent code (property 3's teeth).

Running the ring-buffer suites under TSan is not ceremony: relaxed-CAS
protocols like chapter 06's are exactly where TSan either confirms the
documented memory-ordering argument or finds the hole the author missed. The
ordering comments in `ring_buffer.hpp:93` exist because they are *checked*,
not because they are pretty.

## The system-level gates

Unit tests prove components; the container jobs prove the *system*:

- **`docker-build`** — image builds and the runtime binary reports its version
  (`ci.yml:94`).
- **`docker-compose-smoke`** — stack startup, `failover-smoke.sh`, cleanup
  (`ci.yml:137`); the same PASS line this manual quotes in chapter 04.
- **Perturbation scenarios, 5x each** — S1, S2, S3, S5, S6 run five times
  per push (`ci.yml:158`). Five repeats is a deliberate flake filter for
  timing-sensitive fault tests: the 41–105 ms recovery spread chapter 11
  mentions is exactly what a 1x run misreports.
- **Deterministic replay (S1R)** — recorded crash replayed against a fresh
  stack with target remap, event/ownership parity with bounded record-count
  tolerance (`ci.yml:173`).

`agent-guidance-drift` (`ci.yml:85`) even verifies the generated agent
guidance files match their source — the same "generated, not hand-maintained"
discipline this manual follows.

## The phase exit gate

`docs/STATUS.md` records what was green at each phase boundary — e.g. Phase
5's exit: 134/134 tests, sanitizers clean, all five scenarios green 5x, S1
replay determinism passing. The number has grown (217 today); the rule
hasn't: **a phase closes when its gates pass, and the evidence lands under
`docs/evidence/`.**

## Try it

Run the whole verification story locally, roughly as CI does:

```console
$ cmake -S . -B build/gtest  -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest && cmake --build build/gtest
$ ctest --test-dir build/gtest --output-on-failure
$ cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2 && cmake --build build/catch2
$ ctest --test-dir build/catch2 --output-on-failure
$ containers/compose/scenarios/s1_crash.sh   # after docker compose down -v
```

For the sanitizer legs, see [`docs/DEVELOPMENT.md`](../DEVELOPMENT.md) for the
preset flags CI uses.

## What to look for

- **A test's value is proportional to what it would catch.** The dual
  framework catches test bugs; the 5x scenarios catch timing flakes; replay
  catches "fixed it" that didn't.
- **Evidence is an artifact.** `docs/evidence/` holds run logs; STATUS.md
  cites them. Claims age; artifacts don't.
- **Determinism is load-bearing infrastructure** — three chapters argue it;
  this one cashes it out.

## Further reading

- `.github/workflows/ci.yml` — the whole gate graph, ~180 lines.
- `docs/phases/PHASE_6_OBSERVABILITY.md` — current phase tasks T-0033+.
- Next: [Chapter 14 — Running as a System](14-running-production.md).

# Chapter 04 — Getting Started

## Motivation

This chapter gets you from clone to (a) a green test suite, (b) a worker
running by hand, and (c) the full Compose stack failing over in front of you.
Every command below was run on a Linux x86_64 host; output is real.

Toolchain baseline: C++20, CMake >= 3.24 + Ninja, primary compiler g++ (12/13),
supplementary clang-14 via a pinned verify image. Tests run under **two**
interchangeable frameworks (GoogleTest and Catch2) — that is a project goal,
not an accident (chapter [13](13-testing.md)).

## 1. Configure and build

```console
$ cmake -S . -B build/gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
$ cmake --build build/gtest
```

The switch selects the test framework for the `*-tests` targets; the
production libraries are framework-free. The other leg:

```console
$ cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
$ cmake --build build/catch2
```

## 2. Run the test suite

```console
$ ctest --test-dir build/gtest --output-on-failure
...
100% tests passed, 0 tests failed out of 217

Total Test time (real) =  35.93 sec
```

217 tests in the current tree (the Catch2 build carries the same coverage in
its own discovery scheme). If you prefer the raw binary over ctest:

```console
$ ./build/gtest/tests/shared-memory-tests --gtest_brief=1
```

## 3. Your first worker

```console
$ ./build/gtest/app/safety-critical-ha --version
safety-critical-ha version 0.1.0
Compiler: GNU 13.3.0

$ mkdir -p /tmp/pids
$ ./build/gtest/app/safety-critical-ha worker --id a --role hot \
    --ticks 5 --tick-interval-ms 20 --region /manual_demo --pid-dir /tmp/pids
worker: scheduling fallback (errno 1)
worker 0 (hot): 5 ticks, 0 overruns
```

What just happened: the worker created the named region (attach-or-create,
chapter [05](05-shared-memory.md)), wrote its pidfile, ran 5 deterministic
ticks (chapter [08](08-workers.md)) publishing into ring 0, and exited
cleanly. `scheduling fallback (errno 1)` is expected outside a privileged
container — SCHED_FIFO needs `CAP_SYS_NICE` and the fallback is designed
behavior (`runtime/include/safety_crit/runtime/scheduling.hpp:33`).

## 4. Watch it fail — natively

Run the monitor against the same region in one terminal while a worker is
running (or SIGSTOP'd) in another:

```console
$ ./build/gtest/app/safety-critical-ha monitor --interval-ms 50 \
    --stall-threshold-ms 200 --region /manual_demo --pid-dir /tmp/pids --polls 30
monitor: scheduling fallback (errno 1)
{"ts":...,"level":"info","component":"monitor","event":"worker_running","worker":0}
{"ts":...,"level":"warn","component":"monitor","event":"worker_stalled","worker":0}
{"ts":...,"level":"info","component":"monitor","event":"worker_recovered","worker":0}
{"ts":...,"level":"info","component":"monitor","event":"monitor_report","polls":30,"alerts":{"worker_crashed":0,"worker_stalled":1,"worker_recovered":1,"worker_overrun":0,"worker_idle":0,"worker_running":1},"workers":["running","unknown","unknown"]}
```

That transcript is the health state machine live: running -> stalled (worker
was SIGSTOP'd, ring tail frozen past the threshold) -> recovered (SIGCONT'd).
`workers:["running","unknown","unknown"]` — workers 1/2 never appeared, and
`unknown` is a first-class state, not a blank (chapter [09](09-monitor.md)).

A bounded supervisor run with failover, natively:

```console
$ ./build/gtest/app/safety-critical-ha supervisor --runtime-ms 4000 --ticks 200
...
supervisor: first post-failover record observed in 85 ms
supervisor: shutdown state=4 a_records=343 a_corruptions=0 a_first_post_failover=1 b_records=200 b_corruptions=0 b_first_post_failover=0 failover_timing_emitted=1
```

## 5. The container path

The pinned image bundles the runtime, demo entrypoint, compose stack, and
scenario scripts. One command does build, up, smoke, and failover:

```console
$ OBSERVABILITY_PORT=18080 ./run_demo.sh
```

(Use `OBSERVABILITY_PORT=18080` only if your host port 8080 is busy; inside
the containers everything talks to 8080 as usual — do **not** override
`DAEMON_URL`.) Under the hood:

```console
$ docker compose build
$ docker compose up -d
$ docker compose ps
$ docker compose exec supervisor ./app/safety-critical-ha --version
$ containers/compose/failover-smoke.sh
```

The smoke run is the acceptance line for the whole stack:

```text
==> SIGKILL hot physical A (pid=8)
==> waiting up to 15s for supervisor to stabilize
==> state=healthy
==> verifying new physical-A pidfile points to a live pid
==> verifying supervisor stdout shows standby C (physical worker 2) promoted to RUNNING
==> verifying shared region /safety_crit_region survived the failover
==> verifying daemon /health exposes data_loss_events_total
==> verifying daemon /metrics exposes worker_status and data_loss_events_total
==> failover smoke PASSED
```

And the daemon's view mid-failover (`http://localhost:18080/health`):

```json
{"status":"ok","uptime_ms":42942,
 "workers":[{"id":0,"state":0,"last_sequence":4723},{"id":1,"state":1,"last_sequence":4730},{"id":2,"state":1,"last_sequence":0}],
 "ownership":[{"ring":0,"physical_owner":2,"epoch":4},{"ring":1,"physical_owner":1,"epoch":2},{"ring":2,"physical_owner":2,"epoch":2}],
 "data_loss_events_total":0}
```

Read it: ring 0 is now physically owned by worker 2 (the promoted standby,
epoch bumped to 4), everything is `ok`, and the data-loss counter is zero.
Chapters [12](12-observability.md) and [14](14-running-production.md) unpack
every field.

## 6. Handy one-offs

Read-only ownership probe (chapter [10](10-supervisor-failover.md)):

```console
$ ./build/gtest/app/safety-critical-ha ownership --region /manual_demo
ownership 0 physical=0 epoch=2
ownership 1 physical=1 epoch=2
ownership 2 physical=2 epoch=2
```

One-shot observability snapshot (no socket, exits; chapter 12):

```console
$ ./build/gtest/app/safety-critical-ha observability --once --region /manual_demo
```

Full flag list: [Appendix A](A-cli-reference.md).

## What to look for

- **Exit codes are part of the API.** 0 = success, 2 = CLI usage error,
  1 = runtime failure. Scripts rely on this.
- **Nothing is silent.** If something degraded (scheduling, a corrupt slot),
  a line was printed. If you see a clean run with no warnings, it really was
  clean.

## Further reading

- [`docs/DEVELOPMENT.md`](../DEVELOPMENT.md) — build environment details, sanitizers.
- Next: [Chapter 05 — The Shared Memory Region](05-shared-memory.md).

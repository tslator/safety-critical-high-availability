# Chapter 08 — Workers

## Motivation

A worker is the only component doing "real work" — and the component the
recovery machinery exists to replace. For replacement to be safe, the work
itself must be boring in the best sense: deterministic, bounded per tick,
self-measuring against a deadline, and obedient to stop signals with no
cleanup handshake. This chapter walks the three pieces: the workload, the
work loop, and the process entry.

## The workload: deterministic by construction

`workers/include/safety_crit/workers/workload.hpp` implements the Phase 2
fake-sensor pipeline (T2.1, DEC-0009 #1). The headline design decision:
**the workload is a pure function of `(seed_base, worker_idx, tick)`**, via
splitmix64:

```cpp
// workload.hpp:43 (paraphrased from the header)
const uint64_t seed = seed_base ^ (worker_idx * kPhi) ^ tick;
```

Consequences:

- Same inputs => byte-identical output, always. Fault injection replay can
  therefore compare record *counts and identities* across runs (chapter 11) —
  nondeterministic payload would force equality checks to be timing
  comparisons, which is the trap DEC-0009 exists to avoid.
- Hot-path legal: fixed `std::array` storage, no allocation, no syscalls, no
  exceptions (`workload.hpp:113`).
- The math is toy-grade (synthesize sine+noise, detect threshold crossings,
  quantize); the *structure* — input, filter, transform, output, each with a
  fixed-size record type — is what generalizes.

`WorkerConfig` (`workers/include/safety_crit/workers/worker_config.hpp`)
binds a process to a job:

```cpp
enum class WorkerRole : std::uint32_t { kHot = 0, kStandby = 1 };
```

with fields for `worker_idx`, `logical_ring` (defaults to unassigned for the
standby), `process_generation`, `ticks`, `tick_interval` (default 10 ms),
`cpu_budget` (default 1000 us), `seed_base`, and the T-0027 opt-in
`corrupt_hook` flag.

## The work loop: five rules per tick

`run_work_loop` (`workers/include/safety_crit/workers/work_loop.hpp:44`) is
region-agnostic — it takes callbacks (`push_fn`, `tick_fn`, `pacer_fn`), so
tests inject a fake clock and a fake publisher. Per tick, in order:

1. **Check stop first** — after the first tick, stop is observed at precise
   tick boundaries between iterations (`work_loop.hpp:52`).
2. **Deadline window** — CPU time measured with `CLOCK_THREAD_CPUTIME_ID`
   (excludes sleep, so the budget is honest); a tick exceeding
   `cpu_budget` sets the overrun flag and counts a CPU overrun
   (`work_loop.hpp:72`).
3. **Do the tick** (`work_fn`).
4. **Backpressure** — `push_fn` returns false when the ring is full; the
   loop retries with `yield()`, aborting the retry if stop is requested
   (`work_loop.hpp:66`). A full ring slows the worker; it never breaks it.
5. **Pace** — sleep to the interval boundary in slices, checking stop each
   slice (`work_loop.hpp:83`).

Overruns are *flagged, not fatal* (DEC-0009 #4): `kOverrun` appears in the
status word, the monitor turns it into a `worker_overrun` alert, and the
worker's exit line reports it:

```text
worker 0 (hot): 5 ticks, 0 overruns
```

The `WorkLoopStats` struct (`work_loop.hpp:20`) also tracks deadline misses on
the interval pacer separately from CPU overruns — two different failure
shapes (slow work vs. scheduling starvation) get two counters.

## The process entry: `run_worker`

`workers/include/safety_crit/workers/worker_entry.hpp` is what the `worker`
subcommand executes:

1. Apply SCHED_FIFO (record the fallback if denied — normal outside a
   privileged container) (`worker_entry.hpp:41`).
2. Attach-or-open the region; `SharedRegionHandle` RAII guarantees munmap on
   exit (`worker_entry.hpp:51`).
3. Write the pidfile (`safety_crit_worker_<idx>.pid`)
   (`worker_entry.hpp:59`) — removed on *clean* exit only, so a crash leaves
   a stale file the monitor detects (chapter 09).
4. **Hot path**: mark `kRunning`, run the work loop, mark `kIdle` on exit.
   **Standby path**: mark `kIdle` and poll a sibling's status word until it
   changes (promotion), never touching a ring (`worker_entry.hpp:8`). A dead
   hot worker always leaves `kRunning` set, so any change — including the
   post-handoff RUNNING bit — is a valid trigger.
5. **Signals**: `SIGTERM`/`SIGINT` set the stop flag (observed at the next
   tick boundary); `SIGUSR1` is a crash hook (`worker_entry.hpp:67`).
   `SIGUSR2` arms the one-shot corruption injection only when
   `--corrupt-hook` was passed — the S3 poison path, opt-in so production
   builds never carry it armed.

Signal-safety is honored: handlers only flip an atomic flag; all logic lives
in the loop.

## Try it

```console
$ ./build/gtest/app/safety-critical-ha worker --id b --role hot \
    --ticks 200 --tick-interval-ms 5 --budget-us 200 \
    --region /manual_ch08 --pid-dir /tmp/pids
worker: scheduling fallback (errno 1)
worker 1 (hot): 200 ticks, 0 overruns
```

Now break the budget on purpose (`--budget-us 1` forces every tick over):

```console
$ ./build/gtest/app/safety-critical-ha worker --id c --role hot \
    --ticks 10 --budget-us 1 --region /manual_ch08b --pid-dir /tmp/pids
worker: scheduling fallback (errno 1)
worker 2 (hot): 10 ticks, 10 overruns
```

Ten counted overruns, exit code 0 — the worker told you, via the status word
and the witness line, and did not die. That is DEC-0009 #4 in two commands.

And determinism, in one: run worker `a` twice with the same
`--seed`/ticks and diff the ring contents (or use the replay tooling,
chapter 11) — byte-identical.

## What to look for

- **Determinism is a testability feature**, not a purity obsession.
- **Every wait is interruptible.** Ticks, paces, and backpressure retries all
  check stop; there is no "wait until someone talks to me."
- **Counters over drama.** Overruns, misses, and fallbacks are recorded and
  reported; the process stays up unless *you* killed it.

## Further reading

- `docs/phases/PHASE_2_WORKERS.md` — deviations incl. DEC-0009 rationale.
- Next: [Chapter 09 — The Monitor](09-monitor.md).

# Chapter 09 — The Monitor

## Motivation

The supervisor can only fix what somebody notices. The monitor is that
somebody: a separate process that watches every worker strictly from the
outside — status word, ring tail, process liveness — and translates raw
observations into a small, stable **alert vocabulary**. Two design rules do
most of the safety work: the monitor never writes the region (an observer
that can corrupt what it observes is not an observer), and every transition
it reports is an *edge*, not a level, so a ten-second stall produces one
alert, not two hundred.

## Inputs: `WorkerObservation`

To keep the health algorithm testable, one poll of one worker is reduced to
a plain struct (`monitors/include/safety_crit/monitors/health.hpp:16`):

- the status word (the `WorkerStatusFlag` bits, chapter 05),
- the ring tail counter (is the worker producing?),
- process liveness (does the pidfile's pid still exist? via `kill(pid, 0)`
  with the semantics in `monitors/include/safety_crit/monitors/pidfile_liveness.hpp`).

The monitor binds observations to the real region and pidfiles; the algorithm
itself is region-agnostic. Clock injection is a template parameter —
production uses `steady_clock`, tests use a fake clock so thresholds and
alert edges are deterministic (the DEC-0009 #2 pattern, `health.hpp:23`).
This code is *not* on the ring hot path: allocation and STL are permitted
here, once per poll, never per ring operation (`health.hpp:26`).

## The alert vocabulary

Six names, contract-graded — renames or removals require a decision record;
additions are additive (`health.hpp:29`):

| Alert | Trigger |
|-------|---------|
| `worker_crashed` | RUNNING (or CRASHED) observed with a **dead process** |
| `worker_stalled` | RUNNING, ring tail unchanged beyond threshold |
| `worker_recovered` | a stalled worker's ring tail advanced again |
| `worker_overrun` | OVERRUN status bit observed (once per assertion) |
| `worker_idle` | worker quiesced (IDLE observed; includes clean exit) |
| `worker_running` | first RUNNING observation of an episode |

Read the definitions as logic, not prose — the crash rule's key word is
"dead process." A process stopped with SIGSTOP still *exists*, so it is
stalled, not crashed; a process that exited cleanly left IDLE, so it is
idle, not crashed. Three failure shapes, three different alerts, three
different supervisor responses (chapter 10).

## The state machine

Between polls, each worker is classified `HealthState`:
`kUnknown, kRunning, kStalled, kCrashed, kIdle` (`health.hpp:63`).
`kUnknown` means "never observed RUNNING (word zero) — worker not started" —
a first-class state rather than a blank, which is why the compose smoke can
legitimately print `worker_idle` for the standby before anyone has been
promoted. A typical episode:

```text
unknown ──RUNNING──► running ──tail frozen > threshold──► stalled ──tail advances──► running
   (word zero)          │                                     │
                        ├──── dead process ──► crashed        └──► recovered (edge, then running)
                        └──── IDLE ─────────► idle
```

Every arrow *edge* emits at most one alert. The stall rule deserves emphasis:
a worker is stalled when its status word says RUNNING **and** its ring tail
has not advanced for `stall_threshold_ms` — two independent signals
agreeing, so a slow-but-alive tick cadence does not false-alarm, and a live
process spinning uselessly with a frozen ring does.

## The poll loop and its report

`run_monitor_loop` (`monitors/include/safety_crit/monitors/monitor_loop.hpp:51`)
is template-driven like the health algorithm (`ClockT`, `EmitFn`,
`AlertSinkT`), emits alerts as JSON objects, and ends every run with a
`monitor_report` summary — ticks, alert counts by kind, and per-worker
classification. From a real run against a SIGSTOP'd worker:

```json
{"ts":...,"level":"info","component":"monitor","event":"worker_running","worker":0}
{"ts":...,"level":"warn","component":"monitor","event":"worker_stalled","worker":0}
{"ts":...,"level":"info","component":"monitor","event":"worker_recovered","worker":0}
{"ts":...,"level":"info","component":"monitor","event":"monitor_report","polls":30,"alerts":{"worker_crashed":0,"worker_stalled":1,"worker_recovered":1,"worker_overrun":0,"worker_idle":0,"worker_running":1},"workers":["running","unknown","unknown"]}
```

Levels carry severity (`warn` for stall), the alert counters are the
vocabulary indexed densely (`health.hpp:58`), and workers 1/2 sit in
`unknown` because they were never started in that experiment.

Where do alerts go? In production they are JSON lines on the monitor's
stdout, which the supervisor *validates* (T-0032: a schema-valid
`worker_crashed` alert alone can drive failover — the supervisor parses and
trusts only well-formed lines,
`supervisor/include/safety_crit/supervisor/supervisor.hpp:21`) and mirrors
into the event log (chapter 12).

## Liveness subtleties the monitor defends against

- **Pidfiles are recycled.** A replacement worker writes the same filename,
  so liveness alone can't tell "which generation" you're looking at; the
  health rules never depend on that attribution
  (`monitors/include/safety_crit/monitors/health.hpp:92`).
- **Stale pidfiles are meaningful.** They survive crashes (clean exit is the
  only path that removes them), which is exactly why `kill(pid, 0)` returning
  ESRCH turns "worker exists with frozen ring" into "worker_crashed."
- **The handoff window.** Right after failover, status words change for
  reasons unrelated to failure; the monitor's handoff grace (T-0032) keeps
  it from reporting the cure as a new disease.

## Try it

Terminal 1 — a worker that you control:

```console
$ ./build/gtest/app/safety-critical-ha worker --id a --role hot \
    --ticks 10000 --tick-interval-ms 10 --region /manual_ch09 --pid-dir /tmp/pids &
```

Terminal 2 — the monitor:

```console
$ ./build/gtest/app/safety-critical-ha monitor --interval-ms 50 \
    --stall-threshold-ms 200 --region /manual_ch09 --pid-dir /tmp/pids --polls 200
```

Now play god:

```console
$ kill -STOP $(cat /tmp/pids/safety_crit_worker_0.pid)   # -> worker_stalled
$ kill -CONT $(cat /tmp/pids/safety_crit_worker_0.pid)   # -> worker_recovered
$ kill -9    $(cat /tmp/pids/safety_crit_worker_0.pid)   # -> worker_crashed
```

Three signals, three alerts, each fired once, the monitor never missing a
beat — because it never depended on the worker to tell it anything.

## What to look for

- **Outside-looking observation is a safety feature.** Shared nothing
  except read-only bytes, trusted nothing.
- **Edges, not levels.** One alert per transition keeps signal-to-noise at
  whatever scale you poll.
- **Two signals agree before a name is printed.** Status word + ring tail,
  or status word + liveness. Single-signal theories are what cause flaky
  paging.

## Further reading

- `docs/phases/PHASE_3_MONITOR_DAEMON.md` — T3.1/T3.2, DEC-0010.
- Next: [Chapter 10 — Supervision and Failover](10-supervisor-failover.md).

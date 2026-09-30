# Chapter 12 — Observability

## Motivation

A safety-critical system that works but can't explain itself is a black box
with good uptime. Observability here means three things: a **log that audits
itself** (gap detection is the log's own job), an **always-on HTTP surface**
that answers "how are you?" during outages, and a strict rule that **metrics
derive from witness records, never from log scraping**. This chapter covers
the event log (T-0033), the daemon (T-0035–T-0038), and the three endpoints.

## The event log: schema v1

`observability/include/safety_crit/observability/event_log.hpp` defines an
append-only JSON-lines log — one record per line, every record carrying its
own version marker:

```json
{"schema":1,"ts":1790697349379990444,"level":"info","component":"worker","seq":1,"event":"worker_started","instance":0,"role":"hot","ring":0}
{"schema":1,"ts":1790697349581667963,"level":"info","component":"worker","seq":2,"event":"worker_stopped","instance":0,"role":"hot","reason":"stop_signal","ticks":4,"overruns":0}
```

Field-by-contract (`event_log.hpp:5`):

- **`schema`** — record-level version marker, on *every* record (like the
  region's magic — self-describing beats out-of-band).
- **`ts`** — unix nanoseconds (CLOCK_REALTIME).
- **`level`** — `info|warn|error`; the writer **fsyncs synchronously on
  level >= warn** (`event_log.hpp:17`). Info records ride process-crash
  durability; a warning must be on disk before the system continues.
- **`component` + optional `instance`** — the *sequence key*. `worker` +
  `instance:0` has its own sequence; the unique components (monitor,
  supervisor, observability) omit `instance` (`event_log.hpp:21`).
- **`seq`** — monotonic per sequence key, starting at 1. A writer recovers
  its next seq by scanning the existing log tail, so **restarts never reset
  or duplicate the sequence** (`event_log.hpp:25`).

Why per-key sequences? Because components start and stop independently; a
global counter would flag worker B's restart as a gap in everything else.
The reader contract is a sequential scan with per-key continuity checks
(`event_log.hpp:40`), and the daemon runs exactly that scan continuously —
the log detects its own holes.

Durability mechanism: a single `O_CLOEXEC` append fd, one `write()` per
record, records capped at `kMaxEventRecordBytes` (4096) so POSIX append
atomicity guarantees no interleaving (`event_log.hpp:35`). The writer never
throws; components that stamp their own stdout JSON share the record
builder, minus the bookkeeping fields (`event_log.hpp:81`).

## The daemon

`observability/include/safety_crit/observability/daemon.hpp` assembles:
attach the region **read-only** (never initialize, never write, never
signal), tail the event log, serve HTTP until SIGTERM/SIGINT
(`daemon.hpp:8`).

Two postures that matter more than the endpoints:

1. **Attach-or-refuse at startup.** A region failing identity verification is
   a startup error (nonzero exit) — the daemon does not serve numbers it
   cannot trust (`daemon.hpp:14`).
2. **Region loss mid-run: degrade, never quit.** If the region disappears
   (S6 kills the container's IPC namespace; an operator removes the object),
   `/health` flips to `"degraded"` and the daemon **keeps serving** —
   "visibility outranks a clean exit" (`daemon.hpp:14`). Verification runs
   per poll against the live mapping; a stale view can never leak into a
   handler (`daemon.hpp:19`).

CLI (full list in Appendix A):

```text
observability --listen HOST:PORT --region NAME --event-log PATH [--poll-interval-ms MS] [--ticks N] [--once]
```

`--once` prints exactly one status snapshot and exits — the operator's
one-liner, no socket needed (`daemon.hpp:25`).

## The three endpoints

Captured from the live stack mid-failover:

**`/health`** — verdict plus the evidence:

```json
{"status":"ok","uptime_ms":42942,
 "workers":[{"id":0,"state":0,"last_sequence":4723},
            {"id":1,"state":1,"last_sequence":4730},
            {"id":2,"state":1,"last_sequence":0}],
 "ownership":[{"ring":0,"physical_owner":2,"epoch":4},
              {"ring":1,"physical_owner":1,"epoch":2},
              {"ring":2,"physical_owner":2,"epoch":2}],
 "data_loss_events_total":0}
```

**`/metrics`** — Prometheus format, each metric with the *why* in its HELP
line:

```text
# HELP data_loss_events_total Data-plane drain-witness sequence gaps (CORE property #1)
# TYPE data_loss_events_total counter
data_loss_events_total 0
# HELP event_log_gaps_total Control-plane event-log read gaps per sequence key
# TYPE event_log_gaps_total counter
event_log_gaps_total{component="monitor"} 0
```

**`/status`** — the full machine-readable snapshot (region identity, per-ring
positions and ownership with `owned` flags, worker status words — e.g.
`"0x0000000000000002"` for IDLE) — the body `--once` prints.

## The metric-derivation rule

`data_loss_events_total` is reported **exclusively from supervisor
`data_loss_observed` witness records** (`daemon.hpp:8`). Not parsed from log
lines, not inferred from worker counters. Consequences:

- The number can only move when the drain witness actually rejects a
  sequence gap — the same event the failover-smoke greps for.
- Log tampering, dropped log lines, or a wedged log writer cannot falsify
  (or silence) the CORE property-1 metric.
- `event_log_gaps_total` sits on the *control* plane: it counts holes in the
  log itself, per sequence key. Two planes, two gap counters, never merged.

## Try it

With the compose stack up (chapter 14; use `OBSERVABILITY_PORT=18080` if host
port 8080 is busy):

```console
$ curl -s localhost:18080/health | jq .
$ curl -s localhost:18080/metrics | grep data_loss
$ curl -s localhost:18080/status | jq '.rings'
```

Watch the daemon degrade — in another terminal:

```console
$ docker compose exec supervisor kill -9 1   # or run s6_supervisor_kill.sh
$ curl -s localhost:18080/health | jq .status   # "degraded", still serving
```

And the one-shot snapshot against any region:

```console
$ ./build/gtest/app/safety-critical-ha observability --once --region /safety_crit_region
```

## What to look for

- **Self-auditing log**: `seq` per key turns "did we lose log lines?" from a
  philosophical question into a counter.
- **fsync on warn only** — durability where it costs, speed where it doesn't,
  and the reasoning written in the header.
- **A degraded daemon is a working daemon.** Observability's job is to tell
  you the system is on fire; quitting when it catches fire is malpractice.

## Further reading

- DEC-0014 — event-log schema, daemon posture, metric-derivation rules.
- `docs/phases/PHASE_6_OBSERVABILITY.md` — T-0033 through T-0039.
- Next: [Chapter 13 — Testing and Verification](13-testing.md).

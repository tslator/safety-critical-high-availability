# Chapter 01 — Introduction

## Motivation

Modern control systems — sensors fusing data for a brake decision, a robot
cell aggregating vision channels — have a property in common: they cannot
afford to silently be wrong, and they cannot afford to stop. A crash that
reboots the box in 40 ms and loses nothing is acceptable. A crash that
*hides* — freezes mid-stream, or drops samples so quietly that nobody notices
for an hour — is not.

This repository is a teaching-and-demonstration implementation of exactly
that engineering discipline: an application that keeps running through
failures, proves (with tests, not prose) that it never loses or duplicates
data, and makes every fault visible.

## The system in one paragraph

A **supervisor** process starts and manages a small fleet: two **hot workers**
(A and B) producing sensor-processing records, one warm **standby** (C) ready
to replace either, and a **monitor** that watches all three from the outside.
They all share one POSIX shared-memory region containing three lock-free
ring buffers. A separate **observability daemon** exposes the system over
HTTP, and a deliberately separate, never-for-production **perturbation
harness** crashes, stalls, and corrupts things on demand so that recovery can
be exercised — and recorded for replay. See
[`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) for the canonical version of
this paragraph.

## The four ideas everything else builds on

1. **Shared memory with integrity, not trust.** Workers publish into ring
   buffers where *every slot* carries a sequence number and a CRC-32C
   (chapters [06](06-ring-buffer.md), [07](07-data-integrity.md)). A corrupt
   slot is detected and tolerated; a *sequence gap* is a reportable data-loss
   event. The system never trusts bytes it cannot verify.

2. **Recovery is a designed path, not an accident.** When a worker dies, the
   supervisor promotes the standby to the dead worker's *logical ring* and
   spawns a replacement — in the captured demo below, the first
   post-failover record appeared **85 ms** after the crash was noticed. The
   handoff is governed by ownership tokens with epoch fencing so a zombie
   writer can never corrupt the ring after handoff (chapter
   [10](10-supervisor-failover.md)).

3. **Nothing is invisible.** Every lifecycle transition, alert, perturbation,
   and recovery lands in a schema-versioned JSON-lines event log with
   per-component sequence numbers, so even the *log* detects its own gaps
   (chapter [12](12-observability.md)).

4. **Every claim has a gate.** The project's safety properties (chapter
   [02](02-safety-properties.md)) are each paired with an automated test
   suite or CI job. "We believe it's deadlock-free" is not a claim here;
   "the TSan leg of the sanitizers job passes" is.

## A 10-minute orientation

### 1. Meet the binary

One executable implements every role; the subcommand selects the role
(`app/src/main.cpp:403`). Build it (chapter
[04](04-getting-started.md) has the full walkthrough) and run:

```console
$ ./build/gtest/app/safety-critical-ha --version
safety-critical-ha version 0.1.0
Compiler: GNU 13.3.0
```

### 2. Run one worker by itself

```console
$ ./build/gtest/app/safety-critical-ha worker --id a --role hot \
    --ticks 5 --tick-interval-ms 20 --region /manual_demo --pid-dir /tmp/pids
worker: scheduling fallback (errno 1)
worker 0 (hot): 5 ticks, 0 overruns
```

That single line of output contains three lessons:

- The worker attached (or created) a shared-memory region, did 5 ticks of
  deterministic fake sensor processing, and exited cleanly — no mutexes, no
  heap allocation in the hot path.
- `scheduling fallback (errno 1)` is the *correct* behavior outside a
  privileged container: SCHED_FIFO requires `CAP_SYS_NICE`, failure is
  non-fatal by design, and the fallback is recorded rather than hidden
  (`runtime/include/safety_crit/runtime/scheduling.hpp:33`).
- The final line is a witness summary: ticks completed and CPU-budget
  overruns — the worker measures itself against a deadline every tick
  (chapter [08](08-workers.md)).

### 3. Watch a system recover

Start the supervisor with a bounded budget and inject a crash (the full
walkthrough is chapter [10](10-supervisor-failover.md)); here is real output
from such a run:

```text
{"ts":...,"component":"monitor","event":"worker_running","worker":0}
{"ts":...,"component":"monitor","event":"worker_running","worker":1}
{"ts":...,"component":"monitor","event":"worker_idle","worker":2}
supervisor: first post-failover record observed in 85 ms
{"ts":...,"component":"monitor","event":"worker_running","worker":2}
supervisor: shutdown state=4 a_records=343 a_corruptions=0 a_first_post_failover=1 b_records=200 b_corruptions=0 b_first_post_failover=0 failover_timing_emitted=1
```

Read that shutdown line carefully — it is the system's confession and its
proof at the same time: ring A produced 343 records with **zero
corruptions**, its first post-failover record was observed (the handoff
worked), and the run ended in a controlled state. Nobody eyeballed this; the
failover-smoke script asserts it (chapter
[14](14-running-production.md)).

### 4. Break it on purpose

With the Compose stack running (chapter 14), one command crashes a hot worker
and verifies bounded recovery:

```console
$ containers/compose/scenarios/s1_crash.sh
==> S1: crashing hot worker A (pid 8)
==> S1 PASS: crash alert, replacement live, 41 ms < 100 ms, replay /tmp/s1_replay.cuJmbW.jsonl
```

The scenario *also wrote a replay log*: the same crash, replayed against a
fresh stack with `s1_replay.sh`, reproduced the same event sequence and
record counts (chapter [11](11-fault-injection.md)).

## What this project is *not*

- **Not a general message bus.** The ring buffer, region layout, and
  ownership protocol are tailored to one supervisor-managed fleet on one
  machine.
- **Not production-deployed as-is.** It is a reference implementation whose
  job is to make the safety argument concrete and executable. The fault
  harness (`perturb`) is explicitly marked *NEVER FOR PRODUCTION USE*
  (`perturb/include/safety_crit/perturb/harness.hpp:1`).
- **Not performance-obsessed.** Deadlines are measured and bounded, but this
  is a correctness-and-recovery demonstration, not a benchmark.

## Further reading

- [`docs/ai-guidance/GOALS.md`](../ai-guidance/GOALS.md) — the five project goals.
- [`SAFETY_CRITICAL_HA_PLAN.md`](../../SAFETY_CRITICAL_HA_PLAN.md) — the original 750-line design plan.
- Next: [Chapter 02 — The Seven Safety Properties](02-safety-properties.md).

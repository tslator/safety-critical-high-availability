# Chapter 02 — The Seven Safety Properties

## Motivation

Safety-critical engineering lives or dies on one question: *how do you know?*
Prose assurances ("we use lock-free code, so no deadlocks") are not evidence.
In this project, every safety claim is a **property** paired with a **gate** —
an automated check that fails CI when the property breaks. The canonical list
lives in [`docs/ai-guidance/CORE.md`](../ai-guidance/CORE.md) (distilled from
[`TENETS.md`](../ai-guidance/TENETS.md) and
[`SAFETY_CRITICAL_HA_PLAN.md` §7](../../SAFETY_CRITICAL_HA_PLAN.md)); this
chapter explains what each property *means* and points at the mechanism that
upholds it.

## The properties

### 1. No data loss

**Meaning.** A record that a worker committed must eventually be consumable —
across stalls, crashes, failovers, and corrupt slots. Loss is only allowed if
it is *detected and reported*: a silently dropped record is the failure mode;
a loudly counted one is a handled one.

**Mechanism.** Three layers stack up:

- Every ring slot carries the payload's CRC-32C and an immutable sequence
  class (chapter [07](07-data-integrity.md)); a corrupt slot is skipped and
  *counted*, never silently passed on (`shared-memory/include/safety_crit/shared_memory/ring_buffer.hpp`).
- The consumer's position is the authoritative sequence witness: the
  supervisor's `drain_output_witness()` rejects real sequence gaps while
  tolerating counted corruption skips
  (`supervisor/include/safety_crit/supervisor/supervisor.hpp:47`).
- The observability daemon reports `data_loss_events_total`, sourced
  *exclusively* from supervisor `data_loss_observed` witness records — the
  log's content can never move the counter any other way.

**Gate.** Shared-memory tests in the `native-gtest` and `native-catch2` CI
jobs (chapter [13](13-testing.md)).

### 2. No duplicate processing

**Meaning.** Each committed slot is consumed exactly once. Retries, retries
after partial failure, and concurrent consumers must never double-deliver.

**Mechanism.** Monotonic sequence numbers plus compare-and-swap claims: a
consumer wins a position by CAS-ing the shared `head_` counter; the winner is
unique by construction, and the sequence class in the slot tells the consumer
whether the position is committed yet
(`shared-memory/include/safety_crit/shared_memory/ring_buffer.hpp:47`).

**Gate.** Same test jobs as property 1.

### 3. No deadlock

**Meaning.** Nothing can block forever. The hot path contains no locks at
all; everything outside it bounds its waits.

**Mechanism.** Lock-free hot path (CAS only — no mutexes anywhere in the ring
or region hot path, `shared-memory/include/safety_crit/shared_memory/atomic_flags.hpp:40`).
Outside the hot path, waiting is always bounded: workers poll signals between
ticks (`workers/include/safety_crit/workers/work_loop.hpp:58`), the monitor's
pacer sleeps in 1 ms slices checking the stop flag
(`monitors/include/safety_crit/monitors/monitor_loop.hpp:95`), and the
supervisor's stall recovery escalates on a fixed grace timer rather than
waiting indefinitely
(`supervisor/include/safety_crit/supervisor/supervisor.hpp:104`).

**Gate.** `sanitizers` CI job, TSan matrix leg.

### 4. No undefined behavior in hot-path / lock-free code

**Meaning.** The atomics, casts, and pointer arithmetic that make lock-free
code work are exactly the places where C++ quietly permits catastrophe. Here
they are instrumented, not trusted.

**Mechanism.** Hot-path discipline: fixed-size storage, no allocation, no
exceptions, no syscalls; memory orders written out explicitly at each use;
narrowing casts done in `std::int64_t` first with the reasoning commented
(`workers/include/safety_crit/workers/workload.hpp:113`).

**Gate.** `sanitizers` CI job, ASan+UBSan matrix leg.

### 5. Bounded recovery time

**Meaning.** Time from fault to full recovery must stay under an SLA
(service-level agreement — a stated, agreed-upon bound the system commits to,
here the 100 ms recovery budget), and the bound must be *measured*, not
asserted.

**Mechanism.** The S1 scenario crashes a hot worker and measures the
interval; a typical captured run:

```text
==> S1 PASS: crash alert, replacement live, 41 ms < 100 ms, replay /tmp/s1_replay.cuJmbW.jsonl
```

The supervisor emits `failover_duration_seconds` (observed: `0.074`) and the
scenario asserts the budget (<100 ms on this host class — CI has seen
36–96 ms). A *near-miss* is a run that lands right at the edge of the budget
(e.g. 105 ms) on a loaded machine: host noise, not a code regression. Treat a
lone near-miss as a flake to rerun; only a consistent trend across runs is a
real bug (Appendix B).

**Gate.** The Phase 5 scenario suite, run 5× green at the phase exit gate
(`docs/STATUS.md`).

### 6. No priority inversion

**Meaning.** A low-priority process must never hold up a more important one.
Here the ordering is explicit and total: monitor < supervisor < workers.

**Mechanism.** `SCHED_FIFO` with fixed RT priorities at process startup:
monitor 10, supervisor 20, worker 30
(`runtime/include/safety_crit/runtime/scheduling.hpp:20`). Failure to apply
(e.g. missing `CAP_SYS_NICE`) is non-fatal and recorded as
`scheduling fallback (errno 1)` — visible, never silent.

**Gate.** Planned supervisor/worker scheduling tests (Phases 3/4) — treat as
binding for new code in these paths.

### 7. Graceful degradation

**Meaning.** With half the workers down, the system continues at reduced
throughput rather than failing totally.

**Mechanism.** The double-fault scenario (S5) SIGKILLs both hot workers with
no gap: one logical ring recovers via standby promotion, the other — with no
promotable standby left — enters the terminal `DEGRADED` state (flag bit
`kDegraded`, `shared-memory/include/safety_crit/shared_memory/atomic_flags.hpp:22`)
while everything else keeps running:

```text
==> S5 PASS: one promotion + one DEGRADED, replacements live, replay /tmp/s5_replay.xAwRWs.jsonl
```

`DEGRADED` is reported through `/status` and never silently clears within the
supervisor's lifetime.

**Gate.** The Phase 5 perturbation scenario suite (S5 run 5× green).

## The runtime baseline

On top of the seven properties, the container/runtime contract must hold: the
demo entrypoint and Compose stack keep running. **Gate:** `docker-build` and
`docker-compose-smoke` CI jobs (chapter
[14](14-running-production.md)).

## The meta-property: determinism

Not in the seven, but load-bearing: the workload is a pure function of
`(seed_base, worker_idx, tick)` (`workers/include/safety_crit/workers/workload.hpp:43`)
and perturbations are recorded as replay logs, so a fault's *outcome* is
reproducible — the S1R scenario replays a recorded crash against a fresh
stack and asserts event and record-count parity. Determinism is what turns a
heisenbug-prone distributed-ish system into something you can unit-test.

## What each property demands of *new* code

The properties are binding on contributions, not just existing code:

- Touching the ring/region hot path? No mutex, no allocation, no syscall, no
  exception; state the memory order and why (chapter 06).
- Touching recovery logic? Keep the bound — every wait needs a timer and an
  escalation path (chapter 10).
- Adding a state? It must be reportable through `/status` and the event log
  (chapter 12).
- Adding a test? Keep **both** test frameworks green (chapter 13).

## Further reading

- [`docs/ai-guidance/CORE.md`](../ai-guidance/CORE.md) — the authoritative card.
- [`docs/ai-guidance/TENETS.md`](../ai-guidance/TENETS.md) — property → gate table.
- Next: [Chapter 03 — Architecture Overview](03-architecture.md).

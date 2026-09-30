# Chapter 03 — Architecture Overview

## Motivation

You cannot reason about safety properties in a system you cannot picture.
This chapter gives you the whole board: which processes exist, who is allowed
to do what, where the shared memory sits, and which paths are "hot." Every
later chapter zooms into one box on this board.

## The process model

The system is a small fleet of cooperating **processes** (not threads — the
fault model treats a crashed process as the unit of failure, and separate
address spaces give the monitor an honest outside view):

```text
                        ┌────────────────────────────┐
   HTTP clients ──────► │  observability daemon      │  /health /metrics /status
                        │  (read-only observer)      │  event-log gap counting
                        └──────────┬─────────────────┘
                                   │ attaches region read-only, tails event log
┌──────────────────────────────────────────────────────────────────┐
│                        shared memory region                      │
│   /safety_crit_region  (SharedRegion v4)                         │
│   rings[0..2] · ownership[0..2] · worker_status[0..2] · meta     │
└──────────────────────────────────────────────────────────────────┘
        ▲ push (own ring)      ▲ poll (read-only)     ▲ orchestrate
        │                      │                      │
┌───────┴───────┐      ┌───────┴────────┐     ┌───────┴────────────┐
│ workers       │      │ monitor        │     │ supervisor         │
│ A (hot, RT30) │      │ (RT10)         │     │ (RT20, PID 1 in    │
│ B (hot, RT30) │      │ never writes   │     │  the container)    │
│ C (standby)   │      │ the region     │     │ spawns & reaps all │
└───────────────┘      └────────────────┘     └────────────────────┘
                                                  ▲
                                    ┌─────────────┴──────────┐
                                    │ perturb (harness CLI)  │  profile-only,
                                    │ NEVER for production   │  test stack
                                    └────────────────────────┘
```

**Supervisor** — owns the lifecycle. Creates/verifies the region, launches
the monitor plus workers A/B (hot) and C (standby), consumes the monitor's
alert stream, orchestrates failover (promotion + replacement), and terminates
and reaps every child on shutdown
(`supervisor/include/safety_crit/supervisor/supervisor.hpp:136`). In the
container it is PID 1; S6 uses that fact to test supervisor loss.

**Monitor** — the outside observer. Polls each worker's status cell and ring
tail, checks its pidfile for process liveness, runs the health state machine,
and emits JSON alerts on stdout (`{"component":"monitor",...}`) which the
supervisor validates and forwards
(`monitors/include/safety_crit/monitors/monitor_loop.hpp:41`). The monitor
**never writes the region** — an observer that can corrupt what it observes is
not an observer.

**Workers** — three physical processes, indices 0/1/2 (a/b/c). Hot workers run
the tick loop and publish to their ring; the standby sits idle and never
touches a ring until promoted (`workers/include/safety_crit/workers/worker_entry.hpp:17`).
The standby is *warm*: it has attached the region and knows its assignment,
so promotion is an ownership handoff, not a cold start.

**Observability daemon** — a long-lived read-only consumer: attaches the
region (verify or refuse to start), serves HTTP endpoints, and tail-scans the
event log for gap counting (`observability/include/safety_crit/observability/daemon.hpp:1`).

**Perturb harness** — a profile-only container in the test stack that issues
recorded fault signals (chapter [11](11-fault-injection.md)).

## Logical rings vs physical workers

The pivotal indirection: a **logical ring** (the output stream with an
identity, a sequence, and consumers) is decoupled from the **physical
worker** currently allowed to write it. Ownership is a token
(`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:66`):

```cpp
struct OwnershipToken {
    std::uint32_t physical_owner;  // which process may publish
    std::uint64_t epoch;           // bumped on every handoff
    ...
};
```

Normally ring 0 ↔ worker 0, ring 1 ↔ worker 1. When worker 0 dies, the
supervisor CAS-transfers ring 0 to the standby (physical 2) and bumps the
epoch — the dead worker, if it ever wakes (SIGSTOP'd), holds a stale token
and is fenced out. Here is the region after a failover, straight from
`ownership`:

```text
ownership 0 physical=0 epoch=2
ownership 1 physical=1 epoch=2
ownership 2 physical=2 epoch=2
```

and from `/status` after the compose failover-smoke, where ring 0's physical
owner is **2** (the promoted standby) with epoch bumped to **4**:

```json
{"rings":[{"ring":0,"owned":1,"physical_owner":2,"epoch":4,...},
          {"ring":1,"owned":1,"physical_owner":1,"epoch":2,...}, ...]}
```

Full protocol in chapter [10](10-supervisor-failover.md).

## Two planes

**Data plane** — the ring-buffer hot path: worker tick → `push()` → slot
(seq + CRC) → consumer drain. Rules: no allocation, no syscalls, no mutexes,
no exceptions (`docs/ai-guidance/TENETS.md` "Hot Path"). Measured by
per-tick CPU budgets, not by hope.

**Control plane** — everything else: monitor polling, alerts, supervisor
orchestration, pidfiles, the event log, HTTP. Allocation and STL are legal
here (they happen once per poll, never per ring operation). The boundary is
enforced by review and by the sanitizer legs of CI.

## Scheduling priorities

| Role | SCHED_FIFO priority | Rationale |
|------|--------------------:|-----------|
| Monitor | 10 | Lowest: must never starve the work it watches |
| Supervisor | 20 | Must out-rank workers to recover them |
| Workers | 30 | Highest: they carry the real-time payload |

(`runtime/include/safety_crit/runtime/scheduling.hpp:20`; property 6 in
chapter [02](02-safety-properties.md).)

## Process discovery: pidfiles

Each worker writes `<pid-dir>/safety_crit_worker_<idx>.pid` right after
attach; the file is removed on clean exit only, so a crash leaves a *stale*
pidfile the monitor rejects via `kill(pid, 0)`
(`workers/include/safety_crit/workers/worker_entry.hpp:21`). A subtlety the
monitor defends against: pidfiles are per *slot* and recycled by replacements,
so liveness alone cannot attribute a status word to a process generation — the
health rules never rely on that attribution
(`monitors/include/safety_crit/monitors/health.hpp:92`).

## What lives in the shared region

One fixed-size `/dev/shm` object (SharedRegion v4): three ring buffers, three
ownership tokens, three worker status cells, plus region metadata
(magic/version, `global_seq`, `integrity_word`). Laid out so each slot is one
cache line and hot words avoid false sharing. The full annotated layout is
chapter [05](05-shared-memory.md).

## Failure model at a glance

| Failure | Detected by | Response |
|---------|-------------|----------|
| Worker crash (SIGKILL/SIGSEGV) | Monitor: dead pid + RUNNING bit | Supervisor: promote standby, spawn replacement |
| Worker stall (SIGSTOP) | Monitor: ring tail frozen past threshold | Supervisor: one bounded SIGCONT, then SIGKILL escalation |
| Corrupt slot | Consumer: CRC mismatch | Skip, count, keep sequence continuity |
| Sequence gap | Supervisor witness drain | `data_loss_observed` event → `data_loss_events_total` |
| Supervisor death | Container runtime | `unless-stopped` rebuilds the whole stack; region survives |
| Region loss mid-run (daemon) | Identity re-verify per poll | `/health` flips to `degraded`, daemon keeps serving |

The first five rows are exercised by scenarios S1–S6 (chapter
[11](11-fault-injection.md)).

## Further reading

- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — the canonical reference this chapter condenses.
- Next: [Chapter 04 — Getting Started](04-getting-started.md).

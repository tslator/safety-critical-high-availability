# Chapter 10 — Supervision and Failover

## Motivation

Replacement is easy — `fork()` is the easy part. The hard part is making
replacement *safe*: how do you know the dead worker is really dead? What if
it was only frozen and wakes up to find a stranger writing "its" ring? This
chapter is the heart of the system: ownership epochs, the failover
orchestration, and the bounded stall-recovery ladder.

## Ownership tokens and epoch fencing

Every logical ring carries an `OwnershipToken` with a `physical_owner` and an
`epoch` (`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:66`).
The rules:

1. A process may publish to ring *r* only if token `physical_owner` is its
   index — checked in the publish path (chapter 07).
2. `transfer_ownership()` CAS-swaps the token **and bumps the epoch**
   (`shared_region.hpp:139`).
3. A writer holding a pre-bump token is **fenced**: its publish path refuses,
   forever, even if the process is alive.

Epoch fencing is what lets the system treat "crashed worker" and "frozen
worker that might wake up" as the *same* case: either way, it can never touch
the ring after handoff. And per chapter 06, an epoch bump also abandons any
in-flight claim from the prior epoch — `transfer_ownership` rolls `tail_`
back to the last committed sequence so the new owner resumes from a quiescent
ring, never force-committing a half-written slot.

See it live after a failover (from `/status` in the compose smoke — ring 0
now physically owned by the standby, index 2, epoch bumped):

```json
{"rings":[{"ring":0,"owned":1,"physical_owner":2,"epoch":4,...},
          {"ring":1,"owned":1,"physical_owner":1,"epoch":2,...}]}
```

## The supervisor's job description

The supervisor (PID 1 in the container) owns: region create-or-verify,
launching monitor + workers A/B (hot) + C (standby), consuming the monitor's
alert stream, running recovery, recording witness events, and orderly
shutdown with child reaping
(`supervisor/include/safety_crit/supervisor/supervisor.hpp:136`).

Config knobs that matter
(`supervisor/include/safety_crit/supervisor/supervisor.hpp:50`):

| Knob | Default | Meaning |
|------|---------|---------|
| `runtime_ms` | 0 (until SIGTERM) | Bounded runs for demos/tests |
| `stall_grace_ms` | 200 | Wait between SIGCONT and SIGKILL escalation |
| `handoff_grace_ms` | 750 | Window in which stall alerts describe the handoff, not a new fault |

## Crash failover, step by step

When the monitor reports `worker_crashed` for hot worker A:

1. **Confirm** — the supervisor trusts only schema-valid alert lines on the
   monitor's stdout; a valid `worker_crashed` alone can drive failover
   (T-0032).
2. **Promote** — standby C CAS-acquires ring A's token (`transfer_ownership`,
   epoch bumps). C's standby loop watches exactly this word and starts
   ticking (`workers/include/safety_crit/workers/worker_entry.hpp:8`).
3. **Replace** — spawn a new process for slot C and have it attach (fresh
   pidfile; stale-file semantics in chapter 09).
4. **Witness** — the drain loop consumes ring A and records
   `first_post_failover` the moment a record appears from the promoted owner,
   and times the whole thing (`supervisor: first post-failover record observed in 85 ms`).
5. **Keep running** — everything else (worker B, the monitor, the daemon)
   never noticed a pause beyond a sequence continuation.

The graceful shutdown line from a real bounded run is the audit trail:

```text
supervisor: shutdown state=4 a_records=343 a_corruptions=0 a_first_post_failover=1 b_records=200 b_corruptions=0 b_first_post_failover=0 failover_timing_emitted=1
```

`a_records=343` spans the crash: pre-crash records, the ownership handoff at
the sequence level, and post-failover records, with zero corruptions and the
post-failover marker set. The drain treats the transport position as the
authoritative sequence witness — corruption skips tolerated, real gaps
rejected (`supervisor.hpp:28`).

## The stall ladder: one SIGCONT, then mercy for nobody

A stalled worker (RUNNING, frozen ring tail, process alive) gets exactly one
chance, on a fixed timer (`StallRecoveryTracker`, `supervisor.hpp:104`):

- First `worker_stalled` alert ⇒ issue **one bounded SIGCONT**
  (`supervisor.hpp:85`).
- Tail changes before `stall_grace_ms` expires ⇒ recovered (event recorded).
- Grace expires ⇒ escalate to **SIGKILL** through the existing crash path —
  which means the failover machine above, not a bespoke path.
- A second stall alert while a recovery is already open is a no-op — the
  ladder doesn't stack.

The `handoff_grace_ms` window is the subtlety: stall alerts arriving while a
promotion is in flight describe the handoff, not a wedged replacement, and
must not arm the SIGCONT/SIGKILL path (`supervisor.hpp:61`). Without that,
the cure pages you; with it, the grace still bounds the window.

## Degradation, not despair

What if both hot workers die and there is only one standby? The S5 scenario
answers: ring with a promotable standby recovers; the other ring's owner
crashes with no promotable standby and takes the terminal `kDegraded` flag
(`shared-memory/include/safety_crit/shared_memory/atomic_flags.hpp:22`):

```text
==> S5 PASS: one promotion + one DEGRADED, replacements live, replay /tmp/s5_replay.xAwRWs.jsonl
```

`DEGRADED` is terminal and reportable — never silently cleared within the
supervisor's lifetime — and the rest of the system (healthy ring, monitor,
daemon) keeps running at reduced throughput. Partial failure with an honest
label beats total failure with a shrug.

## Supervisor loss itself

Who supervises the supervisor? The container runtime. The compose stack uses
`unless-stopped`; killing PID 1 (S6) exits the container, and the runtime
rebuilds it — the shared region (a `/dev/shm` object in the container's IPC
namespace) is re-created fresh with the stack:

```text
==> S6 PASS: container exited and policy-rebuilt (RestartCount 0 -> 1), replay /tmp/s6_replay.nkoocs.jsonl
```

The daemon keeps serving throughout, flipping `/health` to `degraded` while
the region is gone (chapter 12).

## Try it

The native bounded run (4 s, crash injected by the supervisor's own test
path — full walkthrough in chapter 14):

```console
$ ./build/gtest/app/safety-critical-ha supervisor --runtime-ms 4000
...
supervisor: first post-failover record observed in 85 ms
supervisor: shutdown state=4 a_records=343 a_corruptions=0 a_first_post_failover=1 ...
```

The Docker version, with an external kill:

```console
$ containers/compose/failover-smoke.sh
==> SIGKILL hot physical A (pid=8)
==> waiting up to 15s for supervisor to stabilize
==> state=healthy
==> verifying supervisor stdout shows standby C (physical worker 2) promoted to RUNNING
==> failover smoke PASSED
```

Watch the ownership epoch climb as you kill things
(`safety-critical-ha ownership --region /safety_crit_region`).

## What to look for

- **Fencing beats trust.** The protocol assumes the dead *might* wake up and
  makes that harmless, instead of betting it won't.
- **One recovery path.** Stalls escalate into the crash machine; there is
  only one failover implementation to get right.
- **Every wait has a timer and an escalation.** `stall_grace_ms`,
  `handoff_grace_ms` — bounded, configurable, auditable.

## Further reading

- DEC-0011 — v4 ownership layout; DEC-0012 — abandoned claims (#2), stall
  escalation (#3), degraded state (#4).
- `docs/phases/PHASE_4_SUPERVISOR.md`.
- Next: [Chapter 11 — Fault Injection](11-fault-injection.md).

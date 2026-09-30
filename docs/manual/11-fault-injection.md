# Chapter 11 — Fault Injection

## Motivation

"You have tests" tells me the system works when nothing goes wrong. Recovery
paths that never execute rot — and an untested recovery path is not a
feature, it is a rumor. Fault injection is the discipline of *scheduling*
failures: crash this worker at second 2, stall that one, poison one slot —
then assert the system's *response* (alerts, ownership, recovery time, record
counts), not just its survival.

This project has a first-class harness for it and six scripted scenarios.
The harness is blunt about its place in the world:

> It exists solely as the fault-injection test facility... **NEVER FOR
> PRODUCTION USE.** (`perturb/include/safety_crit/perturb/harness.hpp:4`)

## The harness: signals in, replay log out

`perturb` issues real signals (SIGSTOP, SIGKILL, SIGSEGV, SIGCONT, SIGUSR2)
at target processes, and records **every action** as a JSON-lines replay-log
entry (`harness.hpp:4`). One category per action
(`perturb/include/safety_crit/perturb/harness.hpp:30`):

```cpp
enum class Category : std::uint8_t {
    kCrash, kStall, kRecoverStall, kCorrupt,
    kDoubleFault, kSupervisorKill, kSupervisorExit, ...
};
```

A record is `{ts_ns, category, target_pid, params}` (`harness.hpp:48`):

```json
{"schema":1,"ts":1727590000123456789,"category":"crash","target":8,"params":{}}
```

Design notes worth stealing:

- **Real signals, not test doubles.** The thing under test cannot tell an
  injected crash from a real one — that is the point.
- **`crash()` prefers SIGSEGV, falls back to SIGKILL** (`harness.hpp:73`) —
  segv exercises the "died mid-tick" shape; kill the "no grace at all" shape.
- **Corruption is opt-in at the target.** `corrupt_next_slot` uses SIGUSR2
  against a worker that passed `--corrupt-hook`; production builds never arm
  it (`harness.hpp:80`, chapter 08).
- **Error idiom**: `bool fn(..., std::error_code&)` — the Phase 2 deviation
  style, no exceptions across the process-boundary-adjacent code.
- **`recover-stall` is harness bookkeeping only** — a SIGCONT record exists
  so replays can reconstruct the timeline, but it is never replayed itself
  (`harness.hpp:28`).

## The six scenarios

Each lives in `containers/compose/scenarios/`, shares helpers from
`common.sh` (compose up/down, `worker_pid`, `perturb`, `wait_log`,
`wait_healthy`), and **asserts the recovery response**. Captured results from
one green pass (each scenario on a fresh stack):

### S1 — crash a hot worker (`s1_crash.sh`)

Kill worker A; assert crash alert, replacement live, and recovery *under
budget*:

```text
==> S1: crashing hot worker A (pid 8)
==> S1 PASS: crash alert, replacement live, 41 ms < 100 ms, replay /tmp/s1_replay.cuJmbW.jsonl
```

### S2 — stall a hot worker (`s2_stall.sh`)

SIGSTOP A; assert stall detection and that the supervisor's *own* SIGCONT
recovery (the stall ladder, chapter 10) fired:

```text
==> S2 PASS: stall detected, supervisor SIGCONT recovery observed, replay /tmp/s2_replay.1DF2L5.jsonl
```

### S3 — poison a slot (`s3_corrupt.sh`)

SIGUSR2 the poison hook on A; assert the corrupt slot is tolerated as
exactly one counted corruption while the supervisor survives:

```text
==> S3 PASS: poisoned slot tolerated (a_corruptions=1), supervisor survived, replay /tmp/s3_replay.0kMpgm.jsonl
```

### S5 — double fault (`s5_double_fault.sh`)

Kill both hot workers with no gap; assert one promotion, one terminal
DEGRADED, and live replacements (graceful degradation, chapter 10):

```text
==> S5 PASS: one promotion + one DEGRADED, replacements live, replay /tmp/s5_replay.xAwRWs.jsonl
```

### S6 — kill the supervisor (`s6_supervisor_kill.sh`)

Kill PID 1 inside the container; assert the daemon keeps serving through the
outage and the runtime policy rebuilds the stack:

```text
==> S6: daemon still serving during supervisor loss (HTTP 200)
==> S6: daemon /health ok after supervisor rebuild
==> S6 PASS: container exited and policy-rebuilt (RestartCount 0 -> 1), replay /tmp/s6_replay.nkoocs.jsonl
```

### S1R — deterministic replay (`s1_replay.sh`)

The same crash recorded in S1, replayed against a fresh stack with target-PID
remapping:

```text
==> S1R[replay]: replaying /tmp/s1_replay.M9dO0a.jsonl with target remap 8=8
==> S1R PASS: events, ownership, and records match (a_records 1351 vs 1352, tolerance 200)
```

1351 vs 1352 records is a *pass*: the assertion checks event-sequence and
ownership parity with a bounded record-count tolerance for timing jitter —
and the tolerance is meaningful only because the workload is deterministic
(chapter 08). Replaying a nondeterministic workload and hoping the totals land
"close enough" is exactly the trap DEC-0012 #8 avoids.

## Try it

One scenario, from a stopped stack:

```console
$ docker compose down --volumes --remove-orphans
$ containers/compose/scenarios/s1_crash.sh
```

**Run each scenario on a fresh stack.** Running them back-to-back on a shared
stack fails for state-dependent reasons — e.g. S2 finds the standby already
promoted, or S6's PID-namespace kill takes the perturb sidecar with it (S6
kills the shared PID namespace, so anything after it on the same stack sees
dead tooling).

Two timing notes from the field:

- S1's recovery measured 41–105 ms across runs on one host (CI range 36–96
  ms; budget < 100 ms). A near-miss `!! recovery over budget: 105 ms` on a
  loaded host is a flake to rerun, not a code bug — but treat a *trend* of
  near-misses seriously.
- The scenario scripts use `docker compose` v2 syntax with `--wait` health
  gating; if your stack has leftover state, `down --volumes` first.

## What to look for

- **Assert the response, not the survival.** "Container still running" would
  pass even if recovery silently did nothing; "ownership epoch bumped, 41 ms,
  zero corruption" cannot.
- **The replay log turns incidents into fixtures.** Every scenario PASS
  leaves behind a file that re-creates the exact fault timeline.
- **Faults are typed.** Crash, stall, corruption, double-fault, supervisor
  loss — each is a category with its own expected response contract.

## Further reading

- `perturb/include/safety_crit/perturb/harness.hpp`, `replay_log.hpp` — DEC-0012 #8.
- `docs/phases/PHASE_5_PERTURBATION.md` — scenario definitions, exit gate.
- Next: [Chapter 12 — Observability](12-observability.md).

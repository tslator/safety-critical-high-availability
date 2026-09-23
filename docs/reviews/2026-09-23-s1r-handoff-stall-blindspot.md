# D-2026-09-23-001: S1-R CI Flake — Failover Handoff Stall Blind Spot

- Date: 2026-09-23
- Status: Accepted
- Related decision: [DEC-0013](../decisions/0013-handoff-visibility-and-stall-bounding.md)
- Resulting task: [T-0032](../tasks/T-0032-handoff-stall-blindspot.md)

## Question

Hosted CI run
[35865376613](https://github.com/tslator/safety-critical-high-availability/actions/runs/35865376613)
failed `Run deterministic replay scenario (S1 replay)` on a commit whose
immediately preceding run
([35864589526](https://github.com/tslator/safety-critical-high-availability/actions/runs/35864589526))
was green. What actually races there, and what is the correct fix — rerunning
CI obviously cannot be the answer?

## Symptom

`s1_replay.sh` compares the supervisor event categories of the original and the
replayed run and requires an exact match. The replayed run carried one extra
category:

```
1a2
> stall recovered
!! event category sequences differ (original vs replayed above)
```

## Findings

1. **Where the line comes from.** `supervisor: stall recovered` is printed only
   from the T-0025 bounded stall-recovery path, which arms when a monitor
   `worker_stalled` alert arrives while the supervisor state is `kRunning` or
   `kStalledRecovering`. A pure crash scenario has no business arming it.
2. **The monitor never reports the crash.** `perturb crash` is SIGSEGV; the
   dead worker's status cell keeps `RUNNING` until the replacement process
   stores its own IDLE word into the *same* cell, and the slot's pidfile is
   recycled by the replacement (a zombie also answers `kill(pid, 0)`). With a
   stale `RUNNING` word plus a "live" pidfile, the monitor reports
   `worker_idle` (or `worker_stalled`) instead of `worker_crashed`. Every local
   run confirmed: zero `worker_crashed` alerts across crash runs. T-0030
   evidence already flagged this edge as a known race; this is that race, made
   load-dependent.
3. **Nothing advances the supervisor state.** The state machine only reaches
   `kFailoverDetected` from the (masked) `worker_crashed` alert, so it sits in
   `kRunning` for the whole handoff and the stall handler stays armed against
   the processes recovering the ring.
4. **Two ways the stall rule arms on a handoff.**
   - A promoted owner is announced `RUNNING` at ownership transfer, before it
     can commit on its new ring. Measured first post-failover commit latency:
     18–96 ms, i.e. straddling `stall_threshold` = 100 ms, which is also the
     failover SLA in `scripts/phase4-failover-timing.sh`.
   - The crashed slot itself reads as alive + `RUNNING` with a frozen tail
     (finding 2), so the stall fires 100 ms after the crash — and `begin()`
     then attributes the ring through the `value_or(physical_worker)` fallback,
     i.e. a ring the alerted worker no longer owns.
5. **Both outcomes are wrong, and one is an availability bug.** If the first
   post-failover commit lands inside the 200 ms escalation grace, the run logs
   a spurious `stall recovered` (the CI failure). If it does not, the grace
   expires and the supervisor SIGKILLs the replacement — a healthy worker in
   the middle of recovery. The latter was reproduced in the same scenario under
   CPU contention as a `shutdown state=4` vs `state=2` divergence, and the same
   signature was caught deterministically by the new regression test run
   against the pre-fix code (the failover is never recorded at all: no
   `worker_crashed` alert, no state advance).
6. **Harness diagnosability.** On failure the scenario emitted the two-line
   diff and nothing else; the whole investigation had to be re-run locally
   because CI kept no logs.
7. **Non-obligations.** The scenario's exact-match contract is correct and
   stays; the deterministic replay log and its schema are unaffected; no
   shared-memory layout change is in scope.

## Options considered

- **Harness-only exclusion** (drop stall categories from the S1-R comparison):
  hides a supervisor that SIGKILLs healthy workers. Rejected.
- **Monitor-only fix** (re-baseline the stall window on ring handoff): fixes
  the promoted-owner case but not the crashed-slot case, whose observed ring
  identity is unchanged at alert time. Insufficient alone.
- **Supervisor-only fix** (record failover at reap, suppress stall arming during
  the window): fixes the dangerous action, leaves a false `worker_stalled`
  alert in the stream for every crash run. Insufficient alone.
- **Generation-stamped status word now**: correct at the root, but changes the
  shared-memory contract mid-phase and would need its own decision and tests.
  Deferred to a follow-up with the residual risk recorded.
- **Adopted**: monitor first-commit rule plus supervisor failover-from-reap
  plus harness settle/diagnostics (DEC-0013), with the attribution defect
  recorded as residual risk.

## Verification summary

- New regression test `Supervisor.CrashRecordsFailoverWithoutStallArtifacts`
  fails 5/5 against the pre-fix sources and passes 10/10 after the fix; four
  new monitor health cases pin the handoff bound.
- GoogleTest 134/134 and Catch2 134/134 locally; the full local
  `docker-compose-smoke` sequence (base smoke, S1, S2, S3, S5, S6) green, S1-R
  green 3x unconstrained and 8x under 16-way CPU contention.

# DEC-0013: Failover Handoff Visibility and Stall Bounding

- Status: Accepted
- Date: 2026-09-23
- Related discussion: [D-2026-09-23-001](../reviews/2026-09-23-s1r-handoff-stall-blindspot.md)
- Authorizes: [T-0032](../tasks/T-0032-handoff-stall-blindspot.md)
- Amends: [DEC-0010](0010-phase3-monitor-daemon.md) #3 (stall rule),
  [DEC-0012](0012-phase5-perturbation-engine.md) #3 (bounded stall recovery)

## Context

Hosted CI run 35865376613 failed the T-0030 S1 deterministic-replay step with
one extra supervisor event (`stall recovered`); the same commit was green nine
minutes earlier, so the failure is a race rather than a regression. The
investigation (see the linked discussion) found that during a crash failover
the monitor cannot see what is actually happening, and the supervisor acts on
that blind spot:

1. The monitor never reports the crash in a crash run. The crashed slot's
   status cell keeps `RUNNING` until the replacement process stores its own
   word into the same cell, and the slot's pidfile is recycled by the
   replacement (`kill(zombie, 0)` also succeeds), so the slot reads as alive +
   `RUNNING` with a frozen ring tail. Observed directly: the monitor reported
   `worker_stalled` 100 ms into a SIGSEGV crash and only reported
   `worker_crashed` after the replacement's status store.
2. Because no `worker_crashed` alert ever arrives, nothing advances the
   supervisor's state machine during the handoff, so the T-0025 stall handler
   stays armed against the very processes that are recovering the ring. A
   promoted owner is announced `RUNNING` at ownership transfer — before it can
   commit anything — and its first post-failover commit is timing bound
   (18–96 ms measured, above `stall_threshold` = 100 ms on a loaded runner).
   Whichever way the race lands, the run is wrong: a spurious
   `stall recovered` (breaking the DEC-0012 #8 determinism contract), or —
   when the 200 ms escalation grace expires first — `stall escalation` and
   SIGKILL against the replacement/promoted worker, i.e. the recovery path
   killing a healthy worker.
3. The scenario harness contributed diagnosability only: on failure it printed
   the two-line event diff and nothing else.

## Decision

1. **Stall detection is armed by the first commit of an episode.** A `RUNNING`
   episode — fresh, promoted, or restarted — is measured against
   `MonitorConfig::handoff_grace` (default 750 ms) until the observed ring tail
   advances once; after that the ordinary `stall_threshold` applies. A ring
   epoch change under a live episode is a handoff (`transfer_ownership` bumps
   the epoch) and re-baselines the window silently: no alert is emitted for the
   handoff itself. Detection stays bounded — an owner that never commits is
   still reported, just on the longer bound.
2. **The supervisor records failover from reap.** Reap of a crashed child is
   the authoritative failover trigger (it already is for the ownership
   transfer and the timing witness); the state machine now advances to
   `kFailoverDetected` there instead of waiting for a monitor alert that the
   status-cell recycling can mask. `kDegraded` stays sticky and terminal.
3. **The handoff window bounds stall action, not stall reporting.** From the
   reap iteration until the first post-failover commit — bounded by
   `SupervisorConfig::handoff_grace_ms` so it cannot stay open forever — stall
   alerts are still forwarded, but they do not arm the SIGCONT/SIGKILL path.
   After the window closes, `kFailoverDetected` is admitted by the stall
   handler, so genuine post-failover stalls still recover.
4. **Scenarios keep their evidence.** The S1-R scenario settles longer than
   the handoff grace before snapshotting (so trailing timing-bound edges are
   ordered identically in both phases) and dumps the raw supervisor logs and
   witness snapshots whenever it exits non-zero.

## Consequences and residual risk

- Monitor and supervisor agree on the failover story without relying on the
  crash alert edge; the S1-R event sequence becomes `first post-failover
  record observed` in both phases regardless of runner load.
- The underlying attribution defect — a status cell and a pidfile that cannot
  be attributed to a process generation, so a crash can be misread as an idle
  edge or a stall — is **not** fixed here. Fixing it means stamping the status
  word with the process generation (or making liveness generation-scoped),
  which touches the shared-memory contract and needs its own decision.
  Related: the stall handler still resolves an alert's ring through
  `owned_logical_ring()`'s home-index fallback, so an alert for a worker that
  no longer owns a ring can still be attributed to that ring's current owner;
  suppressing such alerts outright would lose real stalls (the monitor latches
  one alert per episode), so this also waits for generation attribution.
  Follow-up recorded as residual risk in the Phase 5 evidence.
- `s1_crash.sh` still asserts a hard < 100 ms recovery budget, which is the
  same runner-sensitivity class; relaxing or documenting that budget belongs to
  T-0031 (phase exit), not here.

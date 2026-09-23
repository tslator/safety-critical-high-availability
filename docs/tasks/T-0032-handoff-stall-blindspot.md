# T-0032: Failover Handoff Visibility and Stall Bounding

- Status: In Review
- Owner: AI agent (opencode)
- Priority: High
- Depends on: T-0025, T-0026, T-0030
- Phase: Phase 5
- Related decision: [DEC-0013](../decisions/0013-handoff-visibility-and-stall-bounding.md)
- Related discussion: [D-2026-09-23-001](../reviews/2026-09-23-s1r-handoff-stall-blindspot.md)

## Scope

Remove the failover-handoff blind spot that made the T-0030 S1 deterministic
replay scenario nondeterministic in hosted CI (run 35865376613), and fix the
availability defect behind it: under load the supervisor's bounded stall path
escalated to SIGKILL against the worker that was recovering the ring.

Three parts, per DEC-0013:

1. **Monitor** (`monitors/include/safety_crit/monitors/health.hpp`,
   `monitor_config.hpp`): stall detection arms on the first observed commit of
   a `RUNNING` episode. Until then — and after any ring epoch change under a
   live episode, which re-baselines the window silently — the bound is
   `MonitorConfig::handoff_grace` (default 750 ms) instead of
   `stall_threshold` (100 ms). Validation rejects a non-positive grace or one
   shorter than `stall_threshold`.
2. **Supervisor** (`supervisor/src/supervisor.cpp`): reap of a crashed child
   advances the state to `kFailoverDetected` (the monitor cannot be relied on —
   the replacement recycles the crashed slot's status cell and pidfile). A
   bounded window from reap to first post-failover commit
   (`SupervisorConfig::handoff_grace_ms`) stops stall alerts from arming the
   SIGCONT/SIGKILL path; after the window, `kFailoverDetected` is admitted so
   genuine post-failover stalls still recover. `kDegraded` stays sticky.
3. **Scenario** (`containers/compose/scenarios/s1_replay.sh`): settle longer
   than the handoff grace before snapshotting both phases, and dump the raw
   supervisor logs plus witness snapshots on any non-zero exit.

Out of scope: generation-stamped status word / generation-scoped liveness (the
root attribution defect; needs a shared-memory contract change and its own
decision), and the `s1_crash.sh` hard < 100 ms recovery budget.

## Acceptance Criteria

- `Supervisor.CrashRecordsFailoverWithoutStallArtifacts`: after a crash, the
  run records `kFailoverDetected`, emits no `stall recovered`, no
  `stall escalation`, and no `worker_stalled` alert. Fails against the pre-fix
  code (verified 5/5), passes after (verified 10/10).
- Monitor unit coverage in both frameworks for: promoted owner not reported as
  stalled; epoch change re-baselining the window; an owner that never commits
  still reported on the longer bound; the post-crash episode running on the
  longer bound; `handoff_grace` defaults and validation.
- T-0025 stall behaviour preserved: `RecoversStalledWorkerWithBoundedSigcont`
  and `EscalatesStalledWorkerToCrashRecovery` green, plus a new case proving a
  stall injected after the handoff window still recovers.
- S2 (stall), S3 (corruption), S5 (double fault), S6 (supervisor kill) and the
  base failover smoke unchanged; S1-R deterministic across repeated runs,
  including under CPU contention.
- GoogleTest and Catch2 suites green; ASan+UBSan and TSan legs green.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

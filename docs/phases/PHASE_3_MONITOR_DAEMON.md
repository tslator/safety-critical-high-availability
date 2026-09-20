# Phase 3 - Monitor Daemon

## Purpose

Add a monitor process that observes the Phase 1 shared region and the Phase 2
worker status lifecycle from outside: poll `worker_status` cells (including
`OVERRUN`), distinguish CRASHED (RUNNING + dead process) from STALLED
(RUNNING + unchanged ring counters beyond threshold), and publish every
detection as a structured JSON log line — the observation surface the Phase 4
supervisor consumes and the Phase 5 fault-injection harness witnesses.

Authoritative decision: [DEC-0010](../decisions/0010-phase3-monitor-daemon.md)
(discussion: [D-2026-09-20-001](../reviews/2026-09-20-phase3-monitor-architecture.md)).

## Scope and Non-Goals

**In scope**

- `monitors/` library (`safety_crit::monitors`): monitor config, health
  algorithm with alert vocabulary, poll loop with injected clock/liveness/
  pacer, JSON line formatter, pidfile liveness reader.
- Worker-side pidfile (write after attach, remove on clean exit; crash
  leaves a stale file rejected through liveness) — DEC-0010 #2.
- `safety-critical-ha monitor` subcommand with `--interval-ms`,
  `--stall-threshold-ms`, `--region`, `--pid-dir`, `--polls`.
- Structured JSON alert/report lines on stdout (plan's JSON-logging
  deliverable); in-process observation counters.
- Fork-based integration tests in the plain build (crash, clean exit,
  SIGSTOP stall/recovery, standby); standard matrix on all unit tests.

**Out of scope (later phases)**

- HTTP `/health` `/metrics` `/status` endpoints and Prometheus metrics —
  deferred to Phase 6 (deviation #1; single implementation of the endpoint
  surface against this event vocabulary).
- Failover/promotion decisions and pidfile restart loops (Phase 4 supervisor).
- Fault injection beyond SIGKILL/SIGSTOP/SIGCONT driven by tests (Phase 5).
- Any shared-memory layout change (`kRegionVersion` stays 3, per DEC-0010 #1).
- Writing the `CRASHED` status bit — the monitor *observes* crash through
  liveness; writing recovery-state bits is future supervisor scope.

## Handoff Contract From Phase 2

- Status lifecycle RUNNING/IDLE (plus stale-RUNNING after an unclean death)
  and the `OVERRUN` bit are the monitor's input surface; ring `tail_`
  counters are read as-is (the exposed supervisor-inspection words).
- Worker CLI flags remain the perturbation surface Phase 5 reuses.
- Hosted CI green on the Phase 3 exit commit is part of the exit gate
  (pattern established by Phases 1 and 2).

## Deviations From the Plan Sketch (recorded decisions)

1. **No `std::format`, and no HTTP in this phase.** The pinned clang-14
   verification pipeline links libstdc++ 12, which predates `<format>`
   (implemented in libstdc++ 13) — the same upstream wall Phase 2 recorded
   for `<ranges>`. JSON lines are built with fixed-format `snprintf` instead
   (identical output on every compiler; shape pinned by exact-string unit
   tests), verified green on clang-verify. A future toolchain update
   (libstdc++ >= 13) may retire `snprintf` for `std::format` in an
   intentional toolchain change. HTTP/Prometheus are deferred to Phase 6 by
   DEC-0010 #4 to avoid building the health-report surface twice.

(Further deviations recorded during implementation.)

## Task Plan

### Task T3.1 - Monitor Core _(3-4 h)_

**Record:** [T-0012](../tasks/T-0012-monitor-core.md) ·
[DEC-0010](../decisions/0010-phase3-monitor-daemon.md) · Complete
2026-09-20 (G3.1 green: [evidence](../../NOTES.md)).

**Dependencies:** Phase 2 exit (G2.1–G2.3).

**Implementation steps**

1. Create `monitors/` as static library `safety_crit::monitors` (C++20,
   warnings, sanitizers; standard test wiring), add to root
   `CMakeLists.txt`, update `docs/ARCHITECTURE.md` +
   `ARCHITECTURE_RULES.md` ownership rows.
2. `monitor_config.hpp/.cpp`: poll interval (default 10 ms), stall threshold
   (default 100 ms); validation boundary (positive, threshold >= interval).
3. `health.hpp`: alert vocabulary (`worker_crashed` / `worker_stalled` /
   `worker_recovered` / `worker_overrun` / `worker_idle` / `worker_running`,
   string names published per DEC-0010 #3), `HealthState`,
   `WorkerObservation` (status word, ring tail, liveness), `WorkerTrack`
   latch state, and `observe_worker` — at most one alert per poll with
   priority crashed > episode edge > recovered/stalled > overrun; stall
   latches until the tail advances, crash latches until the process
   returns, overrun latches until an observed clear. Region-agnostic:
   inputs are plain values, clock injected (DEC-0009 #2 pattern).

**Deliverables**

- `safety_crit::monitors` config + health core; `monitors_health_test`
  (exhaustive classification matrix with fake clock + scripted liveness).

**Verification gate G3.1**

```bash
for cfg in "GoogleTest" "Catch2"; do
  cmake -S . -B build/t31-$cfg -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=$cfg \
    && cmake --build build/t31-$cfg --parallel \
    && ctest --test-dir build/t31-$cfg --output-on-failure
done
# plus ASan+UBSan configuration
```

Pass when the classification matrix passes in both frameworks and under
ASan+UBSan, zero warnings.

---

### Task T3.2 - Pidfile, Poll Loop, JSON Alerts, Monitor CLI _(4-5 h)_

**Record:** [T-0013](../tasks/T-0013-crash-detection-json-alerts-cli.md) ·
[DEC-0010](../decisions/0010-phase3-monitor-daemon.md) · Complete
2026-09-20 (G3.2 green: [evidence](../../NOTES.md)).

**Dependencies:** G3.1.

**Implementation steps**

1. `workers/include/safety_crit/workers/pidfile.hpp` + `src/pidfile.cpp`:
   contract path `<dir>/safety_crit_worker_<idx>.pid`, atomic publish
   (temp + rename), removal on clean exit only; `run_worker` writes after
   attach (fatal on failure) and removes at the end; `--pid-dir` on the
   worker subcommand.
2. `monitors/pidfile_liveness.hpp/.cpp`: parse + range-check the pidfile and
   liveness-probe with `kill(pid, 0)` (EPERM = alive); stale/malformed
   files are dead (DEC-0010 #2).
3. `monitor_loop.hpp`: poll loop (status + `tail_` acquire loads, injected
   liveness/clock/pacer, stop via `stop_token`/`sig_atomic_t`, one shared
   timestamp per poll, alerts attributed to `MonitorStats`, bounded stop
   latency); `interval_pacer` sleeps 1 ms slices.
4. `json_lines.hpp/.cpp`: alert line + shutdown `monitor_report` line
   (snprintf, deviation #1), flushed immediately on stdout.
5. `monitor_entry.hpp/.cpp` `run_monitor`: create-or-open attach +
   `verify_identity` (metadata-only, live-safe), signal wiring reused from
   `workers::signals`, loop, final report; `monitor` subcommand + `--pid-dir`
   on `worker` in `app/` (hand-rolled parsing, rc=2 on invalid).

**Deliverables**

- Pidfile module (writer) + liveness reader; `run_monitor`; `monitor`
  subcommand; `monitors_loop_test` (accounting, stall/recovery via real
  ring counters, crash via injected verdict, clean-exit idle, stop latency,
  exact JSON formats); `WorkersPidfile.ContractPathWriteRemove` in
  `workers_core_test`.

**Verification gate G3.2**

Full matrix (both frameworks × plain, ASan+UBSan, TSan) on the loop and
pidfile tests: pass when accounting is exact, classification is
deterministic, and cancellation is clean under sanitizers.

---

### Task T3.3 - Monitor Integration, Phase Exit _(3-4 h)_

**Record:** [T-0014](../tasks/T-0014-monitor-integration-phase-exit.md) ·
[DEC-0010](../decisions/0010-phase3-monitor-daemon.md) · Complete
2026-09-20 (G3.3 green: [evidence](../../NOTES.md); hosted CI run
35518074368 on commit e4cb5ab — **Phase 3 exit gate satisfied**).

**Dependencies:** G3.2.

**Implementation steps**

1. `monitors_integration_test` (fork-based, plain build only; sanitizer
   skip-pass per G1.4/G2.3 precedent): (a) hot worker + monitor, SIGKILL →
   `worker_crashed` exactly once, report count 1, stale pidfile left and
   rejected via liveness; (b) worker finishes cleanly → `worker_idle`, no
   crash event, pidfile removed; (c) SIGSTOP → `worker_stalled` (never
   crashed — liveness passes while stopped), SIGCONT → `worker_recovered`;
   (d) standby → `worker_idle` alive, no crash, rings untouched.
2. Full local matrix + clang-verify; record Phase 3 matrix + exit evidence
   in `NOTES.md`; update task/registry/STATUS docs.

**Deliverables**

- `monitors_integration_test` (4 scenarios × 5 local repetitions stable);
  README monitor command docs; Phase 3 exit evidence.

**Verification gate G3.3**

```bash
cmake -S . -B build/t33 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest \
  && cmake --build build/t33 --parallel \
  && ctest --test-dir build/t33 --output-on-failure
ctest --test-dir build/t33 -R monitors_integration --output-on-failure
```

Pass when integration tests are green in the plain build (both frameworks)
and the full unit matrix stays green under sanitizers.

## Phase Exit Gate

Phase 3 is complete only when G3.1–G3.3 pass and the following evidence is
attached in `NOTES.md`:

| Required evidence | Source gate |
|---|---|
| Health classification matrix deterministic (both frameworks) | G3.1 |
| Loop accounting + pidfile contract clean under sanitizers | G3.2 |
| Integration: crash-on-SIGKILL, clean-exit idle, stall/recovery, standby | G3.3 |
| Full local matrix (both frameworks × plain/ASan+UBSan/TSan) green | exit |
| Hosted CI green on the Phase 3 exit commit | exit |

No Phase 4 supervisor implementation may be accepted until this exit gate is
satisfied.

## Handoff to Phase 4

Phase 4 adds the supervisor: launch/manage workers, consume the monitor's
alert stream (stdout JSON lines, or a channel chosen by a new decision),
promote the standby on `worker_crashed`, restart victims as standby, and
enforce the failover budget. The monitor's alert vocabulary and the pidfile
contract are that phase's input surface; `--pid-dir` and the worker CLI
flags remain the perturbation surface Phase 5 reuses.

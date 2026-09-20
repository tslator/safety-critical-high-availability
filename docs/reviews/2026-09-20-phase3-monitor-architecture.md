# D-2026-09-20-001: Phase 3 Monitor Architecture Reconciliation

- Date: 2026-09-20
- Status: Accepted
- Related decision: [DEC-0010](../decisions/0010-phase3-monitor-daemon.md)
- Resulting tasks: [T-0012](../tasks/T-0012-monitor-core.md),
  [T-0013](../tasks/T-0013-crash-detection-json-alerts-cli.md),
  [T-0014](../tasks/T-0014-monitor-integration-phase-exit.md)

## Question

The Phase 3 plan sketch (`SAFETY_CRITICAL_HA_PLAN.md` §Phase 3) specifies a
monitor daemon with `co_yield`-based alert generation, HTTP endpoints
(`/health`, `/metrics`, `/status`), and Prometheus-facing metrics. None of
this can be adopted verbatim: the project is C++20 with gcc-12 as the pinned
primary (`std::generator`/`co_yield`-returning types are C++23), HTTP and
Prometheus overlap the Phase 6 observability deliverables, and the project
carries no third-party dependencies to build an HTTP server with.
Additionally, the sketch assumes a `CRASHED` status flag becomes observable,
but the Phase 2 baseline never sets it — a process killed by SIGSEGV/SIGKILL
cannot modify its own status word, so its cell keeps `RUNNING` forever.
How should Phase 3 be scoped and built against the real baseline without
touching the frozen v3 layout or pre-building Phase 6?

## Findings

- **Crash vs stall needs process liveness.** The only in-region signals are
  the status word and the ring counters (`derive_state()` head/tail).
  A dead worker looks identical to a stalled one through both (frozen
  `RUNNING`, unchanged counters). The monitor must consult something outside
  shared memory to distinguish CRASHED from STALLED. Options: pid in the
  status cell (layout v4 — breaking, ripples through all attach/verify
  tests), pidfiles outside `/dev/shm` written at worker startup, or CLI-
  passed pids (awkward and race-prone for the Phase 3 launch flow).
  Pidfiles are the smallest change: no layout impact, deterministic in the
  fork-based tests, and reusable by the Phase 4 supervisor; a v4 pid field
  stays open as a future decision if Phase 4/5 need in-region truth.
- **`kCrashed` semantics settle themselves.** With external liveness, the
  monitor is the component that *observes* CRASHED (RUNNING set but pid
  gone); no component *sets* the bit in Phase 3. The flag stays a Phase 4
  write-surface for the supervisor if it wants to advertise recovery state.
- **HTTP belongs to Phase 6.** The plan's Phase 6 "health report API" and
  Prometheus metrics are the same surface as the Phase 3 endpoints; building
  them twice (or building a throwaway hand-rolled HTTP server now) adds
  thread + socket test surface to the safety phase for no gate value. Phase
  3 delivers the detection core and emits alerts as structured JSON log
  lines on stdout — which is itself a plan Phase 3 deliverable ("Write
  structured JSON logs with timestamps") — and Phase 6 wraps that same
  event stream in the HTTP/Prometheus surface.
- **C++20 ceiling applies as in Phase 2 (DEC-0009 #2).** The health
  algorithm is a plain loop collecting alerts into a vector (the monitor is
  outside the ring hot path, so allocation there is allowed); no
  `std::generator`, no coroutines needed.
- **Monitor reads are cheap and non-invasive.** Acquire loads of
  `worker_status[]` cells plus `derive_state()` snapshots of each ring are
  exactly the quiescence-safe observation path T1.3/T1.5 designed for
  external observers; no layout change, no new shared-memory API needed.
- **Alert delivery to Phase 4.** The supervisor will want the alert stream;
  stdout JSON lines are parseable by any process manager and by the Phase 5
  harness, and can be superseded by a dedicated channel via a future
  decision without changing the event semantics.
- **Component shape follows DEC-0009 #6:** new `monitors/` static library
  `safety_crit::monitors`, headers under
  `monitors/include/safety_crit/monitors/`, tests under `monitors/tests/`,
  CLI parsing stays in `app/` (hand-rolled, no new dependencies);
  `safety-critical-ha monitor` subcommand; `--version` untouched so image
  healthchecks and Compose keep working; Compose itself unchanged.

## Alternatives Considered

- **HTTP endpoints in Phase 3 (as sketched):** rejected — duplicates the
  Phase 6 health report API, adds socket/thread test surface to the safety
  phase, and no CI gate consumes it before Phase 6.
- **Layout v4 with pid in the status cell:** rejected for Phase 3 — a
  breaking change (version bump, attach-path and verify revalidation) for a
  need solvable outside shared memory; kept open for a Phase 4/5 decision
  if in-region pid truth becomes necessary.
- **CLI-passed pids:** rejected — races between worker startup and monitor
  launch, and the fork tests would fabricate the same information the
  pidfile provides natively.
- **In-region alert counters:** rejected — layout change with the same
  duplication problem; stdout JSON lines deliver the same events.
- **Detection core + pidfile liveness + JSON-line alerts (Chosen):** see
  DEC-0010.

## Result

Build Phase 3 as the monitor core: a `safety_crit::monitors` library
polling status cells and ring counters on a fixed interval with injected
clocks, distinguishing CRASHED (RUNNING + dead pidfile pid) from STALLED
(RUNNING + unchanged ring counters beyond threshold), surfacing OVERRUN,
and emitting structured JSON alert lines on stdout behind a
`safety-critical-ha monitor` subcommand. HTTP/Prometheus deferred to
Phase 6. Rationale in [DEC-0010](../decisions/0010-phase3-monitor-daemon.md).

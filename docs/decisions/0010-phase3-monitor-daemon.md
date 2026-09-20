# DEC-0010: Phase 3 Monitor Daemon

- Status: Accepted
- Date: 2026-09-20
- Related discussion: [D-2026-09-20-001](../reviews/2026-09-20-phase3-monitor-architecture.md)
- Authorizes: [T-0012](../tasks/T-0012-monitor-core.md),
  [T-0013](../tasks/T-0013-crash-detection-json-alerts-cli.md),
  [T-0014](../tasks/T-0014-monitor-integration-phase-exit.md)

## Context

Phase 3 adds the monitor daemon: poll `worker_status` cells (including
`OVERRUN`), detect stalled rings via unchanged head/tail counters, and
collect metrics — the Phase 2 handoff contract. The plan sketch predates the
implemented baseline: it uses `co_yield` (C++23), assumes an HTTP surface
that Phase 6 also owns, assumes a `CRASHED` flag someone sets, and assumes
third-party libraries the project does not carry. The decision must keep the
frozen v3 layout and the hot-path policies intact while making crash and
stall detection real and testable.

## Decision

1. **Layout stays v3; no new shared-memory API.** The monitor observes via
   acquire loads of `worker_status[]` and `derive_state()` snapshots of each
   ring — the quiescence-safe external-observer path T1.3/T1.5 designed. No
   field is added, reinterpreted, or bumped.
2. **Crash detection via pidfiles outside shared memory.** Each worker
   writes its pid to `<dir>/safety_crit_worker_<idx>.pid` (default dir
   `/tmp`, `--pid-dir` overridable) at startup and removes it on clean
   exit. The monitor reads the pidfile and liveness-checks it with
   `kill(pid, 0)`. Classification: `RUNNING` + pid gone → CRASHED;
   `RUNNING` + pid alive + ring counters unchanged for longer than the stall
   threshold → STALLED; `RUNNING` + advancing → healthy. Stale pidfiles
   (pid reused/dead) are rejected by the liveness check, never trusted.
   `kCrashed` remains unset by any Phase 3 component — the monitor
   *observes* CRASHED; writing the bit is future supervisor scope. A v4
   in-region pid field stays open as a separate future decision if Phase
   4/5 demonstrate the need.
3. **Alerts are structured JSON lines on stdout.** One event per line:
   `{"ts":<unix-ns>,"level":"...","component":"monitor","event":"...",
   "worker":<idx|null>,...}` built with `std::format`. Event vocabulary
   (Phase 3): `worker_crashed`, `worker_stalled`, `worker_recovered`
   (stall cleared), `worker_overrun`, `worker_idle`, `worker_running`.
   The same event stream is what Phase 6 wraps in HTTP/Prometheus and what
   the Phase 4 supervisor may consume; delivery-channel changes are future
   decisions, event semantics are not.
4. **HTTP/Prometheus deferred to Phase 6.** Phase 3 ships no sockets. The
   deviation from the plan sketch is recorded here; the Phase 6 health
   report API and metrics implement the endpoint surface once, against this
   event vocabulary.
5. **C++20 ceiling (carries DEC-0009 #2).** No `std::generator`, no
   coroutines: the health algorithm is a plain loop collecting alerts into
   a `std::vector` (monitor code is outside the ring hot path, so
   allocation/algorithms are permitted there, never per ring operation).
   Clock injection is a template parameter (steady_clock in production,
   fake clock in tests); the poll loop uses `std::stop_token` plus the
   existing `sig_atomic_t` signal pattern; SIGTERM/SIGINT stop cleanly.
6. **New `monitors/` component (DEC-0009 #6 pattern).** Static library
   `safety_crit::monitors` with headers under
   `monitors/include/safety_crit/monitors/` and tests under
   `monitors/tests/` (own suite, standard matrix). `app/` keeps only CLI
   parsing/wiring: `safety-critical-ha monitor [--interval-ms MS]
   [--stall-threshold-ms MS] [--region NAME] [--pid-dir DIR] [--ticks N]
   [--once]`, hand-rolled parsing, validation errors to stderr with nonzero
   exit; `--version` and the existing `worker` subcommand untouched.
   Compose and image entrypoints unchanged (`docker-build` and
   `docker-compose-smoke` must stay green).
7. **Configuration defaults.** Poll interval 10 ms (plan sketch), stall
   threshold 100 ms (10 missed ticks at the default 10 ms worker interval;
   the plan's Phase 4 failover budget will constrain tuning there, not
   here). Both validated strictly positive and threshold ≥ interval at the
   config boundary.
8. **Metrics collection is in-memory counters.** Phase 3 aggregates per-
   worker observation counters (polls, alerts by type, last-advance
   sequence/timestamp) inside the monitor process, exposed in `worker_*`
   events and a final `monitor_report` event on shutdown. No external
   metrics system, no layout addition.

## Consequences

- New files: `monitors/` (library + tests), Phase 3 plan
  (`docs/phases/PHASE_3_MONITOR_DAEMON.md`), tasks T-0012..T-0014.
- Modified: root `CMakeLists.txt` (`add_subdirectory(monitors)`),
  `workers/` entry (pidfile write/cleanup + `--pid-dir`-consistent naming),
  `app/src/main.cpp` (`monitor` subcommand), `README.md` (monitor command),
  `docs/ARCHITECTURE.md`, `docs/ai-guidance/ARCHITECTURE_RULES.md`
  (ownership row). `CORE.md` is untouched, so generated adapters stay
  byte-identical.
- New CI coverage arrives with the test targets (native ×2, sanitizers ×4,
  clang-verify pick up `monitors_*` tests automatically); no workflow edits
  are planned in Phase 3. Fork-based integration tests run in the plain
  build only (G1.4/G2.3 precedent).
- Pidfiles are a filesystem contract between worker and monitor (path +
  content format); changing it requires updating both sides and this
  decision's successors.

## Maintenance Rules

- The monitor never writes to the shared region; monitor code performing
  region writes is a review blocker.
- Pidfile checks always go through the liveness helper (never trust file
  contents alone); pid-reuse misjudgment is the known accepted limitation
  (pid wrap is unobservable at demo scale; revisit via the v4 decision if
  it ever matters).
- Alert event names are a published vocabulary: additions are additive,
  renames/removals need a new decision (Phase 4/5/6 consumers depend on
  them).
- Accepted decisions are superseded, not rewritten.

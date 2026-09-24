# D-2026-09-24-001: Phase 6 Observability and Certification-Grade Logging Architecture

- Date: 2026-09-24
- Status: Accepted
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)
- Resulting tasks: [T-0033](../tasks/T-0033-event-log-library.md)–[T-0039](../tasks/T-0039-phase6-integration-exit.md)

## Question

How should Phase 6 deliver structured JSON logs, Prometheus metrics, and a
health report API over the Phase 3–5 event and counter surfaces without
breaking the frozen monitor alert vocabulary, the v4 shared-memory layout,
the lock-free hot-path policy, or the currently green Compose/smoke greps —
and what does "certification-grade logging" concretely mean here?

## Findings

- **The event vocabulary already exists and is frozen.** DEC-0010 #3
  publishes `worker_crashed`, `worker_stalled`, `worker_recovered`,
  `worker_overrun`, `worker_idle`, `worker_running` as JSON lines on the
  monitor's stdout; DEC-0010 #4 explicitly deferred HTTP/Prometheus to
  Phase 6, which is therefore the single implementation venue. The same
  stream is already consumed by the supervisor pipe and grepped by
  `containers/compose/failover-smoke.sh` and the S1–S6 scenario scripts.
- **Supervisor output is half-structured.** The supervisor forwards validated
  monitor lines verbatim but emits its own witness data (failover timing,
  degradation, stall escalation, shutdown summary) as plain text
  (`supervisor/src/supervisor.cpp`), parsed by shell greps — an implicit,
  unversioned contract that Phase 6 should formalize.
- **Workers are deliberately silent.** DEC-0009 #6 keeps "logging" to
  in-region counters; workers have no stdout/stderr events at all.
  DEC-0009 notes Phase 6 owns real observability.
- **Ready-made metric sources exist.** In-region: per-ring sequence counters,
  `corruption_count` (observability-only by DEC-0005 design), `worker_status`
  bits, ownership tokens (physical owner + epoch), region integrity word.
  Process-side: `MonitorStats` (polls, per-kind alert counts), supervisor
  `OutputWitness` (records, corruptions, first-post-failover). None of these
  requires a layout change; the DEC-0010 #1 read-only acquire-load path is
  the sanctioned observation route.
- **No HTTP/JSON/Prometheus infrastructure exists**, and the toolchain
  constrains options: pinned g++-12 has no `std::format` (the standing
  deviation behind `monitors/src/json_lines.cpp`), and the dependency
  allowlist is GoogleTest/Catch2 only (DEC-0012 rejected even `expected-lite`
  for this reason).
- **Compose health semantics are non-HTTP today** (`docker-compose.yml`
  healthcheck: shm file + worker pidfile `kill -0`), and `docker-build` /
  `docker-compose-smoke` must stay green throughout.
- **Scope collision on redundancy.** Phase 5 docs call supervisor redundancy
  "Phase 6 HA scope", but the master plan's Phase 6 is observability-only
  and never lists redundancy; DEC-0011 #8 and scenario S6 treat the
  supervisor as a deliberate single point of trust.
- **Phase 6 sketch in `SAFETY_CRITICAL_HA_PLAN.md` predates the baseline.**
  It uses `std::format` (unavailable) and places `metrics.hpp` /
  `http_server.hpp` inside `monitor/`, contradicting DEC-0010's socket-free
  monitor.

## Alternatives Considered

- **HTTP server inside the monitor process (plan-sketch placement).**
  Rejected: adds an accept thread sharing `MonitorStats` with the poll loop
  (new TSan surface on the safety-relevant component) and contradicts the
  DEC-0010 decision to keep the monitor socket-free; the deviation would
  exist only to save one process.
- **Supervisor-served endpoints or supervisor-launched sidecar fed by a
  pipe.** Rejected: widens the single point of trust with sockets and adds
  fan-out wiring to the supervisor's already load-bearing line loop.
- **Vendor cpp-httplib / nlohmann/json.** Rejected: outside the
  GoogleTest/Catch2-only allowlist (same rationale as DEC-0012's
  `expected-lite` rejection); HTTP/1.1-served-reads and the Prometheus text
  exposition format are small, well-specified surfaces the project can
  hand-roll with the established `snprintf` idiom.
- **Restructure monitor alert lines to the plan's nested
  `{ts,level,component,event,details}` shape.** Rejected: the flat alert wire
  format is a published contract (DEC-0010 maintenance rule) grepped by
  smoke/scenario scripts; the loss/risk of churn buys nothing safety-side.
  New component events may use richer shapes; existing lines stay
  byte-compatible and additive.
- **Hash-chained audit log (per-record CRC of the previous record).**
  Rejected for now: tamper-evidence is not a stated project tenet; per-record
  chaining adds a new format contract and complicates concurrent appends.
  Per-component monotonic sequence numbers give gap detection (the property
  that maps to the project's continuity tenet) at a fraction of the cost.
  Chaining can be a future additive decision.
- **Include supervisor redundancy in Phase 6.** Rejected: the master plan
  assigns Phase 6 to observability; redundancy has no plan sketch, expands
  the ownership/topology model, and would jeopardize the phase's bounded
  delivery. Explicitly re-deferred (see DEC-0014 #2).
- **Feed event-derived metrics from a pipe tap of the monitor stream.**
  Rejected: requires supervisor-side fan-out or duplicating the monitor; a
  shared append-only event log (already required as the logging deliverable)
  is the natural, restart-survivable fan-out point — one writer per writer
  process, one tailing reader.

## Result

Phase 6 introduces a new `observability/` component (`safety_crit::observability`,
`safety-critical-ha observability` subcommand): a socket-only daemon that
attaches to the shared region read-only, tails a new sequenced append-only
JSON-lines event log, and serves hand-rolled HTTP/1.1 `GET /health`,
`GET /metrics` (Prometheus text format), and `GET /status`. Monitor and
supervisor gain zero sockets; the supervisor's plain-text witness lines
migrate to JSON lines in lockstep with every in-repo grep; workers gain
lifecycle/tick-level JSON events emitted strictly outside the ring hot path.
"Certification-grade logging" is defined as: one consolidated append-only
event log, `O_APPEND` writes of at most 4096-byte lines (atomic across
processes), per-component monotonic sequence numbers with gap detection at
read time, `fsync` on WARN and above, and a documented durability policy.

The shared-memory layout stays v4 with zero region changes; the alert
vocabulary is wrapped, not changed; the dependency set stays
GoogleTest/Catch2-only; supervisor redundancy is explicitly excluded and the
single-point-of-trust posture (DEC-0011 #8, S6) is re-documented. See
[DEC-0014](../decisions/0014-phase6-observability-logging.md) for the full
decision and [the Phase 6 plan](../phases/PHASE_6_OBSERVABILITY.md) for the
task sequence and gates.

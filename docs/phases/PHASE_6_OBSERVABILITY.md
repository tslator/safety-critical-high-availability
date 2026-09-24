# Phase 6 - Observability and Certification-Grade Logging

## Purpose

Make the system's safety story observable without weakening it: a
certification-grade append-only event log, Prometheus metrics, and a health
report API layered over the frozen monitor alert vocabulary, in-region
counters, and supervisor witness events — with zero shared-memory changes,
zero hot-path impact, and the supervisor remaining a deliberate single point
of trust.

Authoritative decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)
(discussion: [D-2026-09-24-001](../reviews/2026-09-24-phase6-observability-architecture.md)).

## Scope

- New `observability/` component (`safety_crit::observability`): sequenced
  event-log writer/reader, hand-rolled bounded HTTP/1.1 server, metrics
  registry + Prometheus text encoder, `/health` `/metrics` `/status`
  endpoints.
- `safety-critical-ha observability` subcommand; `--event-log` on
  worker/monitor/supervisor (default off).
- Supervisor witness lines to JSON events (lockstep grep migration across
  `failover-smoke.sh`, S1-S6, S1-R replay helper).
- Worker lifecycle/tick-level events (opt-in, outside the hot path).
- Compose `observability` service container + metrics/health smoke.

Out of scope: supervisor redundancy (re-deferred per DEC-0014 section 1; S6
semantics unchanged), hash-chained/tamper-evident audit mode, HTTP auth,
Prometheus/Grafana deployment (scrape surface only), region layout changes
(v4 frozen), `run_demo.sh` demo choreography (Phase 7), Phase 5b items
(still backlog).

## Task Sequence

1. [T-0033](../tasks/T-0033-event-log-library.md): sequenced append-only
   event log library. Independent.
2. [T-0034](../tasks/T-0034-structured-logging-migration.md): structured
   logging migration (supervisor JSON, worker lifecycle, monitor append,
   grep lockstep). Depends on T-0033.
3. [T-0035](../tasks/T-0035-http-server-core.md): bounded HTTP/1.1 server
   core. Independent of T-0033/T-0034.
4. [T-0036](../tasks/T-0036-metrics-registry-prometheus.md): metrics
   registry + Prometheus encoder. Depends on T-0033, T-0035.
5. [T-0037](../tasks/T-0037-health-status-endpoints.md): health report and
   status endpoints. Depends on T-0035, T-0036.
6. [T-0038](../tasks/T-0038-observability-cli-compose.md): `observability`
   CLI subcommand + Compose service + smoke extension. Depends on T-0034,
   T-0036, T-0037.
7. [T-0039](../tasks/T-0039-phase6-integration-exit.md): Phase 6
   integration, evidence, and exit.

## State Model

Daemon lifecycle (new; existing processes unchanged):

```text
STARTING -> SERVING -> (region verify failure) DEGRADED -> (region restored) SERVING
     |          |
     |          +----> (SIGTERM/SIGINT) STOPPED (clean shutdown, fds closed)
     +-----> (config/attach error) EXIT nonzero
```

The daemon never exits for region loss (visibility outranks restart,
DEC-0014 section 2); `worker_status`, ownership, and `data_loss_events_total`
are rendered from the latest region read at scrape time, and event-derived
counters advance on event-log tail progress at `--poll-interval-ms` cadence.

## Verification Gates

- G6.1: event log library green in both frameworks and both sanitizer
  configurations; injected sequence gap detected; concurrent-writer
  atomicity test green; restart seq recovery green; fsync policy exercised.
- G6.2: logging migration green — supervisor/worker format tests pin the
  JSON wire contract in both frameworks; `docker-compose-smoke` and S1/S2/
  S3/S5/S6/S1-R green against the new format; replay determinism (DEC-0012
  section 8) preserved with the migrated parser.
- G6.3: HTTP server bounded — malformed-request/timeout/connection-cap
  tests green; fd-leak check green; TSan clean with socket tests active.
- G6.4: metrics correctness — gauges equal region state at scrape time in
  tests; `data_loss_events_total == 0` across all green scenario runs;
  `event_log_gaps_total` independently sourced from data loss;
  `failover_duration_seconds` observed and consistent with the `<100 ms`
  crash-recovery budget (observation, not a new SLA).
- G6.5: Compose — new service healthy, existing supervisor healthcheck
  unchanged, port published on loopback, metrics/health smoke green 5x
  consecutively; S6 still exits + rebuilds the supervisor container while
  the daemon keeps serving.
- Exit: full matrix (GoogleTest/Catch2, ASan+UBSan, TSan, clang-verify,
  docker-build, docker-compose-smoke, scenarios 5x) + hosted CI run
  recorded in [Phase 6 evidence](../evidence/phase-6.md).

## Residual Risks

- Event-derived counters lag by log-tail cadence (`--poll-interval-ms`,
  default 100 ms); acceptable for metrics, not for alerting paths (which
  keep using the existing synchronous monitor pipe).
- `O_APPEND` atomicity assumes records <= 4096 B; the writer enforces the
  cap and a test pins the behavior, but exotic filesystems could still
  differ — the log path lives on the tmpfs-backed runtime volume to limit
  exposure.
- A process crash can drop buffered info-level log records; by design this
  surfaces as `event_log_gaps_total` (control plane) and never as
  `data_loss_events_total` (data plane). Consumers must not conflate them.
- The daemon widens the attack surface (first sockets in the project);
  mitigated by loopback default bind, hard request/connection bounds, and
  auth being explicitly scoped out — non-loopback binds are operator
  opt-in at their own risk.
- In-repo consumers of the supervisor's plain-text witness lines are
  migrated in lockstep (T-0034), but external dashboards/greps outside the
  repo will break on the new format; the alert vocabulary (monitor lines)
  is byte-compatible, and the change is documented in the evidence file.
- `docker-compose-smoke` CI job grows one more moving part; mitigated by
  the daemon having no dependency on supervisor liveness (read-only region
  + log tail only).

# T-0037: Health Report and Status Endpoints

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0035, T-0036
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Implement the `observability` daemon's HTTP surface on the T-0035 server
core per DEC-0014 §7:

- `GET /health` → always 200 while serving; JSON body with `ts`, `status`
  (`ok` iff every logical ring is owned and `data_loss_events_total == 0`,
  else `degraded`), `uptime_ms`, per-worker `state` + `last_sequence`,
  per-ring ownership (`physical_owner`, `epoch`), and
  `data_loss_events_total`.
- `GET /metrics` → wire the T-0036 registry render onto the route.
- `GET /status` → diagnostic snapshot: region version/integrity, per-ring
  counters, per-component event-log watermarks and gap counts, daemon
  uptime.
- Region-loss behavior: mid-run region verify failure flips `/health` to
  `degraded` (daemon keeps serving; never exits).

## Acceptance Criteria

- Response shapes documented in
  `observability/include/safety_crit/observability/health_report.hpp`;
  field sets pinned by tests in both frameworks.
- Loopback integration tests: healthy fixture region → `/health` `ok`;
  unowned ring / injected drain gap → `degraded`; `/metrics` matches the
  T-0036 registry fixture; `/status` reflects event-log watermarks;
  unknown paths 404.
- JSON validity asserted (bracket balance + field extraction) without a
  third-party JSON library.
- Sanitizer legs green.

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

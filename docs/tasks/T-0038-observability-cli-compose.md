# T-0038: Observability CLI Subcommand and Compose Service

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0034, T-0036, T-0037
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Wire the daemon end-to-end per DEC-0014 §9–§10:

- `safety-critical-ha observability [--listen HOST:PORT] [--region NAME]
  [--event-log PATH] [--poll-interval-ms MS] [--ticks N] [--once]` in
  `app/src/main.cpp` — hand-rolled parsing, strict validation, errors to
  stderr with nonzero exit. `--once` prints one snapshot to stdout without
  binding (test path). SIGTERM/SIGINT clean shutdown.
- `docker-compose.yml`: new always-on `observability` service container
  sharing `/dev/shm` and `/run/safety-critical-ha` volumes (T-0021 model);
  port `127.0.0.1:8080:8080`; curl-based healthcheck against `/health`;
  supervisor service and its healthcheck unchanged; event log path
  `/run/safety-critical-ha/events.jsonl` wired via `--event-log` on all
  emitting components.
- Smoke extension: fetch `/metrics` and `/health` from the host in the
  Compose smoke; assert `worker_status` gauges present,
  `data_loss_events_total 0`, and `/health` `status:"ok"`.
- Docs updates: `README.md` (observability command + endpoints),
  `docs/ARCHITECTURE.md` (component, endpoints, single-point-of-trust
  re-affirmation per DEC-0014 §1),
  `docs/ai-guidance/ARCHITECTURE_RULES.md` (ownership row; daemon read-only
  / never-signal rule).

## Acceptance Criteria

- CLI validation tests (bad port, bad interval, missing args) in both
  frameworks via the `--once` path; hand-rolled parsing consistent with the
  monitor/worker CLI idiom.
- `docker-build` green; Compose stack boots with the new service; both the
  existing supervisor healthcheck and the new daemon healthcheck go
  healthy; S6 still shows supervisor-container exit + restart-policy
  rebuild while the daemon keeps serving `degraded` during the window.
- New metrics/health smoke assertions green 5× consecutively in the phase
  integration run (executed with T-0039).
- ASan+UBSan and TSan legs green.

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

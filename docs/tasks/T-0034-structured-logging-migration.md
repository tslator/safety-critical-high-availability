# T-0034: Structured Logging Migration (Supervisor, Workers, Monitor Append)

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0033
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Land the logging side of Phase 6 across existing processes, in lockstep with
every in-repo consumer:

1. `--event-log <path>` flag on `worker`, `monitor`, and `supervisor`
   subcommands (default off — with the flag absent, all output stays
   byte-identical to Phase 5). Appended records use the T-0033 writer.
2. Supervisor witness lines migrate to JSON events per DEC-0014 §4:
   `failover_started`, `failover_recovered` (carries `latency_ms`),
   `ring_degraded`, `stall_recovered`, `stall_escalated`,
   `shutdown_summary`. Verbatim monitor-line forwarding and exit codes
   0/3/4 unchanged.
3. Worker lifecycle events (only when `--event-log` set): `worker_started`,
   `worker_stopped`, `worker_deadline_overrun` — emitted between ticks,
   never in the ring hot path.
4. Migrate all consumers in the same task: `containers/compose/failover-smoke.sh`,
   `containers/compose/scenarios/*` greps (including S1's
   `first post-failover record observed in N ms` → JSON field
   `latency_ms`), and the T-0030 replay comparison helper (plain-text
   shutdown-summary parsing → JSON `shutdown_summary` record).

## Acceptance Criteria

- Supervisor witness event vocabulary documented in
  `supervisor/include/safety_crit/supervisor/`; format pinned by
  `supervisor/tests/` cases in both frameworks.
- Worker lifecycle event tests in both frameworks (fork-based ones plain
  build only); negative test: no event-log file created and no behavior
  change when `--event-log` is absent.
- `docker-compose-smoke` and scenarios S1, S2, S3, S5, S6, S1-R all green
  against the new format (run in the phase integration task; smoke must not
  regress before).
- Replay comparison still satisfies the DEC-0012 #8 determinism contract on
  S1-R (semantics unchanged, parser migrated).
- ASan+UBSan and TSan green in applicable configs.

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

# DEC-0014: Phase 6 Observability and Certification-Grade Logging

- Status: Accepted
- Date: 2026-09-24
- Related discussion: [D-2026-09-24-001](../reviews/2026-09-24-phase6-observability-architecture.md)
- Authorizes: [T-0033](../tasks/T-0033-event-log-library.md)–[T-0039](../tasks/T-0039-phase6-integration-exit.md)

## Context

Phase 5 closed with all gates green. DEC-0010 #4 deferred HTTP/Prometheus to
Phase 6 and froze the monitor alert vocabulary (`worker_crashed`,
`worker_stalled`, `worker_recovered`, `worker_overrun`, `worker_idle`,
`worker_running`); Phase 6 is the single venue implementing the endpoint
surface over that stream. The supervisor emits plain-text witness lines that
scenario scripts grep; workers are deliberately silent (DEC-0009 #6). The
master plan's Phase 6 sketch uses `std::format` (unavailable on the pinned
libstdc++ 12) and places the HTTP server inside `monitor/`, contradicting the
socket-free monitor. Phase 5 docs informally called supervisor redundancy
"Phase 6 HA scope"; the master plan never does. The decision must deliver
structured logging, Prometheus metrics, and a health report API without
touching the v4 layout, the hot path, the alert vocabulary, or the green
Compose/smoke baseline beyond a lockstep migration.

## Decision

1. **Scope is observability only; supervisor redundancy stays excluded.**
   The supervisor remains a deliberate single point of trust (DEC-0011 #8,
   scenario S6). Redundancy is re-deferred to post-Phase 6 planning (Phase 8
   or backlog) and this is recorded in `docs/ARCHITECTURE.md`; Phase 5's
   "Phase 6 HA work is the venue" note resolves to this decision.
2. **New `observability/` component serves all sockets.** Static library
   `safety_crit::observability` plus `safety-critical-ha observability`. The
   daemon: (a) attaches to the shared region read-only via the DEC-0010 #1
   acquire-load observer path, (b) tails the event log (§3), (c) serves
   HTTP/1.1 (§5). The monitor and supervisor gain zero sockets and zero new
   threads. A region verify failure at attach is a startup error; loss of the
   region mid-run makes `/health` report `degraded` (the daemon never exits
   for region loss — visibility outranks a restart here; recorded as a
   deviation from the monitor's fail-fast posture).
3. **Certification-grade event log.** One consolidated append-only
   JSON-lines event log per deployment. Schema v1, one JSON object per line
   ≤ 4096 bytes, mirroring the replay-log idiom:
   `{"schema":1,"ts":<unix-ns>,"level":"info|warn|error","component":"monitor|supervisor|worker|observability|perturb","seq":<per-component uint64, starts at 1>,"event":"<name>",<extra flat fields>}`.
   - **Atomicity:** every record is a single `write()` to an `O_APPEND |
     O_CREAT | O_CLOEXEC` file; ≤ 4096-byte POSIX appends are atomic, so
     concurrent processes cannot interleave records. Oversized records are a
     writer error (never split).
   - **Continuity:** `seq` is monotonic per component (keyed by component +
     worker index, not pid); a writer recovers its next `seq` by scanning the
     existing file at open, so gaps remain detectable across restarts. The
     reader validates continuity and tolerates a torn trailing line mid-read.
   - **Durability:** `fsync` synchronously on `level >= warn`; info records
     rely on process-crash durability only. This policy is documented in the
     header; host-crash durability of info records is explicitly out of
     scope (hash-chained/`fsync`-everything audit mode is a possible future
     additive decision).
   - **Opt-in:** `--event-log <path>` on worker/monitor/supervisor; default
     off, so with the flag absent every process stays byte-identical to
     Phase 5. Default path when set by Compose:
     `/run/safety-critical-ha/events.jsonl` on the shared runtime volume.
4. **Logging migration is lockstep and vocabulary-safe.**
   - Monitor alert/report lines: **byte-compatible, unchanged** (DEC-0010 #3
     published contract); the monitor only gains event-log append of the same
     records when `--event-log` is given.
   - Supervisor plain-text witness lines migrate to JSON event-log records
     and JSON on stdout: `failover_started`, `failover_recovered` (carries
     `latency_ms`, replacing `first post-failover record observed in N ms`),
     `ring_degraded`, `stall_recovered`, `stall_escalated`,
     `shutdown_summary` (replaces the plain-text shutdown summary parsed by
     the T-0030 replay comparison helper). Verbatim monitor-line forwarding
     and exit codes 0/3/4 are unchanged. Every in-repo grep
     (`failover-smoke.sh`, S1–S6, replay helper) migrates in the same task
     as the emitting code.
   - Workers emit lifecycle/tick-level events only, only when `--event-log`
     is given: `worker_started` (role, ring), `worker_stopped` (reason),
     `worker_deadline_overrun` (observed between ticks, never in the ring
     hot path). Hot-path allocation/syscall/lock bans unchanged.
5. **Hand-rolled HTTP/1.1 server, hard-bounded.** No threads: a
   single-threaded `poll()` loop. GET (and HEAD, body omitted) only; 405
   otherwise. Bounds: request line ≤ 512 B, headers ≤ 32 lines / 8 KiB
   total, request timeout 2 s, keep-alive idle timeout 5 s, ≤ 16 concurrent
   connections; responses built in fixed buffers with the `snprintf` idiom.
   Default bind `127.0.0.1:8080`; `--listen` for other binds (documented
   unauthenticated, demo scope).
6. **Hand-rolled JSON and Prometheus text format.** No new third-party
   dependencies; the allowlist stays GoogleTest/Catch2. Prometheus output is
   the text exposition format v0.0.4 with `# HELP`/`# TYPE` lines.
7. **Endpoints.**
   - `GET /health` → always HTTP 200 while serving; body
     `{"ts",…,"status":"ok|degraded","uptime_ms",…,"workers":[{"id","state","last_sequence"}],"ownership":[{"ring","physical_owner","epoch"}],"data_loss_events_total":N}`.
     `ok` iff every logical ring is owned and `data_loss_events_total == 0`.
     Status stays in the body (never HTTP codes): container liveness remains
     the non-HTTP Compose healthcheck's job.
   - `GET /metrics` → Prometheus text (table in §8).
   - `GET /status` → full diagnostic snapshot: region version/integrity,
     per-ring counters (sequences, corruption counts), per-component event
     log watermarks and gap counts, daemon uptime.
   - Anything else → 404.
8. **Metrics and sources.**

   | Metric | Type | Source |
   |---|---|---|
   | `worker_status{worker}` | gauge | `worker_status` bits / monitor classification: IDLE=0, RUNNING=1, CRASHED=2, RECOVERING=3, DEGRADED=4 (additive extension of the plan enum) |
   | `ring_buffer_sequence{worker}` | gauge | acquire-read committed sequence per logical ring |
   | `ring_corruptions_total{worker}` | counter | region `corruption_count` (DEC-0005 observability-only word) |
   | `perturbation_count_total{type}` | counter | event-log records with `component=perturb`, by `category` (crash, stall, corrupt, double-fault); supervisor-kill excluded — a daemon cannot observe its own trust domain's death, S6 documents it |
   | `failover_duration_seconds` | gauge | latest `failover_recovered` event `latency_ms / 1000` |
   | `data_loss_events_total` | counter | **data plane only**: supervisor drain-witness sequence gaps (CORE property #1 violations). Always 0 in green scenarios |
   | `event_log_gaps_total{component}` | counter | control-plane event-log read gaps; reported separately because a process crash can legitimately drop buffered info-level records — must never pollute `data_loss_events_total` |
   | `observability_up`, `observability_uptime_seconds` | gauge | daemon self-liveness |
   | `ownership_epoch{ring}` | gauge | in-region ownership token epoch |
9. **CLI.** `safety-critical-ha observability [--listen HOST:PORT]
   [--region NAME] [--event-log PATH] [--poll-interval-ms MS] [--ticks N]
   [--once]` — hand-rolled parsing and strict validation per the DEC-0010 #6
   monitor idiom; `--once` validates config and prints one snapshot to
   stdout without binding a socket (test path). Error handling follows the
   standing libstdc++-12 idiom `bool fn(..., T& out, std::error_code& ec)`.
10. **Compose topology.** New always-on `observability` service container
    (supervisor-container restarts must not blind the dashboard) sharing the
    `/dev/shm` and `/run/safety-critical-ha` volumes per the T-0021 model;
    port published as `127.0.0.1:8080:8080`; existing supervisor healthcheck
    unchanged; daemon healthcheck uses the runtime image's curl against
    `/health`. S6 semantics unchanged: while the supervisor container is
    being rebuilt by restart policy, the daemon keeps serving and reports
    `degraded`.
11. **Zero region changes.** `kRegionVersion` stays 4; no field is added,
    reinterpreted, or bumped anywhere in `shared-memory/`. All region access
    from Phase 6 code is read-only via the sanctioned observer path; the
    daemon performing a region write is a review blocker (monitor rule,
    extended).

## Consequences

- New files: `observability/` (library + tests, own suite, standard matrix),
  Phase 6 plan (`docs/phases/PHASE_6_OBSERVABILITY.md`), tasks
  T-0033–T-0039, `docs/evidence/phase-6.md`.
- Modified: root `CMakeLists.txt` (`add_subdirectory(observability)`),
  `app/src/main.cpp` (new subcommand + `--event-log` flags), supervisor
  output paths and in-repo greps (lockstep), worker/monitor entry plumbing,
  `docker-compose.yml`, `README.md`, `docs/ARCHITECTURE.md`,
  `docs/ai-guidance/ARCHITECTURE_RULES.md` (ownership row + daemon read-only
  rule). `CORE.md` is untouched, so generated adapters stay byte-identical.
- The daemon is the project's first socket surface: CI sanitizer legs now
  cover socket lifetimes (fds under ASan, single-threaded loop trivially
  TSan-clean); `docker-build` and `docker-compose-smoke` remain green with
  the new service and a metrics/health smoke assertion.
- Replay comparison (T-0030 helper) migrates from plain-text shutdown-summary
  parsing to JSON records; S1-R determinism semantics are unchanged.

## Maintenance Rules

- Metric names and labels are a published contract once T-0039 closes:
  additions are additive; renames/removals require a new decision.
- The event log schema is versioned (`"schema":1`) like the replay log;
  field changes require a version bump and a decision.
- Supervisor witness event names join the published vocabulary (DEC-0010
  rule applies): additions are additive, renames/removals need a decision.
- The observability daemon never writes the shared region and never signals
  any process; both are review blockers.
- Accepted decisions are superseded, not rewritten.

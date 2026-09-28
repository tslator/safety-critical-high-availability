# Phase 6 Evidence Plan

This record is the evidence target for
[Phase 6](../phases/PHASE_6_OBSERVABILITY.md) and will be populated as tasks
close.

## Required Evidence

- GoogleTest and Catch2 plain configurations.
- ASan+UBSan and TSan for the new observability library, daemon, and
  migrated logging paths.
- Pinned Clang verification (CI container; no clang-14 on the reference
  host).
- Event log schema v1 writer/reader contract coverage, including the
  fork-based concurrent-writer atomicity test (T-0033).
- Structured logging migration with byte-compatible monitor lines and
  lockstep in-repo grep migration (T-0034).
- HTTP server bound enforcement (request/header/timeout/connection limits)
  (T-0035).
- Metrics registry and Prometheus text exposition conformance (T-0036).
- `/health`, `/status`, and 404 behavior (T-0037).
- `observability` CLI subcommand (incl. `--once`) and Compose service with
  metrics/health smoke assertion (T-0038).
- Zero changes under `shared-memory/`; region layout stays v4
  (DEC-0014 #11) for every task.
- Docker build and Compose smoke green with the new service.
- Hosted CI run link.

Each closed task must link implementation and durable command/result
evidence here or in `NOTES.md`.

## T-0033 Result

- Implementation: new static library `safety_crit::observability` in
  `observability/` (`include/safety_crit/observability/event_log.hpp`,
  `src/event_log.cpp`); root `CMakeLists.txt` gains
  `add_subdirectory(observability)`. Schema v1 documented in the header.
  Writer: `O_APPEND | O_CREAT | O_CLOEXEC`, one `write()` per record,
  oversized record rejected (`file_too_large`) without splitting or burning
  a sequence number, per-component-instance seq recovered by tail-scan at
  open (unsupported schema → `EPROTO` startup error), fsync barrier on
  `level >= warn` via injectable policy, injectable clock (CLOCK_REALTIME
  default). Reader: sequential scan, per-key continuity validation (gaps +
  missed counts keyed `component` or `component/<instance>`),
  torn-trailing-line tolerance (kIncomplete, watermark unchanged),
  watermark tracking with `seek()` resume for tail-follow.
- Tests: `observability/tests/event_log_test.cpp`, 11 cases in both
  frameworks: happy path; field round-trip incl. `instance`; oversized
  rejection; seq recovery after reopen; independent per-instance recovery;
  gap detection (injected missing seq); torn trailing line + tail-follow
  resume; empty/absent file; level→fsync policy via injected counting
  policy; unsupported-schema rejection + forward-compatible unknown fields;
  concurrent-writer atomicity (4 forked processes × 100 interleaved records
  → 400 parse-clean records, zero mixed, gap-free per-instance sequences;
  plain builds only per G1.4/G2.3 precedent).
- GoogleTest 145/145 (134 pre-existing + 11 new); Catch2 145/145.
- ASan+UBSan ×2 (GoogleTest, Catch2): 128/118+10, zero reports (fork case
  skipped under sanitizers, established precedent).
- TSan ×2 (GoogleTest, Catch2) under `setarch --addr-no-randomize`,
  `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`: 128/128 each,
  zero race reports.
- Clang verification: deferred to the CI container leg (no clang-14 on the
  reference host); standard matrix above is green.
- No shared-memory code touched: `git status` shows zero changes under
  `shared-memory/`; region layout untouched (v4).
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0035 Result

- Implementation: `observability/include/safety_crit/observability/http_server.hpp`
  + `observability/src/http_server.cpp` (library
  `safety_crit::observability`). Single-threaded `poll()` loop (100 ms
  tick), `std::stop_token` + `sig_atomic_t` shutdown pattern; IPv4 only.
  GET/HEAD accepted (HEAD omits body), other methods → 405; unknown path →
  404; request target validated (no control chars/spaces) → 400;
  `Content-Length` request bodies not accepted → 400; keep-alive with
  per-connection buffer reuse; ≤ 64 KiB handler body → else 500.
  Bounds enforced per task spec: request line ≤ 512 B and headers ≤ 32
  lines / 8 KiB → 431 with connection closed; request timeout 2 s → 408;
  idle timeout 5 s → silent close; ≤ 16 concurrent connections (excess
  accepted then immediately closed). Responses built with `snprintf` into
  a fixed per-connection header buffer; no per-request allocation beyond
  the bounded connection array; route table is a fixed vector of
  path → handler entries. Live-connection count backed by an atomic
  counter so the test harness can observe it from another thread without
  racing the loop.
- Tests: `observability/tests/http_server_test.cpp`, 15 cases in both
  frameworks using a raw POSIX socket client (no third-party HTTP lib):
  `ParseListenAddress` (config-boundary address validation);
  `GetRoundtrip200`; `HeadReturnsHeadersWithoutBody`; `UnknownPath404`;
  `PostAndPut405`; `OversizedRequestLine431AndClose`;
  `HeaderOverflow431AndClose` (line-count and byte-count overflow);
  `MalformedRequests400` (bad request line, invalid target,
  `Content-Length` body); `HandlerFailure500`; `ResponseBodyOverBound500`;
  `RequestTimeout408`; `IdleTimeoutClosesConnection`;
  `KeepAliveServesSequentialRequests`;
  `ConnectionCapAcceptsAndClosesExcess` (16 held, excess accepted-then-
  closed, slot released after close);
  `NoFdLeakAcrossCyclesAndCleanShutdown` (N sequential connect/close
  leaves fd count unchanged; shutdown closes the listening socket, later
  connect → `ECONNREFUSED`).
- GoogleTest 160/160 (145 pre-existing + 15 new); Catch2 160/160.
- ASan+UBSan (GoogleTest): 143/143, zero reports — HTTP socket tests
  active under sanitizers (no skip-pass).
- TSan (Catch2) under `setarch --addr-no-randomize`,
  `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`: 143/143, zero
  race reports. Note: the gcc-13 TSan runtime intermittently aborts at
  process startup on the reference host's kernel 7.0 with
  `FATAL: ThreadSanitizer: unexpected memory mapping` (affects test
  discovery of pre-existing targets too, e.g. `supervisor_test`);
  `setarch --addr-no-randomize` is the established workaround from
  T-0033 and remains required for local TSan builds.
- No monitor/supervisor process changes; no shared-memory code touched:
  `git status` shows zero changes under `shared-memory/`; region layout
  untouched (v4).
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0034 Result

- Implementation: `--event-log <path>` flag on the `worker`, `monitor`, and
  `supervisor` subcommands (default off; absent → Phase 5 byte-identical
  output). New supervisor vocabulary + dual-write sink in
  `supervisor/include/safety_crit/supervisor/witness_events.hpp` (+
  `src/witness_events.cpp`): `failover_started`, `failover_recovered`
  (carries `latency_ms`), `ring_degraded`, `stall_recovered`,
  `stall_escalated`, `shutdown_summary`; every Phase 5 plain witness `printf`
  site in `supervisor.cpp` is now null-sink gated
  (`if (witness != nullptr) witness->emit(...) else printf(...)`), emitting
  the JSON envelope on stdout plus an event-log record (component
  "supervisor"). New worker lifecycle vocabulary in
  `workers/include/safety_crit/workers/lifecycle_events.hpp` (+
  `src/lifecycle_events.cpp`): `worker_started`, `worker_stopped`
  (reason `completed`/`stop_signal`/`ownership_lost`),
  `worker_deadline_overrun` — emitted strictly between ticks in
  `worker_entry.cpp` (never in the ring hot path), log-file-only (no stdout
  change, per DEC-0009 #6); a lost-ownership push closure sets an atomic
  flag surfaced as the `ownership_lost` stop reason. New `format_stdout_event`
  + `now_unix_ns` helpers in `observability/` render the stdout envelope
  (record content without `schema`/`seq`). Monitor report/alerts mirror to
  the event log via `EventLogWriter` (component "monitor") with the stdout
  JSON stream unchanged. Verbatim monitor-line forwarding and exit codes
  0/3/4 unchanged.
- Consumer migration (lockstep): all Compose scenario greps moved from
  Phase 5 plain-text witnesses to JSON field/event matches
  (`s1_crash` `latency_ms`, `s2_stall` `stall_recovered`, `s3_corrupt`
  `shutdown_summary`+`a_corruptions`, `s5_double_fault` `ring_degraded`,
  `s1_replay` event categories + shutdown-summary parser on
  `shutdown_summary`/`state`/records/corruptions). `docker-compose.yml`
  supervisor passes `--event-log /run/safety-critical-ha/events.jsonl`.
  `scripts/phase4-failover-timing.sh` and `failover-smoke.sh` verified
  needing no change (run without the flag / already JSON).
- Tests (both frameworks): `EventLog.StdoutFormatPinsEnvelopeWithoutSeq`
  (envelope shape, no `seq`/`schema`, extra-field rejection rules);
  `Supervisor.WitnessFieldBuildersPinVocabulary` (pinned field sets/orders,
  booleans as 0/1); `Supervisor.EventLogCapturesWitnessRecords` (fork,
  `--event-log`: stdout carries supervisor JSON, plain witnesses replaced,
  consolidated log has gap-free supervisor seq ending at `shutdown_summary`,
  monitor records co-located); `Supervisor.NoJsonWitnessesWithoutEventLogFlag`
  (negative AC: flag absent → no supervisor JSON anywhere on stdout);
  `workers_integration.EventLogRecordsHotLifecycle` +
  `EventLogRecordsStandbyStopSignal` (fork → event log has continuous
  `worker_started`/`worker_stopped` seq with pinned reason/role/ring fields;
  standby omits `ring`); `monitors_integration.EventLogMirrorsStdoutStream`
  (fork → monitor log mirrors stdout JSON with gap-free seq, report closes
  the stream, stdout shape unchanged). Fork-based cases plain-build only
  (established idiom).
- GoogleTest 167/167; Catch2 167/167.
- ASan+UBSan (GoogleTest): 147/147, zero reports.
- TSan (Catch2) under `setarch --addr-no-randomize`,
  `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`: 147/147, zero
  race reports.
- Replay comparison: S1-R parser migrated to `shutdown_summary` JSON record;
  determinism semantics (DEC-0012 #8) unchanged — validated at phase
  integration (T-0039) against the running stack.
- No shared-memory code touched: `git status` shows zero changes under
  `shared-memory/`; region layout untouched (v4).
- Docker build + Compose smoke + scenarios against the new format: run in
  the phase integration task (T-0039); no smoke regression introduced in
  the native matrix.
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0040 Result

- Implementation: `observability/include/safety_crit/observability/metrics.hpp`
  + `observability/src/metrics.cpp` (added to library
  `safety_crit::observability`). Pure in-memory layer, no region/event-log/
  socket dependencies. `MetricsRegistry`: `declare_family` (name validated
  `[a-zA-Z_:][a-zA-Z0-9_:]*`, label key validated without colons, duplicate
  → `EEXIST`); `value()` returns a stable `std::atomic<double>&` created at 0
  on first touch (samples heap-allocated, never removed — scrape-path loads
  are atomic, structure mutation is owner-thread only); `set`/`add` relaxed
  atomic store/fetch_add; `render_prometheus` emits `# HELP`/`# TYPE` per
  family with families sorted by name and samples sorted by label value
  (byte-identical re-renders); label-value escaping (`\\`, `\"`, `\n`,
  backslash first); values via `snprintf` (`%lld` for integral < 1e15,
  `%.10g` otherwise, `NaN`/`+Inf`/`-Inf`). Error idiom:
  `bool(..., std::error_code&)` with `EINVAL`/`EEXIST`/`ENOENT`.
- Tests: `observability/tests/metrics_test.cpp`, 11 cases in both
  frameworks: empty registry → empty string; type/help rendering; labeled
  counter samples; label escaping (incl. explicit escape-helper case);
  deterministic ordering (declared out of order, byte-stable across
  renders, unlabeled-set-on-labeled rejected without creating a sample);
  declared family without samples renders comment lines only; declaration
  validation (invalid/duplicate names, colon label key); lookup/label
  contract (ENOENT/EINVAL, stable reference identity); value formatting
  (int/float/large/NaN/Inf); atomic update visible on next render;
  concurrent scrape-vs-update (updater thread stores through the atomic
  handle, main thread renders and asserts monotonic counter — TSan leg).
- GoogleTest 178/178 (167 pre-existing + 11 new); Catch2 178/178.
- ASan+UBSan (GoogleTest): 158/158, zero reports.
- TSan (GoogleTest) under `setarch --addr-no-randomize`: 158/158, zero race
  reports (concurrent scrape-vs-update case active under TSan).
- Clang verification (pinned clang-14 container, `clang-verify` preset):
  178/178.
- No shared-memory code touched: `git status` shows zero changes under
  `shared-memory/`; region layout untouched (v4).
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0041 Result

- Implementation: `observability/include/safety_crit/observability/region_metrics.hpp`
  + `observability/src/region_metrics.cpp` (library
  `safety_crit::observability`, now linking `safety_crit::shared_memory`).
  `declare_region_metrics` registers the DEC-0014 §8 region families
  (`worker_status{worker}` gauge, `ring_buffer_sequence{worker}` gauge,
  `ring_corruptions_total{worker}` counter, `ownership_epoch{ring}` gauge;
  labels = decimal logical indices). `collect_region_metrics` samples with
  one atomic load per value via the sanctioned observer surface only
  (`WorkerStatusCell.status` acquire load, `RingBuffer::pushed()` /
  `corruption_count()`, `read_ownership()`); no CAS/store/pop anywhere
  (DEC-0014 §11). `worker_status` mapping exposed as a standalone pure
  function: IDLE/zero=0, RUNNING=1, CRASHED=2, RECOVERING=3, DEGRADED=4,
  precedence DEGRADED > RECOVERING > CRASHED > RUNNING > IDLE, kOverrun not
  a state. `read_ownership` failure (unassigned/mid-transfer ring) keeps the
  last reported epoch instead of fabricating a value.
- Tests: `observability/tests/region_metrics_test.cpp`, 7 cases in both
  frameworks: mapping pinned (all flags, all precedence pairs, overrun,
  zero); declaration (4 families, EEXIST on duplicate); initialized region
  renders per-worker zeros + epoch 2 (region init hands ring i to physical
  i); fixture state changes (status flip, push, bad-CRC push consumed via
  the skip-and-count path, ownership transfer → epoch 4) visible on next
  collect; DEGRADED-beats-RUNNING through the real status cell; read-only
  proof (double collect → `verify_identity()` true, `integrity_word ==
  compute_region_integrity()`, `verify_worker_ring()` true, byte-identical
  renders); undeclared registry → ENOENT without partial mutation.
- GoogleTest 185/185 (178 pre-existing + 7 new); Catch2 185/185.
- ASan+UBSan (GoogleTest): 165/165, zero reports.
- TSan (GoogleTest) under `setarch --addr-no-randomize`: 165/165, zero race
  reports.
- Clang verification (pinned clang-14 container, `clang-verify` preset):
  185/185.
- No shared-memory code touched: `git status` shows zero changes under
  `shared-memory/`; region layout untouched (v4).
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0042 Result — Event-Log Metrics Collector (2026-09-26)

- Implemented `declare_event_metrics` and `EventMetricsCollector` in
  `observability/include/safety_crit/observability/event_metrics.hpp` and
  `observability/src/event_metrics.cpp`. Declares the six DEC-0014 §8
  event-derived families: `perturbation_count_total{type}` (counter),
  `failover_duration_seconds` (gauge), `data_loss_events_total` (counter),
  `event_log_gaps_total{component}` (counter), `observability_up` (gauge),
  `observability_uptime_seconds` (gauge).
- `poll(reader, registry, ec)` drains records to `kEnd`/`kIncomplete`
  (torn tail never advances counters past the watermark; the consumer
  resumes via the documented `reader.seek(reader.watermark())` tail-follow
  idiom). Perturbation records (`component=perturb`) are counted by their
  `category` extra (crash/stall/corrupt/double-fault; unknown categories and
  records without one are ignored, never an error). `failover_recovered`
  records set the latency gauge from `latency_ms/1000` (last write wins).
  Gap counts are `set` (not added) from the reader's cumulative continuity
  state, so re-polls and watermark resumes never double count.
  `observability_up` becomes 1 from the first poll; uptime derives from an
  injectable nanosecond clock (default `now_unix_ns`).
- `data_loss_events_total` is structurally decoupled: `poll` never touches
  it; the embedding process reports it via `observe_data_loss(n)` (supervisor
  drain-witness gap source, wired at daemon assembly in T-0038).
- JSON extra extraction (`find_json_string_field` / `find_json_number_field`)
  is hand-rolled to keep the DEC-0014 §6 dependency allowlist; matches the
  schema v1 flat-field format only.
- Added a read-only `states()` accessor on `EventLogReader` (continuity map)
  for gap enumeration; no behavior change to the log layer.
- Tests: `observability/tests/event_metrics_test.cpp`, 7 cases in both
  frameworks: declaration (6 families, EEXIST on duplicate); replayed
  synthetic fixtures yield exact counters (crash=2/stall=1/corrupt=1/
  double-fault=1, bogus category ignored, failover gauge 1.234, up=1);
  failover gauge last-write-wins (0.25); injected log gap (seq 1→3) yields
  `event_log_gaps_total{component="supervisor"} 1` and leaves
  `data_loss_events_total` sample-free until `observe_data_loss(3)` moves
  only that counter; torn trailing line not counted until completed and
  re-polled from the watermark (counted exactly once, no double count);
  uptime from an injected clock (0 then 5); undeclared registry → ENOENT
  with no family created.
- GoogleTest 192/192 (185 pre-existing + 7 new); Catch2 192/192.
- ASan+UBSan (GoogleTest): 172/172, zero reports.
- TSan (GoogleTest) under `setarch --addr-no-randomize`: 172/172, zero race
  reports.
- Clang verification (pinned clang-14 container, `clang-verify` preset):
  192/192.
- No shared-memory code touched: `git status` shows zero changes under
  `shared-memory/`; region layout untouched (v4).
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0037 Result — Health Report and Status Endpoints (2026-09-26)

- Implemented the daemon HTTP surface (DEC-0014 §7) in
  `observability/include/safety_crit/observability/health_report.hpp` and
  `observability/src/health_report.cpp` on the T-0035 server core:
  `register_daemon_routes(server, ctx, ec)` installs `GET /health`,
  `GET /metrics`, `GET /status`; response shapes documented in the header.
- `EndpointContext` borrows `const SharedRegion*` (T-0041 sanctioned
  observer surface only — `verify_identity`, `read_ownership`, atomics),
  `MetricsRegistry*`, optional `const EventLogReader*`, `start_ns`, and an
  injectable nanosecond clock (default `now_unix_ns`); per-route scratch
  buffers keep body views alive until the send completes (single-threaded
  server). Unknown paths 404 (T-0035 core behavior).
- `/health`: always 200 while serving; `status` is `ok` iff region identity
  verifies, every logical ring is owned (`read_ownership` succeeds, odd
  epoch, valid owner), and `data_loss_events_total == 0`, else `degraded`.
  Carries `ts`, `uptime_ms`, `data_loss_events_total`, per-worker
  `state` (T-0041 `worker_status_metric_value` precedence mapping) +
  `last_sequence`, and per-ring `physical_owner`/`epoch` (unreadable
  ownership → owner 4294967295, epoch 0). Daemon contract:
  `declare_event_metrics` (T-0042) first — a missing family renders 500;
  a declared family without samples yet reads as 0. Region identity/
  integrity transient skew is deliberately NOT folded into `degraded`
  beyond ownership/loss (T1.3 live-traffic contract); `/status` reports
  it fully.
- `/metrics`: registry `render_prometheus` at text/plain; `/status`: JSON
  snapshot of region version, `verify_identity`, integrity word valid,
  `global_seq`, per-ring `pushed`/`corruption_count` + ownership, event-log
  `available`/watermark/per-component `expected_seq`/`gaps`/`missed`,
  daemon uptime. Booleans render as 0/1 integers (supervisor witness
  idiom).
- Added a read-only `MetricsRegistry::get(family, label, value, ec)`
  accessor (`metrics.hpp/cpp`): ENOENT unknown family/sample, EINVAL label
  mismatch; relaxed atomic load, no mutation.
- Tests: `observability/tests/health_report_test.cpp`, 9 cases in both
  frameworks: `/health` ok-shape (key set + values pinned, hand-rolled
  bracket-balance/field checks, no JSON library); unowned ring → degraded
  (epoch 0 renders unreadable); `data_loss_events_total` 3 and nonzero-
  reset paths → degraded/ok; corrupted region identity → degraded;
  missing data-loss family / null region / null registry → render false;
  `/status` snapshot fields incl. event-log watermarks and gap counts;
  `get()` accessor semantics; loopback end-to-end GETs (Content-Type,
  200/404/405); mid-run region loss flips `/health` degraded while the
  server keeps serving (never exits).
- GoogleTest 201/201 (192 pre-existing + 9 new); Catch2 201/201.
- ASan+UBSan (GoogleTest): 181/181, zero reports.
- TSan (GoogleTest) under `setarch --addr-no-randomize`: 181/181, zero
  race reports.
- Clang verification (pinned clang-14 container, `clang-verify` preset):
  201/201.
- No shared-memory code touched: `git status` shows zero changes under
  `shared-memory/`; region layout untouched (v4).
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0038 Result — Daemon CLI, Producer Wiring, and Observability Compose Topology (2026-09-27)

- Implemented the daemon entry path: `observability/include/safety_crit/
  observability/daemon.hpp` + `observability/src/daemon.cpp` and the
  `observability` subcommand in `app/src/main.cpp` (DEC-0014 §9).
  `parse_daemon_args` accepts `--listen HOST:PORT`, `--region NAME`,
  `--event-log PATH`, `--ticks N` (defaults `127.0.0.1:8080`,
  `/safety_crit_region`, poll 250 ms). `--once` prints one status JSON
  snapshot and exits; the server path attaches with a bounded retry loop
  (daemon may start before the supervisor), registers `/health` `/metrics`
  `/status` (T-0037), samples region + event-log every poll tick via the
  T-0035 tick hook, and exits 0 on SIGTERM/SIGINT via the server stop flag.
- Producer wiring for the two T-0042 sources:
  - Supervisor drain-witness gaps (`supervisor/src/supervisor.cpp`,
    `witness_events.*`): `OutputWitness.gap_lost = sequence - next_sequence`
    and a `data_loss_observed` witness event (`"ring":N,"lost":M`); the
    daemon scans its own supervisor event-log records for that event and
    calls `EventMetricsCollector::observe_data_loss(lost)`, which moves
    `data_loss_events_total` and flips `/health` to `degraded` (CORE
    property #1 exposure end-to-end).
  - Perturb harness `--event-log PATH` (`perturb/src/harness.cpp`):
    appends `component="perturb"` `perturbation_applied` records with
    `"category"`/`"target"` extras for the four pinned categories
    (crash/stall/corrupt/double-fault); supervisor-targeted categories are
    skipped (the supervisor already witnesses those).
  - Daemon start baselines `data_loss_events_total` to 0 so the Prometheus
    exposition always carries the sample line (a declared-but-unset family
    renders only HELP/TYPE and breaks naive `^data_loss_events_total`
    scrapes).
- Defects found by live/integration testing and fixed:
  - `HttpServer` stop flag was `std::sig_atomic_t`, raced between
    `request_stop()`/signal handler and `run()` across threads (TSan
    report). Now `std::atomic<bool>` (relaxed) everywhere
    (`observability/src/http_server.cpp`).
  - `EventLogReader::read_next` latched EOF: after the first end-of-file
    it never re-read, so the Compose daemon never saw records appended
    after startup (perturb counts, data-loss events). EOF is now
    per-call, never latched (tail-follow contract); regression test
    `EventLog.ReaderTailsRecordsAppendedAfterEof`.
  - `HttpServer.TickHookFiresWhileServing` installed the tick hook while
    the server thread was running, violating the class's single-threaded
    configuration contract (TSan report); hook now installed before
    `start()`.
- Compose (T-0021 extension): `docker-compose.yml` gains an `observability`
  service running `observability --listen 0.0.0.0:8080 --region
  /safety_crit_region --event-log /run/safety-critical-ha/events.jsonl`,
  joining the supervisor IPC namespace via `ipc: service:supervisor`
  (supervisor declares `ipc: shareable`, required for the donor),
  `depends_on` supervisor `service_healthy`, healthcheck
  `curl -fsS http://127.0.0.1:8080/health`, port published as
  `${OBSERVABILITY_PORT:-8080}:8080`. A shared named volume
  `ha-runtime:/run/safety-critical-ha` (supervisor + observability +
  perturb) lets the daemon tail the supervisor event log; pidfile writes
  are atomic (`O_TRUNC` + rename) so co-location is safe. Scenario overlay
  and `common.sh` pass `--event-log` to every `perturb` invocation.
- `containers/compose/failover-smoke.sh` extended: waits for daemon
  `/health` ok, asserts `data_loss_events_total` present in the health
  JSON and `worker_status` + `data_loss_events_total` samples in
  `/metrics`.
- Tests: 16 new cases in both frameworks — `daemon_test.cpp` (10: arg
  parsing, `--once` snapshot shape, region-loss tracking, server serve +
  degrade flip, data-loss witness counting, event-log tail counting, stop
  semantics), `HttpServer.TickHookFiresWhileServing`,
  `EventLog.ReaderTailsRecordsAppendedAfterEof`, 3 `Perturb` event-log
  cases, `Supervisor.DrainGapSetsGapLostForDataLossEvent`.
- GoogleTest 217/217 (201 pre-existing + 16 new); Catch2 217/217.
- ASan+UBSan (GoogleTest): 195/195, zero reports.
- TSan (GoogleTest) under `setarch --addr-no-randomize`: 195/195, zero
  race reports (after the two fixes above).
- Clang verification (pinned clang-14 container, `clang-verify` preset):
  217/217.
- Docker/Compose: `docker compose config --quiet` clean (base + perturb
  overlay); live stack `up --wait` green with supervisor and observability
  both healthy; S1 crash scenario PASS (81 ms recovery) with the daemon
  live-counting `perturbation_count_total{type="crash"} 1` from the shared
  event log; `failover-smoke.sh` PASS end-to-end including the new daemon
  assertions.
- No shared-memory code touched: `git status` shows zero changes under
  `shared-memory/`; region layout untouched (v4).
- Hosted CI: pending (recorded at phase integration if not per task).

## T-0039 Result (Phase 6 exit gate) (2026-09-28)

Integration pass over T-0033–T-0038 (incl. T-0040–T-0042) against gates
G6.1–G6.5 and the Phase 6 Exit line of the
[plan](../phases/PHASE_6_OBSERVABILITY.md). The dedicated CI runner is the
reproducible reference host for the bounded-recovery observation (precedent:
Phase 5 G5.4 decision).

- **G6.1 — event log library green**: `EventLog` suite green in GoogleTest
  and Catch2 and under ASan+UBSan and TSan (all four sanitizer legs below);
  injected-gap detection, fork-based concurrent-writer atomicity, restart
  seq recovery, and fsync policy pinned by the T-0033 tests throughout.
- **G6.2 — logging migration green**: supervisor/worker JSON wire-contract
  format tests green in both frameworks; `docker-compose-smoke` and
  S1/S2/S3/S5/S6/S1-R green against the migrated format (hosted run +
  local sweep below); replay determinism preserved (S1-R PASS).
- **G6.3 — HTTP server bounded**: malformed-request/timeout/connection-cap
  and fd-leak tests green; TSan clean with socket tests active in both
  frameworks (the T-0038 `g_stop` atomic fix removed the last reported
  race).
- **G6.4 — metrics correctness**: `data_loss_events_total == 0` captured
  from the live daemon at the end of **every** local scenario run (S1, S2,
  S3, S5, S6 all PASS); `event_log_gaps_total{component=...}` 0 for all
  components in every run (independently sourced from reader continuity,
  never from data loss); `failover_duration_seconds` observed via the
  S1 scenario budget assertion — 22 ms local (budget <100 ms), CI green
  5/5 consecutive runs per scenario (same budget enforced under `bash -e`).
- **G6.5 — Compose**: `observability` service healthy; supervisor
  healthcheck command unchanged (the T-0038 diff adds `ipc: shareable` and
  the `ha-runtime` volume only); daemon port now published on **loopback
  only** (`127.0.0.1:${OBSERVABILITY_PORT:-8080}:8080`, verified via
  `docker port` → `127.0.0.1:18080`); metrics/health smoke (extended
  `failover-smoke.sh`: daemon `/health` ok, `data_loss_events_total` in
  health JSON, `worker_status` + `data_loss_events_total` in `/metrics`)
  green **5× consecutively** locally (5/5); S6 asserts the daemon keeps
  serving during supervisor loss (HTTP 200, container never restarts) and
  `/health` returns ok after the policy rebuild (re-attach path,
  `daemon.cpp` `probe_region`).
- **Local verification matrix (this host, 2026-09-28)**: GoogleTest
  217/217; Catch2 217/217; ASan+UBSan (GoogleTest) 195/195 and (Catch2)
  195/195, zero reports; TSan (GoogleTest) 195/195 and (Catch2) 195/195
  under `setarch --addr-no-randomize`, zero races; clang-verify (pinned
  clang-14 container) 217/217; `docker build --pull` + `--version` PASS;
  both compose configs validate; scenario sweep S1 (22 ms)/S2/S3/S5/S6 +
  S1-R all PASS with the daemon counters above.
- **Zero shared-memory changes for the whole phase**:
  `git diff 0def7b8..HEAD -- shared-memory/` is empty (0 lines) at the exit
  commit; region layout stays v4 (DEC-0014 #11).
- **Phase 6 Exit** — hosted CI
  [run 36422705547](https://github.com/tslator/safety-critical-high-availability/actions/runs/36422705547)
  on commit `0cd4afb` (2026-09-28): all ten jobs green — GoogleTest
  217/217, Catch2 217/217, ASan+UBSan ×2 195/195, TSan ×2 195/195,
  clang-verify 217/217, Docker build, AI-guidance drift check, and the
  Compose job (base failover smoke + S1/S2/S3/S5/S6 ×5 fresh-stack each +
  S1 deterministic replay). The exit commit adds only this evidence, the
  status updates, the loopback port binding, and the S6 daemon-serving
  assertions; the follow-up run on the exit commit is recorded below when
  green.

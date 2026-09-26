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

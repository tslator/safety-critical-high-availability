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

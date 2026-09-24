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

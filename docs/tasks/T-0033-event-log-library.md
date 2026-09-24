# T-0033: Sequenced Append-Only Event Log Library

- Status: Complete
- Owner: AI agent (opencode)
- Priority: High
- Depends on: Phase 5 exit
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Implement the certification-grade event log per DEC-0014 §3 in a new
`safety_crit::observability` static library (`observability/`): writer and
reader for the schema v1 JSON-lines event log.

Schema v1 (one JSON object per line, ≤ 4096 bytes):
`{"schema":1,"ts":<unix-ns>,"level":"info|warn|error","component":"...","seq":<uint64>,"event":"...",<extra flat fields>}`

Writer contract: `O_APPEND | O_CREAT | O_CLOEXEC`; one `write()` per record;
oversized record is an error (never split); per-component monotonic `seq`
(keyed by component + worker index) recovered by tail-scan at open; `fsync`
on `level >= warn`; clock injectable (steady/system clock in production,
fake clock in tests). Reader contract: sequential scan, per-component
sequence continuity validation, torn-trailing-line tolerance, watermark
(offset) tracking for tail-follow use. `snprintf` formatting idiom; error
handling via `bool fn(..., T& out, std::error_code& ec)`.

## Acceptance Criteria

- Schema documented in `observability/include/safety_crit/observability/event_log.hpp`;
  versioned via `"schema":1` header field per record.
- Writer/reader unit tests in both frameworks covering: happy path;
  oversized-record rejection; seq recovery after simulated restart (reopen);
  gap detection (injected missing seq); torn trailing line; empty file;
  level→fsync policy exercised (fsync count observable via injected policy).
- Concurrent-writer atomicity test: ≥ 4 processes × ≥ 100 interleaved records
  → file parses line-by-line with zero mixed records and gap-free
  per-component sequences (fork-based; plain build only per G1.4/G2.3
  precedent).
- Both frameworks green; ASan+UBSan and TSan green (library is single-thread
  per process; TSan leg is a confirmation).
- No shared-memory code touched (`git diff` shows zero changes under
  `shared-memory/`).

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

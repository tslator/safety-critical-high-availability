# T-0035: Bounded HTTP/1.1 Server Core

- Status: Complete
- Owner: AI agent (opencode)
- Priority: High
- Depends on: Phase 5 exit
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

Implement a hand-rolled, hard-bounded HTTP/1.1 server core in
`safety_crit::observability` per DEC-0014 §5: single-threaded `poll()` loop,
accept + per-connection state machine, GET (HEAD with body omitted), route
registration (path → handler producing a fixed-buffer response), status
codes 200/400/404/405/408/431/500, keep-alive.

Bounds: request line ≤ 512 B, headers ≤ 32 lines / 8 KiB total, request
timeout 2 s, idle timeout 5 s, ≤ 16 concurrent connections (excess: accept
and close immediately), default bind `127.0.0.1:8080` via `--listen`-style
address config validated at the config boundary. No threads, no allocations
per request beyond bounded containers; responses built with `snprintf`.

## Acceptance Criteria

- API documented in `observability/include/safety_crit/observability/http_server.hpp`.
- Loopback tests (raw POSIX socket client, no third-party HTTP lib) in both
  frameworks covering: GET 200 roundtrip; HEAD returns headers without body;
  unknown path 404; POST/PUT 405; oversized request line / header overflow →
  400/431 and connection closed; idle timeout closes idle connection;
  keep-alive serves sequential requests on one connection; connection-cap
  behavior.
- fd-leak check: N sequential connects/closes leave fd count unchanged
  (observable in test); server shutdown (`std::stop_token` + `sig_atomic_t`
  pattern, SIGTERM/SIGINT) closes listening socket cleanly.
- ASan and TSan legs clean with socket tests active (no skip-pass needed —
  single-threaded loop).
- No changes to monitor/supervisor processes in this task.

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

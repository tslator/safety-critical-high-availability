# T-0028: Phase 5 Perturbation Harness Library and CLI

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: T-0023, T-0024, T-0025, T-0026, T-0027
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Implement a C++20 `perturb::` library and a `safety-critical-ha perturb`
subcommand. The harness issues signals against target PIDs and emits
JSON-lines records to a caller-supplied stream. Per DEC-0012 #7 the API
uses the Phase 2 error idiom (`bool fn(..., std::error_code& ec)` or a
small `perturb::Result<T>` alias); `std::expected` is not used.

API surface (minimum):
- `crash(pid_t target, std::error_code& ec)` — sends SIGSEGV (or SIGKILL if
  SIGSEGV cannot be delivered).
- `stall(pid_t target, std::error_code& ec)` — sends SIGSTOP.
- `recover_stall(pid_t target, std::error_code& ec)` — sends SIGCONT.
- `corrupt_next_slot(pid_t target, std::error_code& ec)` — sends SIGUSR2
  (worker process must have opted in via `--corrupt-hook` per T-0027).
- `double_fault(pid_t a, pid_t b, std::error_code& ec)` — sends SIGKILL to
  both in a tight window.
- `kill_supervisor(pid_t target, std::error_code& ec)` — sends SIGKILL to
  the container's PID 1 supervisor process.

Every call records `{ts_ns, category, target_pid, params}` to the caller's
stream.

## Acceptance Criteria

- New `perturb/` module: `perturb/include/safety_crit/perturb/*.hpp`,
  `perturb/src/*.cpp`, `perturb/tests/`. Root `CMakeLists.txt` adds it.
- `safety-critical-ha perturb <category> --target <pid> [--target2 <pid>]
  [--out <path>]` CLI subcommand wired through `app/src/main.cpp` with
  hand-rolled parsing matching the existing pattern.
- JSON-lines schema documented in
  `perturb/include/safety_crit/perturb/replay_log.hpp`.
- Unit tests: one per category (verifies signal sent + log line emitted);
  argument-validation tests; unknown-category test.
- Both frameworks green; ASan+UBSan and TSan green in applicable configs.
- Never-for-production note in `perturb/README` matching the DEC-0007
  `destroy()` pattern.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

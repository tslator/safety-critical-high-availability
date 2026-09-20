# T-0013: Phase 3 T3.2 — Pidfile Liveness, Poll Loop, JSON Alerts, Monitor CLI

- Status: In Review
- Owner: AI agent (opencode)
- Priority: High
- Depends on: T-0012
- Phase: Phase 3
- Phase label: T3.2
- Related decision: [DEC-0010](../decisions/0010-phase3-monitor-daemon.md)

> Canonical narrative and gate live in
> [Phase 3 plan T3.2](../phases/PHASE_3_MONITOR_DAEMON.md).

## Scope

Make crash detection real and ship the monitor process: the worker pidfile
contract (writer side), the monitor's liveness reader, the poll loop with
stop-token/sig_atomic_t cancellation, structured JSON alert/report lines on
stdout, and the `safety-critical-ha monitor` subcommand.

## Deliverables

- `workers/pidfile.hpp/.cpp` (contract path, atomic publish, clean-exit
  removal; fatal-on-write-failure wiring in `run_worker`; worker
  `--pid-dir`).
- `monitors/pidfile_liveness.hpp/.cpp` (parse, range-check, `kill(pid, 0)`
  liveness; stale/malformed = dead).
- `monitors/monitor_loop.hpp` (`run_monitor_loop`, `MonitorStats`,
  `interval_pacer`); `monitors/json_lines.hpp/.cpp` (deviation #1:
  snprintf, exact shapes unit-pinned); `monitors/monitor_entry.hpp/.cpp`
  (`run_monitor`); `monitor` subcommand in `app/`.
- Tests: `monitors_loop_test`, `WorkersPidfile.ContractPathWriteRemove`.

## Acceptance Criteria

- Loop accounting exact (polls, pacing, stop latency <= one poll);
  stall/recovery driven by real ring counters; crash driven by the injected
  liveness verdict; clean exit classified idle, never crashed.
- JSON lines exact-match the published shapes; report carries per-kind
  counts and final state snapshot.
- Full matrix (both frameworks, ASan+UBSan, TSan) green; zero warnings;
  `--version` and existing `worker` behavior untouched.

## Validation

Gate G3.2 (see phase plan). Record in `NOTES.md`.

## Completion Notes

Implemented 2026-09-20. Local G3.2 green: 79/79 GoogleTest and Catch2,
ASan+UBSan clean, TSan pass (setarch harness). CLI witness: `monitor
--polls 3` emits report line; threshold < interval rejected rc=2;
`--version` unchanged. Deviation #1 recorded in the phase plan
(std::format unavailable on clang-14/libstdc++-12; snprintf used). Hosted
CI pending before closure.

# T-0010: Phase 2 T2.2 — Work Loop, Signals, Deadline Monitoring

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: [T-0009](T-0009-worker-core.md)
- Phase: Phase 2
- Phase label: T2.2
- Related decision: [DEC-0009](../decisions/0009-phase2-worker-runtime.md)

> Canonical narrative and gate live in
> [Phase 2 plan T2.2](../phases/PHASE_2_WORKERS.md).

## Scope

Implement the worker work loop (C++20, stop-token cancellation), signal
wiring (SIGTERM/SIGINT stop; SIGUSR1 test-only crash hook), and per-tick CPU
budget/deadline monitoring with the `OVERRUN` status bit.

## Deliverables

- `work_loop.hpp`: bounded-iteration tick loop, template clock injection,
  budget check → `OVERRUN` + overrun count, `sig_atomic_t`-backed stop
  consumed between ticks, guarded `__builtin_ia32_pause` backoff.
- Signal module (install + flag-store handler only; no non-trivial work in
  handlers).
- `workers_loop_test`: exact tick accounting, clean mid-run stop,
  deterministic overrun witness under a fake clock.

## Acceptance Criteria

- Full matrix (both frameworks × plain/ASan+UBSan/TSan) green; no sanitizer
  reports from loop or signal tests.
- Cancellation: stop observed within one tick; no partial committed outputs
  after stop.
- Overrun detection deterministic with injected clock.

## Validation

Gate G2.2 (see phase plan). Record in `NOTES.md`.

## Completion Notes

(To be recorded at completion.)

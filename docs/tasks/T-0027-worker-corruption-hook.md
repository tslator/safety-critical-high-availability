# T-0027: Phase 5 Worker SIGUSR2 Memory-Corruption Hook

- Status: Planned
- Owner: Unassigned
- Priority: High
- Depends on: Phase 4 exit (no upstream task)
- Phase: Phase 5
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)

## Scope

Add a worker-process debug hook that corrupts the CRC of the next slot the
worker pushes, so memory-corruption scenarios (S3) can exercise the
skip-and-count detection path end-to-end without introducing a second
writer to shared memory.

Per DEC-0012 #5: SIGUSR2 sets a `sig_atomic_t` "poison next slot" flag; the
worker's push path — outside the ring hot path, between ticks — corrupts the
CRC of its own next slot when the flag is set. The hook is opt-in (CLI flag
`--corrupt-hook` or env var `SAFETY_CRIT_CORRUPT_HOOK=1`); the production
default does not install the handler, so DEC-0009 #4 hot-path policy remains
enforced by the same opt-in discipline used for DEC-0007 `destroy()`.

## Acceptance Criteria

- `install_corruption_hook(SignalState&)` in `workers/signals.hpp` registers
  SIGUSR2 only when opt-in is set; production default installs nothing extra.
- Push path checks the flag between ticks (not inside `run_work_loop`'s
  per-tick hot path); when set, writes the slot then flips the CRC field to
  an obviously wrong value. Flag cleared after the poison push.
- Skip-and-count path (`shared-memory/crc_integrity.hpp`) observes one
  increment on the ring's `corruption_count()` per poisoned slot.
- Unit tests: hook not installed by default; hook installed via CLI flag;
  SIGUSR2 causes exactly one corrupted push then normal operation resumes.
- Integration test: hot worker + monitor + supervisor; SIGUSR2 to worker,
  supervisor's output witness reports at least one corruption, no CRC
  assertion failures, monitor continues.
- Both frameworks green; ASan+UBSan and TSan green in applicable configs.
- Negative test confirms production default (no CLI flag / env var) does
  not install the SIGUSR2 handler.

## Evidence

Record implementation and validation in [Phase 5 evidence](../evidence/phase-5.md).

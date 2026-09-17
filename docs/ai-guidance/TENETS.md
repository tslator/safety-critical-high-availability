# Tenets

This is the full detail behind the [`CORE.md`](CORE.md) core card. `CORE.md`
is a distilled, always-injected subset of this file; if they ever disagree,
update `CORE.md` and treat this file as the source that was distilled.

## Key Safety Properties

Reproduced from [`SAFETY_CRITICAL_HA_PLAN.md` §7](../../SAFETY_CRITICAL_HA_PLAN.md#7-key-safety-properties-to-prove),
each paired with how it is proved today or how it will be proved once the
relevant phase is implemented:

| Property | How it is proved | Current gate |
|---|---|---|
| No data loss | CRC per ring-buffer slot; verify sequence continuity after every fault | `native-gtest`, `native-catch2` CI jobs |
| No duplicate processing | Monotonic sequence numbers; each slot consumed exactly once (CAS semantics) | `native-gtest`, `native-catch2` CI jobs |
| No deadlock | Lock-free data structures; no mutexes in hot path | `sanitizers` CI job (TSan leg) |
| No priority inversion | `SCHED_FIFO` with explicit priority; monitor < supervisor < workers | Planned (Phase 3/4) |
| Bounded recovery time | Measure time from fault injection to full recovery; must be under SLA | Planned (Phase 5) |
| Graceful degradation | With N/2 workers down, system continues at reduced throughput | Planned (Phase 5) |
| Deterministic replay | Recorded perturbations produce identical outcomes on replay | Planned (Phase 5) |

Undefined-behavior freedom in hot-path/lock-free code is a standing tenet
across all of the above and is proved by the `sanitizers` CI job's
ASan+UBSan leg.

## Hot Path

The hot path is the ring-buffer producer/consumer code in `shared-memory/`
(see [`ARCHITECTURE_RULES.md`](ARCHITECTURE_RULES.md) for the exact
boundary). No allocation, no syscalls, no exceptions, and no mutexes belong
in this path; see [`SAFETY_CRITICAL_HA_PLAN.md`](../../SAFETY_CRITICAL_HA_PLAN.md)
§1b and §5b for the rationale.

## Framework Parity

Any change to shared-memory or test code must keep both the GoogleTest and
Catch2 configurations passing; the project's stated goal is to demonstrate
framework independence, not GoogleTest-only coverage.

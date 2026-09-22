# DEC-0012: Phase 5 Perturbation and Fault Injection

- Status: Accepted
- Date: 2026-09-22
- Related discussion: [D-2026-09-22-001](../reviews/2026-09-22-phase5-perturbation-architecture.md)
- Authorizes: [T-0023](../tasks/T-0023-monitor-logical-ring-attribution.md)–[T-0031](../tasks/T-0031-phase5-integration-exit.md)

## Context

Phase 4 provides crash-only supervisor recovery, a stable supervisor-side
output witness, and a real Compose process topology. DEC-0011 deferred stall
recovery, double fault, and deterministic replay to Phase 5. Phase 4 exit
additionally recorded two residual items (monitor ring attribution and an
abandoned-producer-claim rule) that block Phase 5 correctness. The
`SAFETY_CRITICAL_HA_PLAN.md` §Phase 5 sketch predates DEC-0009, DEC-0010,
and DEC-0011 and cannot be implemented verbatim.

## Decision

1. **Fault categories in scope.** Phase 5 proves five: process crash
   (`SIGSEGV`/`SIGKILL`), process stall (`SIGSTOP` with SIGCONT-then-SIGKILL
   escalation), memory corruption (worker-side SIGUSR2 poison-next-slot
   flag), double fault (two simultaneous hot crashes), and supervisor loss.
   CPU starvation via cgroups, external `process_vm_writev`, and shm
   truncate are Phase 5b scope and are not implemented here.
2. **Tail fixes land first.** T-0023 (monitor `poll_worker` must read the
   logical ring owned by the physical worker, not its home ring) and T-0024
   (an explicit tested rule for abandoned producer claims: an epoch bump
   implies all in-flight claims from the prior epoch are abandoned; the new
   owner resumes from the last committed sequence, never force-commits an
   in-flight slot) are P0 blockers and land before any scenario task.
3. **Stall recovery.** The supervisor consumes `worker_stalled` alerts
   through the existing monitor pipe and, per logical ring and per epoch,
   issues one bounded SIGCONT followed by an escalation to SIGKILL and the
   existing crash-recovery path if the ring tail has not advanced after a
   documented grace period. Recovery remains idempotent per epoch; an
   already-handled stall cannot trigger a second escalation.
4. **Double fault preserves DEC-0011 topology.** Physical C can own at most
   one logical ring per epoch. On simultaneous crash of hot physical A and
   hot physical B, C promotes to exactly one logical ring (deterministic
   tie-break: lowest logical-ring index); the other logical ring is marked
   DEGRADED in a `worker_status` flag and via a supervisor stdout event, and
   the second hot physical process restarts as standby. Recovery of the
   DEGRADED ring is out of scope. The `kRegionVersion` stays 4; DEGRADED
   state is expressed using existing `worker_status` bits.
5. **Memory corruption stays inside the ownership contract.** The corruption
   hook is a worker-process SIGUSR2 handler that sets a `sig_atomic_t` flag;
   the worker's push path (outside the ring hot path, between ticks) corrupts
   the CRC of its own next slot when the flag is set. No external process
   writes to shared memory. The hook is opt-in per process (CLI flag or
   environment variable) so the production default keeps DEC-0009 #4 hot-path
   policy in force with no additional signal handler installation cost beyond
   the installation itself.
6. **Supervisor loss.** DEC-0011 #8 remains authoritative: supervisor loss
   exits the container and restart policy rebuilds. Phase 5 adds an explicit
   Compose scenario that `kill -9`s the supervisor PID 1 and verifies the
   container exits and the `unless-stopped` policy restores the topology.
7. **Perturbation harness API.** The harness is a C++20 `perturb::` library
   plus a `safety-critical-ha perturb` subcommand. Because the pinned
   container's libstdc++ 12 predates `std::expected`, Phase 5 uses the Phase 2
   deviation idiom: `bool fn(..., T& out, std::error_code& ec)` or a small
   `perturb::Result<T>` alias. This deviation is recorded in `NOTES.md` and
   retires when libstdc++ reaches 13.
8. **Replay is semantic and scoped.** The replay log is JSON lines
   (`{"ts":<ns>,"category":"crash|stall|corrupt|double-fault|supervisor-kill","target":<pid>,"params":{...}}`).
   Replay re-issues the signals at relative time offsets. Determinism
   contract: identical event categories in identical order, identical final
   ownership, identical committed record count per logical ring; wall-clock
   timestamps and process pids are excluded from comparison. Only the S1
   crash scenario is required to have a deterministic replay test in Phase 5.
   Broader `record()`/`replay()` engine scope moves to Phase 5b.
9. **Compose topology unchanged.** The T-0021 supervisor-only model and
   `containers/compose/failover-smoke.sh` stay green unchanged. The `perturb`
   profile service is populated with the perturb binary; scenarios are
   driven by host-side scripts and iterated by the CI Compose job.
10. **Recovery budgets.** `worker_crashed` inherits the DEC-0011 #5 `<100 ms`
    budget. New categories have explicit budgets defined here:
    - stall detection-to-SIGCONT-issued: `<200 ms` from `worker_stalled`
      alert (bounded by monitor poll cadence, currently 10 ms, plus one
      supervisor loop iteration).
    - stall detection-to-SIGKILL-escalation: `<stall_grace_ms + 100 ms>`
      from `worker_stalled` alert, where `stall_grace_ms` defaults to 200 ms.
    - memory-corruption detection: bounded by drain cadence, no absolute
      SLA (detection is synchronous to drain in the supervisor witness).
    - double fault: the first logical ring recovers within `<100 ms` of
      the *second* crash signal; the second logical ring's DEGRADED event
      is emitted within the same supervisor loop iteration.
    All budgets are recorded per scenario in the Phase 5 evidence.

## Consequences

Phase 5 changes must preserve CRC, monotonic sequence, CAS, lock-free
hot-path, sanitizer, and read-only-monitor requirements. Supervisor state
machine grows two new transitions (RUNNING→STALLED_RECOVERING and
RUNNING→DEGRADED) and one new DEGRADED state. Perturbation harness,
replay log, and Compose scenarios become Phase 5 evidence targets.

The harness, replay subcommand, and worker SIGUSR2 hook are test surfaces
and are documented as never-for-production, matching the DEC-0007 `destroy()`
pattern. Region layout stays v4; DEGRADED state uses `worker_status` bit
space.

## Maintenance Rules

- New fault categories or scenario additions require a decision amendment
  before code.
- The `perturb::` API surface and the replay log schema are a published
  contract once T-0031 closes; renames require a new decision.
- Accepted decisions are superseded rather than rewritten.

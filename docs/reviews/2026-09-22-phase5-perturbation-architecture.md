# D-2026-09-22-001: Phase 5 Perturbation and Fault-Injection Architecture

- Date: 2026-09-22
- Status: Accepted
- Related decision: [DEC-0012](../decisions/0012-phase5-perturbation-engine.md)
- Resulting tasks: [T-0023](../tasks/T-0023-monitor-logical-ring-attribution.md)–[T-0031](../tasks/T-0031-phase5-integration-exit.md)

## Question

How should Phase 5 turn the Phase 4 crash-only supervisor and Phase 3 alert
vocabulary into a fault-injection harness that proves the bounded-recovery,
graceful-degradation, and deterministic-replay tenets without destabilizing
the shared-memory safety contract?

## Findings

- DEC-0011 deferred stall recovery, double fault, and deterministic replay to
  Phase 5; they are not currently implemented.
- Phase 4 exit recorded two residual items that block Phase 5 correctness:
  (a) `poll_worker` reads the physical worker's home-ring tail, so after
  promotion physical C's home ring looks stalled and produces a spurious
  `worker_stalled` alert; (b) there is no explicit tested rule for an
  abandoned producer claim (crash between claim and commit).
- Existing perturbation surface today is thin but sufficient to bootstrap:
  SIGKILL (used in Phase 3/4 integration), SIGSTOP/SIGCONT (Phase 3 stall
  detection), and SIGUSR1 forced-crash hook (DEC-0009 #4, immediate `abort()`
  semantics, no cleanup).
- The Phase 5 sketch in `SAFETY_CRITICAL_HA_PLAN.md` uses `std::expected`
  (C++23) which is not available in the pinned g++-12 container (libstdc++ 12
  predates it — the same wall that produced Phase 2's `snprintf` and
  `std::ranges` deviations). The sketch also assumes `process_vm_writev` and
  cgroup quota writes from inside the container; both are privilege-sensitive
  and change the Compose topology.
- The Compose `perturb` service stub from Phase 0 is still `sleep infinity`
  and profile-gated; Phase 5 turns it into the fault injector.
- The supervisor is a deliberate single point of trust (DEC-0011). Phase 5
  must demonstrate supervisor loss exits the container and restart policy
  rebuilds the topology, without adding supervisor redundancy.

## Alternatives Considered

- **All 7 fault categories from the sketch in Phase 5.** Rejected: cgroup
  quota writes require additional Compose privileges and cross-container IPC;
  external `process_vm_writev` needs a `SYS_PTRACE` sidecar. Both are
  separable and better landed in a Phase 5b tail.
- **Bump the pinned container to Debian trixie (g++-14) for `std::expected`.**
  Rejected: toolchain-wide change blocked by DEC-0008 toolchain policy; not
  Phase-5-scoped.
- **Vendor `expected-lite`.** Rejected: introduces a third-party dependency
  outside the current GoogleTest/Catch2 allowlist.
- **Second writer to shared memory for corruption injection.** Rejected:
  violates the DEC-0010 monitor-read-only invariant and the DEC-0011
  physical-owner-writes-its-own-slot rule; would require its own decision.
- **Two standbys so both hot rings can recover on double fault.** Rejected
  for Phase 5: bumps `kRegionVersion` (region v4 → v5), expands the physical
  topology beyond DEC-0011, and changes the ownership model. Graceful
  degradation to a DEGRADED logical ring is the correct Phase 5 outcome.

## Result

Phase 5 will implement the fault-injection harness in this scope: crash,
stall (with SIGCONT then SIGKILL escalation), memory corruption via a
worker-side SIGUSR2 "poison next slot" flag, double fault (one promotion +
DEGRADED marking for the other ring), and supervisor loss (container exits,
restart policy rebuilds). Deterministic replay is scoped to a JSON-lines
replay log plus one deterministic S1 crash scenario; the broader
`PerturbationEngine::record/replay` surface is Phase 5b. The Phase 4 tail
fixes (monitor ring attribution and abandoned-producer-claim rule) land as
T-0023 and T-0024 before any scenario work.

The `std::expected` gap resolves to the Phase 2 pattern: `bool fn(..., T&
out, std::error_code& ec)` or a small `perturb::Result<T>` alias, with a
deviation recorded in `NOTES.md` and Phase 5 evidence, retiring when the
pinned libstdc++ reaches 13. Region layout stays v4.

Compose keeps the T-0021 supervisor-only model. The `perturb` profile service
becomes the fault injector; scenarios are driven from host-side scripts and
iterated by the CI Compose job. The Compose failover smoke (T-0021) stays
green unchanged.

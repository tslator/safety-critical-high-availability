# Project Status

- Current phase: Phase 5 complete (T-0023–T-0032 closed) — phase exit gate satisfied; Phase 6 (Observability and Certification-Grade Logging) planning next
- Current gate: Phase 5 exit gate satisfied — GoogleTest/Catch2 134/134, ASan+UBSan + TSan 118/118, Clang, Docker, Compose, five scenarios green 5× each on the CI reference host, and S1 replay determinism observed (see [evidence](evidence/phase-5.md))
- Last updated: 2026-09-24

## Phase Status

| Work | Status | Notes |
|---|---|---|
| Phase 0: container and tooling | Complete | Native, container, Compose, and CI baseline established |
| T-0008: clang-verify pipeline | Complete | Supplementary Clang build + ctest in pinned container; hosted CI run 35458929055 green (10/10 jobs) |
| T2.1: worker core | Complete | Config, splitmix64 workload, ranges pipeline (clang guard), OVERRUN bit |
| T2.2: work loop, signals, deadlines | Complete | Clock-injected loop, stop_token/sig_atomic_t cancellation, SIGUSR1 crash hook |
| T2.3: worker CLI + integration + exit | Complete | worker subcommand, attach/roles, fork integration a-d; exit run 35485369860 |
| T1.1: shared region layout and atomic flags | Complete | GoogleTest and Catch2 coverage |
| T1.2: lock-free ring buffer | Complete | MPMC protocol, scoped verification, sanitizer matrix |
| T1.3: CRC integrity and corruption detection | Complete | In-cell CRC-32C (layout v3), skip-and-count corruption path, region integrity word |
| T1.4: shared-memory attach/detach | Complete | Named `/dev/shm` create-or-open/stale rejection; `SharedRegionHandle` RAII; `MADV_DONTFORK`; cross-process re-attach |
| T1.5: stress tests and Phase 1 exit | Complete | 1M-op MPMC stress (1Px4C, 4Px4C) with exactly-once accounting; 44/44 in all configs incl. TSan and clang-verify; hosted exit run 35463176042 |
| T3.1: monitor core | Complete | Config validation, health classification + alert vocabulary (DEC-0010 #3), latched state machine; G3.1 |
| T3.2: pidfile, poll loop, JSON alerts, monitor CLI | Complete | Worker pidfile contract, liveness reader, injected-clock poll loop, snprintf JSON lines, `monitor` subcommand; G3.2 |
| T3.3: monitor integration and Phase 3 exit | Complete | 4 fork-based scenarios (SIGKILL crash, clean-exit idle, SIGSTOP stall/recovery, standby) x5 stable; exit run 35518074368 |
| T-0015: ownership and epoch model | Complete | Versioned v4 ownership metadata, generation fencing, CAS promotion, stale-token test; GoogleTest/Catch2 84/84 |
| T-0016: worker promotion control | Complete | Physical/logical identity separation, generation-aware acknowledgement, stale publication fence, standby promotion controls; GoogleTest/Catch2 85/85 |
| T-0017: supervisor lifecycle and monitor pipe | Complete | Shared-memory preflight, monitor JSON pipe validation, worker/monitor launch, bounded shutdown, and deterministic reaping; GoogleTest/Catch2 90/90 |
| T-0018: crash failover and replacement | Complete | Epoch-fenced logical-A promotion to physical C, replacement physical A standby launch, idempotent crash handling, and explicit degraded/failsafe result; GoogleTest/Catch2 91/91 |
| T-0019: sequence continuity and output witness | Complete | Supervisor-side ring drain validates CRC, monotonic transport sequence, epoch fencing, and post-takeover output observation; GoogleTest/Catch2 92/92 |
| T-0020: scheduling priority policy | Complete | Runtime policy attempts SCHED_FIFO with monitor < supervisor < workers ordering and records explicit unprivileged fallback; GoogleTest/Catch2 94/94 |
| T-0021: Compose runtime integration | Complete | Supervisor-only Compose topology sharing /dev/shm and /run/safety-critical-ha; healthcheck on region + worker pidfile liveness; in-container SIGKILL failover smoke verifies standby C promotion; GoogleTest/Catch2 95/95 |
| T-0022: Phase 4 integration, evidence, and exit | Complete | Full Phase 4 matrix green (GoogleTest/Catch2 95/95; ASan+UBSan x2 87/87 each; TSan x2 87/87 each; clang-verify 95/95; Docker build; Compose smoke + in-container failover x5); 10-iteration crash-recovery timing min 84 / median 84 / max 91 / avg 85 ms (target `<100 ms`) with zero corruptions; supervisor witness events expose timing and shutdown summary on stdout |
| Phase 4 supervisor and crash failover | Complete | Accepted review D-2026-09-20-002, decision DEC-0011; T-0015–T-0022 all complete; Phase 4 exit gate satisfied (see [evidence](evidence/phase-4.md)) |
| Phase 5 perturbation and fault injection | Complete | Accepted review [D-2026-09-22-001](reviews/2026-09-22-phase5-perturbation-architecture.md), decision [DEC-0012](decisions/0012-phase5-perturbation-engine.md); T-0023–T-0032 all complete; Phase 5 exit gate satisfied (see [evidence](evidence/phase-5.md)) |
| T-0032: failover handoff visibility and stall bounding | Complete | Fix for the nondeterministic S1-R CI failure (run 35865376613): monitor stall bound now arms on an episode's first commit, the supervisor records failover from reap and shields the handoff window; green hosted run 35910007625 on commit 42614a2; review [D-2026-09-23-001](reviews/2026-09-23-s1r-handoff-stall-blindspot.md), decision [DEC-0013](decisions/0013-handoff-visibility-and-stall-bounding.md), [evidence](evidence/phase-5.md) |
| T-0031: Phase 5 integration, evidence, and phase exit | Complete | Full Phase 5 matrix green on the CI reference host (run 35997271961, commit 8ba4b2d): GoogleTest/Catch2 134/134, ASan+UBSan + TSan 118/118, Clang, Docker, Compose; each perturbation scenario (S1/S2/S3/S5/S6) green 5× each (25 fresh-stack runs, G5.4); S1 replay determinism observed (G5.5); crash recovery 36–96 ms 0/5 over the `<100 ms` budget and other categories within DEC-0012 #10 bounds (G5.6); [evidence](evidence/phase-5.md) |

## Next Work

1. Phase 6 (Observability and Certification-Grade Logging) planning — review,
   decision, and task breakdown per the task workflow.
2. Optional tooling follow-ups from DEC-0008: `clang-static-analysis`
   (advisory) and `clang-sanitizers` presets.
3. Deferred residual risk from DEC-0013 (Phase 5 evidence): a
   generation-stamped status word so the monitor can attribute a slot to a
   process generation and assert the `worker_crashed` alert edge in S1/S1-R.

## Documentation Map

- [Development guide](DEVELOPMENT.md)
- [Architecture](ARCHITECTURE.md)
- [AI guidance](ai-guidance/README.md)
- [Discussions](DISCUSSIONS.md)
- [Tasks](tasks/README.md)
- [Decisions](decisions/)
- [Reviews](reviews/)
- [Evidence](evidence/)
- [Phase plans](phases/README.md)
- [Phase 4 plan](phases/PHASE_4_SUPERVISOR.md)
- [Phase 5 plan](phases/PHASE_5_PERTURBATION.md)
- [Phase 0 plan](phases/PHASE_0_CONTAINER_AND_TOOLING.md) and [Phase 1 plan](phases/PHASE_1_SHARED_MEMORY.md)

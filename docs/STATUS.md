# Project Status

- Current phase: Phase 3 complete (G3.1–G3.3 + exit gate) — Phase 4 supervisor planning initiated
- Current gate: G3.3 complete (Phase 3 exit, hosted run 35518074368)
- Last updated: 2026-09-20

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
| Phase 4 supervisor and crash failover | In progress | Accepted review D-2026-09-20-002, decision DEC-0011; T-0015 complete, T-0016–T-0022 planned |

## Next Work

1. T-0018: crash failover and replacement.
2. Optional tooling follow-ups from DEC-0008: `clang-static-analysis`
   (advisory) and `clang-sanitizers` presets.

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
- [Phase 0 plan](phases/PHASE_0_CONTAINER_AND_TOOLING.md) and [Phase 1 plan](phases/PHASE_1_SHARED_MEMORY.md)

# Project Status

- Current phase: Phase 2 complete (G2.1–G2.3 + exit gate) — Phase 3 planning next
- Current gate: G2.3 complete (Phase 2 exit, hosted run 35485369860)
- Last updated: 2026-09-19

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

## Next Work

1. Phase 3 kickoff (monitor daemon): poll `worker_status` cells (incl.
   `OVERRUN`), stall detection via unchanged head/tail counters, metrics
   collection — the Phase 2 handoff contract. Discussion/decision + task
   records first.
2. Optional tooling follow-ups from DEC-0008: `clang-static-analysis`
   (advisory), `clang-sanitizers` presets, `COPY_ONLY` dev-warning cleanup.

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
- [Phase 0 plan](phases/PHASE_0_CONTAINER_AND_TOOLING.md) and [Phase 1 plan](phases/PHASE_1_SHARED_MEMORY.md)

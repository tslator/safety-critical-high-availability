# Project Status

- Current phase: Phase 1 complete (G1.1–G1.5 + exit gate) — Phase 2 planning next
- Current gate: G1.5 complete (Phase 1 exit, hosted run 35463176042)
- Last updated: 2026-09-19

## Phase Status

| Work | Status | Notes |
|---|---|---|
| Phase 0: container and tooling | Complete | Native, container, Compose, and CI baseline established |
| T-0008: clang-verify pipeline | Complete | Supplementary Clang build + ctest in pinned container; hosted CI run 35458929055 green (10/10 jobs) |
| T1.1: shared region layout and atomic flags | Complete | GoogleTest and Catch2 coverage |
| T1.2: lock-free ring buffer | Complete | MPMC protocol, scoped verification, sanitizer matrix |
| T1.3: CRC integrity and corruption detection | Complete | In-cell CRC-32C (layout v3), skip-and-count corruption path, region integrity word |
| T1.4: shared-memory attach/detach | Complete | Named `/dev/shm` create-or-open/stale rejection; `SharedRegionHandle` RAII; `MADV_DONTFORK`; cross-process re-attach |
| T1.5: stress tests and Phase 1 exit | Complete | 1M-op MPMC stress (1Px4C, 4Px4C) with exactly-once accounting; 44/44 in all configs incl. TSan and clang-verify; hosted exit run 35463176042 |

## Next Work

1. Phase 2 (workers) kickoff: reconciliation decision for the plan sketch's
   C++23/26 facilities (`std::generator`, `std::this_thread::pause`) against
   the C++20 project, then canonical task records (T-0009+).
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

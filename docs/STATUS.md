# Project Status

- Current phase: Phase 1 - Shared Memory & Lock-Free Ring Buffers
- Current gate: G1.2 complete
- Last updated: 2026-09-17

## Phase Status

| Work | Status | Notes |
|---|---|---|
| Phase 0: container and tooling | Complete | Native, container, Compose, and CI baseline established |
| T1.1: shared region layout and atomic flags | Complete | GoogleTest and Catch2 coverage |
| T1.2: lock-free ring buffer | Complete | MPMC protocol, scoped verification, sanitizer matrix |
| T1.3: CRC integrity and corruption detection | Planned | Not started |
| T1.4: shared-memory attach/detach | Planned | Not started |
| T1.5: stress tests and Phase 1 exit | Blocked | Soak evidence exists; waiting on T1.3 and T1.4 |

## Next Work

1. Implement T1.3 CRC integrity and corruption handling.
2. Implement T1.4 named shared-memory attach/detach and stale-object rejection.
3. Complete T1.5 exit evidence and update the Phase 1 plan.

## Documentation Map

- [Development guide](DEVELOPMENT.md)
- [Architecture](ARCHITECTURE.md)
- [AI guidance](ai-guidance/README.md)
- [Discussions](DISCUSSIONS.md)
- [Tasks](tasks/README.md)
- [Decisions](decisions/)
- [Reviews](reviews/)
- [Evidence](evidence/)
- [Phase plans](../PHASE_0_CONTAINER_AND_TOOLING.md) and [Phase 1 plan](../PHASE_1_SHARED_MEMORY.md)

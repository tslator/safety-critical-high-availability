# Tasks

This is the single task registry for all work in the project — code, docs, and
infra. Every task is reachable from here with an owner, status, priority,
dependencies, and a link to its decision and evidence. Canonical IDs are
sequential `T-####`; phase-scoped labels such as `T1.3` are aliases. See
[DEC-0006](../decisions/0006-unified-task-registry.md) and
[TASK_WORKFLOW.md](../ai-guidance/TASK_WORKFLOW.md).

| ID | Phase | Title | Owner | Status | Priority | Depends On | Related |
|---|---|---|---|---|---|---|---|
| [T-0001](T-0001-documentation-index.md) | Meta | Add documentation index | Unassigned | Complete | Medium | None | [DEC-0003](../decisions/0003-documentation-discussion-records.md) |
| [T-0002](T-0002-ai-guidance-implementation.md) | Meta | Implement AI guidance architecture | Unassigned | Complete | High | None | [DEC-0004](../decisions/0004-ai-guidance-architecture.md) |
| [T-0003](T-0003-unify-record-process.md) | Meta | Unify record process on docs/tasks | Unassigned | Complete | High | [DEC-0006](../decisions/0006-unified-task-registry.md) | [D-2026-09-18-001](../reviews/2026-09-18-record-process-scope.md) |
| [T-0004](T-0004-t1.3-crc-integrity.md) | Phase 1 | T1.3 — CRC integrity and corruption detection | Unassigned | Complete | High | G1.2 | [DEC-0005](../decisions/0005-t1.3-crc-integrity.md) |
| [T-0005](T-0005-t1.4-attach-detach.md) | Phase 1 | T1.4 — shared memory attach/detach | Unassigned | Complete | High | G1.2 | [DEC-0007](../decisions/0007-t1.4-attach-detach.md) |
| [T-0006](T-0006-t1.5-stress-and-exit.md) | Phase 1 | T1.5 — stress tests and phase exit | AI agent (opencode) | Complete | High | T-0004, T-0005 | [Phase 1 plan](../phases/PHASE_1_SHARED_MEMORY.md), [evidence](../../NOTES.md) |
| [T-0007](T-0007-pin-primary-baseline.md) | Phase 0 | Pin the primary build compiler | AI agent (opencode) | Complete | High | None | [DEC-0008](../decisions/0008-clang-supplementary-verification.md), [evidence](../../NOTES.md) |
| [T-0008](T-0008-clang-verify-pipeline.md) | Phase 0 | Clang supplementary verification pipeline | AI agent (opencode) | Complete | Medium | T-0007 | [DEC-0008](../decisions/0008-clang-supplementary-verification.md), [evidence](../verification/clang-verification.md) |
| [T-0009](T-0009-worker-core.md) | Phase 2 | T2.1 — worker core (config, workload, pipeline) | AI agent (opencode) | Complete | High | Phase 1 exit | [DEC-0009](../decisions/0009-phase2-worker-runtime.md), [evidence](../../NOTES.md) |
| [T-0010](T-0010-work-loop-signals-deadlines.md) | Phase 2 | T2.2 — work loop, signals, deadline monitoring | AI agent (opencode) | Complete | High | T-0009 | [DEC-0009](../decisions/0009-phase2-worker-runtime.md) |
| [T-0011](T-0011-worker-cli-integration-phase-exit.md) | Phase 2 | T2.3 — worker CLI, integration, phase exit | AI agent (opencode) | Complete | High | T-0010 | [DEC-0009](../decisions/0009-phase2-worker-runtime.md) |
| [T-0012](T-0012-monitor-core.md) | Phase 3 | T3.1 — monitor core (config, health algorithm) | AI agent (opencode) | Complete | High | Phase 2 exit | [DEC-0010](../decisions/0010-phase3-monitor-daemon.md), [evidence](../../NOTES.md) |
| [T-0013](T-0013-crash-detection-json-alerts-cli.md) | Phase 3 | T3.2 — pidfile liveness, poll loop, JSON alerts, monitor CLI | AI agent (opencode) | Complete | High | T-0012 | [DEC-0010](../decisions/0010-phase3-monitor-daemon.md) |
| [T-0014](T-0014-monitor-integration-phase-exit.md) | Phase 3 | T3.3 — monitor integration, phase exit | AI agent (opencode) | Complete | High | T-0013 | [DEC-0010](../decisions/0010-phase3-monitor-daemon.md) |
| [T-0015](T-0015-phase4-ownership-epoch.md) | Phase 4 | Ownership and epoch model | AI agent (opencode) | Complete | High | T-0014 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| [T-0016](T-0016-worker-promotion-control.md) | Phase 4 | Worker promotion control | AI agent (opencode) | Complete | High | T-0015 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| [T-0017](T-0017-supervisor-lifecycle-monitor-pipe.md) | Phase 4 | Supervisor lifecycle and monitor pipe | AI agent (opencode) | Complete | High | T-0015 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| [T-0018](T-0018-crash-failover-replacement.md) | Phase 4 | Crash failover and replacement | AI agent (opencode) | Complete | High | T-0016, T-0017 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| [T-0019](T-0019-sequence-continuity-witness.md) | Phase 4 | Sequence continuity and output witness | Unassigned | Planned | High | T-0015, T-0016, T-0018 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| [T-0020](T-0020-scheduling-priority.md) | Phase 4 | Scheduling priority policy | Unassigned | Planned | Medium | T-0017 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| [T-0021](T-0021-compose-runtime-integration.md) | Phase 4 | Compose runtime integration | Unassigned | Planned | High | T-0017, T-0018 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| [T-0022](T-0022-phase4-integration-exit.md) | Phase 4 | Integration, evidence, and phase exit | Unassigned | Planned | High | T-0018, T-0019, T-0020, T-0021 | [DEC-0011](../decisions/0011-phase4-supervisor-failover.md) |
| T1.1 | Phase 1 | Shared region layout and atomic flags _(roll-up)_ | — | Complete | — | — | [Phase 1 plan](../phases/PHASE_1_SHARED_MEMORY.md) |
| T1.2 | Phase 1 | Lock-free ring buffer _(roll-up)_ | — | Complete | — | — | [Phase 1 plan](../phases/PHASE_1_SHARED_MEMORY.md), [evidence](../evidence/phase-1.md) |
| Phase 0 | Phase 0 | Container and tooling (0.1–0.6) _(roll-up)_ | — | Complete | — | — | [Phase 0 plan](../phases/PHASE_0_CONTAINER_AND_TOOLING.md) |

## Task Rules

- Tasks must have an owner, status, priority, dependencies, scope, and
  acceptance criteria before assignment. Closed tasks must link to validation
  evidence.
- Canonical IDs are sequential `T-####`; never reuse an ID. A phase-scoped label
  (`T1.3`) is an alias that links to the canonical record.
- A full per-task file is required for any task that has an authorizing decision
  or is not yet complete. Completed phase tasks with no decision of their own may
  be a **roll-up** row (shown above) that links to the phase plan instead.
- Status vocabulary: `Planned | Ready | In Progress | In Review | Complete |
  Blocked`. `docs/STATUS.md` is a human roll-up briefing; this registry is the
  source of truth for task status.

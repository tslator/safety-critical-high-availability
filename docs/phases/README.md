# Project Phases

This directory contains the detailed execution plan for each project phase.
The high-level roadmap remains in
[`SAFETY_CRITICAL_HA_PLAN.md`](../../SAFETY_CRITICAL_HA_PLAN.md).

## Placement Rules

- Put each phase's authoritative execution plan in this directory.
- Name files `PHASE_<number>_<short-description>.md`, using zero-padded
  numbers when the project reaches double-digit phases.
- Keep phase plans focused on scope, dependencies, implementation tasks,
  deliverables, and verification gates.
- Link phase plans from [`docs/STATUS.md`](../STATUS.md) when they become
  active or are otherwise relevant to current work.
- Record implementation decisions, assignable tasks, and validation evidence
  in their dedicated directories under `docs/` — assignable tasks live in the
  single task registry at [`docs/tasks/`](../tasks/README.md). Keep the phase
  plan focused on scope, dependencies, implementation steps, deliverables, and
  verification gates, linking each task to its canonical registry record; do not
  duplicate those records in a phase plan unless the plan needs a concise
  cross-reference.

## Phase Plans

- [Phase 0 - Container and Tooling](PHASE_0_CONTAINER_AND_TOOLING.md)
- [Phase 1 - Shared Memory](PHASE_1_SHARED_MEMORY.md)
- [Phase 2 - Worker Processes](PHASE_2_WORKERS.md)
- [Phase 3 - Monitor Daemon](PHASE_3_MONITOR_DAEMON.md)
- [Phase 4 - Supervisor and Crash Failover](PHASE_4_SUPERVISOR.md)
- [Phase 5 - Perturbation and Fault Injection](PHASE_5_PERTURBATION.md)

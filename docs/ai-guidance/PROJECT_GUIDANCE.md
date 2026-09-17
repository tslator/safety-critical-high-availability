# Project Guidance Briefing

This is a short briefing for any agent working in this repository. Read
[`CORE.md`](CORE.md) for the always-binding core card, then read the focused
document relevant to the task at hand.

## Status

Check [`docs/STATUS.md`](../STATUS.md) for the current phase, current gate,
and next planned work before proposing new work or claiming a task complete.

## Safety Posture

This project is a demo of safety-critical, high-availability engineering
practice: lock-free shared memory, CRC-verified ring buffers, sanitizer
coverage, and fault-injection testing. Treat the [`SAFETY_CRITICAL_HA_PLAN.md`](../../SAFETY_CRITICAL_HA_PLAN.md)
key safety properties (`CORE.md` Tier 1 items) as binding constraints on any
change that touches shared memory, worker, monitor, or supervisor code.

## Where to Look Next

| Need | Read |
|---|---|
| Project goals and scope | [`GOALS.md`](GOALS.md) |
| Full safety tenets and rationale | [`TENETS.md`](TENETS.md) |
| Mandatory and advisory rules | [`POLICIES.md`](POLICIES.md) |
| Ownership boundaries, hot-path definition | [`ARCHITECTURE_RULES.md`](ARCHITECTURE_RULES.md) |
| Build/test loop, CI gates | [`DEVELOPMENT_WORKFLOW.md`](DEVELOPMENT_WORKFLOW.md) |
| Discussion → decision → task → evidence | [`TASK_WORKFLOW.md`](TASK_WORKFLOW.md) |
| Architecture detail | [`../ARCHITECTURE.md`](../ARCHITECTURE.md) |
| Build/test commands | [`../DEVELOPMENT.md`](../DEVELOPMENT.md) |
| Phase execution plans | [`../phases/README.md`](../phases/README.md) |

This directory (`docs/ai-guidance/`) is authoritative. Do not duplicate its
guidance into `AGENTS.md`, `.github/copilot-instructions.md`, or elsewhere;
those files are generated pointers, not a second authoring location.

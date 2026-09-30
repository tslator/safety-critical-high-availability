# Safety-Critical HA — The Manual

A guided, hands-on tour of this repository: a C++20 framework for
safety-critical, high-availability applications. Every "Try it" block in this
manual has actually been run against this codebase, and the output you see
quoted is real output, lightly trimmed.

**Who this is for.** A competent C++/Linux developer who is new to
safety-critical and high-availability engineering concepts. You do not need
prior experience with lock-free programming, supervision patterns, or fault
injection — each concept is introduced from first principles and then tied to
the exact code and tests that implement it.

**How this manual relates to the rest of `docs/`.** The manual teaches; the
authoritative facts live elsewhere. Where a number, contract, or decision
matters, the chapters link to the source of truth instead of copying it:

- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — canonical architecture reference.
- [`docs/STATUS.md`](../STATUS.md) — phase/task progress.
- [`docs/decisions/`](../decisions/) — decision records (DEC-xxxx).
- [`docs/phases/`](../phases/) — phase plans (T-xx tasks).
- [`docs/ai-guidance/`](../ai-guidance/) — project tenets, goals, and rules.
- [`SAFETY_CRITICAL_HA_PLAN.md`](../../SAFETY_CRITICAL_HA_PLAN.md) — the original design plan.

Source references use the `path:line` convention (e.g.
`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:21`) so you
can jump straight to the code.

## Chapters

| # | Chapter | What you'll learn |
|---|---------|-------------------|
| 01 | [Introduction](01-introduction.md) | What the project is, what "safety-critical" means here, and a 10-minute orientation |
| 02 | [The Seven Safety Properties](02-safety-properties.md) | The non-negotiable requirements and the gate that verifies each one |
| 03 | [Architecture Overview](03-architecture.md) | Processes, roles, the shared region, and the data/control planes |
| 04 | [Getting Started](04-getting-started.md) | Build, test, and your first worker — native and in Docker |
| 05 | [The Shared Memory Region](05-shared-memory.md) | SharedRegion v4 layout, attach/verify discipline, `shm_attach` |
| 06 | [The Lock-Free Ring Buffer](06-ring-buffer.md) | The Vyukov MPMC protocol, sequence classes, and claim soundness |
| 07 | [Data Integrity](07-data-integrity.md) | CRC-32C per slot, the region integrity word, sequence continuity |
| 08 | [Workers](08-workers.md) | Deterministic workload, the work loop, deadlines, roles, and signals |
| 09 | [The Monitor](09-monitor.md) | The health state machine, alert vocabulary, and stall detection |
| 10 | [Supervision and Failover](10-supervisor-failover.md) | Ownership epochs, promotion, stall recovery, and the supervisor loop |
| 11 | [Fault Injection](11-fault-injection.md) | The perturbation harness, scenarios S1–S6, and deterministic replay |
| 12 | [Observability](12-observability.md) | The event log, the daemon, and `/health`, `/metrics`, `/status` |
| 13 | [Testing and Verification](13-testing.md) | Dual test frameworks, sanitizers, and how every property gets its gate |
| 14 | [Running as a System](14-running-production.md) | The Compose stack, health checks, restart policy, and an operator runbook |

## Appendices

- [Appendix A — CLI Reference](A-cli-reference.md) — every subcommand and flag, with exit codes.
- [Appendix B — Troubleshooting](B-troubleshooting.md) — observed failure modes and what they mean.
- [Appendix C — Glossary](C-glossary.md) — the vocabulary of this project (and of the field).

## Suggested reading order

- **New to the repo?** Read 01 → 04 in order, then follow your curiosity.
- **Here for the lock-free code?** 05 → 06 → 07, then 10 for the ownership protocol.
- **Here for operations?** 03 → 11 → 12 → 14, with Appendix B at hand.
- **Here for the verification story?** 02 → 13 → 11.

## Running the examples yourself

Everything in this manual runs on Linux x86_64 with either:

- a native build (`cmake -S . -B build/gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest`), or
- the Docker image `safety-critical-ha:phase0` and the Compose stack in the repo root.

Chapter [04 — Getting Started](04-getting-started.md) walks through both. Two
environment quirks to know up front (explained in Appendix B):

1. `scheduling fallback (errno 1)` on stderr is **normal** outside a
   privileged container — SCHED_FIFO fails without `CAP_SYS_NICE` and the
   process continues on the default scheduler by design.
2. The Docker demos bind the observability port; if your host already uses
   8080, set `OBSERVABILITY_PORT=18080` before `docker compose up`.

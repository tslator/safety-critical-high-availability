# Project Goals

- Demonstrate safety-critical, high-availability application engineering
  practice end to end: shared-memory IPC, lock-free data structures, worker
  supervision, fault injection, and recovery, in a reproducible container
  environment.
- Prove the [key safety properties](../../SAFETY_CRITICAL_HA_PLAN.md#7-key-safety-properties-to-prove)
  with automated tests and sanitizers rather than with prose claims alone.
- Keep the project buildable and testable with two interchangeable test
  frameworks (GoogleTest and Catch2) to avoid framework lock-in in the
  demonstrated pattern.
- Keep design reasoning, decisions, tasks, and validation evidence traceable
  through the `docs/` records rather than only in chat history or commit
  messages.
- Keep AI agent guidance itself under the same traceability discipline: one
  authored source, generated adapters, and CI-enforced drift checks.

See [`SAFETY_CRITICAL_HA_PLAN.md`](../../SAFETY_CRITICAL_HA_PLAN.md) for the
full phased plan and [`docs/STATUS.md`](../STATUS.md) for current progress.

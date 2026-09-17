# Architecture Rules

See [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) for the current implemented
architecture and [`SAFETY_CRITICAL_HA_PLAN.md`](../../SAFETY_CRITICAL_HA_PLAN.md)
§1 and §4 for the target architecture and project structure.

## Ownership Boundaries

- `app/` — the versioned command-line application entry point.
- `shared-memory/` — the shared region layout, atomic worker-status flags,
  and lock-free MPMC ring buffer library (`safety_crit::shared_memory`).
- `tests/` — top-level smoke tests; component tests live alongside their
  library under `shared-memory/tests/`.
- `docs/` — all project documentation, including `docs/ai-guidance/`.

## Hot-Path Boundary

The hot path is the producer/consumer code in `shared-memory/include` and
`shared-memory/src` that runs per ring-buffer operation: slot claim, write,
CRC compute/verify, and slot release. It excludes one-time setup/teardown
such as region creation, attach/detach, and whole-region `verify()` calls,
which may allocate or take locks because they only run when rings are
quiescent.

## Ring Verification Scope

Use `verify_identity()` for metadata checks, `verify_worker_ring()` for a
single quiescent worker being reattached, and whole-region `verify()` only
when all rings are quiescent. Do not widen verification scope without
updating this document and `docs/ARCHITECTURE.md` together.

## Layout Versioning

Shared-memory layout changes are breaking changes for any attached worker.
Any change to `SharedRegion` layout, ring metadata, or slot format must be
called out explicitly in the change description and validated against both
test frameworks before merge.

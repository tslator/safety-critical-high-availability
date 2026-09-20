# Current Architecture

The implemented system currently contains a C++20 application baseline and
the Phase 1 `safety_crit::shared_memory` library.

```text
app/                    versioned command-line application
shared-memory/          shared region, flags, and lock-free ring buffer
workers/                worker runtime (config, workload, pipeline, loop)
tests/                  Phase 0 smoke test
Docker / Compose        reproducible runtime baseline
```

`SharedRegion` contains per-worker status cells, ring metadata, and one fixed
size lock-free MPMC ring per worker. Ring verification is scoped deliberately:
use `verify_identity()` for metadata, `verify_worker_ring()` for a quiescent
worker being reattached, and whole-region `verify()` only when all rings are
quiescent.

The long-term monitor, supervisor, and perturbation architecture is
specified in [the project plan](../SAFETY_CRITICAL_HA_PLAN.md); the Phase 2
worker runtime (`workers/`, per [DEC-0009](decisions/0009-phase2-worker-runtime.md))
attaches to the region, produces deterministic processed output onto its own
ring, maintains `worker_status` (including `OVERRUN`), and stops on SIGTERM.
Monitor, supervisor, and fault-injection components remain planned.

# Current Architecture

The implemented system currently contains a C++20 application baseline and
the Phase 1 `safety_crit::shared_memory` library.

```text
app/                    versioned command-line application
shared-memory/          shared region, flags, and lock-free ring buffer
runtime/                process scheduling policy and priority fallback
workers/                worker runtime (config, workload, pipeline, loop)
monitors/               health monitor and alert stream
supervisor/             lifecycle, failover, and output witness
tests/                  Phase 0 smoke test
Docker / Compose        reproducible runtime baseline
```

`SharedRegion` v4 contains per-worker status cells, ring metadata, management-
plane ownership cells, and one fixed-size lock-free MPMC ring per worker.
Ownership is identified by logical ring, physical owner, process generation,
and an even promotion epoch; stale tokens are rejected by CAS fencing. Ring
verification is scoped deliberately:
use `verify_identity()` for metadata, `verify_worker_ring()` for a quiescent
worker being reattached, and whole-region `verify()` only when all rings are
quiescent.

The monitor and supervisor consume the management plane and own process
lifecycle according to [DEC-0011](decisions/0011-phase4-supervisor-failover.md).
The Phase 2 worker runtime (`workers/`, per [DEC-0009](decisions/0009-phase2-worker-runtime.md))
attaches to the region, produces deterministic processed output onto its own
ring, maintains `worker_status` (including `OVERRUN`), and stops on SIGTERM.
The `runtime/` policy attempts `SCHED_FIFO` outside the ring hot path and
records an explicit fallback when process elevation is unavailable.

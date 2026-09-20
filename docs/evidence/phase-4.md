# Phase 4 Evidence Plan

This record is the evidence target for
[Phase 4](../phases/PHASE_4_SUPERVISOR.md) and will be populated as tasks close.

## Required Evidence

- GoogleTest and Catch2 plain configurations.
- ASan+UBSan and TSan for affected control/shared-memory paths.
- Pinned Clang verification.
- Ownership/epoch fencing and stale-generation rejection.
- C assuming logical A after an A crash, with A returning as standby.
- No duplicate or lost output under the accepted continuity contract.
- Repeated fault-to-first-output measurements below 100 ms.
- Clean shutdown, child reaping, and drain witness.
- Privileged priority enforcement and unprivileged fallback.
- Docker build and real-process Compose failover validation.

Each closed task must link implementation and durable command/result evidence
here or in `NOTES.md`.

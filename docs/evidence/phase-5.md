# Phase 5 Evidence Plan

This record is the evidence target for
[Phase 5](../phases/PHASE_5_PERTURBATION.md) and will be populated as tasks close.

## Required Evidence

- GoogleTest and Catch2 plain configurations.
- ASan+UBSan and TSan for affected control/harness/scenario paths.
- Pinned Clang verification.
- Monitor logical-ring attribution (Phase 4 tail fix, T-0023).
- Abandoned producer-claim rule and test (Phase 4 tail fix, T-0024).
- Supervisor stall recovery state machine and integration scenario (T-0025).
- Supervisor double-fault handling and DEGRADED state (T-0026).
- Worker SIGUSR2 memory-corruption hook and end-to-end CRC observation
  (T-0027).
- Perturbation harness library, CLI, and JSON-lines log (T-0028).
- Compose `perturb` service and per-scenario scripts S1/S2/S3/S5/S6 (T-0029).
- Replay log schema and deterministic S1 replay scenario (T-0030).
- Docker build and Compose smoke unchanged plus new scenario coverage.
- Repeated scenario runs (5× each scenario on the reference host).
- Bounded recovery timings per DEC-0012 #10 budgets.
- Hosted CI run link.

Each closed task must link implementation and durable command/result evidence
here or in `NOTES.md`.

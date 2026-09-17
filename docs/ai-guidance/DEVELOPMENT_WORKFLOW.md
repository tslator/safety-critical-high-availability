# Development Workflow

Full commands live in [`docs/DEVELOPMENT.md`](../DEVELOPMENT.md); this file
describes the agent loop and where the enforcement gates are.

## Agent Loop

1. Read [`CORE.md`](CORE.md) and [`PROJECT_GUIDANCE.md`](PROJECT_GUIDANCE.md).
2. Check [`docs/STATUS.md`](../STATUS.md) for the current phase, current
   gate, and next planned work.
3. Make the change, keeping [`ARCHITECTURE_RULES.md`](ARCHITECTURE_RULES.md)
   and [`POLICIES.md`](POLICIES.md) in scope.
4. Build and test locally with both test frameworks:

   ```bash
   cmake -S . -B build/gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
   cmake --build build/gtest --parallel
   ctest --test-dir build/gtest --output-on-failure

   cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
   cmake --build build/catch2 --parallel
   ctest --test-dir build/catch2 --output-on-failure
   ```

5. For shared-memory or lock-free changes, also run (or rely on CI to run)
   the ASan+UBSan and TSan sanitizer configurations described in
   [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml).
6. If `docs/ai-guidance/CORE.md` changed, regenerate the adapters:

   ```bash
   ./scripts/sync-agent-guidance.sh
   ```

7. Do not claim a task complete before its CI gates (see `CORE.md`) pass.

## CI Gates

`native-gtest`, `native-catch2`, `sanitizers` (ASan+UBSan and TSan legs),
`docker-build`, `docker-compose-smoke`, and `agent-guidance-drift` (checks
that `AGENTS.md` and `.github/copilot-instructions.md` match what
`scripts/sync-agent-guidance.sh` would generate from `CORE.md`).

Optionally enable a local pre-commit check with
`git config core.hooksPath scripts/hooks`; it runs the same drift check
before each commit. This is a convenience only — the CI job is the mandatory
enforcement point.

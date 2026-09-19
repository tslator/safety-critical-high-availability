# T-0008: Clang Supplementary Verification Pipeline (clang-verify)

- Status: Ready
- Owner: Unassigned
- Priority: Medium
- Depends on: [T-0007](T-0007-pin-primary-baseline.md)
- Phase: Phase 0 (tooling)
- Phase label: (new)
- Related decision: [DEC-0008](../decisions/0008-clang-supplementary-verification.md)

## Scope

Add an independent Clang clean build + test run as supplementary verification
evidence. This increment is build + ctest only (no clang-tidy, no sanitizers).

## Deliverables

- `CMake/toolchains/clang-verify.cmake` (compilers + C++20 cache vars only).
- Root `CMakePresets.json` `clang-verify` configure/build/test presets.
- `containers/verification/Dockerfile.clang` (digest-pinned, `clang-14`, version
  echo for evidence).
- `clang-verify` CI job (SHA-pinned actions, `contents: read`) running the preset
  in the image; CTest log uploaded on failure.
- `docs/verification/clang-verification.md` (role framing + evidence table);
  `DEVELOPMENT_WORKFLOW.md`/`DEVELOPMENT.md`/`README.md` command updates.

## Acceptance Criteria

- `cmake --preset clang-verify` configures with `CMAKE_CXX_COMPILER_ID=Clang`.
- Build and full ctest pass in the pinned image; `clang-verify` job green.
- Evidence table (toolchain identity, config, build, test) recorded.
- `./scripts/sync-agent-guidance.sh --check` still exits 0 (CORE untouched).

## Validation

Record in `NOTES.md` / `docs/evidence/`; DEC-0008 → T-0008 → evidence chain.

## Completion Notes

Deferred to later tasks: `clang-static-analysis` (advisory), `clang-sanitizers`,
libc++ independence, any CORE.md gate use (e.g. clang-tidy for hot-path rules).

# T-0008: Clang Supplementary Verification Pipeline (clang-verify)

- Status: Complete
- Owner: AI agent (opencode)
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

Recorded 2026-09-19 in [`NOTES.md`](../../NOTES.md) (section "T-0008") and
[`docs/verification/clang-verification.md`](../verification/clang-verification.md)
(evidence table): image built from the digest-pinned Dockerfile;
`cmake --preset clang-verify` configures with `CMAKE_CXX_COMPILER_ID=Clang`;
build clean (zero first-party warnings) and 41/41 ctest pass in the image;
gcc-primary regression green (GoogleTest/Catch2/ASan+UBSan, 41/41 each, link
line unchanged); `sync-agent-guidance.sh --check` exits 0. Hosted CI run 35458929055 on commit
769c8a7 (2026-09-19): all ten jobs succeeded, `clang-verify` job green with
toolchain identity matching the local pinned image.
Chain: D-2026-09-19-001 → DEC-0008 → T-0007 → T-0008 → evidence.

## Completion Notes

- Scope deviation (in service of AC "build passes"): the first Clang run
  surfaced three genuine diagnostic differences fixed in first-party sources
  — `std::bit_cast` for the `constexpr line_of()` test helpers
  (`-Winvalid-constexpr`), removal of shadowing local `Ring8` aliases
  (`-Wshadow`), and a Clang-only `target_link_libraries(... atomic)` in
  `shared-memory/CMakeLists.txt` (Clang emits an out-of-line
  `__atomic_is_lock_free` call; the gcc-primary link line is unchanged).
  Details in NOTES.md and the verification doc.
- `clang-tidy-14` is installed in the verification image per DEC-0008 #2 but
  unused by the preset, reserved for `clang-static-analysis`.

Deferred to later tasks: `clang-static-analysis` (advisory), `clang-sanitizers`,
libc++ independence, any CORE.md gate use (e.g. clang-tidy for hot-path rules).

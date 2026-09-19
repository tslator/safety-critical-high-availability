# DEC-0008: Clang Supplementary Verification

- Status: Accepted
- Date: 2026-09-19
- Related discussion: [D-2026-09-19-001](../reviews/2026-09-19-clang-supplement.md)
- Authorizes: [T-0007](../tasks/T-0007-pin-primary-baseline.md),
  [T-0008](../tasks/T-0008-clang-verify-pipeline.md)

## Context

The project wants an independent compiler to surface diagnostic differences and
run the suite under a second implementation, without letting Clang become an
unversioned host dependency or a false signal of safety validation. Primary and
Clang roles must be unambiguous and reproducible in CI and locally.

## Decision

1. **Terminology.** Use consistently in docs, dirs, presets, image tags, CI:
   `gcc-primary`, `clang-verify`, `clang-static-analysis`, `clang-sanitizers`,
   `verification-toolchain`.
2. **Delivery = pinned verification container.** A dedicated
   `containers/verification/Dockerfile.clang`, digest-pinned `debian:bookworm`
   base (same libc/libstdc++ as primary, isolating compiler differences), a
   **specific Debian Clang major pinned to `clang-14`**, plus `clang-tidy`, cmake,
   ninja. Not vendored, not a host-global authoritative install. Tag
   `safety-critical-ha:verify-clang-14`. Newer Clang needs the LLVM APT repo and
   is out of scope (independence, not recency).
3. **Scope now = `clang-verify` (clean build + ctest).** `clang-static-analysis`
   (clang-tidy) is deferred and, when introduced, is **advisory / report-only**
   (findings published as artifacts; graduates check families to blocking by later
   decision). `clang-sanitizers` presets are deferred.
4. **Warning policy stays in `Warnings.cmake`.** The Clang toolchain file sets only
   `CMAKE_C/CXX_COMPILER` and the C++20 cache vars — **no** `add_compile_options`
   (would double-apply and hit FetchContent'd GTest/Catch2). Warnings remain
   per-target via `safety_crit_apply_warnings`, non-fatal unless
   `SAFETY_CRIT_WARNINGS_AS_ERRORS` is set.
5. **Preset at repo root, version 3.** `CMakePresets.json` lives at the source root
   for auto-discovery (not `CMake/presets/`), `version: 3` for Bookworm CMake
   compatibility, with its own `build/clang-verify` tree.
6. **Framing.** A successful Clang run is verification evidence; it does not, by
   itself, constitute system validation or safety certification. **No CORE.md
   Tier-1 change** — Clang is supplementary, not a safety-property gate.
7. **Primary baseline prerequisite.** T-0007 makes the primary build compiler
   explicit and records exact versions; container/CI results are treated as
   baseline evidence only after that.

## Consequences

- New files: `CMake/toolchains/clang-verify.cmake`, `CMakePresets.json`,
  `containers/verification/Dockerfile.clang`, `docs/verification/clang-verification.md`,
  and a `clang-verify` job in `.github/workflows/ci.yml`.
- `docs/ai-guidance/DEVELOPMENT_WORKFLOW.md` gains the preset commands and the new
  CI gate; `docs/DEVELOPMENT.md` and `README.md` gain Clang commands/notes. None of
  these are `CORE.md`, so the generated adapters stay byte-identical and the
  `agent-guidance-drift` job stays green (no regeneration).
- `build/clang-verify` is ignored by `.gitignore`; the Clang build tree re-fetches
  GTest/Catch2 via FetchContent.
- TSan-under-Clang (needs `setarch --addr-no-randomize`) is deferred with the
  sanitizer presets.

## Maintenance Rules

- Rebuild the verification image only through an intentional toolchain-update
  change; record the base digest, Clang/`clang-tidy` versions, and package source
  (build log or a tool manifest) each time.
- Never reuse a `T-####`; `T-0007`/`T-0008` are the first tooling-chain tasks.
- Accepted decisions are superseded, not rewritten.

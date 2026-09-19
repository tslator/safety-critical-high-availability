# D-2026-09-19-001: Clang Supplementary Verification Toolchain

- Date: 2026-09-19
- Status: Accepted
- Related decision: [DEC-0008](../decisions/0008-clang-supplementary-verification.md)
- Resulting tasks: [T-0007](../tasks/T-0007-pin-primary-baseline.md),
  [T-0008](../tasks/T-0008-clang-verify-pipeline.md)

## Question

GCC 13 is the intended primary compiler, but running Clang as an independent
checker is valuable. Should the Clang toolchain live globally on the host, or be
project-scoped and "tagged" for its verification purpose, and how should that
purpose be made unambiguous? (Source:
[clang-supplement-discussion.pdf](../discussions/clang-supplement-discussion.pdf).)

## Findings

- A project-owned, version-pinned Clang *verification container* plus a named
  CMake preset gives a documented role, reproducible versions, no host drift, a
  separate build tree (no CMake compiler/object mixing), and collectable
  evidence — without vendoring Clang into the repo or trusting an unversioned
  host install.
- Repo reality today: host is GCC 13.3.0 with **no** Clang; the container builds
  with **GNU 12.2.0** via `build-essential` (NOTES.md), so the *primary* compiler
  is neither explicit nor GCC 13 — the PDF's "immediate item to resolve."
- Warnings are already per-target and compiler-conditional
  (`safety_crit_apply_warnings`, `GNU|Clang`), so a Clang build inherits them and
  third-party framework headers stay exempt — the toolchain file must NOT add
  global `add_compile_options`.
- Sanitizers already support `Clang|GNU` (`Sanitizers.cmake`), so Clang sanitizer
  presets are presets-only, not new logic.
- `SAFETY_CRITICAL_HA_PLAN.md` already anticipated "GCC 13 / Clang 17"; the
  Clang half was never built.

## Alternatives Considered

- **Global host Clang install:** rejected — unversioned, drifts, not an
  authoritative or reproducible checker.
- **Vendor Clang into the repo:** rejected — heavy, supply-chain burden.
- **CI-native pinned `apt install` only:** rejected for the authoritative path —
  less reproducible and still drifts locally; kept as an optional convenience.
- **Pinned verification container (Chosen):** matches the existing digest-pinned
  Dockerfile precedent and makes the role explicit in image tag, preset, and CI.

## Result

Adopt a pinned Clang verification container + a `clang-verify` preset, roll the
gates out incrementally (build/test first; clang-tidy advisory later; sanitizers
later), keep Clang framed as *verification evidence, not validation*, resolve the
primary-baseline ambiguity as a separate prerequisite, and make no CORE.md Tier-1
change. Rationale in
[DEC-0008](../decisions/0008-clang-supplementary-verification.md).

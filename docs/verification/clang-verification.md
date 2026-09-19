# Clang Supplementary Verification (`clang-verify`)

## Role

`clang-verify` is an independent clean build + CTest run with
`clang-14` (Debian bookworm's supported Clang major) inside a dedicated,
digest-pinned verification container. It exists to surface diagnostic
differences between compiler implementations and to run the suite under a
second implementation. See
[DEC-0008](../decisions/0008-clang-supplementary-verification.md) for the
decision and [T-0008](../tasks/T-0008-clang-verify-pipeline.md) for the task.

Per DEC-0008 #6: a successful `clang-verify` run is **verification evidence**;
it does not, by itself, constitute system validation or safety certification.
Clang is supplementary - it is not a CORE.md Tier-1 safety gate, and the
`gcc-primary` container build remains the baseline artifact.

## Naming

| Term | Meaning |
|---|---|
| `gcc-primary` | Primary compiler: `g++-12` in the digest-pinned primary image (see [DEVELOPMENT.md](DEVELOPMENT.md) "Compiler baseline") |
| `clang-verify` | This pipeline: clean build + ctest with `clang-14` |
| `clang-static-analysis` | Deferred clang-tidy run (advisory/report-only when introduced) |
| `clang-sanitizers` | Deferred sanitizer presets under Clang |
| `verification-toolchain` | The `safety-critical-ha:verify-clang-14` image |

## Components

- `CMake/toolchains/clang-verify.cmake` - compilers + C++20 cache vars only
  (no compile options; the warning policy stays in `CMake/Warnings.cmake` so
  FetchContent'd GTest/Catch2 are never warned on).
- `CMakePresets.json` (repo root, `version: 3`) - `clang-verify` configure,
  build, and test presets with their own `build/clang-verify` tree.
- `containers/verification/Dockerfile.clang` - digest-pinned `bookworm` base
  (same libc/libstdc++ as the primary image, isolating compiler differences);
  `clang-14` + `clang-tidy-14` (for the deferred static-analysis task);
  toolchain identity echo on every image build.
- CI job `clang-verify` in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) -
  builds the image and runs the presets in it; CTest log uploaded on failure.

## Running Locally

```bash
docker build -f containers/verification/Dockerfile.clang \
  -t safety-critical-ha:verify-clang-14 .
docker run --rm --workdir /workspace -v "$PWD:/workspace" \
  safety-critical-ha:verify-clang-14 \
  bash -o pipefail -c "
    cmake --preset clang-verify &&
    cmake --build --preset clang-verify --parallel &&
    ctest --preset clang-verify
  "
```

## Evidence

### 2026-09-19 (T-0008 implementation, host-side run)

Host: x86_64 Linux, Docker Engine 29.6.2. Full command log: `NOTES.md`,
section "T-0008".

| Aspect | Evidence | Result |
|---|---|---|
| Toolchain identity | Verification image build log (`dpkg-query` + `--version` echo) | `clang-14 1:14.0.6-12` (src `llvm-toolchain-14`), `clang version 14.0.6`, `clang-tidy-14 1:14.0.6-12`, cmake 3.25.1-1, ninja 1.11.1-2~deb12u1; base `debian:bookworm@sha256:813017f3...` |
| Configure | `cmake --preset clang-verify` in image; `CMakeCache.txt` / `CMakeCXXCompiler.cmake` | PASS - `CMAKE_CXX_COMPILER:STRING=/usr/bin/clang++-14`, `CMAKE_CXX_COMPILER_ID "Clang"` |
| Build | `cmake --build --preset clang-verify --parallel` in image | PASS - all targets built, zero warnings on first-party code under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` |
| Test | `ctest --preset clang-verify` in image | PASS - 100% tests passed, 0 failed out of 41 |
| gcc-primary regression | Host fresh dirs: GoogleTest, Catch2, ASan+UBSan | PASS - 41/41 each, zero compile warnings (GCC link line unchanged: `libatomic` links only under `CMAKE_CXX_COMPILER_ID MATCHES "Clang"`) |
| Adapter drift | `./scripts/sync-agent-guidance.sh --check` | PASS - adapters byte-identical (CORE.md untouched) |
| Hosted CI `clang-verify` job | `.github/workflows/ci.yml` | Pending first hosted run (record run ID here and in NOTES.md after push) |

### Diagnostic differences surfaced (the point of the pipeline)

First run under Clang found three real issues, all fixed without touching
`gcc-primary` behavior:

1. `constexpr line_of()` helpers in three test files used `reinterpret_cast`,
   which is not a constant expression; Clang flags this as default-error
   `-Winvalid-constexpr`. Fixed with `std::bit_cast` (C++20), valid on both
   compilers.
2. Three local `using Ring8 = ...` aliases in `ring_buffer_test.cpp` shadowed
   the identical namespace-scope alias (Clang `-Wshadow` covers type aliases;
   GCC does not). Removed the duplicates.
3. Clang's codegen does not inline `std::atomic<T>::is_lock_free()` the way
   GCC's does, leaving an out-of-line `__atomic_is_lock_free` reference; the
   link failed without `libatomic`. `shared-memory/CMakeLists.txt` now links
   `atomic` PUBLIC only when the compiler is Clang, so the primary link line
   is unchanged.

### 2026-09-19 (hosted CI)

(To be appended after the first hosted run of the `clang-verify` job: run ID,
commit, job result, toolchain versions from the job log.)

## Maintenance

Rebuild the verification image only through an intentional toolchain-update
change, re-recording the base digest, Clang/`clang-tidy` versions, and package
source per the DEC-0008 maintenance rule (Dockerfile comment + NOTES.md + the
table above). Newer Clang majors require the LLVM APT repository and a new
decision (independence, not recency).

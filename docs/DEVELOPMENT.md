# Development Guide

## Native Build and Test

```bash
cmake -S . -B build/gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
cmake --build build/gtest --parallel
ctest --test-dir build/gtest --output-on-failure

cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
cmake --build build/catch2 --parallel
ctest --test-dir build/catch2 --output-on-failure
```

The sanitizer matrix uses the same framework configurations with ASan+UBSan
or TSan enabled. See [CI](../.github/workflows/ci.yml) for the authoritative
workflow commands.

## Compiler Baseline

The primary ("gcc-primary") build compiler is the container image's compiler:
Debian bookworm's supported default GCC 12, installed explicitly as the
versioned `g++-12` package (source: `deb.debian.org` `bookworm/main`) and
selected with `-DCMAKE_CXX_COMPILER=g++-12` in the Dockerfile builder stage.
The exact package version and `g++-12 --version` output are recorded in the
Dockerfile comment and in the `NOTES.md` "T-0007" section at each validated
rebuild ([DEC-0008](decisions/0008-clang-supplementary-verification.md)
maintenance rule).

Debian bookworm and `bookworm-backports` ship no `gcc-13`/`g++-13` package
(verified 2026-09-19). Pinning GCC 13 would require a third-party toolchain
repository, which the Phase 0 toolchain policy
([PHASE_0_CONTAINER_AND_TOOLING.md](phases/PHASE_0_CONTAINER_AND_TOOLING.md),
"Toolchain Decision") declines to adopt.

Reporting per environment:

| Environment | Compiler | How it is reported |
|---|---|---|
| Container image (baseline artifact) | `g++-12` (Debian 12.2.0-14+deb12u1) 12.2.0 | `docker run --rm safety-critical-ha:phase0 --version` prints `Compiler: GNU ...`; the builder stage logs `g++-12 --version` |
| Host (reference dev host) | distro-default `g++` (Ubuntu 13.3.0) | `g++ --version` |
| CI native/sanitizer jobs | distro-default `g++` on `ubuntu-latest` (13.x) | the "Report primary compiler version" step prints `g++ --version` on every run |

The host/CI-versus-container major-version divergence is explicit and
intentional: the digest-pinned container with its explicit compiler package
is the reproducible baseline artifact, while host and CI iterate on their
distro-default compilers. Per [DEC-0008](decisions/0008-clang-supplementary-verification.md)
§7, results count as attributable baseline evidence once the compiler is
recorded as above.

```bash
docker build --pull --tag safety-critical-ha:phase0 .
docker run --rm safety-critical-ha:phase0 --version   # prints "Compiler: GNU ..."
```

## Clang Verification (clang-verify)

Supplementary verification with an independent compiler
([DEC-0008](decisions/0008-clang-supplementary-verification.md)): a clean
build + full CTest run with `clang-14` from the digest-pinned verification
container. A successful run is verification evidence, not system validation;
the `gcc-primary` container build remains the baseline artifact.

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

The `clang-verify` root CMake presets (toolchain file
`CMake/toolchains/clang-verify.cmake`, separate `build/clang-verify` tree)
also work directly on a host that has `clang-14` installed. See
[clang verification](verification/clang-verification.md) for role framing,
the evidence table, and maintenance rules.

## Container Smoke Test

```bash
./run_demo.sh
```

This validates Compose configuration, builds the image, starts the default
services, checks the runtime binary, and tears the stack down.

## Documentation Workflow

Record design discussions as concise review or discussion notes. Record an
accepted design choice in `decisions/`, turn agreed implementation into a task
under `tasks/`, and link validation results from `evidence/`.

# Phase 0 - Container and Tooling Setup

## Purpose

Establish a reproducible C++20 build and test baseline for the Safety-Critical
High-Availability (HA) application. At the end of this phase, a developer can
build, test, and run a minimal application locally and inside Docker. The
baseline also proves that the intended runtime capabilities and resource limits
can be requested before shared memory, worker, supervisor, or fault-injection
logic is introduced.

This document decomposes Phase 0 from
`SAFETY_CRITICAL_HA_PLAN.md` into tasks intended to take one developer three to
four hours each. Complete tasks in order; each verification gate is a required
input to the next task.

## Scope and Non-Goals

**In scope**

- C++20 CMake project bootstrap and a smoke-test executable.
- A reproducible multi-stage Debian-based Docker image.
- Test-framework selection between GoogleTest and Catch2 v3.
- Docker Compose runtime configuration for the future supervisor, monitor,
  workers, and on-demand perturbation tool.
- CI verification of container builds and tests.
- Documented host and container prerequisites for future real-time and
  ptrace-based experiments.

**Out of scope**

- Lock-free queues, shared-memory data structures, worker processing, HTTP
  endpoints, failover, and actual fault injection. Those begin in Phase 1.
- Claiming hard real-time guarantees. Docker can request scheduling privileges,
  but host scheduling, kernel configuration, and container runtime behavior
  still add jitter.
- Giving the application privileged mode. The initial configuration uses the
  least capabilities planned for later work: `SYS_NICE` and `SYS_PTRACE`.

## Target Outcome

The repository contains this initial layout after all tasks complete:

```text
.
├── .github/workflows/ci.yml
├── CMakeLists.txt
├── CMake/
│   ├── Sanitizers.cmake
│   └── TestFramework.cmake
├── Dockerfile
├── docker-compose.yml
├── app/
│   ├── CMakeLists.txt
│   └── src/main.cpp
├── tests/
│   ├── CMakeLists.txt
│   └── smoke_test.cpp
├── run_demo.sh
├── README.md
├── PHASE_0_CONTAINER_AND_TOOLING.md
└── SAFETY_CRITICAL_HA_PLAN.md
```

The project uses `SAFETY_CRIT_TEST_FRAMEWORK` consistently. Valid values are
`GoogleTest` (the default) and `Catch2`. This spelling is deliberate: the
source plan uses both `SAFETY_CRIT_TEST_FRAMEWORK` and `SAFCRIT_TEST_FRAMEWORK`;
the former is the Phase 0 contract.

## Prerequisites

Perform these checks before Task 0.1. Record the command output in the pull
request or task record when a gate asks for evidence.

### Required Host Software

| Requirement | Minimum | Verification |
|---|---:|---|
| Linux host | Kernel with cgroups v2 | `test -f /sys/fs/cgroup/cgroup.controllers` |
| Docker Engine | 24 or newer | `docker version --format '{{.Server.Version}}'` |
| Docker Compose plugin | v2 | `docker compose version` |
| Git | Any maintained version | `git --version` |
| Disk space | 10 GB free for images and build cache | `df -h .` |
| Memory | 4 GB available to Docker | Docker Desktop/Engine configuration or `free -h` |

The host user must be able to run `docker` without `sudo`, or the project team
must document the approved privilege-escalation procedure. Do not put user
passwords or registry credentials in repository files.

### Required Runtime Capabilities

Later phases need Linux-only behaviors. Confirm that the development
environment permits them, but do not fail Phase 0 merely because a restricted
CI runner cannot use real-time scheduling.

```bash
docker run --rm --cap-add=SYS_NICE debian:bookworm sh -ec \
  'grep CapEff /proc/self/status; test -r /proc/self/status'

docker run --rm --cap-add=SYS_PTRACE debian:bookworm sh -ec \
  'grep CapEff /proc/self/status; test -r /proc/self/status'
```

For local cgroup v2 validation, `docker info` must report a cgroup v2 driver or
the environment must document why CPU and memory controls cannot be tested
locally. `SYS_PTRACE` permits planned ptrace/process-memory experiments but
does not bypass Linux Yama `ptrace_scope`, user namespace, or PID namespace
restrictions. Task 0.6 records the runtime behavior observed on the supported
development host.

### Toolchain Decision

Use a compiler image that positively supports C++20. Debian bookworm's default
compiler may be GCC 12, so the Dockerfile must not assume that an unconfigured
bookworm package repository supplies GCC 13. Choose and document one of these
repeatable options in Task 0.3:

1. Debian bookworm with its supported default `g++`, provided the compiler
   passes the configured C++20 feature probe.
2. A pinned LLVM or GCC toolchain repository/image with package-version pins.
3. A pinned compiler base image used only by the builder stage, with a Debian
   bookworm-slim runtime stage.

The project standard is C++20. C++23 features, including `std::expected`, are
not permitted in Phase 0 production code unless a compatibility layer is added
and tested later.

## Task Plan

### Task 0.1 - Confirm Host Baseline and Repository Conventions

**Estimated effort:** 3 hours

**Dependencies:** None.

**Implementation steps**

1. Run every command in [Required Host Software](#required-host-software).
2. Run the capability probes in [Required Runtime Capabilities](#required-runtime-capabilities).
3. Create `.gitignore` entries for CMake build directories, test output,
   coverage output, editor files, and local environment files. Do not ignore
   source, CMake, Docker, or CI configuration files.
4. Add a `README.md` bootstrap section covering the host requirements, the
   supported commands, and the known Docker/real-time limitations.
5. Decide which compiler strategy from [Toolchain Decision](#toolchain-decision)
   will be used and record the exact image, repository, and package versions in
   the README or Dockerfile comments.

**Deliverables**

- `.gitignore`.
- `README.md` with host prerequisites, supported platform, and limitations.
- Recorded baseline output for Docker Engine, Docker Compose, cgroup mode, and
  selected compiler strategy.

**Verification gate G0.1**

```bash
test -f /sys/fs/cgroup/cgroup.controllers
docker version --format '{{.Server.Version}}'
docker compose version
git status --short
```

Pass when Docker and Compose are available, cgroups v2 is either verified or a
documented platform exception exists, and `git status` shows only intentional
bootstrap files. A restricted remote runner may proceed with an explicit note
that capability behavior will be verified on the supported Linux host.

---

### Task 0.2 - Bootstrap a Strict CMake C++20 Project

**Estimated effort:** 3-4 hours

**Dependencies:** G0.1.

**Implementation steps**

1. Create the top-level `CMakeLists.txt` with `cmake_minimum_required(VERSION
   3.24)`, a project declaration, and strict C++20 configuration:

   ```cmake
   set(CMAKE_CXX_STANDARD 20)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
   set(CMAKE_CXX_EXTENSIONS OFF)
   ```

2. Define options with safe defaults:

   ```cmake
   option(SAFETY_CRIT_BUILD_TESTING "Build tests" ON)
   set(SAFETY_CRIT_TEST_FRAMEWORK "GoogleTest" CACHE STRING
       "Test framework: GoogleTest or Catch2")
   set_property(CACHE SAFETY_CRIT_TEST_FRAMEWORK PROPERTY STRINGS GoogleTest Catch2)
   option(SAFETY_CRIT_ENABLE_ASAN "Enable AddressSanitizer" OFF)
   option(SAFETY_CRIT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
   option(SAFETY_CRIT_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
   ```

3. Create `app/CMakeLists.txt` and `app/src/main.cpp`. The executable must be
   named `safety-critical-ha`, return zero for `--version`, and print the
   project name and build compiler. It must reject unknown arguments with a
   nonzero exit code.
4. Set warnings on project-owned C++ targets, using compiler-conditional flags:
   `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` for GCC/Clang. Warnings are
   errors only when a `SAFETY_CRIT_WARNINGS_AS_ERRORS` option is enabled; do not
   force third-party test framework headers to satisfy project warning policy.
5. Call `include(CTest)` and add subdirectories conditionally. The initial
   configuration must succeed with tests disabled.
6. Add a CMake configure-time compiler probe for a required C++20 facility
   used by the project (for example `std::span` or `std::jthread`) and fail with
   a clear message if unavailable.

**Deliverables**

- Top-level and app CMake files.
- Minimal `safety-critical-ha` executable.
- C++20 compiler capability probe.

**Verification gate G0.2**

```bash
cmake -S . -B build/local -G Ninja -DSAFETY_CRIT_BUILD_TESTING=OFF
cmake --build build/local --parallel
./build/local/app/safety-critical-ha --version
ctest --test-dir build/local --output-on-failure
```

Pass when configuration and compilation succeed, `--version` exits zero, and
CTest completes successfully with zero registered tests. If Ninja is not
available, use the platform's default CMake generator and document it; Docker
builds should install and use Ninja for consistent CI behavior.

---

### Task 0.3 - Build a Reproducible Docker Image

**Estimated effort:** 3-4 hours

**Dependencies:** G0.2.

**Implementation steps**

1. Add `.dockerignore` to exclude Git metadata, host build directories,
   editor files, test artifacts, and local environment files. It must not
   exclude any source, CMake, test, Docker, or script file needed by the build.
2. Create a multi-stage `Dockerfile`:
   - The builder stage installs the selected C++ compiler, CMake 3.24 or newer,
     Ninja, Git, and the test-framework build prerequisites.
   - The builder configures, builds, and runs CTest. Use a noninteractive APT
     configuration and remove package-list files in each stage.
   - The runtime stage is `debian:bookworm-slim` (pinned by digest or a
     documented immutable release policy) and contains only the executable plus
     diagnostic tools required by the plan: `gdb`, `perf`, `sysstat`, `procps`,
     `curl`, `strace`, and `valgrind` where available.
   - The binary is installed at `/usr/local/bin/safety-critical-ha` and is the
     runtime entry point.
3. Add `/dev/shm` documentation, but do not declare it as a normal Docker image
   `VOLUME`; Docker's default tmpfs behavior must be configured at run time by
   Compose. The Compose definition will set `shm_size` explicitly.
4. Add a container smoke command that runs `safety-critical-ha --version`.
5. Build with plain progress in the task record when troubleshooting, so the
   configured compiler version and failing package operation remain visible.

**Deliverables**

- `Dockerfile` and `.dockerignore`.
- Builder image that compiles and runs tests during `docker build`.
- Minimal runtime image with the production executable and diagnostic tools.

**Verification gate G0.3**

```bash
docker build --pull --tag safety-critical-ha:phase0 .
docker run --rm safety-critical-ha:phase0 --version
docker image inspect safety-critical-ha:phase0 \
  --format '{{json .Config.Entrypoint}}'
```

Pass when the image builds from a clean context, the build-stage CTest run
succeeds, the runtime smoke command exits zero, and the entry point is exactly
the installed application. Record `docker image inspect` size as a baseline;
it is observational, not a hard Phase 0 limit.

---

### Task 0.4 - Add Framework-Independent Test Configuration

**Estimated effort:** 3-4 hours

**Dependencies:** G0.3.

**Implementation steps**

1. Create `CMake/TestFramework.cmake` and validate
   `SAFETY_CRIT_TEST_FRAMEWORK` before resolving dependencies. Invalid values
   must fail configuration with an explicit list of valid choices.
2. Use CMake `FetchContent` with pinned, versioned release archives and
   checksums where CMake supports them. Do not rely on host-installed GTest or
   Catch2 packages. Provide `FETCHCONTENT_FULLY_DISCONNECTED` support for
   approved offline builds with pre-populated dependencies.
3. For `GoogleTest`, provide a target that links `GTest::gtest_main` and
   register tests with `gtest_discover_tests`.
4. For `Catch2`, provide a target that links `Catch2::Catch2WithMain`, include
   its CMake extras, and register tests with `catch_discover_tests`.
5. Do not attempt to define test assertions through CMake compile definitions
   or include a `.cmake` file from C++. Instead, create a small C++ test adapter
   header only if common test source is necessary. The adapter must select the
   correct framework headers and map only the assertions actually required.
6. Create `tests/CMakeLists.txt` and a `smoke_test.cpp` that proves the chosen
   framework executes. Keep the test independent of future shared-memory APIs;
   test the application version string or a small header-only utility instead.
7. Add `CMake/Sanitizers.cmake`. It must reject incompatible sanitizer
   combinations such as ThreadSanitizer with AddressSanitizer and apply flags to
   project targets only. Sanitizers remain opt-in in Phase 0.

**Deliverables**

- Pinned framework dependency configuration in `CMake/TestFramework.cmake`.
- `CMake/Sanitizers.cmake` with explicit compatibility checks.
- One registered smoke test that runs under each framework.

**Verification gate G0.4**

```bash
cmake -S . -B build/gtest -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
cmake --build build/gtest --parallel
ctest --test-dir build/gtest --output-on-failure

cmake -S . -B build/catch2 -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
cmake --build build/catch2 --parallel
ctest --test-dir build/catch2 --output-on-failure

cmake -S . -B build/invalid-framework \
  -DSAFETY_CRIT_TEST_FRAMEWORK=InvalidFramework
```

Pass when each valid framework has at least one discovered and passing test,
and the final configure command fails with the expected invalid-value message.
Repeat the GoogleTest configuration with `-DSAFETY_CRIT_ENABLE_ASAN=ON
-DSAFETY_CRIT_ENABLE_UBSAN=ON` and run CTest successfully. TSan is configured
and documented but need not run in this task because it is often incompatible
with constrained container environments and future lock-free code.

---

### Task 0.5 - Configure Compose Services and Resource Controls

**Estimated effort:** 3-4 hours

**Dependencies:** G0.3 and G0.4.

**Implementation steps**

1. Create `docker-compose.yml` using Compose Specification syntax; omit the
   obsolete top-level `version` field.
2. Define a shared named volume or tmpfs strategy for future application state,
   and set `shm_size` explicitly for every service that will access `/dev/shm`.
   Do not use a bind mount over `/dev/shm`.
3. Define these services from the same built image: `supervisor`, `monitor`,
   `worker-a`, `worker-b`, `worker-c`, and a profile-gated `perturb` service.
   The Phase 0 command for each regular service may be `--version`; it only
   proves image wiring until Phase 2 creates real modes.
4. Set `cap_add: [SYS_NICE, SYS_PTRACE]` only on services that will need them:
   supervisor, workers, and perturb. The monitor receives neither capability
   unless a demonstrated future requirement appears.
5. Set explicit CPU and memory controls for every service. Compose support for
   `deploy.resources` varies outside Swarm, so also use supported local Compose
   controls (`cpus` and `mem_limit`) and confirm them through container inspect.
   Start conservatively: aggregate CPU allocations must not exceed the host
   capacity, and each memory limit must leave room for the configured shared
   memory size.
6. Add explicit networks, service names, restart behavior, healthcheck stubs,
   and labels identifying the component role. The perturb service must use a
   Compose profile so it is never started by the default `docker compose up`.
7. Keep the default stack non-privileged. Document that a specific later test
   may require additional host policy changes; do not add `privileged: true` as
   a general workaround.

**Deliverables**

- Valid `docker-compose.yml` defining the six named services.
- Explicit capabilities, resources, shared-memory sizing, healthcheck stubs,
  network, and perturbation profile.
- README commands for start, status, logs, and teardown.

**Verification gate G0.5**

```bash
docker compose config --quiet
docker compose build
docker compose up --wait --no-build
docker compose ps
docker compose exec supervisor safety-critical-ha --version
docker compose down --volumes --remove-orphans
```

Pass when Compose validation succeeds, all default services become healthy or
complete successfully, `perturb` is absent from the default stack, and teardown
leaves no project containers or volumes. Inspect at least one worker container
to prove the expected capabilities, memory limit, CPU limit, and `/dev/shm`
size are actually present; attach that output to the task evidence.

---

### Task 0.6 - Add CI and Runtime Diagnostics

**Estimated effort:** 3-4 hours

**Dependencies:** G0.5.

**Implementation steps**

1. Create `.github/workflows/ci.yml` with separate jobs for:
   - Native CMake configure/build/test using the default GoogleTest framework.
   - Native CMake configure/build/test using Catch2.
   - Container image build, which runs its build-stage CTest gate.
   - Docker runtime smoke test: run the built image with `--version`.
2. Pin actions by immutable commit SHA under the repository's security policy.
   Configure minimal `contents: read` permissions and no write token access.
3. Cache CMake/FetchContent artifacts only if cache keys include OS, compiler,
   CMake configuration, selected framework, and dependency-lock inputs. A cache
   miss must still produce a clean build.
4. Make CI logs preserve `ctest --output-on-failure` output. Upload CTest logs
   and sanitizer logs on failure, using a short retention period appropriate to
   the repository policy.
5. Add `run_demo.sh` as an executable Phase 0 smoke script. It must build the
   Compose stack, validate configuration, start default services, show status,
   run the version check, and always tear down through a shell `trap`.
6. Add a documented diagnostic checklist to the README for failed container
   builds, missing capabilities, cgroup mode, and service logs. Include
   `docker compose logs`, `docker inspect`, `cat /proc/self/status`, and
   `cat /sys/fs/cgroup/cgroup.controllers` as applicable.

**Deliverables**

- GitHub Actions workflow with native, framework-switch, container-build, and
  runtime-smoke coverage.
- Executable `run_demo.sh` that is idempotent and cleans up on failure.
- CI artifact retention and failure-log behavior.
- README diagnostics and recovery commands.

**Verification gate G0.6**

```bash
shellcheck run_demo.sh
./run_demo.sh
git diff --check
```

Pass when ShellCheck reports no errors, the demo exits zero from a clean Docker
state and removes its resources, and Git reports no whitespace errors. Before
merge, the CI workflow must pass in the repository's hosted CI environment.
If the hosted runner cannot honor real-time or ptrace capabilities, its job
must run the smoke test without claiming those features were exercised; retain
the capability probe evidence from G0.1 for the supported host.

## Phase Exit Gate

Phase 0 is complete only when all six task gates pass and the following
evidence is attached to the implementation change:

| Required evidence | Source gate |
|---|---|
| Host Docker/Compose/cgroup prerequisite record | G0.1 |
| Clean local CMake C++20 configuration and app smoke output | G0.2 |
| Clean Docker build and runtime image smoke output | G0.3 |
| Passing GoogleTest and Catch2 CTest output; invalid option failure | G0.4 |
| Compose configuration, stack status, and runtime-inspection output | G0.5 |
| Passing demo, shell lint, whitespace check, and hosted CI result | G0.6 |

No Phase 1 shared-memory implementation may be accepted until this exit gate
is satisfied. This creates a known-good platform baseline and makes later
failures attributable to application behavior rather than missing build or
runtime setup.

## Handoff to Phase 1

Phase 1 begins by adding the `shared-memory/` CMake target and its tests to the
existing build graph. It must preserve the same compiler standard, warning
policy, sanitizer options, framework selection, container image, and CI gates
established here. New shared-memory tests become additional CTest coverage;
they must not replace the Phase 0 smoke tests.
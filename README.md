# Safety-Critical High Availability

## Bootstrap

This Phase 0 project provides a C++20 CMake build, smoke tests, and a
containerized runtime baseline. It does not yet implement the shared-memory,
worker, supervisor, failover, or fault-injection features planned for later
phases.

### Host Requirements

The supported development environment is a Linux host with:

- A kernel using cgroups v2 (`test -f /sys/fs/cgroup/cgroup.controllers`)
- Docker Engine 24 or newer (`docker version --format '{{.Server.Version}}'`)
- Docker Compose plugin v2 (`docker compose version`)
- Git (`git --version`)
- At least 10 GB of free disk space for images and build caches
- At least 4 GB of memory available to Docker

The host user must be able to run Docker without `sudo`, or use the approved
local privilege procedure. Ninja is recommended for consistent native builds.
The project requires a compiler with C++20 support.

### Supported Commands

Configure and build without tests:

```bash
cmake -S . -B build/local -G Ninja \
	-DSAFETY_CRIT_BUILD_TESTING=OFF
cmake --build build/local --parallel
./build/local/app/safety-critical-ha --version
ctest --test-dir build/local --output-on-failure
```

Configure, build, and test with GoogleTest or Catch2:

```bash
cmake -S . -B build/gtest -G Ninja \
	-DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
cmake --build build/gtest --parallel
ctest --test-dir build/gtest --output-on-failure

cmake -S . -B build/catch2 -G Ninja \
	-DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
cmake --build build/catch2 --parallel
ctest --test-dir build/catch2 --output-on-failure
```

Build and smoke-test the container image:

```bash
docker build --pull --tag safety-critical-ha:phase0 .
docker run --rm safety-critical-ha:phase0 --version
docker image inspect safety-critical-ha:phase0 \
	--format '{{json .Config.Entrypoint}}'
```

Validate and exercise the default Compose stack:

```bash
docker compose config --quiet
docker compose build
docker compose up --wait --no-build
docker compose ps
docker compose exec supervisor safety-critical-ha --version
docker compose logs
docker compose down --volumes --remove-orphans
```

The default Compose stack does not start the profile-gated `perturb` service.
The repository's smoke script runs the Compose validation, startup, version
check, status display, and teardown sequence:

```bash
./run_demo.sh
```

### Known Limitations

- This is a Linux-oriented baseline. Windows and macOS hosts may run some
	build steps, but the documented cgroups, capability, shared-memory, and
	scheduling behavior is only supported on Linux.
- Docker and Compose can request `SYS_NICE`, `SYS_PTRACE`, CPU, memory, and
	shared-memory settings, but they cannot guarantee hard real-time behavior.
	Host scheduling, kernel configuration, cgroup policy, container runtime
	overhead, and resource contention can all add jitter.
- `SYS_PTRACE` does not bypass Linux Yama `ptrace_scope`, user namespaces, or
	PID namespace restrictions. A restricted CI runner may be unable to exercise
	ptrace or real-time capabilities even when the image builds successfully.
- The default stack is intentionally non-privileged. Do not use
	`privileged: true` as a general workaround; later experiments may require
	separately documented host policy changes.
- `/dev/shm` sizing is configured at Compose runtime with `shm_size`; it is
	not declared as an image volume.
- Phase 0 smoke commands verify build and wiring only. They do not demonstrate
	failover, shared-memory correctness, fault injection, or real-time guarantees.

For diagnostics, inspect service logs and runtime capabilities with:

```bash
docker compose logs <service>
docker inspect <container>
cat /proc/self/status
cat /sys/fs/cgroup/cgroup.controllers
```

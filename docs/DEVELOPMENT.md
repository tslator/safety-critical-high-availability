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

Gate: G0.3
Date: 2026-08-20
|Command|Result|
|---|---|
|`docker build --pull --progress=plain --tag safety-critical-ha:phase0 .`|PASS (builder CTest succeeded)|
|`docker run --rm safety-critical-ha:phase0 --version`|PASS|
|`docker image inspect safety-critical-ha:phase0 --format '{{json .Config.Entrypoint}}'`|["/usr/local/bin/safety-critical-ha"]|
|`docker image inspect safety-critical-ha:phase0 --format '{{.Size}}'`|124498471|

---

Gate: G0.4
Date: 2026-08-20
|Command|Result|
|---|---|
|`cmake -S . -B build/gtest -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
cmake --build build/gtest --parallel
ctest --test-dir build/gtest --output-on-failure`|`cmake -S . -B build/gtest -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
cmake --build build/gtest --parallel
ctest --test-dir build/gtest --output-on-failure
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /home/tim/projects/github/safety-critical-high-availability/build/gtest
ninja: no work to do.
Internal ctest changing into directory: /home/tim/projects/github/safety-critical-high-availability/build/gtest
Test project /home/tim/projects/github/safety-critical-high-availability/build/gtest
    Start 1: Smoke.AlwaysPasses
1/1 Test #1: Smoke.AlwaysPasses ...............   Passed    0.00 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.00 sec`|
|`cmake -S . -B build/catch2 -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
cmake --build build/catch2 --parallel
ctest --test-dir build/catch2 --output-on-failure`|`cmake -S . -B build/catch2 -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
cmake --build build/catch2 --parallel
ctest --test-dir build/catch2 --output-on-failure
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /home/tim/projects/github/safety-critical-high-availability/build/catch2
ninja: no work to do.
Internal ctest changing into directory: /home/tim/projects/github/safety-critical-high-availability/build/catch2
Test project /home/tim/projects/github/safety-critical-high-availability/build/catch2
    Start 1: Smoke AlwaysPasses
1/1 Test #1: Smoke AlwaysPasses ...............   Passed    0.00 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.00 sec`|
|`cmake -S . -B build/invalid-framework \
  -DSAFETY_CRIT_TEST_FRAMEWORK=InvalidFramework`|`cmake -S . -B build/invalid-framework \
  -DSAFETY_CRIT_TEST_FRAMEWORK=InvalidFramework
CMake Error at CMake/TestFramework.cmake:3 (message):
  Invalid SAFETY_CRIT_TEST_FRAMEWORK='InvalidFramework'.  Supported values
  are 'GoogleTest' and 'Catch2'.
Call Stack (most recent call first):
  CMakeLists.txt:21 (include)


-- Configuring incomplete, errors occurred!`|
|`cmake -S . -B build/gtest-asan-ubsan -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest \
  -DSAFETY_CRIT_ENABLE_ASAN=ON \
  -DSAFETY_CRIT_ENABLE_UBSAN=ON
cmake --build build/gtest-asan-ubsan --parallel
ctest --test-dir build/gtest-asan-ubsan --output-on-failure`|`cmake -S . -B build/gtest-asan-ubsan -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest \
  -DSAFETY_CRIT_ENABLE_ASAN=ON \
  -DSAFETY_CRIT_ENABLE_UBSAN=ON
cmake --build build/gtest-asan-ubsan --parallel
ctest --test-dir build/gtest-asan-ubsan --output-on-failure
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /home/tim/projects/github/safety-critical-high-availability/build/gtest-asan-ubsan
ninja: no work to do.
Internal ctest changing into directory: /home/tim/projects/github/safety-critical-high-availability/build/gtest-asan-ubsan
Test project /home/tim/projects/github/safety-critical-high-availability/build/gtest-asan-ubsan
    Start 1: Smoke.AlwaysPasses
1/1 Test #1: Smoke.AlwaysPasses ...............   Passed    0.00 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.00 sec`|

---


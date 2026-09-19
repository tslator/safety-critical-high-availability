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


---

Gate: G0.4 (supplement - offline FetchContent support)
Date: 2026-09-15
|Command|Result|
|---|---|
|`cmake -S . -B /tmp/repro-offline -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest -DFETCHCONTENT_FULLY_DISCONNECTED=ON` (fresh, unpopulated dir)|PASS - configure now fails fast with: "GoogleTest was not made available: target 'GTest::gtest_main' does not exist... sources must already be populated in '/tmp/repro-offline/_deps/googletest-src'". Same fail-fast verified for Catch2 ('Catch2::Catch2WithMain', '_deps/catch2-src'). Previously this combination silently skipped the dependency and failed later at generate time with a misleading "target not found" error.|
|`cmake -S . -B build/gtest-offline -G Ninja -DSAFETY_CRIT_BUILD_TESTING=ON -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest -DFETCHCONTENT_FULLY_DISCONNECTED=ON` (after pre-populating `build/gtest-offline/_deps/googletest-src`) + `cmake --build build/gtest-offline --parallel` + `ctest --test-dir build/gtest-offline --output-on-failure`|PASS - 12/12 targets built offline, 100% tests passed (1/1 Smoke.AlwaysPasses)|
|`cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2 -DFETCHCONTENT_FULLY_DISCONNECTED=ON` + ctest (pre-populated dir)|PASS - configure offline, 100% tests passed|

---

Gate: G0.5
Date: 2026-09-15
Host: x86_64 Linux, Docker Engine 29.6.2, cgroups v2 (`/sys/fs/cgroup/cgroup.controllers` present)
|Command|Result|
|---|---|
|`docker compose config --quiet`|PASS (exit 0)|
|`docker compose build`|PASS - image safety-critical-ha:phase0 built; builder stage ran CMake + Ninja + CTest (1/1 smoke test passed in-image, GNU 12.2.0)|
|`docker compose up --wait --no-build`|PASS - all default services healthy: supervisor, monitor, worker-a, worker-b, worker-c (each "Up ... (healthy)"). `perturb` absent from default stack (profile-gated).|
|`docker compose ps`|PASS - 5/5 services listed, all healthy|
|`docker compose exec supervisor safety-critical-ha --version`|PASS - "safety-critical-ha version 0.1.0 / Compiler: GNU 12.2.0"|
|worker-a runtime inspection (`docker inspect` + in-container `df /dev/shm`) |PASS - CapAdd [CAP_SYS_NICE CAP_SYS_PTRACE]; Memory 536870912 (512 MiB); NanoCpus 500000000 (0.5 CPU); ShmSize 67108864; /dev/shm mounted at 64M|
|`docker compose down --volumes --remove-orphans`|PASS - containers, network, and volumes removed; no project resources remain|

---

Gate: G0.6
Date: 2026-09-15
|Command|Result|
|---|---|
|`shellcheck run_demo.sh` (via `docker run --rm -v $PWD/run_demo.sh:/f:ro koalaman/shellcheck /f`; shellcheck not host-installable without root)|PASS - no findings|
|`bash -n run_demo.sh`|PASS|
|`./run_demo.sh`|PASS - exit 0 from a clean Docker state; validated compose config, built stack, started default services, showed status, ran version check, and tore down via trap (0 containers remaining after run)|
|`git diff --check`|PASS - no whitespace errors|
|Hosted CI (.github/workflows/ci.yml: native-gtest, native-catch2, docker-build, docker-compose-smoke)|PASS - run 35010759345 on commit 157d582 (2026-09-15): all four jobs (Native CMake + GoogleTest, Native CMake + Catch2, Container image smoke, Compose runtime smoke) succeeded|

---

Gate: G1.1 (Phase 1 T1.1 - shared region layout and atomic flags)
Date: 2026-09-15
|Command|Result|
|---|---|
|`cmake -S . -B build/gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest && cmake --build build/gtest --parallel && ctest --test-dir build/gtest --output-on-failure` (fresh dir)|PASS - 8/8 tests, zero warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`. New: AtomicFlags.{SetClearHas,BitsAreIndependent,WithFlagCombinesWithoutCrossBleed}, SharedRegionLayout.{CompileTimeInvariants,NoFalseSharingBetweenWorkers}, SharedRegion.{InitializeAndVerify,RejectsBadIdentity}; Phase 0 Smoke.AlwaysPasses retained.|
|`cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2 && ... ctest` (fresh dir)|PASS - 8/8 tests under Catch2 (suites via `[tag]`).|
|ASan+UBSan matrix: `-DSAFETY_CRIT_ENABLE_ASAN=ON -DSAFETY_CRIT_ENABLE_UBSAN=ON`|PASS - 8/8, no sanitizer diagnostics.|
|Tests-off path: `-DSAFETY_CRIT_BUILD_TESTING=OFF`|PASS - app builds and `--version` exits zero; CTest reports zero tests (expected).|

Notes: `safety_crit_register_tests` now centralizes adapter-header generation + framework selection macro + include path (previously duplicated per module); test adapter macro generalized to `SAFETY_CRIT_TEST_CASE(suite, name)`. `CMake/Warnings.cmake` materializes the Phase 0 Task 0.2 warning policy for first-party targets. `enable_testing()` moved ahead of `add_subdirectory(shared-memory)` so that subtree's tests register (was silently dropped).

Gate: CI Sanitizer Matrix (added at Phase 1 start, ahead of T1.2)
Date: 2026-09-15
|Command / Check|Result|
|---|---|
|`.github/workflows/ci.yml`: new `sanitizers` job, matrix {ASan+UBSan, TSan} x {GoogleTest, Catch2}, fail-fast off. ASAN_OPTIONS=detect_leaks=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1, TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1|PASS - local matrix green; hosted run 35037234276 on commit 545ad5a: all four sanitizer jobs plus native and docker jobs succeeded (8/8 jobs)|
|Local ASan+UBSan + GoogleTest (fresh dir)|PASS - 8/8 tests, no diagnostics|
|Local ASan+UBSan + Catch2 (fresh dir)|PASS - 8/8 tests, no diagnostics|
|Local TSan + GoogleTest (fresh dir)|PASS - 8/8 tests, no diagnostics (under `setarch $(uname -m) --addr-no-randomize`)|
|Local TSan + Catch2 (fresh dir)|PASS - 8/8 tests, no diagnostics (under `setarch $(uname -m) --addr-no-randomize`)|

Notes: TSan's fixed shadow-memory reservation collides with ASLR in this dev sandbox - even `g++ -fsanitize=thread` on `int main(){return 0;}` fails with `FATAL: ThreadSanitizer: unexpected memory mapping` (exit 66). Running the toolchain under `setarch --addr-no-randomize` resolves it, so the CI step does exactly that for the TSan combinations (personality is inherited by discovery and ctest children); no effect on the ASan+UBSan jobs. Workflow renamed "Phase 0 CI" -> "CI".

Gate: G1.2 (Phase 1 T1.2 - lock-free MPMC ring buffer)
Date: 2026-09-17
|Command / Check|Result|
|---|---|
|Local matrix {GoogleTest, Catch2} x {plain, ASan+UBSan, TSan}, fresh build dirs each; TSan under `setarch $(uname -m) --addr-no-randomize`|PASS - 24/24 tests per configuration (16 new: RingBufferProtocol x10 incl. FutureLapCorruptionRejectedByWindowCheck / WindowCheckAcceptsQuiescentBoundaries / InFlightClaimReadsAsInconsistentUntilCommitted, RingBufferOrdering.SPSC FIFO 4096, RingBufferMultithreaded x2, SharedRegionRings x3), zero warnings, zero sanitizer diagnostics in all six configs|
|Stress soaks (standalone against the shipped header): 8 producers x 20k items through the default 1024-slot ring, per-producer FIFO integrity + `verify_consistent()` at quiescence, 5x10s runs|PASS - 160,000 items per run, zero payload corruption, state consistent at quiescence every run|
|Negative-witness experiments (GCC 13.3.0): `requires { typename LockFreeRingBuffer<1,16>; }` and a type-parameter void_t trait both hard-error; the NTTP-parameter `ring_instantiable<N,B>` trait compiles with all three constraint witnesses holding|PASS - behavior matches the reworded test comment (deviation #8)|
|Hosted CI (.github/workflows/ci.yml: native x2, sanitizers {ASan+UBSan, TSan} x2, docker x2)|PASS - run 35214976207 on commit 95f9506 (2026-09-17): all eight jobs succeeded|

Notes: T1.2 shipped as two commits (b4dd249 code+tests, 95f9506 docs). Protocol and verification decisions are recorded as deviations #6-#8 in docs/phases/PHASE_1_SHARED_MEMORY.md: the plan sketch's CAS logic was replaced by a per-slot ready/commit marker protocol (an initial Vyukov position-marker variant livelocked after one lap); attach-time verification uses a per-slot [head, tail] window check with documented quiescence precondition and one-way failure direction; `SharedRegion` exposes scoped attach checks (`verify_identity`, `verify_worker_ring`, whole-region `verify`); the redundant `RingBufferHeader::committed_seq` was removed (the watermark is the ring's `head_` counter). An independent review round drove the window check, scoping API, and executable negative witness; its proposed class-parameter void_t idiom was shown not to compile on GCC 13 and corrected to the NTTP form.

Gate: G1.3 (Phase 1 T1.3 - CRC integrity and corruption detection)
Date: 2026-09-17
|Command / Check|Result|
|---|---|
|Plan gate command, fresh dir: `cmake -S . -B build/gtest-asan-ubsan -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest -DSAFETY_CRIT_ENABLE_ASAN=ON -DSAFETY_CRIT_ENABLE_UBSAN=ON && cmake --build build/gtest-asan-ubsan --parallel && ctest --test-dir build/gtest-asan-ubsan --output-on-failure`|PASS - 35/35 tests (24 pre-existing + 11 new: Crc32cKnownVectors x3, RingBufferCorruption x3, RegionIntegrity x4, IntegrityLayout x1), zero warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`, no sanitizer diagnostics|
|Local matrix {GoogleTest, Catch2} x {plain, ASan+UBSan, TSan}, fresh build dirs each; TSan under `setarch $(uname -m) --addr-no-randomize`|PASS - 35/35 tests per configuration, zero warnings, zero sanitizer diagnostics in all six configs (hosted baseline flags have no SSE4.2, so the differential intrinsic-vs-table test is compiled out; table path verified by known vectors)|
|Hosted CI (.github/workflows/ci.yml: native x2, sanitizers {ASan+UBSan, TSan} x2, docker x2, AI guidance adapter drift)|PASS - run 35302088231 on commit a23a283 (2026-09-18): all nine jobs succeeded|

Notes: Region layout v3 ships with T1.3 (deviations #9-#10 in docs/phases/PHASE_1_SHARED_MEMORY.md; decision DEC-0005 from discussion D-2026-09-17-003): per-slot CRC appended in-cell at the end of the payload field (`Cell` = seq[0..8) + payload[8..60) + crc[60..64), still one 64-byte line; `kDefaultSlotBytes` 56→52; `kRegionVersion` 2→3. Per-ring corruption counter added on its own cache line. The previously-vestigial `integrity_word` is now maintained: `initialize()` seeds it, and the new region-level commit wrapper `push(region, worker_idx, value)` advances `global_seq` and refreshes the word with a relaxed CAS-converging loop (monotone in hashed sequence; converges exactly at quiescence; documented one-way transient under live traffic). Slot format version history is commented in shared_region.hpp. T1.5 remains blocked on T1.4.

---

Gate: G1.4 (Phase 1 T1.4 - named shared-memory attach/detach)
Date: 2026-09-18
Host: x86_64 Linux (glibc 2.39; `shm_open` in libc, no `-lrt`), `/dev/shm` tmpfs
|Command / Check|Result|
|---|---|
|Plan gate, fresh dir: `cmake -S . -B build/t14-gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest && cmake --build build/t14-gtest --parallel && ctest --test-dir build/t14-gtest --output-on-failure`|PASS - 41/41 tests (35 pre-existing + 6 new `shm_attach.*`), zero warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`|
|Gate filter `ctest --test-dir build/t14-gtest --output-on-failure -R shm`|PASS - exactly 6 matched, 6/6 passed: shm_attach.{CreateRoundTrip, ExistingOpenPreservesState, RejectsWrongSize, RejectsStaleIdentity, CrossProcessReattach, DestroyRemovesName}|
|Catch2 discovery + filter (`-DSAFETY_CRIT_TEST_FRAMEWORK=Catch2`): 41/41, `-R shm`|PASS - 41/41; `-R shm` matches all 6 via the `[shm_attach]` tag (lowercase suite so the case-sensitive `-R` matches under both frameworks)|
|Local matrix {GoogleTest, Catch2} x {plain, ASan+UBSan, TSan}, fresh build dirs each; TSan under `setarch $(uname -m) --addr-no-randomize` for BOTH build and ctest|PASS - 41/41 per configuration, zero warnings, zero sanitizer diagnostics|
|Cross-process coverage under sanitizers|N/A - the `fork`-based `CrossProcessReattach` case is compiled out under ASan/TSan (fork is not reliably supported under sanitizers; cross-process races are outside a sanitizer's per-process scope). Runs in the plain build; the same-process re-open paths cover create-vs-existing and identity/stale logic under sanitizers.|
|Hosted CI (.github/workflows/ci.yml: native x2, sanitizers {ASan+UBSan, TSan} x2, container x2, AI guidance adapter drift)|PASS - run 35443509963 on commit 7d4b659 (2026-09-19): all nine jobs succeeded|

Notes: T1.4 adds named `/dev/shm` attach/detach (decision DEC-0007; deviation #11 in docs/phases/PHASE_1_SHARED_MEMORY.md). New files `shm_attach.hpp` / `src/shm_attach.cpp` (deviation: the attach API lives in its own header to keep POSIX system headers out of the widely-included `shared_region.hpp`) and test module `shm_attach_test.cpp`. `SharedRegionHandle` is a move-only RAII value-or-error handle (C++20 stand-in for `std::expected`, which is forbidden): `create_or_open(name)` returns a handle carrying either the mapped region or an `AttachError`+`errno`, never throws. Create-vs-existing is decided by object size (`fstat`): 0 => fresh (ftruncate to `sizeof(SharedRegion)` + mmap + `madvise` + `initialize`), `sizeof(SharedRegion)` => existing (`verify_identity`, never re-initialize), any other nonzero size => `kSizeMismatch`; a header failing identity => `kStaleIdentity` (zero-fill from tmpfs yields magic 0). Detach is `munmap` only; a separate test-only `destroy()` does the destructive `shm_unlink` (production never unlinks while peers may be attached). `MADV_DONTFORK` is set so forked children re-attach by name (exercised by the fork test). No layout change, so `kRegionVersion` stays 3. The TSan build itself must run under `setarch --addr-no-randomize` (test discovery executes TSan binaries at build time; without it, discovery aborts with the known exit-66 memory-mapping fatal). T1.5 (T-0006) is now unblocked.
---

Gate: T-0007 (Pin the primary build compiler - baseline evidence prerequisite)
Date: 2026-09-19
Host: x86_64 Linux, Docker Engine 29.6.2

Primary compiler resolution: Debian bookworm ships no gcc-13/g++-13 package (verified 2026-09-19 on the digest-pinned base `debian:bookworm@sha256:813017f3...`: `apt-cache show gcc-13` -> "No packages found"; `apt-cache madison g++-13 gcc-13` empty even with `bookworm-backports` added; `g++` candidate = 4:12.2.0-3 from bookworm/main). A third-party toolchain repository is declined by the Phase 0 toolchain policy, so T-0007 resolves via its "Debian-supported default with a note" option.

Recorded primary compiler (gcc-primary): package `g++-12` = 12.2.0-14+deb12u1, source deb.debian.org bookworm/main; `g++-12 --version` -> "g++-12 (Debian 12.2.0-14+deb12u1) 12.2.0". Explicitly installed in the Dockerfile builder stage and selected with -DCMAKE_CXX_COMPILER=g++-12; builder stage logs `g++-12 --version` on every image build.

|Command / Check|Result|
|---|---|
|`docker build --pull --progress=plain --tag safety-critical-ha:phase0 .`|PASS - exit 0; build log shows `g++-12 (Debian 12.2.0-14+deb12u1) 12.2.0` before configure; in-image CTest 41/41 passed|
|`docker run --rm safety-critical-ha:phase0 --version`|PASS - "safety-critical-ha version 0.1.0 / Compiler: GNU 12.2.0" - the image now reports an explicit, recorded primary compiler|
|`./run_demo.sh` (compose config, build, up --wait, ps, exec version, teardown)|PASS - exit 0; 5/5 services healthy; `docker compose exec supervisor safety-critical-ha --version` -> "Compiler: GNU 12.2.0"; 0 project containers remaining after run|
|`dpkg-query -W -f=... g++-12` in the digest-pinned base|`g++-12 = 12.2.0-14+deb12u1` (deb.debian.org bookworm/main)|
|Host native builds (fresh dirs, distro-default `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`)|PASS - GoogleTest 41/41, Catch2 41/41, ASan+UBSan 41/41, no sanitizer diagnostics (TSan unchanged by this task; covered by CI)|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI (.github/workflows/ci.yml: native x2, sanitizers {ASan+UBSan, TSan} x2, container x2, AI guidance adapter drift)|PASS - run 35450545373 on commit db73b6a (2026-09-19): all nine jobs succeeded. New "Report primary compiler version" steps logged `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0` in all four native/sanitizer jobs; container/compose jobs logged `Compiler: GNU 12.2.0` - CI matches the recorded baseline, and the container-vs-host/CI major divergence is documented (Dockerfile comment, docs/DEVELOPMENT.md "Compiler baseline")|

Notes: T-0007 changes the Dockerfile builder stage (explicit `g++-12` package + `-DCMAKE_CXX_COMPILER=g++-12` + build-log version report), adds CI compiler-report steps, and documents the per-environment compiler baseline in README.md and docs/DEVELOPMENT.md. No source, CMake logic, or warning-policy changes. After this, container/CI results are attributable per DEC-0008 #7, unblocking T-0008 clang-vs-primary comparisons.

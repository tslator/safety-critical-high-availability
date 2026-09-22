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

---

Gate: T-0008 (Clang supplementary verification pipeline - clang-verify)
Date: 2026-09-19
Host: x86_64 Linux, Docker Engine 29.6.2 (no host clang; verification runs in the pinned container per DEC-0008 #2)

Toolchain pin (clang-verify): Debian bookworm `clang-14 = 1:14.0.6-12` (src `llvm-toolchain-14`, deb.debian.org bookworm/main) - `clang version 14.0.6`; `clang-tidy-14 = 1:14.0.6-12` (installed for the deferred clang-static-analysis task only); `cmake = 3.25.1-1`; `ninja-build = 1.11.1-2~deb12u1`. Base: `debian:bookworm@sha256:813017f3d62be4b5891a7acca6a01bdcd4b8513daa81b1ab99d3a50385b26931` (same pin as the primary image, so libc/libstdc++ match and only the compiler differs). Image: `safety-critical-ha:verify-clang-14` from `containers/verification/Dockerfile.clang`; identity echoed on every image build.

|Command / Check|Result|
|---|---|
|`docker build -f containers/verification/Dockerfile.clang --tag safety-critical-ha:verify-clang-14 .`|PASS - exit 0; build log records `Debian clang version 14.0.6` and the `dpkg-query` package/source report above|
|`cmake --list-presets` (host, CMake 3.28.3; also parsed by in-image CMake 3.25.1)|PASS - configure/build/test preset `clang-verify` listed (version 3, root CMakePresets.json)|
|`cmake --preset clang-verify` in image|PASS - `CMAKE_CXX_COMPILER:STRING=/usr/bin/clang++-14`, `CMAKE_CXX_COMPILER_ID "Clang"` (AC #1)|
|`cmake --build --preset clang-verify --parallel` in image|PASS (after fixes below) - all 26 targets, zero warnings on first-party code under `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`|
|`ctest --preset clang-verify` in image|PASS - 100% tests passed, 0 failed out of 41|
|CI-form pipeline (`bash -o pipefail -c` with `tee /workspace/build/clang-verify-ctest.log`, exactly the CI job steps)|PASS - exit 0, CTest log written for the on-failure artifact step|
|gcc-primary regression after source fixes (host fresh dirs, distro g++ 13.3.0): GoogleTest / Catch2 / ASan+UBSan|PASS - 41/41 each, zero compile warnings; primary link line unchanged (`libatomic` links only when `CMAKE_CXX_COMPILER_ID MATCHES "Clang"`)|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI (.github/workflows/ci.yml: clang-verify NEW, native x2, sanitizers {ASan+UBSan, TSan} x2, container x2, AI guidance adapter drift)|PASS - run 35458929055 on commit 769c8a7 (2026-09-19): all ten jobs succeeded. clang-verify job log records `Debian clang version 14.0.6`, `clang-14 = 1:14.0.6-12 (src: llvm-toolchain-14 1:14.0.6-12)`, `clang-tidy-14 = 1:14.0.6-12`, and `100% tests passed, 0 tests failed out of 41` - matches the local pinned-image toolchain and results exactly|

Notes: First Clang run surfaced three real diagnostic differences (the purpose of the pipeline): (1) `constexpr line_of()` test helpers used `reinterpret_cast`, default-error `-Winvalid-constexpr` under Clang - switched to `std::bit_cast` in ring_buffer_test.cpp, integrity_test.cpp, shared_region_layout_test.cpp (valid constexpr on both compilers; runtime behavior identical); (2) three local `using Ring8` aliases in ring_buffer_test.cpp shadowed the identical namespace-scope alias (Clang `-Wshadow` covers type aliases, GCC does not) - duplicates removed; (3) Clang does not inline `std::atomic<T>::is_lock_free()`, leaving an out-of-line `__atomic_is_lock_free` reference - `shared-memory/CMakeLists.txt` now links `atomic` PUBLIC under a Clang-only condition, keeping the gcc-primary link line byte-identical. Toolchain file sets compilers + C++20 cache vars only (DEC-0008 #4); warnings stay in `Warnings.cmake`. CI job uses SHA-pinned actions (`actions/checkout@11bd7190...`, `actions/upload-artifact@0b2256b8...` = v4.3.4) with workspace-level `contents: read`; CTest log uploaded on failure. Evidence table: docs/verification/clang-verification.md. Chain: D-2026-09-19-001 -> DEC-0008 -> T-0007 -> T-0008.

---

Gate: G1.5 (Phase 1 T1.5 - stress tests and phase exit; T-0006)
Date: 2026-09-19
Host: x86_64 Linux (glibc 2.39), distro-default `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang legs in the pinned `safety-critical-ha:verify-clang-14` image

Stress suite (new `shared-memory/tests/ring_buffer_stress.cpp`, 3 cases): 1P x 4C (1M msgs), 4P x 4C (1M msgs, 250k/producer), quiescence + cache-line witnesses after a saturated-and-drained run. Exactly-once accounting per (producer, seq) atomic-byte table; `pushed()==consumed`, `corruption_count()==0`, `verify_consistent()` at quiescence; methodology in deviation #12 (docs/phases/PHASE_1_SHARED_MEMORY.md). Volume 1M in every config (the assumed TSan cost did not materialize: legs add ~7 s; `SAFETY_CRIT_STRESS_OPS` override exists as an escape hatch only, unused by the gate and CI).

|Command / Check|Result|
|---|---|
|Plan gate, fresh dir: `cmake -S . -B build/gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest && cmake --build ... && ctest --output-on-failure`|PASS - 44/44 (41 pre-existing + 3 stress), total 0.56 s|
|Gate config 2: Catch2 (fresh dir)|PASS - 44/44, total 0.61 s|
|Gate config 3: `-DSAFETY_CRIT_ENABLE_ASAN=ON -DSAFETY_CRIT_ENABLE_UBSAN=ON` (GoogleTest, fresh dir; ASAN_OPTIONS=detect_leaks=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1)|PASS - 44/44, 1.16 s, no sanitizer diagnostics|
|ASan+UBSan + Catch2 (CI matrix parity, fresh dir)|PASS - 44/44, 1.24 s, no diagnostics|
|TSan + GoogleTest, FULL 1M ops (fresh dir, `setarch $(uname -m) --addr-no-randomize` for build and ctest, TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1)|PASS - 44/44, 6.64 s, no diagnostics (64k pilot first: 1.05 s; full volume then used)|
|TSan + Catch2, FULL 1M ops (same harness)|PASS - 44/44, 7.60 s, no diagnostics|
|clang-verify (pinned clang-14 image, fresh preset tree)|PASS - 44/44, 0.79 s; zero clang warnings on the new stress code|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI / Phase Exit Gate (.github/workflows/ci.yml: native x2, sanitizers {ASan+UBSan, TSan} x2, clang-verify, container x2, AI guidance adapter drift)|PASS - run 35463176042 on commit 96e0d3f (2026-09-19): all ten jobs succeeded, stress suite green in every hosted configuration|

Notes: T1.5 completes Phase 1: G1.1-G1.5 all green and the exit row (hosted CI on the Phase 1 merge commit) satisfied by 35463176042. Deviation #12 records the accounting methodology, the done-flag/drain termination protocol (drain must run concurrently with producers - the ring is bounded; `done` published only after all producers join), and the full-1M-under-TSan finding. No region-layout change (`kRegionVersion` stays 3); the layout is now frozen for Phase 2 per the Handoff section. Phase 2 (workers) is the next planned work; its plan sketch uses C++23/26 facilities and needs a reconciliation decision before canonical task records are opened.

---

Gate: G2.1 (Phase 2 T2.1 - worker core; T-0009)
Date: 2026-09-19
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image (clang 14.0.6 / libstdc++ 12.2)

New `workers/` component (DEC-0009): `safety_crit::workers` static library (worker_config validate, splitmix64 workload sim with per-(worker,tick) seed derivation, C++20 ranges processing pipeline with manual-loop witness) + `workers_core_test` (6 cases) + `OVERRUN` status flag bit (semantics addition, layout unchanged) + `AtomicFlags.OverrunBitIsIndependent`.

|Command / Check|Result|
|---|---|
|GoogleTest (fresh dir)|PASS - 51/51 (44 pre-existing + 6 workers_core + 1 flags), zero warnings|
|Catch2 (fresh dir)|PASS - 51/51|
|ASan+UBSan (GoogleTest, fresh dir)|PASS - 51/51, no diagnostics|
|TSan + GoogleTest / Catch2 (fresh dirs, setarch harness)|PASS - 51/51 each, no diagnostics|
|clang-verify (pinned image, preset tree)|PASS - 51/51; first run FAILED: clang 14.0.6 + libstdc++ 12.2 cannot instantiate `<ranges>` at all (even `views::iota`; root cause: view_interface/`__cust_access::__begin` bootstrap failure, upstream workarounds landed with GCC 13). Fix: `SAFETY_CRIT_WORKERS_NO_RANGES` guard selects a byte-identical manual fallback on that combo (phase-plan deviation #1); the equivalence witness runs with the live ranges path on GCC locally and in CI|
|Splitmix64 reference vectors|PASS - computed independently from the reference stream (state-chained, not output-chained); the test caught an initial mis-chained vector|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI|PASS - run 35472130840 on commit 697172e (2026-09-19): all ten jobs succeeded, workers_core_test green in every hosted configuration|

Notes: Determinism, seed sensitivity (xor/mix over worker idx and tick), pipeline/manual equivalence over 3x1000 ticks, and an explicit policy witness (SN threshold 96, Q8 calibrate, take-cap 12 at the boundary) are all green on every configuration.

---

Gate: G2.2 (Phase 2 T2.2 - work loop, signals, deadline monitoring; T-0010)
Date: 2026-09-19
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image

New: `work_loop.hpp` (region-agnostic template loop: clock injection, per-tick budget check -> OVERRUN flag + count, stop_token OR sig_atomic_t cancellation between ticks, yield-retry push backpressure, optional `pending_tick` accounting), `signals.hpp/.cpp` (sigaction: SIGTERM/SIGINT -> async-signal-safe stop flag only; SIGUSR1 -> `_exit(138)` forced-crash hook for Phase 5), `workers_loop_test` (8 cases).

|Command / Check|Result|
|---|---|
|GoogleTest (fresh dir)|PASS - 59/59 (51 pre-existing + 8 loop), zero warnings|
|Catch2 (fresh dir)|PASS - 59/59|
|ASan+UBSan (GoogleTest, fresh dir)|PASS - 59/59, no diagnostics|
|TSan + GoogleTest / Catch2 (fresh dirs, setarch harness)|PASS - 59/59 each, no data-race reports (loop cancellation + flag traffic clean)|
|clang-verify (pinned image)|PASS - 59/59, zero clang warnings|
|Loop witnesses|PASS - exact tick accounting (in-order, no duplication); stop-token landing mid-push completes the in-flight push then stops before the next tick (bounded latency, `pending_tick` correct); signal-flag stop equivalent; fake-clock budget overrun sets OVERRUN every tick (25/25) and stays clear when respected (0/25); ring-full retry loop retries and exits only on stop; end-to-end payload determinism through the loop matches direct workload computation across repeated runs|
|Signal install/query|PASS - custom dispositions installed for SIGTERM/SIGINT/SIGUSR1 and restored on uninstall; SIGUSR1 _exit path deliberately left to fork-based T2.3 integration (never exercised in-process)|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI|PASS - run 35473680814 on commit 0fdba35 (2026-09-19): all ten jobs succeeded|

---

Gate: G2.3 / Phase 2 exit (T-0011)
Date: 2026-09-19
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image

New: `safety-critical-ha worker` subcommand (hand-rolled parsing: --id a|b|c, --role hot|standby, --ticks, --tick-interval-ms, --budget-us, --region; rc=2 on invalid input; --version untouched), `workers/src/worker_entry.cpp` (create-or-open attach; hot: RUNNING -> loop -> IDLE with region-level `push()`; standby: IDLE + read-only status polling, never touches a ring), `workers_integration_test` (fork-based, plain build; degrades to a skip-pass under sanitizers per G1.4 convention). `ProcessedData` re-sized/re-ordered to 48 bytes (cap 8 samples) to fit the 52-byte slot.

|Command / Check|Result|
|---|---|
|GoogleTest (fresh dir)|PASS - 63/63 (59 pre-existing + 4 integration), zero warnings|
|Catch2 (fresh dir)|PASS - 63/63 (integration names discoverable under both frameworks; Catch2 assertion-chain incompatibility fixed)|
|ASan+UBSan / TSan x2 (fresh dirs, setarch harness)|PASS - 60/60 each (4 fork cases replaced by the sanitizer skip-pass); no diagnostics|
|clang-verify (pinned image)|PASS - 63/63, zero clang warnings|
|Integration (a) RingDrainRoundTrip|PASS - 200 ticks drained concurrently, exactly-once in-order, payloads byte-equal to recomputed workload; ring empty; worker left IDLE|
|Integration (b) SigtermCleanStop|PASS - child exits 0 within timeout; committed outputs form a gap-free valid tick prefix; pushed == consumed after drain; IDLE|
|Integration (c) Sigusr1CrashHook|PASS - exit code 138 (128+SIGUSR1), no cleanup; stale RUNNING left (supervisor declares CRASHED in Phase 4); committed outputs still drain as valid prefix|
|Integration (d) StandbyPassivity|PASS - rings untouched (pushed == consumed == 0); exits 0 on SIGTERM|
|CLI manual witness|PASS - `--version` unchanged; hot 50 ticks rc=0; `--budget-us 5` witnessed 2 real-clock overruns; missing/invalid args rc=2; standby SIGTERM rc=0|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI / Phase Exit Gate|PASS - run 35485369860 on commit d00526b (2026-09-19): all ten jobs succeeded — **Phase 2 exit gate satisfied**|

Phase Exit Gate table:
| Required evidence | Source | Status |
|---|---|---|
| Core determinism + pipeline equivalence (both frameworks) | G2.1 | PASS (NOTES "G2.1"; hosted 35472130840) |
| Loop cancellation/deadline clean under sanitizers | G2.2 | PASS (NOTES "G2.2"; hosted 35473680814) |
| Integration a-d | G2.3 | PASS (above) |
| Full local matrix green | exit | PASS (above) |
| Hosted CI green on exit commit | exit | PASS (run 35485369860 on d00526b) |

---

Gate: G3.1 (Phase 3 T3.1 - monitor core; T-0012)
Date: 2026-09-20
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image

New: `monitors/` static library `safety_crit::monitors` (C++20, warnings, sanitizers; standard test wiring; root CMakeLists + ARCHITECTURE/ARCHITECTURE_RULES ownership rows updated). `monitor_config.hpp/.cpp` (poll interval default 10 ms, stall threshold default 100 ms, validation: positive and threshold >= interval), `health.hpp` region-agnostic classification core: alert vocabulary strings per DEC-0010 #3 (`worker_crashed`/`worker_stalled`/`worker_recovered`/`worker_overrun`/`worker_idle`/`worker_running`), `HealthState`, `WorkerObservation` (status word, ring tail, liveness), `WorkerTrack` latches (stall until tail advances, crash until process returns, overrun until observed clear), `observe_worker` with at most one alert per poll, priority crashed > episode edge > recovered/stalled > overrun; fake clock injected (DEC-0009 #2 pattern).

|Command / Check|Result|
|---|---|
|GoogleTest (fresh dir)|PASS - 83/83 (73 pre-existing + 10 health), zero warnings|
|Catch2 (fresh dir)|PASS - 83/83|
|ASan+UBSan (GoogleTest, fresh dir)|PASS - no diagnostics|
|clang-verify (pinned image)|PASS - zero clang warnings|
|Health classification matrix|PASS - exhaustive `monitors_health_test`: idle/running, stall latch + release on tail advance, crash latch + recovery on liveness return, overrun latch + clear, priority ordering, at-most-one-alert-per-poll, episode-edge dedupe — identical under both frameworks|

---

Gate: G3.2 (Phase 3 T3.2 - pidfile, poll loop, JSON alerts, monitor CLI; T-0013)
Date: 2026-09-20
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image

New: `workers/pidfile.hpp/.cpp` (contract path `<dir>/safety_crit_worker_<idx>.pid`, atomic publish temp+rename, removal on clean exit only; `run_worker` writes after attach — fatal on failure — and removes at the end; `--pid-dir` on the worker subcommand), `monitors/pidfile_liveness.hpp/.cpp` (parse + range-check, `kill(pid, 0)` probe with EPERM = alive; stale/malformed = dead per DEC-0010 #2), `monitor_loop.hpp` (status + `tail_` acquire loads, injected liveness/clock/pacer, stop via `stop_token`/`sig_atomic_t`, one shared timestamp per poll, alerts attributed to `MonitorStats`, bounded stop latency; `interval_pacer` 1 ms slices), `json_lines.hpp/.cpp` (alert line + shutdown `monitor_report`, snprintf per deviation #1, flushed immediately), `monitor_entry.cpp` `run_monitor` (create-or-open attach + metadata-only `verify_identity`, signal wiring reused from `workers::signals`, final report), `monitor` subcommand in `app/` (hand-rolled parsing, rc=2 on invalid).

|Command / Check|Result|
|---|---|
|GoogleTest / Catch2 (fresh dirs)|PASS - 83/83 each|
|ASan+UBSan (GoogleTest, fresh dir)|PASS - 77/77 (fork integration skip-pass), no diagnostics|
|TSan x2 (GoogleTest + Catch2, setarch harness)|PASS - 77/77 each, no data-race reports (loop cancellation + status/tail traffic clean)|
|clang-verify (pinned image)|PASS - 83/83, zero clang warnings|
|Loop witnesses (`monitors_loop_test`)|PASS - exact poll accounting; stall/recovery through real ring counters; crash via injected verdict; clean-exit idle; bounded stop latency; exact-string JSON formats for alert and `monitor_report` lines|
|Pidfile contract (`WorkersPidfile.ContractPathWriteRemove`)|PASS - contract path, atomic publish, write/remove lifecycle|

---

Gate: G3.3 / Phase 3 exit (T-0014)
Date: 2026-09-20
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image

New: `monitors_integration_test` (fork-based, plain build only; sanitizer skip-pass per G1.4/G2.3 precedent), README monitor command docs, Phase 3 exit evidence.

|Command / Check|Result|
|---|---|
|GoogleTest (fresh dir)|PASS - 83/83 (79 pre-existing + 4 integration), zero warnings|
|Catch2 (fresh dir)|PASS - 83/83|
|ASan+UBSan / TSan x2 (fresh dirs, setarch harness)|PASS - 77/77 each (4 monitor fork cases replaced by the sanitizer skip-pass); no diagnostics|
|clang-verify (pinned image)|PASS - 83/83, zero clang warnings|
|Integration (a) CrashDetectionSigkill|PASS - hot worker + monitor, SIGKILL -> `worker_crashed` exactly once, report crash count 1, stale pidfile left behind and rejected via liveness|
|Integration (b) CleanExitIdle|PASS - worker finishes cleanly -> `worker_idle`, no crash event, pidfile removed|
|Integration (c) StallRecoverySigstop|PASS - SIGSTOP -> `worker_stalled` (never crashed; liveness passes while stopped), SIGCONT -> `worker_recovered`|
|Integration (d) StandbyStandby|PASS - standby -> `worker_idle` alive, no crash, rings untouched|
|Integration stability|PASS - 5 consecutive repetitions of the 4-scenario set, all green|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI / Phase Exit Gate|PASS - run 35518074368 on commit e4cb5ab (2026-09-20): all ten jobs succeeded — **Phase 3 exit gate satisfied**|

Phase Exit Gate table:
| Required evidence | Source | Status |
|---|---|---|
| Health classification matrix deterministic (both frameworks) | G3.1 | PASS (NOTES "G3.1") |
| Loop accounting + pidfile contract clean under sanitizers | G3.2 | PASS (NOTES "G3.2"; TSan/ASan+UBSan above) |
| Integration: crash-on-SIGKILL, clean-exit idle, stall/recovery, standby | G3.3 | PASS (above) |
| Full local matrix green | exit | PASS (above) |
| Hosted CI green on exit commit | exit | PASS (run 35518074368 on e4cb5ab) |

---

Gate: G4.6 / Phase 4 exit (T-0022)
Date: 2026-09-21
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image; Docker 29.8.0, Compose v5.3.1

Full task-by-task evidence lives in [docs/evidence/phase-4.md](docs/evidence/phase-4.md) ("T-0015 Result" through "Phase 4 Exit"). This gate entry summarizes the Phase 4 exit matrix.

New: supervisor stdout witness events (`first post-failover record observed in <N> ms` and a shutdown summary carrying drain records/corruptions/first-post-failover for both logical rings); `scripts/phase4-failover-timing.sh` runs repeated host-side crash-recovery iterations with fresh shm regions and reports fault-to-first-output distribution. Two Phase 4 bugs fixed during exit integration: (a) `run_monitor_loop` never seeded `tracks[i].worker`, so every alert reported worker 0 regardless of emitter (regression test `MonitorsLoop.AlertsCarryPhysicalWorkerIndex` added); (b) `records_before_failover` was snapshotted from the monitor alert handler, which lags the reap-driven ownership transfer — the snapshot moved to the reap iteration so pre-crash records cannot trigger a false first-post-failover event.

|Command / Check|Result|
|---|---|
|GoogleTest (plain, fresh dir)|PASS - 95/95|
|Catch2 (plain, fresh dir)|PASS - 95/95|
|ASan+UBSan GoogleTest (fresh dir, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`)|PASS - 87/87 (8 fork-integration cases skip-pass under sanitizers, established precedent)|
|ASan+UBSan Catch2 (fresh dir)|PASS - 87/87|
|TSan GoogleTest (fresh dir, `setarch --addr-no-randomize`)|PASS - 87/87, zero data-race reports|
|TSan Catch2 (fresh dir, `setarch --addr-no-randomize`)|PASS - 87/87, zero data-race reports|
|clang-verify (pinned `safety-critical-ha:verify-clang-14`, `cmake --preset clang-verify`)|PASS - 95/95, zero clang warnings|
|Docker build (`docker build --pull --tag safety-critical-ha:phase0 .`; image `--version`)|PASS|
|Docker Compose stack (`config --quiet`, `build`, `up --wait --no-build`, `down`)|PASS|
|Compose failover smoke (`containers/compose/failover-smoke.sh`: SIGKILL physical A in-container → verify healthy, fresh physical-A pidfile, `worker_running worker:2` promotion edge, region survives)|PASS - 5 consecutive runs|
|Repeated crash recovery timing (`scripts/phase4-failover-timing.sh 10`)|PASS - min 84 ms, median 84 ms, max 91 ms, avg 85 ms; 0/10 iterations over `<100 ms` SLA; `a_corruptions=0` and `a_first_post_failover=1` on every iteration|
|`./scripts/sync-agent-guidance.sh --check`|PASS - adapters byte-identical (CORE.md untouched)|
|Hosted CI / Phase Exit Gate|PASS - run 35668187681 on commit 9013df9 (2026-09-21): all ten jobs succeeded — AI guidance adapter drift check, native GoogleTest, native Catch2, ASan+UBSan x2, TSan x2, clang-verify (pinned image), container image smoke, Compose runtime + failover smoke — **Phase 4 exit gate satisfied**|

Phase Exit Gate table:
| Required evidence | Source | Status |
|---|---|---|
| Ownership / epoch / fencing / control tests in both frameworks and applicable sanitizers | G4.1 | PASS (docs/evidence/phase-4.md "T-0015 Result" … "T-0022 Exit") |
| Supervisor launches/reaps topology, consumes alerts, shuts down cleanly | G4.2 | PASS (T-0017 Result; Supervisor.LaunchesAndReapsTopology; shutdown summary line) |
| Crash → C assumes logical A → first post-failover record `<100 ms` → A restarts as standby | G4.3 | PASS (above, timing matrix) |
| Stale epochs rejected, no lost or duplicate output | G4.4 | PASS (T-0015 stale-token test; drain sequence monotonicity; a_corruptions=0 across all timing runs) |
| Priority order enforced when privileged; unprivileged fallback explicit | G4.5 | PASS (T-0020 Result; runtime scheduling tests) |
| Real Compose runtime smoke and failover | G4.6 | PASS (above, 5 consecutive in-container SIGKILL runs) |
| Full local matrix green | exit | PASS (above) |
| Hosted CI green on exit commit | exit | PASS (run 35668187681 on 9013df9) |

---

Gate: G5.1 (Phase 5 T-0023 monitor logical-ring attribution)
Date: 2026-09-22
Host: x86_64 Linux, `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; clang leg in the pinned `safety-critical-ha:verify-clang-14` image

Phase 4 tail fix (DEC-0012 #2, P0 blocker for Phase 5 stall/double-fault scenarios). TDD red → green: `MonitorsLoop.AttributionFollowsLogicalRingAfterPromotion` was written first against the pre-fix code, observed to FAIL (spurious `worker_stalled` for physical 2 after ownership transfer to it), then the fix was added and the test flipped to PASS.

New: `owned_logical_ring(const SharedRegion&, std::size_t physical_idx)` helper in `monitors/include/safety_crit/monitors/health.hpp`; `poll_worker` reads the owned logical ring's tail (home-ring fallback for standby workers, whose status word is IDLE and never trips the stall rule). `#include <optional>` added.

|Command / Check|Result|
|---|---|
|TDD red step: `ctest -R AttributionFollowsLogicalRingAfterPromotion` before fix|FAIL (spurious `worker_stalled` for physical 2)|
|GoogleTest (plain, fresh dir)|PASS 96/96 (95 pre-existing + 1 new)|
|Catch2 (plain, fresh dir)|PASS 96/96|
|ASan+UBSan GoogleTest (fresh dir)|PASS 88/88|
|ASan+UBSan Catch2 (fresh dir)|PASS 88/88|
|TSan GoogleTest (`setarch --addr-no-randomize`, fresh dir)|PASS 88/88, zero race reports|
|TSan Catch2 (`setarch --addr-no-randomize`, fresh dir)|PASS 88/88, zero race reports|
|clang-verify (pinned image, clang-14 preset)|PASS 96/96, zero clang warnings|
|Docker build + `--version`|PASS|
|Compose `up --wait` healthy + `containers/compose/failover-smoke.sh`|PASS 5 consecutive runs|
|`scripts/phase4-failover-timing.sh 5` (regression check on supervisor drain timing)|PASS min/median/max/avg 85 ms, 0/5 over `<100 ms` SLA|
|`./scripts/sync-agent-guidance.sh --check`|PASS adapters byte-identical|
|Hosted CI|PASS - run 35761919434 on commit c14934c (2026-09-22): all ten jobs succeeded|

# Phase 1 - Shared Memory & Lock-Free Ring Buffers

## Purpose

Implement the `safety_crit::shared_memory` library: the `/dev/shm` region
layout, per-worker atomic status flags, and the lock-free ring buffer with
per-slot integrity checks. At the end of this phase, multiple processes can
attach to one shared region, exchange fixed-size messages through ring
buffers without locks, detect corrupted slots, and verify layout invariants —
the substrate every later phase (workers, monitor, supervisor, perturbation)
builds on.

This document decomposes Phase 1 from `SAFETY_CRITICAL_HA_PLAN.md` into tasks
intended to take one developer three to four hours each. Complete tasks in
order; each verification gate is a required input to the next task.

## Scope and Non-Goals

**In scope**

- `shared-memory/` CMake target (library `safety_crit::shared_memory`).
- `SharedRegion` layout with cache-line discipline and identity word.
- Bit-packed atomic worker status flags.
- Lock-free MPMC ring buffer with per-slot CRC integrity.
- `/dev/shm` mmap attach/detach wrapper with re-attachment support.
- Unit and stress tests under both test frameworks, plus sanitizer runs.

**Out of scope**

- Worker processes, supervisor/monitor logic, HTTP endpoints, failover, and
  fault injection (Phases 2-5).
- Real-time scheduling (`SCHED_FIFO`) — Phase 4 concern; this phase only
  needs ordinary process scheduling.
- Hard real-time or zero-jitter claims.

## Handoff Contract From Phase 0

Phase 1 must preserve: the C++20 standard, warning policy, sanitizer options,
framework selection (`SAFETY_CRIT_TEST_FRAMEWORK`), the container image and
its in-image CTest gate, and CI jobs. New shared-memory tests are additional
CTest coverage; they must not replace the Phase 0 smoke test.

## Deviations From the Plan Sketch (recorded decisions)

1. **The plan's `LockFreeRingBuffer` sketch is not implementable as written.**
   Its `try_push`/`try_pop` CAS logic does not define correct multi-producer
   ownership of `head_` (producers can claim the same slot), and its
   `is_lock_free()` uses `std::is_constant_evaluated()` invalidly. T1.2
   implements a per-slot sequence protocol (the moodycamel/boost::lockfree
   pattern named in plan §8) instead, keeping the plan's public interface
   shape (`try_push`/`try_pop`, power-of-2 `requires` clause, runtime
   lock-free verification).
2. **No false sharing via real padding, not the sketch's `char padding[]`.**
   The plan comment asks that no two workers share a cache line; its
   implementation (a single pad array at the end) does not achieve that for
   `worker_status`. T1.1 wraps each worker's status in an `alignas(64)`
   cell and verifies distinct lines with compile-time + runtime checks.
3. **Test adapter generalized.** `SAFETY_CRIT_TEST_CASE` now takes
   `(suite, name)` so every test module gets its own suite under both
   frameworks. The Phase 0 smoke test is updated to the same form.
4. **Warning policy materialized as `CMake/Warnings.cmake`.** Phase 0 Task
   0.2 step 4 requires `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` on
   project-owned targets; the helper exists now and is applied to all
   first-party targets (including `app`, closing that Phase 0 gap).
5. **Sanitizer CI moved ahead of T1.2.** The original plan gates sanitizer
   runs near phase exit. Since the lock-free buffer is the first genuinely
   concurrency-sensitive code, a hosted matrix of {ASan+UBSan, TSan} x
   {GoogleTest, Catch2} was added to `ci.yml` at Phase 1 start so every
   commit from T1.2 onward is sanitizer-gated (TSan combinations run the
   toolchain under `setarch --addr-no-randomize`; see NOTES.md for why).
6. **Ring slot-counter markers corrected before first use.** The initial
   T1.2 implementation used the "sequence = position + k*n" marker family
   (commit stores `p + n`, release stores `p + 2*n`). A hand trace plus a
   standalone harness showed that family livelocks after one full drain
   cycle: once a slot is released, its counter no longer satisfies the
   producer's claim condition for the next lap. The implemented protocol
   instead uses per-slot ready/commit markers (`ready(P_k) = P_k`,
   `commit(P_k) = P_k + 1`; release stores `P_k + n`, which is exactly
   `ready(P_{k+1})`). The marker offset (1) must be strictly less than
   SlotCount, so the class requires `SlotCount >= 2`. Correctness evidence:
   deterministic multi-lap + full-queue rejection scenarios in a standalone
   harness (the old scheme fails both), the T1.2 CTest suite under both
   frameworks, and the ASan/TSan CI matrix.
7. **Attach-time verification checks per-slot windows, not derived minima; the
   phantom `committed_seq` watermark was removed.** Independent review of the
   first T1.2 draft found that comparing only the *derived minima* of the slot
   counters with the stored head/tail accepts an impossible state: a well-formed
   counter corrupted into a future lap on a non-minimal slot (e.g. empty ring,
   slot 1 moved `ready(1) -> ready(1+n)`) leaves every minimum untouched.
   `verify_consistent()` now decodes *every* slot and requires it to match the
   stored window exactly: for `h = head_`, `t = tail_` (with `0 <= h <= t
   <= h + n`), slot i's first unconsumed/uncommitted positions must be
   `min{p ≡ i (mod n) : p >= h}` / `min{p ≡ i (mod n) : p >= t}`. This
   requires the ring to be quiescent at verification time (an in-flight claim
   legitimately lags by one step), which matches the plan's re-attachment flow
   (verify a worker's ring only when that worker is known idle or dead); the
   requirement is documented on both `verify_consistent()` and
   `SharedRegion::verify()`. Related: T1.2 step 2 asked for a header
   `committed_seq` recoverable after re-attachment, but the last-consumed
   sequence for a worker is exactly its ring's `head_` counter -- maintained
   by the protocol on every pop and cross-checked against the slot counters by
   the window check above. A separately stored copy could only be written at
   initialization (going stale after the first consumption), so the field was
   replaced by an explicit reserved word (layout stays 64-byte aligned, no
   version bump -- v2 never shipped). If Phase 4 needs a supervisor-facing
   global watermark, derive it at attach time as the max over the workers'
   rings instead of maintaining it incrementally. Regression coverage:
   `RingBufferProtocol.FutureLapCorruptionRejectedByWindowCheck` (the exact
   evasion pattern plus inverted/over-wide windows) and
   `RingBufferProtocol.WindowCheckAcceptsQuiescentBoundaries` (empty/partial/
   full/mid-lap states must not be over-rejected).
   **Residual risk (assessed, mitigated at the API level):** the window check
   depends on its quiescence precondition, which callers must enforce. The
   failure direction is one-way: transient in-flight states (an uncommitted
   claim, an unlanded release, a torn head_/tail_ read) can only produce a
   *spurious false* -- a corrupted slot's stored value is fixed at inspection
   time and no other thread's transient state can move it into coincidence
   with the window. So this is an availability risk (a healthy ring rejected,
   potentially triggering unnecessary re-verification or failover), never an
   integrity risk, and the documented caller rule is: treat a single false as
   "unverified", re-check under established quiescence before acting. Mitigations:
   (a) `verify_consistent()` documents the direction and the caller rule;
   (b) `SharedRegion` now exposes scoped checks -- `verify_identity()`
   (quiescence-independent, safe at any time), `verify_worker_ring(region,
   idx)` (only that worker's ring; its quiescence is guaranteed in the plan's
   re-attachment flow because the victim is dead or paused), and the existing
   `verify(region)` (all rings; for pre-boot/maintenance use) -- so the
   natural Phase 2/4 call pattern never reads live neighbours' rings;
   (c) both behaviours are pinned by deterministic tests that emulate an
   in-flight claim (`RingBufferProtocol.InFlightClaimReadsAsInconsistentUntilCommitted`,
   `SharedRegionRings.VerificationScopesToQuiescentWorkers`). Deliberately NOT
   mitigated by tolerating one-step-off slot states: any tolerance band makes
   a rewound/corrupted counter indistinguishable from a live claim, which
   would convert the availability risk back into an integrity risk.
8. **Negative witness for the one-slot rejection is now executable.** An
   independent review note (`RING_BUFFER_NEGATIVE_WITNESS_NOTE.md`, since
   folded in and removed) confirmed -- and we re-verified on GCC 13.3.0 --
   that naming a constrained class template-id whose constraint fails outside
   an immediate substitution context is a hard error on this toolchain, in
   both `requires { typename LockFreeRingBuffer<1, 16>; }` and as the explicit
   argument of a type-parameter detection trait. The note's suggested
   class-parameter `void_t` witness therefore does not compile; the working
   form passes the dimensions through non-type parameters so the id is formed
   during partial-specialization matching (a substitution context). The test
   now carries that trait (`ring_instantiable<N, B>`) with positive and
   negative witnesses for 1/2/3-slot configurations, plus a comment scoped as
   compiler-observed behavior with both failing forms named for falsification.
   No production code changed; constraint enforcement itself was already
   correct.
9. **Per-slot CRC lives in-cell at the end of the payload field; slot format
   changes 56→52 payload bytes and `kRegionVersion` bumps 2→3.** Plan step 2
   says "store per-slot CRC in each slot" but the frozen v2 layout (T1.2) has
   no room for it: `Cell` is exactly seq(8)+payload[56] = 64B, and the
   architecture rules require an explicit callout plus version bump for any
   slot-format change. DEC-0005 (discussion D-2026-09-17-003) resolves this:
   `crc` is appended to `Cell` at the end of the payload field -- layout v3:
   `[0..8) sequence`, `[8..60) payload[52]`, `[60..64) crc`; the cell stays
   exactly one 64-byte line and total region size is unchanged, so the version
   bump is for format clarity (no silent relayout). `kDefaultSlotBytes` drops
   to 52. The tag is a plain (non-atomic) `uint32_t` written under exclusive
   slot ownership before the commit release-store -- T1.2's publish edge makes
   it visible, and no extra atomic RMW or ordering change is needed.
   `try_pop` validates the tag over the full payload field *before* copying
   out; on mismatch the consumer never copies corrupted payload, releases with
   the normal `ready(p + n)` marker (skipped slots re-arm), increments a new
   per-ring 64-byte-aligned corruption counter (each corrupted position is
   counted exactly once), and returns false indistinguishably from empty --
   message loss at corrupted positions is by design (plan S3).
10. **`integrity_word` is maintained by a region-level commit wrapper, not
   inside `try_push`.** Plan step 3 says "update `integrity_word` on commit
   paths"; a standalone ring's `try_push` has no access to `global_seq` or the
   integrity word (the ring core stays standalone per DEC-0005). The update
   therefore lives in a new region-level commit wrapper,
   `push(SharedRegion&, worker_idx, value)`: ring `try_push`, then on success
   `global_seq.fetch_add` + `refresh_region_integrity()`. The refresh is a
   relaxed CAS-converging loop -- load the word, re-read `global_seq`, compute
   the desired CRC; fast path when equal, CAS otherwise. Because `global_seq`
   only increases, stored values are monotone in hashed sequence and the last
   store at quiescence converges to exactly H(headers, final seq); under live
   traffic a transient mismatch is possible and one-way (spurious mismatch for
   live observers, never masked corruption), mirroring `verify_consistent()`'s
   documented direction. `initialize()` seeds the word from the filled headers
   + seq 0 so a fresh region is self-consistent before any commit.
   Word definition: `crc32c(raw ring_buffers[] header block (kMaxWorkers x 64B)
   || global_seq as 8 little-endian bytes)`; identity words are excluded
   (already checked against compiled-in constants by `verify_identity`).
   The CRC-32C implementation follows the plan's "hardware intrinsics where
   available, table fallback": compile-time table default, `_mm_crc32_u8`
   chain only when `__SSE4_2__` is defined; a differential test runs under
   SSE4.2 builds, and known vectors (published check value plus two 52-byte
   full-payload-field patterns cross-checked against an independent bitwise
   implementation at analysis time) pin whichever path the build selected.

## Target Outcome

```text
├── CMake/
│   ├── Sanitizers.cmake
│   ├── TestFramework.cmake
│   └── Warnings.cmake                     # new
├── shared-memory/                         # new
│   ├── CMakeLists.txt
│   ├── include/safety_crit/shared_memory/
│   │   ├── atomic_flags.hpp
│   │   ├── ring_buffer.hpp
│   │   ├── shared_region.hpp
│   │   └── integrity.hpp
│   ├── src/
│   │   ├── ring_buffer.cpp
│   │   ├── integrity.cpp
│   │   └── shared_region.cpp
│   └── tests/
│       ├── CMakeLists.txt
│       ├── atomic_flags_test.cpp
│       ├── shared_region_layout_test.cpp
│       ├── ring_buffer_stress.cpp         # T1.5
│       └── shm_attach_test.cpp            # T1.4
└── tests/                                 # Phase 0 smoke test (unchanged API)
```

## Task Plan

### Task T1.1 - Shared Region Layout and Atomic Flags _(3-4 h)_

**Dependencies:** Phase 0 exit gate.

**Implementation steps**

1. Add `CMake/Warnings.cmake` with `safety_crit_apply_warnings(target)` and a
   `SAFETY_CRIT_WARNINGS_AS_ERRORS` option (default OFF); apply to all
   first-party targets.
2. Generalize the test adapter macro to
   `SAFETY_CRIT_TEST_CASE(suite, name)`; add
   `safety_crit_configure_test_adapter(dir)` to `CMake/TestFramework.cmake`.
3. Create `shared-memory/` as a static library target
   `safety_crit::shared_memory` (C++20, warnings, sanitizers) built in all
   configurations; tests gated on `SAFETY_CRIT_BUILD_TESTING`.
4. Implement `atomic_flags.hpp`: `WorkerStatusFlag` bits
   (`RUNNING`, `IDLE`, `CRASHED`, `RECOVERING`), non-atomic inspection/combine
   helpers, and atomic set/clear/load helpers using single-word RMW only.
5. Implement `shared_region.hpp` / `shared_region.cpp`: `SharedRegion` with
   identity word (magic + version), one 64-byte `RingBufferHeader` per worker,
   one 64-byte `WorkerStatusCell` per worker, `global_seq`, and
   `integrity_word`; `initialize(region)` for freshly mapped memory and
   `verify(region)` for attachment safety; compile-time layout invariants.

**Deliverables**

- `safety_crit::shared_memory` target with flags + region layout.
- Test executables `atomic_flags_test`, `shared_region_layout_test`.

**Verification gate G1.1**

```bash
cmake -S . -B build/gtest -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest
cmake --build build/gtest --parallel
ctest --test-dir build/gtest --output-on-failure

cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2
cmake --build build/catch2 --parallel
ctest --test-dir build/catch2 --output-on-failure
```

Pass when both frameworks discover and pass the flag and layout tests (and
the Phase 0 smoke test still passes), including compile-time alignment
invariants.

---

### Task T1.2 - Lock-Free Ring Buffer _(3-4 h)_

**Dependencies:** G1.1.

**Implementation steps**

1. Implement `ring_buffer.hpp`: `LockFreeRingBuffer<SlotCount, SlotBytes>`
   with a `requires` clause enforcing power-of-two `SlotCount > 0`.
2. Per-slot sequence protocol: each slot owns a sequence counter; producers
   claim a slot by CAS on its sequence, consumers release it after copying
   out; a global committed sequence is derived from slot counters so the
   header's `committed_seq` is recoverable after re-attachment (plan §3 Phase
   4 recovery requirement).
3. No allocation or syscalls in the hot path; document the memory-order
   argument for each `load`/`store`/CAS in comments.
4. Wire buffer headers into `SharedRegion` (fill `slot_count`, `slot_bytes`)
   and provide `initialize_region(region, slot_count, slot_bytes)`.
5. Runtime lock-free verification: `is_lock_free()` checked at init and in
   tests; document the accepted-platform assumption.

**Deliverables**

- `ring_buffer.hpp` (+ `src/ring_buffer.cpp` for any non-inline code).
- Tests: empty/full transitions, single-producer single-consumer ordering,
  multi-producer no-loss accounting (deterministic small scale).

**Verification gate G1.2**

```bash
cmake -S . -B build/gtest -G Ninja && cmake --build build/gtest --parallel \
  && ctest --test-dir build/gtest --output-on-failure
cmake -S . -B build/catch2 -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=Catch2 \
  && cmake --build build/catch2 --parallel \
  && ctest --test-dir build/catch2 --output-on-failure
```

Pass when all T1.1 + T1.2 tests pass under both frameworks with the default
(sanitizer-free) configuration.

---

### Task T1.3 - CRC Integrity and Corruption Detection _(3-4 h)_

**Record:** [T-0004](../tasks/T-0004-t1.3-crc-integrity.md)

**Dependencies:** G1.2.

**Implementation steps**

1. Implement `integrity.hpp` / `src/integrity.cpp`: CRC-32C (hardware
   intrinsics where available, table fallback) plus a whole-region integrity
   word combining per-worker header state and `global_seq`.
2. Store per-slot CRC in each slot; `try_pop` recomputes and on mismatch:
   skips the slot, advances past it, increments a corruption counter (atomic),
   and returns false — the consumer must never copy corrupted payload out.
3. Update `integrity_word` on commit paths so an external observer can detect
   stale/corrupted headers without parsing slots.

**Deliverables**

- `integrity.hpp/.cpp`, corruption counter, updated pop path.
- Tests: known CRC vectors; corrupt one slot → consumer skips exactly that
   message, count = 1, remaining messages intact.

**Verification gate G1.3**

```bash
cmake -S . -B build/gtest-asan-ubsan -G Ninja \
  -DSAFETY_CRIT_TEST_FRAMEWORK=GoogleTest \
  -DSAFETY_CRIT_ENABLE_ASAN=ON -DSAFETY_CRIT_ENABLE_UBSAN=ON
cmake --build build/gtest-asan-ubsan --parallel
ctest --test-dir build/gtest-asan-ubsan --output-on-failure
```

Pass when all tests pass under ASan+UBSan with zero sanitizer diagnostics.

---

### Task T1.4 - Shared Memory Attach/Detach _(3-4 h)_

**Record:** [T-0005](../tasks/T-0005-t1.4-attach-detach.md)

**Dependencies:** G1.2 (integrity word read-only usage OK before T1.3).

**Implementation steps**

1. Implement `shared_region.hpp` attach API: create-or-open a named `/dev/shm`
   object of the exact compiled-in region size, `mmap` + `madvise`
   (`MADV_DONTFORK`, optional `MADV_HUGEPAGE`), construct in place via
   `initialize`; detect existing-vs-new by identity word and expected size.
2. Detach: `munmap`, never truncate while peers may be attached. Provide a
   separate, explicitly destructive "reset" path used only by tests (truncate
   + re-create) to exercise the re-attachment logic planned for perturbation
   scenario S6.
3. All attach code returns explicit error codes (`std::expected`-style pair),
   no exceptions from the attach path.

**Deliverables**

- Attach/detach/reset API + `shm_attach_test.cpp` (two processes or threads
  sharing one region; stale-object rejection on wrong magic/size).

**Verification gate G1.4**

```bash
cmake -S . -B build/gtest -G Ninja && cmake --build build/gtest --parallel \
  && ctest --test-dir build/gtest --output-on-failure
ctest --test-dir build/gtest --output-on-failure -R shm
```

Pass when the attach tests pass, including rejection of a stale or wrong-size
object in `/dev/shm`.

---

### Task T1.5 - Stress Tests and Phase Exit _(3-4 h)_

**Record:** [T-0006](../tasks/T-0006-t1.5-stress-and-exit.md)

**Dependencies:** G1.3, G1.4.

**Implementation steps**

1. Implement `ring_buffer_stress.cpp` per plan §3: 1 producer × N consumers
   at ≥ 1M ops total; assert no data loss (produced == consumed) and no
   duplication via per-message sequence verification.
2. Multi-producer stress: P producers × C consumers, same accounting.
3. Cache-line/false-sharing check stays green under the stress build.
4. Run the full Phase 1 matrix and record evidence in `NOTES.md`.

**Deliverables**

- Stress tests, recorded gate evidence, updated README build instructions if
  any new option appeared (none expected).

**Verification gate G1.5**

```bash
for fw in GoogleTest Catch2; do
  cmake -S . -B build/phase1-$fw -G Ninja -DSAFETY_CRIT_TEST_FRAMEWORK=$fw \
    && cmake --build build/phase1-$fw --parallel \
    && ctest --test-dir build/phase1-$fw --output-on-failure
done
cmake -S . -B build/phase1-asan-ubsan -G Ninja \
  -DSAFETY_CRIT_ENABLE_ASAN=ON -DSAFETY_CRIT_ENABLE_UBSAN=ON \
  && cmake --build build/phase1-asan-ubsan --parallel \
  && ctest --test-dir build/phase1-asan-ubsan --output-on-failure
```

Pass when all three configurations pass completely.

## Phase Exit Gate

Phase 1 is complete only when G1.1-G1.5 pass and the following evidence is
attached to the implementation change in `NOTES.md`:

| Required evidence | Source gate |
|---|---|
| Flag + layout tests passing under GoogleTest and Catch2 | G1.1 |
| Ring buffer correctness tests (both frameworks) | G1.2 |
| ASan+UBSan-clean run including CRC/corruption tests | G1.3 |
| Attach/detach/stale-rejection test output | G1.4 |
| Full-matrix stress results (≥1M ops, zero loss/duplication) | G1.5 |
| Hosted CI green on the Phase 1 merge commit | exit |

No Phase 2 worker implementation may be accepted until this exit gate is
satisfied.

## Handoff to Phase 2

Phase 2 adds `workers/` that attach to a region created via T1.4, push/pull
through T1.2/T1.3 buffers, and drive `worker_status` via T1.1 flags. The
region layout in this phase is frozen for Phase 2; any later change requires
bumping `kRegionVersion` and updating the verify path.

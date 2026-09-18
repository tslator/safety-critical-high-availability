#include "test_framework.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include "safety_crit/shared_memory/integrity.hpp"
#include "safety_crit/shared_memory/ring_buffer.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace {
using namespace safety_crit::shared_memory;

constexpr std::uintptr_t line_of(const void* p) {
    return reinterpret_cast<std::uintptr_t>(p) / 64u;
}

// Small rings: force backpressure and wrap-arounds in the corruption tests.
using Ring16 = LockFreeRingBuffer<16, 16>;
using Ring8 = LockFreeRingBuffer<8, 16>;

// An 8-byte message; trivially copyable as required by try_push/try_pop.
struct Msg {
    std::uint32_t producer{0};
    std::uint32_t index{0};
};

static_assert(sizeof(Msg) <= Ring16::kSlotBytes);
static_assert(sizeof(Msg) <= kDefaultSlotBytes);  // also fits the region rings
static_assert(std::is_trivially_copyable_v<Msg>);

Msg canary_msg() {
    return Msg{0xDEAD, 0xBEEF};
}

bool is_canary(const Msg& m) {
    return m.producer == 0xDEAD && m.index == 0xBEEF;
}

// Flip all eight bits of a payload byte (typed: the payload is a char array,
// and a bare `^= 0xFF` on signed char would trip -Wconversion).
void flip_byte(char& b) {
    b = static_cast<char>(static_cast<unsigned char>(b) ^ 0xFFu);
}
}  // namespace

// ---------------------------------------------------------------------------
// CRC-32C known vectors (DEC-0005 #2). The canonical value is the published
// iSCSI/RoCE check value; the 52-byte full-payload-field patterns were
// cross-checked at analysis time against an independent bitwise (non-table)
// Castagnoli implementation (D-2026-09-17-003, finding 6).
// ---------------------------------------------------------------------------

SAFETY_CRIT_TEST_CASE(Crc32cKnownVectors, EmptyAndCanonicalValues) {
    SAFETY_CRIT_ASSERT(crc32c("", 0) == 0x00000000u);
    SAFETY_CRIT_ASSERT(crc32c_table("", 0) == 0x00000000u);
    // Published canonical check value for CRC-32C.
    SAFETY_CRIT_ASSERT(crc32c("123456789", 9) == 0xE3069283u);
    SAFETY_CRIT_ASSERT(crc32c_table("123456789", 9) == 0xE3069283u);
}

SAFETY_CRIT_TEST_CASE(Crc32cKnownVectors, FullPayloadFieldPatterns) {
    // 52 zero bytes = an all-zero default payload field.
    std::array<std::uint8_t, kDefaultSlotBytes> zeros{};
    SAFETY_CRIT_ASSERT(crc32c(zeros.data(), zeros.size()) == 0x6B668ACAu);
    SAFETY_CRIT_ASSERT(crc32c_table(zeros.data(), zeros.size()) == 0x6B668ACAu);

    // 52 mixed bytes (0x00..0x33) = a patterned default payload field.
    std::array<std::uint8_t, kDefaultSlotBytes> mix{};
    for (std::size_t i = 0; i < mix.size(); ++i) {
        mix[i] = static_cast<std::uint8_t>(i);
    }
    SAFETY_CRIT_ASSERT(crc32c(mix.data(), mix.size()) == 0xBCF4EC70u);
    SAFETY_CRIT_ASSERT(crc32c_table(mix.data(), mix.size()) == 0xBCF4EC70u);
}

SAFETY_CRIT_TEST_CASE(Crc32cKnownVectors, RunningFormMatchesOneShot) {
    // The running form (init/update/finalize) is what the region integrity
    // word chains over a header block and a separate seq word.
    const std::uint32_t split = crc32c_finalize(crc32c_update(
        crc32c_update(crc32c_init(), "12", 2), "3456789", 7));
    SAFETY_CRIT_ASSERT(split == 0xE3069283u);

    // Zero-length updates are no-ops; finalize(initial) is the empty CRC.
    const std::uint32_t s = crc32c_init();
    SAFETY_CRIT_ASSERT(crc32c_update(s, "123456789", 0) == s);
    SAFETY_CRIT_ASSERT(crc32c_finalize(crc32c_init()) == 0x00000000u);
}

#if defined(__SSE4_2__)
// Only present in TUs compiled with SSE4.2: verifies the dispatched intrinsic
// path against the table path on identical inputs (DEC-0005 #2). Current gated
// builds compile at the x86-64 baseline, so they run the table path only.
SAFETY_CRIT_TEST_CASE(Crc32cKnownVectors, DispatchedPathMatchesTablePath) {
    const std::string a = "123456789";
    SAFETY_CRIT_ASSERT(crc32c(a.data(), a.size()) == crc32c_table(a.data(), a.size()));

    std::array<std::uint8_t, kDefaultSlotBytes> mix{};
    for (std::size_t i = 0; i < mix.size(); ++i) {
        mix[i] = static_cast<std::uint8_t>(0xFFu - i);
    }
    SAFETY_CRIT_ASSERT(crc32c(mix.data(), mix.size()) == crc32c_table(mix.data(), mix.size()));
}
#endif

// ---------------------------------------------------------------------------
// try_pop corruption path (DEC-0005 #4): skip exactly the corrupted position,
// count it, never copy a corrupted payload out, keep protocol state valid.
// ---------------------------------------------------------------------------

SAFETY_CRIT_TEST_CASE(RingBufferCorruption, SingleSlotCorruptionSkipsExactlyThatMessage) {
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    SAFETY_CRIT_ASSERT(ring.corruption_count() == 0);
    for (std::uint32_t i = 0; i < 5; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(Msg{7, i}));
    }

    // Flip one payload byte of the committed slot at position 2.
    flip_byte(ring.slots()[2].payload[3]);

    Msg out{};
    SAFETY_CRIT_ASSERT(ring.try_pop(out));
    SAFETY_CRIT_ASSERT(out.producer == 7);
    SAFETY_CRIT_ASSERT(out.index == 0);
    SAFETY_CRIT_ASSERT(ring.try_pop(out));
    SAFETY_CRIT_ASSERT(out.producer == 7);
    SAFETY_CRIT_ASSERT(out.index == 1);

    // Corrupted slot: no value delivered and the out parameter is never
    // clobbered by a corrupted payload.
    out = canary_msg();
    SAFETY_CRIT_ASSERT(!ring.try_pop(out));
    SAFETY_CRIT_ASSERT(is_canary(out));
    SAFETY_CRIT_ASSERT(ring.corruption_count() == 1);

    // Remaining messages intact and in order; then a sound empty refusal.
    SAFETY_CRIT_ASSERT(ring.try_pop(out));
    SAFETY_CRIT_ASSERT(out.producer == 7);
    SAFETY_CRIT_ASSERT(out.index == 3);
    SAFETY_CRIT_ASSERT(ring.try_pop(out));
    SAFETY_CRIT_ASSERT(out.producer == 7);
    SAFETY_CRIT_ASSERT(out.index == 4);
    out = canary_msg();
    SAFETY_CRIT_ASSERT(!ring.try_pop(out));  // empty: sound refusal, no side effects
    SAFETY_CRIT_ASSERT(is_canary(out));
    SAFETY_CRIT_ASSERT(ring.empty());
    SAFETY_CRIT_ASSERT(ring.corruption_count() == 1);
}

SAFETY_CRIT_TEST_CASE(RingBufferCorruption, CorruptedCrcFieldDetected) {
    // The tag itself is in-cell: corrupting it must be caught by the same
    // mismatch check as a corrupted payload.
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    for (std::uint32_t i = 0; i < 3; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(Msg{8, i}));
    }

    ring.slots()[1].crc ^= 0xDEADBEEFu;

    Msg out{};
    SAFETY_CRIT_ASSERT(ring.try_pop(out));
    SAFETY_CRIT_ASSERT(out.index == 0);

    out = canary_msg();
    SAFETY_CRIT_ASSERT(!ring.try_pop(out));
    SAFETY_CRIT_ASSERT(is_canary(out));
    SAFETY_CRIT_ASSERT(ring.corruption_count() == 1);

    SAFETY_CRIT_ASSERT(ring.try_pop(out));
    SAFETY_CRIT_ASSERT(out.index == 2);
    SAFETY_CRIT_ASSERT(ring.empty());
}

SAFETY_CRIT_TEST_CASE(RingBufferCorruption, AllSlotsCorruptDrainsCleanlyAndRearms) {
    Ring8 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    SAFETY_CRIT_ASSERT(ring.corruption_count() == 0);
    for (std::uint32_t i = 0; i < Ring8::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(Msg{9, i}));
    }

    // Corrupt every committed slot.
    for (std::size_t i = 0; i < Ring8::kSlotCount; ++i) {
        flip_byte(ring.slots()[i].payload[0]);
    }

    Msg out{};
    for (std::uint32_t i = 0; i < Ring8::kSlotCount; ++i) {
        out = canary_msg();
        SAFETY_CRIT_ASSERT(!ring.try_pop(out));
        SAFETY_CRIT_ASSERT(is_canary(out));
    }
    SAFETY_CRIT_ASSERT(ring.corruption_count() == Ring8::kSlotCount);
    SAFETY_CRIT_ASSERT(ring.empty());  // every skip released its slot

    // Skipped slots re-arm: the next lap (positions 8..15) must be fully
    // usable with no new losses.
    for (std::uint32_t i = 0; i < Ring8::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(
            Msg{9, static_cast<std::uint32_t>(Ring8::kSlotCount) + i}));
    }
    for (std::uint32_t i = 0; i < Ring8::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_pop(out));
        SAFETY_CRIT_ASSERT(out.index == Ring8::kSlotCount + i);
    }
    SAFETY_CRIT_ASSERT(ring.corruption_count() == Ring8::kSlotCount);
    SAFETY_CRIT_ASSERT(ring.empty());
}

// ---------------------------------------------------------------------------
// Region integrity word (DEC-0005 #5): seeded at init, refreshed on the
// region-level commit path, consistent at quiescence.
// ---------------------------------------------------------------------------

SAFETY_CRIT_TEST_CASE(RegionIntegrity, InitialWordMatchesComputed) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));
    SAFETY_CRIT_ASSERT(verify_identity(region));
    SAFETY_CRIT_ASSERT(region.global_seq.load(std::memory_order_acquire) == 0);
    SAFETY_CRIT_ASSERT(region.integrity_word.load(std::memory_order_acquire) ==
                       compute_region_integrity(region));
}

SAFETY_CRIT_TEST_CASE(RegionIntegrity, HeaderFlipDetectedByRecompute) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));
    const std::uint32_t stored = region.integrity_word.load(std::memory_order_acquire);

    // One bit flip in a per-worker header word must change the recomputation.
    region.ring_buffers[1].padding[0] ^= 1;
    SAFETY_CRIT_ASSERT(compute_region_integrity(region) != stored);

    region.ring_buffers[1].padding[0] ^= 1;  // restore
    SAFETY_CRIT_ASSERT(compute_region_integrity(region) == stored);
}

SAFETY_CRIT_TEST_CASE(RegionIntegrity, PushAdvancesSeqAndKeepsWordConsistent) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));

    for (std::uint32_t i = 0; i < 10; ++i) {
        SAFETY_CRIT_ASSERT(push(region, 0, Msg{0, i}));
        // Single-threaded: after every commit the seq and word are consistent.
        SAFETY_CRIT_ASSERT(region.global_seq.load(std::memory_order_acquire) ==
                           static_cast<std::uint64_t>(i) + 1);
        SAFETY_CRIT_ASSERT(region.integrity_word.load(std::memory_order_acquire) ==
                           compute_region_integrity(region));
    }

    // A refused commit (full ring) advances nothing. Fill ring 1 to capacity?
    // Too large for a unit test; instead: out-of-range worker is a refusal too.
    SAFETY_CRIT_ASSERT(!push(region, kMaxWorkers, Msg{0, 0}));
    SAFETY_CRIT_ASSERT(region.global_seq.load(std::memory_order_acquire) == 10);

    // Consumption (raw path) does not touch region accounting words.
    Msg out{};
    SAFETY_CRIT_ASSERT(region.rings[0].try_pop(out));
    SAFETY_CRIT_ASSERT(out.index == 0);
    SAFETY_CRIT_ASSERT(region.global_seq.load(std::memory_order_acquire) == 10);
    SAFETY_CRIT_ASSERT(region.integrity_word.load(std::memory_order_acquire) ==
                       compute_region_integrity(region));
}

SAFETY_CRIT_TEST_CASE(RegionIntegrity, ConcurrentCommitsConvergeAtQuiescence) {
    // Exercises the CAS-converging refresh under real contention. Assertions
    // are only on quiescent state: live traffic may transiently disagree by
    // design (documented one-way direction), so nothing is asserted mid-run.
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));

    constexpr std::uint64_t kPerWorker = 64;
    std::barrier sync(2);
    std::atomic<bool> all_ok{true};
    auto worker = [&](std::size_t w) {
        sync.arrive_and_wait();
        for (std::uint32_t i = 0; i < kPerWorker; ++i) {
            if (!push(region, w, Msg{static_cast<std::uint32_t>(w), i})) {
                all_ok.store(false, std::memory_order_relaxed);
            }
        }
    };
    std::thread t0(worker, 0);
    std::thread t1(worker, 1);
    t0.join();
    t1.join();

    SAFETY_CRIT_ASSERT(all_ok.load(std::memory_order_acquire));
    // Quiescent convergence: the stored word covers exactly the final seq.
    SAFETY_CRIT_ASSERT(region.global_seq.load(std::memory_order_acquire) == 2 * kPerWorker);
    SAFETY_CRIT_ASSERT(region.integrity_word.load(std::memory_order_acquire) ==
                       compute_region_integrity(region));
}

// ---------------------------------------------------------------------------
// T1.3 layout witnesses: in-cell CRC placement and the new word's line
// discipline (extends the T1.1/T1.2 false-sharing suite to the counter).
// ---------------------------------------------------------------------------

SAFETY_CRIT_TEST_CASE(IntegrityLayout, CrcSitsAtEndOfPayloadAndWordsOnOwnLines) {
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());

    // Field placement inside the cell (pointer differences: Cell is not
    // standard-layout because of its atomic member, so offsetof would be
    // undefined). sequence at offset 0, payload after it, crc immediately
    // after the full payload field.
    const Ring16::Cell& c = ring.slots()[0];
    SAFETY_CRIT_ASSERT(reinterpret_cast<const char*>(&c.payload) -
                           reinterpret_cast<const char*>(&c.sequence) == 8);
    SAFETY_CRIT_ASSERT(reinterpret_cast<const char*>(&c.crc) -
                           reinterpret_cast<const char*>(&c.payload) == static_cast<std::ptrdiff_t>(Ring16::kSlotBytes));

    // With the region's default dimensions the cell fills one line exactly.
    SAFETY_CRIT_ASSERT(sizeof(LockFreeRingBuffer<2, kDefaultSlotBytes>::Cell) == 64);
    SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(&c) % 64 == 0);

    // Producer word and both consumer-side words each own a cache line.
    SAFETY_CRIT_ASSERT(line_of(&ring.tail_) != line_of(&ring.head_));
    SAFETY_CRIT_ASSERT(line_of(&ring.tail_) != line_of(&ring.corruption_count_));
    SAFETY_CRIT_ASSERT(line_of(&ring.head_) != line_of(&ring.corruption_count_));

    // Cross-worker: corruption words never share lines (the T1.1/T1.2 suite
    // covers head_/tail_; extend the witness to the T1.3 counter).
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));
    for (std::size_t a = 0; a < kMaxWorkers; ++a) {
        for (std::size_t b = a + 1; b < kMaxWorkers; ++b) {
            SAFETY_CRIT_ASSERT(line_of(&region.rings[a].corruption_count_) !=
                               line_of(&region.rings[b].corruption_count_));
        }
    }
}

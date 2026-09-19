#include "test_framework.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

#include "safety_crit/shared_memory/ring_buffer.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace {
using namespace safety_crit::shared_memory;

constexpr std::uintptr_t line_of(const void* p) {
    // std::bit_cast (not reinterpret_cast) keeps this a valid constexpr
    // function under Clang's default-error -Winvalid-constexpr.
    return std::bit_cast<std::uintptr_t>(p) / 64u;
}

// Small ring: forces backpressure and many wrap-arounds in the concurrency
// tests below. SlotBytes = 16 fits Msg (8 bytes).
using Ring16 = LockFreeRingBuffer<16, 16>;
using Ring8 = LockFreeRingBuffer<8, 16>;

// An 8-byte message; trivially copyable as required by try_push/try_pop.
struct Msg {
    std::uint32_t producer{0};
    std::uint32_t index{0};
};

static_assert(sizeof(Msg) <= 16);
static_assert(std::is_trivially_copyable_v<Msg>);

// Compile-time predicate witnesses (the `requires` clauses on the class use
// the same facts; these make the acceptance/rejection set explicit).
static_assert(is_power_of_two_v<1> && is_power_of_two_v<2> && is_power_of_two_v<4> &&
              is_power_of_two_v<1024>);
static_assert(!is_power_of_two_v<3> && !is_power_of_two_v<6> && !is_power_of_two_v<0>);
// A one-slot buffer is rejected outright by the class's `requires` clause
// (`SlotCount >= 2`): it has zero in-flight capacity, and with it the ready/
// commit marker classes would alias (see ring_buffer.hpp). The rejection set
// is witnessed executably below via a void_t detection trait.
//
// Why the witness takes this indirect form: naming a constrained class
// template-id whose constraint fails outside an immediate substitution
// context is ill-formed. Observed on GCC 13 (this project's toolchain) as a
// hard error in two places: inside `requires { typename
// LockFreeRingBuffer<1, 16>; }` and as the explicit template argument of a
// type-parameter detection trait. Passing the dimensions through non-type
// parameters instead defers formation of the id to partial-specialization
// matching -- a substitution context, where the same attempt degrades
// cleanly to `false`. Both behaviors are compiler-observed on GCC 13 rather
// than asserted as standard-mandated; they are falsifiable by attempting the
// two hard-erroring forms above.
template <std::size_t N, std::size_t B, class = void>
struct ring_instantiable : std::false_type {};
template <std::size_t N, std::size_t B>
struct ring_instantiable<N, B, std::void_t<LockFreeRingBuffer<N, B>>> : std::true_type {};

static_assert(!ring_instantiable<1, 16>::value);   // one slot: markers alias
static_assert(ring_instantiable<2, 16>::value);    // narrowest accepted ring
static_assert(!ring_instantiable<3, 16>::value);   // not a power of two
// Layout invariants of the buffer object itself.
static_assert(alignof(Ring16) == 64);
static_assert(sizeof(Ring16) % 64 == 0);
// With the region's default slot width, a cell is exactly one cache line.
static_assert(sizeof(LockFreeRingBuffer<2, kDefaultSlotBytes>::Cell) == 64);
}  // namespace

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, UninitializedBufferPopIsSafeAndEmpty) {
    Ring16 ring;  // default-initialized atomics; initialize() never called
    Msg out{};
    SAFETY_CRIT_ASSERT(!ring.try_pop(out));
    SAFETY_CRIT_ASSERT(ring.pushed() == 0);
    SAFETY_CRIT_ASSERT(ring.consumed() == 0);
    SAFETY_CRIT_ASSERT(ring.empty());
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, InitializeSucceedsOnAcceptedPlatforms) {
    // The design assumes lock-free 64-bit atomics (x86_64/ARM64). If this
    // platform violates that, initialization must refuse rather than degrade.
    std::atomic<std::uint64_t> lock_free_probe{0};
    SAFETY_CRIT_ASSERT(lock_free_probe.is_lock_free());
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
    LockFreeRingBuffer<16, 16>::DerivedState state{};
    SAFETY_CRIT_ASSERT(ring.derive_state(state));
    SAFETY_CRIT_ASSERT(state.head == 0);
    SAFETY_CRIT_ASSERT(state.tail == 0);
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, FillToCapacityThenFull) {
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    for (std::uint32_t i = 0; i < Ring16::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(i));
    }
    SAFETY_CRIT_ASSERT(ring.full());
    SAFETY_CRIT_ASSERT(!ring.try_push(999));  // full: sound refusal, no side effects
    SAFETY_CRIT_ASSERT(ring.pushed() == Ring16::kSlotCount);

    std::uint32_t out = 0;
    for (std::uint32_t i = 0; i < Ring16::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_pop(out));
        SAFETY_CRIT_ASSERT(out == i);  // FIFO order within one lap
    }
    SAFETY_CRIT_ASSERT(ring.empty());
    SAFETY_CRIT_ASSERT(!ring.try_pop(out));  // empty: sound refusal
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, WrapAroundSecondLap) {
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    for (std::int32_t lap = 0; lap < 4; ++lap) {
        const std::uint32_t base = static_cast<std::uint32_t>(lap) * 1000u;
        for (std::uint32_t i = 0; i < Ring16::kSlotCount; ++i) {
            SAFETY_CRIT_ASSERT(ring.try_push(base + i));
        }
        std::uint32_t out = 0;
        for (std::uint32_t i = 0; i < Ring16::kSlotCount; ++i) {
            SAFETY_CRIT_ASSERT(ring.try_pop(out));
            SAFETY_CRIT_ASSERT(out == base + i);
        }
    }
    SAFETY_CRIT_ASSERT(ring.pushed() == 4 * Ring16::kSlotCount);
    SAFETY_CRIT_ASSERT(ring.consumed() == 4 * Ring16::kSlotCount);
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, DerivationMatchesCountersAfterMixedTraffic) {
    // Exercises full/empty detection mid-traffic and cross-checks the stored
    // counters against a slot-counter-only derivation at quiescence.
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());

    for (std::uint32_t i = 0; i < 5; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(i));
    }
    std::uint32_t out = 0;
    for (std::uint32_t i = 0; i < 3; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_pop(out));
        SAFETY_CRIT_ASSERT(out == i);
    }
    // Two items outstanding: 14 more fills the ring exactly, the 15th must be
    // refused (full), then consuming four opens space again.
    for (std::uint32_t i = 0; i < 14; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(5 + i));
    }
    SAFETY_CRIT_ASSERT(!ring.try_push(42));  // outstanding == SlotCount -> full
    for (std::uint32_t i = 0; i < 4; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_pop(out));
    }
    SAFETY_CRIT_ASSERT(ring.pushed() == 19);
    SAFETY_CRIT_ASSERT(ring.consumed() == 7);

    LockFreeRingBuffer<16, 16>::DerivedState state{};
    SAFETY_CRIT_ASSERT(ring.derive_state(state));
    SAFETY_CRIT_ASSERT(state.head == 7);
    SAFETY_CRIT_ASSERT(state.tail == 19);
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, MalformedSlotCounterRejectedByDerivation) {
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    SAFETY_CRIT_ASSERT(ring.try_push(0));
    SAFETY_CRIT_ASSERT(ring.try_push(1));

    // Corrupt a slot counter into an impossible value (not i + k*n).
    ring.slots()[2].sequence.store(7, std::memory_order_relaxed);
    LockFreeRingBuffer<16, 16>::DerivedState state{};
    SAFETY_CRIT_ASSERT(!ring.derive_state(state));
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());

    // A well-formed but *wrong* value on the slot that pins the derived tail
    // must also be caught by the cross-check.
    ring.slots()[2].sequence.store(2 + 3 * Ring16::kSlotCount, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(ring.derive_state(state));
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, FutureLapCorruptionRejectedByWindowCheck) {
    // Regression test for the review finding: a well-formed counter advanced
    // into a future lap on a non-minimal slot leaves every derived minimum
    // untouched, so a minima-only cross-check would accept it.
    Ring8 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());

    // The exact evasion pattern: empty ring, slot 1 moved from ready(1) to
    // ready(1 + n). No valid operation can produce this with head == tail == 0.
    ring.slots()[1].sequence.store(1 + Ring8::kSlotCount, std::memory_order_relaxed);
    LockFreeRingBuffer<8, 16>::DerivedState state{};
    // The minima-based reconstruction is blind to it (derived values stay
    // correct) -- this is precisely why verification must check the window.
    SAFETY_CRIT_ASSERT(ring.derive_state(state));
    SAFETY_CRIT_ASSERT(state.head == 0);
    SAFETY_CRIT_ASSERT(state.tail == 0);
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());

    // Same class of corruption, two laps ahead on another slot.
    ring.slots()[1].sequence.store(1, std::memory_order_relaxed);  // restore
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
    ring.slots()[5].sequence.store(5 + 2 * Ring8::kSlotCount, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());

    // Corrupt stored state words independently of the slots: an inverted
    // window and a window wider than one lap are both impossible.
    ring.slots()[5].sequence.store(5, std::memory_order_relaxed);  // restore
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
    ring.head_.store(1, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());
    ring.head_.store(0, std::memory_order_relaxed);
    ring.tail_.store(Ring8::kSlotCount + 1, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, WindowCheckAcceptsQuiescentBoundaries) {
    // The per-slot window check must not over-reject legal quiescent states;
    // the full ring (tail - head == SlotCount) is the boundary case.
    Ring8 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    SAFETY_CRIT_ASSERT(ring.verify_consistent());  // empty

    for (std::uint32_t i = 0; i < 3; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(i));
    }
    SAFETY_CRIT_ASSERT(ring.verify_consistent());  // partially filled

    for (std::uint32_t i = 3; i < Ring8::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_push(i));
    }
    SAFETY_CRIT_ASSERT(ring.full());
    SAFETY_CRIT_ASSERT(ring.verify_consistent());  // full: tail - head == n

    std::uint32_t out = 0;
    for (std::uint32_t i = 0; i < 5; ++i) {
        SAFETY_CRIT_ASSERT(ring.try_pop(out));
        SAFETY_CRIT_ASSERT(out == i);
    }
    SAFETY_CRIT_ASSERT(ring.verify_consistent());  // mid-lap, head advanced
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, InFlightClaimReadsAsInconsistentUntilCommitted) {
    // Deterministically encodes the quiescence contract and its failure
    // direction (verify_consistent docblock): a transient in-flight state can
    // only cause a spurious false, and completing the operation restores
    // consistency. Constructed by hand: tail_/head_ advanced exactly as an
    // in-flight claim would leave them, with the slot's sequence store still
    // pending.
    Ring8 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());

    // Producer claimed position 0 (tail_ -> 1) but has not stored commit(0).
    ring.tail_.store(1, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(ring.slots()[0].sequence.load(std::memory_order_relaxed) == 0u);
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());
    ring.slots()[0].sequence.store(1, std::memory_order_relaxed);  // commit completes
    SAFETY_CRIT_ASSERT(ring.verify_consistent());

    // Symmetric consumer side: head_ advanced past the committed item but the
    // release store has not happened yet.
    ring.head_.store(1, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(!ring.verify_consistent());
    // Release of position p stores ready(p + n): here 0 + 8 = 8.
    ring.slots()[0].sequence.store(Ring8::kSlotCount, std::memory_order_relaxed);  // release
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(RingBufferProtocol, LayoutSeparatesSidesAndSlots) {
    Ring16 ring;
    // Producer and consumer state words on distinct lines (no false sharing).
    SAFETY_CRIT_ASSERT(line_of(&ring.tail_) != line_of(&ring.head_));
    // Every slot starts on its own cache line and no two slots share one.
    for (std::size_t i = 0; i < Ring16::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(ring.slots() + i) % 64 == 0);
    }
    for (std::size_t a = 0; a < Ring16::kSlotCount; ++a) {
        for (std::size_t b = a + 1; b < Ring16::kSlotCount; ++b) {
            SAFETY_CRIT_ASSERT(line_of(ring.slots() + a) != line_of(ring.slots() + b));
        }
    }
}

SAFETY_CRIT_TEST_CASE(RingBufferOrdering, SingleProducerSingleConsumerPreservesFifo) {
    constexpr std::size_t kItems = 4096;  // many laps over the 16-slot ring
    Ring16 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());

    std::thread producer([&ring] {
        for (std::uint32_t i = 0; i < kItems; ++i) {
            while (!ring.try_push(i)) {
                // Spin: a committed item leaves space shortly.
            }
        }
    });
    std::vector<std::uint32_t> received;
    received.reserve(kItems);
    std::thread consumer([&ring, &received] {
        while (received.size() < kItems) {
            std::uint32_t v = 0;
            if (ring.try_pop(v)) {
                received.push_back(v);
            } else {
                std::this_thread::yield();
            }
        }
    });
    producer.join();
    consumer.join();

    SAFETY_CRIT_ASSERT(received.size() == kItems);
    for (std::size_t i = 0; i < kItems; ++i) {
        SAFETY_CRIT_ASSERT(received[i] == static_cast<std::uint32_t>(i));  // exact FIFO order
    }
    SAFETY_CRIT_ASSERT(ring.pushed() == kItems);
    SAFETY_CRIT_ASSERT(ring.consumed() == kItems);
    SAFETY_CRIT_ASSERT(ring.empty());
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(RingBufferMultithreaded, MultiProducerNoLossAccounting) {
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kPerProducer = 256;
    constexpr std::size_t kTotal = kProducers * kPerProducer;

    Ring8 ring;  // deliberately tiny: heavy contention and wrap-around
    SAFETY_CRIT_ASSERT(ring.initialize());

    // Written only by the consumer thread.
    std::vector<std::uint8_t> seen(kTotal, 0);
    std::thread consumer([&ring, &seen] {
        std::size_t taken = 0;
        while (taken < kTotal) {
            Msg m{};
            if (!ring.try_pop(m)) {
                continue;
            }
            SAFETY_CRIT_ASSERT(m.producer < kProducers);
            SAFETY_CRIT_ASSERT(m.index < kPerProducer);
            const std::size_t idx = m.producer * kPerProducer + m.index;
            SAFETY_CRIT_ASSERT(seen[idx] == 0);  // duplicates would mean loss elsewhere
            seen[idx] = 1;
            ++taken;
        }
    });

    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (std::size_t j = 0; j < kPerProducer; ++j) {
                const Msg m{static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(j)};
                while (!ring.try_push(m)) {
                    // Spin until a slot frees.
                }
            }
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    consumer.join();

    for (std::size_t i = 0; i < kTotal; ++i) {
        SAFETY_CRIT_ASSERT(seen[i] == 1);  // every message delivered exactly once
    }
    SAFETY_CRIT_ASSERT(ring.pushed() == kTotal);
    SAFETY_CRIT_ASSERT(ring.consumed() == kTotal);
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(RingBufferMultithreaded, SynchronizedRoundRobinDeterministicInterleaving) {
    // Deterministic small-scale coverage: in each round every producer commits
    // exactly one message before any consumer pop happens (barrier-synchronized),
    // so a specific interleaving class is exercised on every round.
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kRounds = 8;
    constexpr std::size_t kTotal = kProducers * kRounds;

    Ring8 ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    std::barrier sync(static_cast<std::ptrdiff_t>(kProducers) + 1);

    auto producer_fn = [&ring, &sync](std::size_t p) {
        for (std::size_t r = 0; r < kRounds; ++r) {
            const Msg m{static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(r)};
            while (!ring.try_push(m)) {
                // Four outstanding max in an 8-slot ring: cannot block.
            }
            sync.arrive_and_wait();
        }
    };
    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < kProducers; ++p) {
        producers.emplace_back(producer_fn, p);
    }

    // Consumer: after each round's barrier, exactly the round's messages exist.
    std::array<bool, 16> in_round{};
    for (std::size_t r = 0; r < kRounds; ++r) {
        sync.arrive_and_wait();
        for (std::size_t p = 0; p < kProducers; ++p) {
            Msg m{};
            while (!ring.try_pop(m)) {
                // The round's commits are published by the barrier.
            }
            SAFETY_CRIT_ASSERT(m.producer < kProducers);
            SAFETY_CRIT_ASSERT(m.index == r);  // every popped item is from this round
            in_round[m.producer] = true;
        }
        // All four producers must have contributed exactly one message to the
        // round before the flags are cleared for the next one.
        for (std::size_t p = 0; p < kProducers; ++p) {
            SAFETY_CRIT_ASSERT(in_round[p]);
        }
        in_round.fill(false);
    }

    for (auto& t : producers) {
        t.join();
    }
    SAFETY_CRIT_ASSERT(ring.pushed() == kTotal);
    SAFETY_CRIT_ASSERT(ring.consumed() == kTotal);
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
}

SAFETY_CRIT_TEST_CASE(SharedRegionRings, InitializeValidatesDimensionsBeforeMutating) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));  // valid defaults
    SAFETY_CRIT_ASSERT(verify(region));

    // Wrong dimensions: refused, and the (valid) state must be untouched.
    SAFETY_CRIT_ASSERT(!initialize(region, kDefaultSlotCount - 1, kDefaultSlotBytes));
    SAFETY_CRIT_ASSERT(!initialize(region, kDefaultSlotCount, static_cast<std::uint32_t>(kDefaultSlotBytes + 1)));
    SAFETY_CRIT_ASSERT(verify(region));

    // A fresh default-constructed region must fail verification: its headers
    // do not record the compiled-in dimensions until initialize() runs.
    SharedRegion fresh;
    SAFETY_CRIT_ASSERT(!verify(fresh));
}

SAFETY_CRIT_TEST_CASE(SharedRegionRings, PerWorkerRingsAreUsableAndVerify) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));

    // Traffic through each worker's own ring; other workers' rings untouched.
    for (std::size_t w = 0; w < kMaxWorkers; ++w) {
        const Msg m{static_cast<std::uint32_t>(w), 7};
        SAFETY_CRIT_ASSERT(region.rings[w].try_push(m));
        Msg out{};
        SAFETY_CRIT_ASSERT(region.rings[w].try_pop(out));
        SAFETY_CRIT_ASSERT(out.producer == w);
        SAFETY_CRIT_ASSERT(out.index == 7);
    }
    // Slot-counter-derived state must still agree with stored counters for
    // every worker ring (re-attach cross-check).
    SAFETY_CRIT_ASSERT(verify(region));
}

SAFETY_CRIT_TEST_CASE(SharedRegionRings, VerificationScopesToQuiescentWorkers) {
    // Encodes the residual-risk mitigation for the quiescence precondition:
    // while one worker's ring is mid-operation, the full-region check may
    // report false (availability-direction only), but the scoped checks do
    // the right thing -- verify_identity() is unaffected, and the victim
    // worker's own ring verifies independently of live neighbours.
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));

    // Quiescent traffic on worker 0's ring.
    for (std::uint32_t i = 0; i < 5; ++i) {
        SAFETY_CRIT_ASSERT(region.rings[0].try_push(i));
    }
    std::uint32_t out = 0;
    for (std::uint32_t i = 0; i < 2; ++i) {
        SAFETY_CRIT_ASSERT(region.rings[0].try_pop(out));
        SAFETY_CRIT_ASSERT(out == i);
    }
    SAFETY_CRIT_ASSERT(verify(region));  // all workers quiescent

    // Emulate worker 2 mid-operation: a claim of position 0 whose commit
    // store has not landed yet.
    region.rings[2].tail_.store(1, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(!verify(region));             // needs ALL quiescent
    SAFETY_CRIT_ASSERT(verify_identity(region));     // metadata: unaffected
    SAFETY_CRIT_ASSERT(verify_worker_ring(region, 0));   // live-but-quiescent worker still fine
    SAFETY_CRIT_ASSERT(!verify_worker_ring(region, 2));  // its own check flags the transient

    // The re-attachment flow's actual need: taking over worker 2 after it
    // died mid-claim. Once its commit lands (or a restarted worker finishes
    // its in-flight operation), scoped verification passes without ever
    // requiring live neighbours to stop.
    region.rings[2].slots()[0].sequence.store(1, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(verify_worker_ring(region, 2));
    SAFETY_CRIT_ASSERT(verify(region));
}

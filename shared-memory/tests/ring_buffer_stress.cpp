// Phase 1, T1.5 (T-0006): stress tests proving no data loss and no duplicate
// processing at scale, on the shipped LockFreeRingBuffer defaults (1024 x 52).
//
// Accounting methodology (recorded as deviation #12 in
// docs/phases/PHASE_1_SHARED_MEMORY.md):
//   - Each message carries {producer, seq} (seq is per-producer, 1-based).
//     MPMC claim semantics hand each committed position to exactly one
//     consumer, so "produced == consumed with no duplication" is verified by
//     a per-(producer, seq) receive table of atomic bytes: every message must
//     be recorded exactly once (0 = lost, >1 = duplicated).
//   - Termination: producers finish all pushes, then a `done` flag is
//     published (release). Consumers drain until a pop fails after observing
//     `done` (acquire). A corrupt pop also returns false, so the table check
//     plus corruption_count() == 0 closes that hole; the table is only
//     inspected after all threads join.
//   - Ops volume: kStressOps defaults to 1,000,000 total messages per case
//     in every configuration, including TSan (measured: the full matrix's
//     TSan legs add ~7 s, so no reduction is needed). The build may override
//     the volume with -DSAFETY_CRIT_STRESS_OPS=<n> for constrained
//     environments; the G1.5 gate runs the un-overridden 1M in GoogleTest,
//     Catch2, ASan+UBSan, and TSan alike.

#include "test_framework.hpp"

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "safety_crit/shared_memory/ring_buffer.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace {
using namespace safety_crit::shared_memory;

#ifndef SAFETY_CRIT_STRESS_OPS
#define SAFETY_CRIT_STRESS_OPS 1000000u
#endif

constexpr std::uint64_t kStressOps = SAFETY_CRIT_STRESS_OPS;
constexpr std::uint64_t kQuiesceOps = kStressOps / 8u < 1u ? 1u : kStressOps / 8u;

constexpr std::size_t kNumProducers = 4;
constexpr std::size_t kNumConsumers = 4;

// 8-byte message; trivially copyable as required by try_push/try_pop.
struct Msg {
    std::uint32_t producer{0};
    std::uint32_t seq{0};
};

static_assert(sizeof(Msg) <= kDefaultSlotBytes);
static_assert(std::is_trivially_copyable_v<Msg>);

using ReceiveTable = std::vector<std::atomic<std::uint8_t>>;

void run_producer(RingBuffer& ring, std::uint32_t producer_id, std::uint64_t count) {
    for (std::uint64_t i = 1; i <= count; ++i) {
        const Msg m{producer_id, static_cast<std::uint32_t>(i)};
        // Backpressure is expected (ring is 1024 deep, consumers lag);
        // retry until the claim succeeds.
        while (!ring.try_push(m)) {
            std::this_thread::yield();
        }
    }
}

// Joins all producer threads, then publishes `done`. The drain pool must run
// concurrently with the producers (the ring is bounded: pushing 1M messages
// without concurrent consumers would block forever), and `done` may only be
// set once every producer has finished -- an early true would let consumers
// exit while pushes are still in flight.
void join_then_done(std::vector<std::thread>& producers, std::atomic<bool>& done) {
    for (auto& t : producers) {
        t.join();
    }
    done.store(true, std::memory_order_release);
}

// Consumer pool: pops until `done` is observed and a further pop fails (see
// the drain-protocol note at the top of the file). Records every delivered
// message into received[producer * stride + (seq - 1)].
void run_drain(RingBuffer& ring, const std::atomic<bool>& done, std::size_t num_consumers,
               std::size_t stride, ReceiveTable& received) {
    std::vector<std::thread> consumers;
    consumers.reserve(num_consumers);
    for (std::size_t c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&] {
            Msg out{};
            for (;;) {
                if (ring.try_pop(out)) {
                    received[static_cast<std::size_t>(out.producer) * stride +
                             (out.seq - 1u)]
                            .fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }
                if (done.load(std::memory_order_acquire)) {
                    // All pushes completed-before this observation; a second
                    // failure means every position is released or claimed by
                    // a consumer that records before joining.
                    if (!ring.try_pop(out)) {
                        break;
                    }
                    received[static_cast<std::size_t>(out.producer) * stride +
                             (out.seq - 1u)]
                            .fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }
                std::this_thread::yield();
            }
        });
    }
    for (auto& t : consumers) {
        t.join();
    }
}

// Verifies produced == consumed with zero duplication and zero corruption
// for `producers` x `per_producer` messages over `received`.
void verify_accounting(const RingBuffer& ring, const ReceiveTable& received,
                       std::size_t producers, std::uint64_t per_producer) {
    const std::uint64_t total = producers * per_producer;
    SAFETY_CRIT_ASSERT(ring.pushed() == total);
    SAFETY_CRIT_ASSERT(ring.consumed() == total);
    SAFETY_CRIT_ASSERT(ring.corruption_count() == 0u);
    std::uint64_t once = 0;
    for (const auto& cell : received) {
        const std::uint8_t n = cell.load(std::memory_order_relaxed);
        SAFETY_CRIT_ASSERT(n <= 1u);  // no duplicate processing
        if (n == 1u) {
            ++once;
        }
    }
    SAFETY_CRIT_ASSERT(once == total);  // no data loss
}

constexpr std::uintptr_t line_of(const void* p) {
    // std::bit_cast (not reinterpret_cast) keeps this a valid constexpr
    // function under Clang's default-error -Winvalid-constexpr.
    return std::bit_cast<std::uintptr_t>(p) / 64u;
}
}  // namespace

SAFETY_CRIT_TEST_CASE(RingBufferStress, SingleProducerMultiConsumerNoLossNoDuplication) {
    // G1.5 core case: 1 producer x 4 consumers, kStressOps messages, each
    // delivered exactly once.
    RingBuffer ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    ReceiveTable received(static_cast<std::size_t>(kStressOps));
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    producers.emplace_back(run_producer, std::ref(ring), 0u, kStressOps);
    std::thread finisher(join_then_done, std::ref(producers), std::ref(done));
    run_drain(ring, done, kNumConsumers, kStressOps, received);
    finisher.join();

    verify_accounting(ring, received, /*producers=*/1, kStressOps);
}

SAFETY_CRIT_TEST_CASE(RingBufferStress, MultiProducerMultiConsumerNoLossNoDuplication) {
    // 4 producers x 4 consumers, kStressOps total messages (per_producer each
    // per producer), each message delivered exactly once.
    RingBuffer ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    const std::uint64_t per_producer = kStressOps / kNumProducers;
    ReceiveTable received(kNumProducers * static_cast<std::size_t>(per_producer));
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < kNumProducers; ++p) {
        producers.emplace_back(run_producer, std::ref(ring),
                               static_cast<std::uint32_t>(p), per_producer);
    }
    std::thread finisher(join_then_done, std::ref(producers), std::ref(done));
    run_drain(ring, done, kNumConsumers, per_producer, received);
    finisher.join();

    verify_accounting(ring, received, kNumProducers, per_producer);
}

SAFETY_CRIT_TEST_CASE(RingBufferStress, QuiescentVerificationAndCacheLinesAfterStress) {
    // Short stress run (kStressOps / 8), then the attach-time witnesses on a
    // saturated-and-drained ring: verify_consistent() green at quiescence and
    // the false-sharing layout invariants (adjacent slots on distinct lines;
    // head_/tail_/corruption_count_ mutually separate) still green in the
    // stress build.
    RingBuffer ring;
    SAFETY_CRIT_ASSERT(ring.initialize());
    ReceiveTable received(static_cast<std::size_t>(kQuiesceOps));
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    producers.emplace_back(run_producer, std::ref(ring), 0u, kQuiesceOps);
    std::thread finisher(join_then_done, std::ref(producers), std::ref(done));
    run_drain(ring, done, kNumConsumers, kQuiesceOps, received);
    finisher.join();

    verify_accounting(ring, received, /*producers=*/1, kQuiesceOps);
    SAFETY_CRIT_ASSERT(ring.verify_consistent());
    SAFETY_CRIT_ASSERT(ring.empty());

    for (std::size_t i = 0; i < RingBuffer::kSlotCount; ++i) {
        SAFETY_CRIT_ASSERT(line_of(&ring.slots()[i]) !=
                           line_of(&ring.slots()[(i + 1) % RingBuffer::kSlotCount]));
    }
    SAFETY_CRIT_ASSERT(line_of(&ring.head_) != line_of(&ring.tail_));
    SAFETY_CRIT_ASSERT(line_of(&ring.corruption_count_) != line_of(&ring.head_));
    SAFETY_CRIT_ASSERT(line_of(&ring.corruption_count_) != line_of(&ring.tail_));
}

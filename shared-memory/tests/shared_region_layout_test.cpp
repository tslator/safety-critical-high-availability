#include "test_framework.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace {
using namespace safety_crit::shared_memory;

constexpr std::uintptr_t line_of(const void* p) {
    // std::bit_cast (not reinterpret_cast) keeps this a valid constexpr
    // function under Clang's default-error -Winvalid-constexpr.
    return std::bit_cast<std::uintptr_t>(p) / 64u;
}
}  // namespace

SAFETY_CRIT_TEST_CASE(SharedRegionLayout, CompileTimeInvariants) {
    // These static_asserts also live in the header; repeating them here gives
    // CTest an executable witness under both frameworks.
    SAFETY_CRIT_ASSERT(alignof(SharedRegion) == 64);
    SAFETY_CRIT_ASSERT(sizeof(RingBufferHeader) == 64);
    SAFETY_CRIT_ASSERT(sizeof(RingOwnershipCell) == 64);
    SAFETY_CRIT_ASSERT(sizeof(WorkerStatusCell) == 64);
    SAFETY_CRIT_ASSERT(sizeof(SharedRegion) % 64 == 0);
}

SAFETY_CRIT_TEST_CASE(SharedRegionLayout, NoFalseSharingBetweenWorkers) {
    SharedRegion region;
    for (std::size_t i = 0; i < kMaxWorkers; ++i) {
        SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(&region.ring_buffers[i]) % 64 == 0);
        SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(&region.worker_status[i].status) % 64 == 0);
        // T1.2: each worker's ring starts on its own line.
        SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(&region.rings[i]) % 64 == 0);
        SAFETY_CRIT_ASSERT(reinterpret_cast<std::uintptr_t>(&region.ring_ownership[i]) % 64 == 0);
    }
    // Every worker's header and status cell sits on its own cache line, and
    // none of them shares a line with global_seq.
    for (std::size_t a = 0; a < kMaxWorkers; ++a) {
        for (std::size_t b = a + 1; b < kMaxWorkers; ++b) {
            SAFETY_CRIT_ASSERT(line_of(&region.worker_status[a].status) !=
                               line_of(&region.worker_status[b].status));
            SAFETY_CRIT_ASSERT(line_of(&region.ring_buffers[a]) != line_of(&region.ring_buffers[b]));
            SAFETY_CRIT_ASSERT(line_of(&region.ring_ownership[a]) !=
                               line_of(&region.ring_ownership[b]));
            // T1.2: producer/consumer state words never share a line across workers.
            SAFETY_CRIT_ASSERT(line_of(&region.rings[a].head_) != line_of(&region.rings[b].head_));
            SAFETY_CRIT_ASSERT(line_of(&region.rings[a].tail_) != line_of(&region.rings[b].tail_));
        }
        SAFETY_CRIT_ASSERT(line_of(&region.worker_status[a].status) != line_of(&region.global_seq));
        SAFETY_CRIT_ASSERT(line_of(&region.ring_buffers[a]) != line_of(&region.global_seq));
    }
}

SAFETY_CRIT_TEST_CASE(SharedRegion, InitializeAndVerify) {
    SharedRegion region;
    initialize(region);
    SAFETY_CRIT_ASSERT(verify(region));
    SAFETY_CRIT_ASSERT(region.identity.magic == kRegionMagic);
    SAFETY_CRIT_ASSERT(region.identity.version == kRegionVersion);
    SAFETY_CRIT_ASSERT(region.global_seq.load(std::memory_order_acquire) == 0);
    for (std::size_t i = 0; i < kMaxWorkers; ++i) {
        SAFETY_CRIT_ASSERT(region.worker_status[i].status.load(std::memory_order_acquire) == 0);
        // T1.2: headers record the compiled-in dimensions at init time.
        SAFETY_CRIT_ASSERT(region.ring_buffers[i].slot_count == kDefaultSlotCount);
        SAFETY_CRIT_ASSERT(region.ring_buffers[i].slot_bytes == kDefaultSlotBytes);
        OwnershipToken token;
        SAFETY_CRIT_ASSERT(read_ownership(region, i, token));
        SAFETY_CRIT_ASSERT(token.logical_ring == i);
        SAFETY_CRIT_ASSERT(token.physical_owner == i);
        SAFETY_CRIT_ASSERT(token.process_generation == 1u);
        SAFETY_CRIT_ASSERT(token.epoch == 2u);
        SAFETY_CRIT_ASSERT(!load_has_flag(region.worker_status[i].status, WorkerStatusFlag::kRunning));
    }
}

SAFETY_CRIT_TEST_CASE(SharedRegion, AbandonedClaimOnFirstSlotRollsBackTail) {
    // T-0024 (DEC-0012 #2): an epoch bump implies every in-flight claim
    // from the prior epoch is abandoned. The new owner resumes from the
    // last committed sequence; never force-commit an in-flight slot.
    // Initial-slot variant: physical A claims position 0 (tail_ advances
    // to 1) and crashes before commit (slots()[0].sequence remains at
    // ready(0) = 0).
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));
    SAFETY_CRIT_ASSERT(region.rings[0].tail_.load(std::memory_order_relaxed) == 0u);
    region.rings[0].tail_.store(1u, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(region.rings[0].slots()[0].sequence.load(std::memory_order_relaxed) == 0u);

    OwnershipToken old_owner;
    SAFETY_CRIT_ASSERT(read_ownership(region, 0, old_owner));
    OwnershipToken replacement;
    SAFETY_CRIT_ASSERT(transfer_ownership(region, 0, old_owner, 2u, 1u, replacement));

    SAFETY_CRIT_ASSERT(region.rings[0].tail_.load(std::memory_order_relaxed) == 0u);
    SAFETY_CRIT_ASSERT(region.rings[0].head_.load(std::memory_order_relaxed) == 0u);
}

SAFETY_CRIT_TEST_CASE(SharedRegion, AbandonedClaimOnInteriorSlotRollsBackTail) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));
    struct Payload {
        std::uint32_t worker_idx;
        std::uint64_t tick;
    };
    for (std::uint64_t tick = 0; tick < 3u; ++tick) {
        SAFETY_CRIT_ASSERT(push(region, 0u, Payload{0u, tick}));
    }
    SAFETY_CRIT_ASSERT(region.rings[0].tail_.load(std::memory_order_relaxed) == 3u);
    SAFETY_CRIT_ASSERT(region.rings[0].head_.load(std::memory_order_relaxed) == 0u);

    // Abandon a claim at position 3: advance tail_ to 4 without committing.
    region.rings[0].tail_.store(4u, std::memory_order_relaxed);
    SAFETY_CRIT_ASSERT(region.rings[0].slots()[3].sequence.load(std::memory_order_relaxed) == 3u);

    OwnershipToken old_owner;
    SAFETY_CRIT_ASSERT(read_ownership(region, 0, old_owner));
    OwnershipToken replacement;
    SAFETY_CRIT_ASSERT(transfer_ownership(region, 0, old_owner, 2u, 1u, replacement));

    // Abandoned claim rolled back to last committed sequence.
    SAFETY_CRIT_ASSERT(region.rings[0].tail_.load(std::memory_order_relaxed) == 3u);
    SAFETY_CRIT_ASSERT(region.rings[0].head_.load(std::memory_order_relaxed) == 0u);
}

SAFETY_CRIT_TEST_CASE(SharedRegion, TransferDoesNotRollBackCommittedSlots) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));
    struct Payload {
        std::uint32_t worker_idx;
        std::uint64_t tick;
    };
    SAFETY_CRIT_ASSERT(push(region, 0u, Payload{0u, 42u}));
    SAFETY_CRIT_ASSERT(region.rings[0].tail_.load(std::memory_order_relaxed) == 1u);

    OwnershipToken old_owner;
    SAFETY_CRIT_ASSERT(read_ownership(region, 0, old_owner));
    OwnershipToken replacement;
    SAFETY_CRIT_ASSERT(transfer_ownership(region, 0, old_owner, 2u, 1u, replacement));

    // Committed slot at position 0 is preserved: tail_ stays at 1.
    SAFETY_CRIT_ASSERT(region.rings[0].tail_.load(std::memory_order_relaxed) == 1u);
    SAFETY_CRIT_ASSERT(region.rings[0].slots()[0].sequence.load(std::memory_order_acquire) == 1u);

    // The committed record must be poppable after takeover: proves the
    // takeover rule did not destroy valid data.
    Payload popped{};
    SAFETY_CRIT_ASSERT(region.rings[0].try_pop(popped));
    SAFETY_CRIT_ASSERT(popped.tick == 42u);
}

SAFETY_CRIT_TEST_CASE(SharedRegion, OwnershipFencesStaleGeneration) {
    SharedRegion region;
    SAFETY_CRIT_ASSERT(initialize(region));

    OwnershipToken old_owner;
    SAFETY_CRIT_ASSERT(read_ownership(region, 0, old_owner));
    OwnershipToken replacement;
    SAFETY_CRIT_ASSERT(transfer_ownership(region, 0, old_owner, 2u, 7u, replacement));
    SAFETY_CRIT_ASSERT(replacement.physical_owner == 2u);
    SAFETY_CRIT_ASSERT(replacement.process_generation == 7u);
    SAFETY_CRIT_ASSERT(replacement.epoch == 4u);
    SAFETY_CRIT_ASSERT(ownership_valid(region, replacement));
    SAFETY_CRIT_ASSERT(!ownership_valid(region, old_owner));

    OwnershipToken stale_replacement;
    SAFETY_CRIT_ASSERT(!transfer_ownership(region, 0, old_owner, 1u, 8u, stale_replacement));
}

SAFETY_CRIT_TEST_CASE(SharedRegion, RejectsBadIdentity) {
    SharedRegion region;
    initialize(region);
    region.identity.magic = 0xDEADBEEFu;
    SAFETY_CRIT_ASSERT(!verify(region));
    initialize(region);
    region.identity.version = kRegionVersion + 1;
    SAFETY_CRIT_ASSERT(!verify(region));
}

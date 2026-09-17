#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "safety_crit/shared_memory/ring_buffer.hpp"

namespace safety_crit::shared_memory {

// Workers: A and B are hot, C is the warm standby (plan section 1).
inline constexpr std::size_t kMaxWorkers = 3;

// Region identity ("SHMA"). The magic word guards against attaching to a
// wrong or stale object in /dev/shm; bump the version when the layout changes.
// Version history: v1 (T1.1) had no slot storage; v2 (T1.2) embeds one
// RingBuffer per worker and fills the ring headers at initialization.
inline constexpr std::uint32_t kRegionMagic = 0x53484D41u;
inline constexpr std::uint32_t kRegionVersion = 2u;

// Project-wide default ring dimensions (T1.2). All workers and all processes
// build with the same values: the slot arrays are part of this struct, so the
// dimensions are compile-time facts about the layout, exactly like
// kRegionVersion. SlotBytes = 56 makes each Cell exactly one 64-byte cache
// line (8-byte sequence + 56 payload bytes).
inline constexpr std::size_t kDefaultSlotCount = 1024;  // power of two
inline constexpr std::size_t kDefaultSlotBytes = 56;

using RingBuffer = LockFreeRingBuffer<kDefaultSlotCount, kDefaultSlotBytes>;

// Per-worker ring buffer metadata, one cache line each (T1.2 records the
// compiled-in ring dimensions here at region initialization; zero before that).
//
// Deliberately no per-worker sequence watermark: the last-consumed sequence
// for a worker IS its ring's head_ counter, which the protocol maintains on
// every pop and verify_consistent() cross-checks against the slot counters.
// A separately stored copy would go stale after the first consumption (only
// initialization could write it), and a stale safety-relevant value is worse
// than no field. If a supervisor-facing global watermark becomes necessary,
// derive it at attach time as the max over the workers' rings (deviation #7
// in PHASE_1_SHARED_MEMORY.md).
struct alignas(64) RingBufferHeader {
    std::uint64_t slot_count{0};   // power of two, enforced at construction
    std::uint32_t slot_bytes{0};
    std::uint32_t reserved0{0};
    std::uint64_t reserved1{0};    // was `committed_seq`; see above
    char padding[40];              // explicit pad to a full cache line
};

// One 64-byte cell per worker so concurrent status updates from different
// workers never share a cache line. This implements the plan's
// "no shared cache lines" requirement directly, which the plan sketch's
// trailing pad array did not achieve for worker_status.
struct alignas(64) WorkerStatusCell {
    std::atomic<std::uint64_t> status{0};  // packed with WorkerStatusFlag bits
};

struct alignas(64) SharedRegion {
    struct Identity {
        std::uint32_t magic{kRegionMagic};
        std::uint32_t version{kRegionVersion};
    };

    Identity identity;                                   // offset 0
    RingBufferHeader ring_buffers[kMaxWorkers];          // one cache line each
    WorkerStatusCell worker_status[kMaxWorkers];         // one cache line each
    // One ring per worker. Each RingBuffer is ~64 KiB with the default
    // dimensions, so a full region is ~200 KiB -- well within the 64 MiB
    // /dev/shm mount configured in Phase 0 (compose + CI).
    RingBuffer rings[kMaxWorkers];
    alignas(64) std::atomic<std::uint64_t> global_seq{0};
    std::atomic<std::uint32_t> integrity_word{0};        // shares the last cell with
                                                         // global_seq (global, not
                                                         // per-worker: no false-sharing risk)
};

// Layout invariants are compile-time contracts; violations must not ship.
// (Kept outside the class body: sizeof/incomplete-type rules.)
static_assert(alignof(SharedRegion) == 64);
static_assert(sizeof(RingBufferHeader) == 64);
static_assert(sizeof(WorkerStatusCell) == 64);
static_assert(alignof(RingBuffer) == 64);
static_assert(sizeof(RingBuffer) % 64 == 0);
static_assert(sizeof(SharedRegion) % 64 == 0);

// Initialize a freshly mapped (or stack-allocated for tests) region before any
// other thread can observe it.
//
// slot_count/slot_bytes must match the compiled-in default dimensions; they are
// a *validation* boundary (the supervisor passes its own configuration values
// here), not a way to configure the layout -- the ring arrays' sizes are fixed
// at compile time by kDefaultSlotCount/kDefaultSlotBytes.
//
// Returns false without modifying the region when the dimensions disagree or
// 64-bit atomics are not lock-free on this platform. On success, zeros all
// plain state, initializes every ring (slot sequences set to their slot index),
// fills each RingBufferHeader from the actual layout, and stamps identity.
bool initialize(SharedRegion& region,
                std::uint64_t slot_count = kDefaultSlotCount,
                std::uint32_t slot_bytes = static_cast<std::uint32_t>(kDefaultSlotBytes));

// Quiescence-independent attachment checks (plan section 3): identity and
// version match and every RingBufferHeader records the compiled-in
// dimensions. Reads no ring state, so live worker traffic cannot affect it;
// safe to call at any time.
bool verify_identity(const SharedRegion& region);

// Verifies one worker's ring: its slot counters must be consistent with its
// stored head/tail (per-slot window check). Precondition: that worker is
// quiescent -- idle or dead, which in the plan's re-attachment flow is
// exactly the case of the victim being taken over. A non-quiescent worker can
// only cause a spurious false here, never mask corruption (see
// LockFreeRingBuffer::verify_consistent): interpret a single false as
// "unverified", and re-check under established quiescence before acting.
// Intended for attach time, not the hot path.
bool verify_worker_ring(const SharedRegion& region, std::size_t worker_idx);

// Full-region attachment safety check: verify_identity() plus every worker
// ring. Precondition: ALL workers are quiescent (no in-flight push/pop on any
// ring) -- e.g. pre-boot validation or a maintenance window with workers
// stopped. While workers run live, use verify_worker_ring() for the specific
// worker being taken over instead; this function will read their rings
// mid-operation and may report false for perfectly healthy traffic.
bool verify(const SharedRegion& region);

}  // namespace safety_crit::shared_memory

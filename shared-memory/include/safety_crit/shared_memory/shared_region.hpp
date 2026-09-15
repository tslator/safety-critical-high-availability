#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace safety_crit::shared_memory {

// Workers: A and B are hot, C is the warm standby (plan section 1).
inline constexpr std::size_t kMaxWorkers = 3;

// Region identity ("SHMA"). The magic word guards against attaching to a
// wrong or stale object in /dev/shm; bump the version when the layout changes.
inline constexpr std::uint32_t kRegionMagic = 0x53484D41u;
inline constexpr std::uint32_t kRegionVersion = 1u;

// Per-worker ring buffer metadata, one cache line each (T1.2 fills the
// counters at region initialization; zero before that).
struct alignas(64) RingBufferHeader {
    std::uint64_t slot_count{0};   // power of two, enforced at construction
    std::uint32_t slot_bytes{0};
    std::uint32_t reserved0{0};
    std::uint64_t committed_seq{0};  // last consumed sequence for this worker
    char padding[40];                // explicit pad to a full cache line
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
static_assert(sizeof(SharedRegion) % 64 == 0);

// Initialize a freshly mapped (or stack-allocated for tests) region before any
// other thread can observe it. Zeros all state, then stamps the identity word.
void initialize(SharedRegion& region);

// Cheap attachment safety check: is this plausibly our region and version?
bool verify(const SharedRegion& region);

}  // namespace safety_crit::shared_memory

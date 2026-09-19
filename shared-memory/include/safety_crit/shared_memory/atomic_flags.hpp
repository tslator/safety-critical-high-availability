#pragma once

#include <atomic>
#include <cstdint>

namespace safety_crit::shared_memory {

// Bit flags stored in each SharedRegion worker status cell (plan section 1a).
enum class WorkerStatusFlag : std::uint64_t {
    kRunning = 1ULL << 0,
    kIdle = 1ULL << 1,
    kCrashed = 1ULL << 2,
    kRecovering = 1ULL << 3,
    // Phase 2 (DEC-0009 #4): per-tick CPU budget exceeded at least once since
    // the last clear. Semantics addition only -- the status word layout is
    // unchanged, so kRegionVersion stays 3.
    kOverrun = 1ULL << 4,
};

inline constexpr std::uint64_t to_bits(WorkerStatusFlag flag) {
    return static_cast<std::uint64_t>(flag);
}

// Inspect and combine raw status words (plain values, no atomics involved).
inline constexpr bool has_flag(std::uint64_t status, WorkerStatusFlag flag) {
    return (status & to_bits(flag)) != 0;
}

inline constexpr std::uint64_t with_flag(std::uint64_t status, WorkerStatusFlag flag,
                                         bool on) {
    const std::uint64_t bits = to_bits(flag);
    return on ? (status | bits) : (status & ~bits);
}

// Atomic transitions. Every operation is a single-word read-modify-write or
// store on the status cell; no locks are used anywhere in this layer.
inline void set_status(std::atomic<std::uint64_t>& status, std::uint64_t flags) {
    status.store(flags, std::memory_order_release);
}

inline void set_flag(std::atomic<std::uint64_t>& status, WorkerStatusFlag flag) {
    status.fetch_or(to_bits(flag), std::memory_order_acq_rel);
}

inline void clear_flag(std::atomic<std::uint64_t>& status, WorkerStatusFlag flag) {
    status.fetch_and(~to_bits(flag), std::memory_order_acq_rel);
}

inline bool load_has_flag(const std::atomic<std::uint64_t>& status,
                          WorkerStatusFlag flag) {
    return has_flag(status.load(std::memory_order_acquire), flag);
}

}  // namespace safety_crit::shared_memory

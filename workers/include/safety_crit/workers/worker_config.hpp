#pragma once

#include <chrono>
#include <cstdint>

#include "safety_crit/shared_memory/shared_region.hpp"

namespace safety_crit::workers {

// Role of a worker process (plan Phase 2 "Roles"). Failover orchestration is
// Phase 4 supervisor scope; the role here only selects what the process does
// locally: hot runs the work loop and pushes outputs, standby polls region
// status and pushes nothing (DEC-0009 #8).
enum class WorkerRole : std::uint8_t {
    kHot = 0,
    kStandby = 1,
};

// Immutable configuration of one worker process (T2.1). Durations use
// std::chrono types; the work loop (T2.2) enforces cpu_budget per tick and
// paces ticks with tick_interval.
//
// seed_base derives the deterministic per-tick workload seed:
// seed(worker_idx, tick) = splitmix64(seed_base ^ (worker_idx * phi) ^ tick).
// Determinism ("same input -> same output", plan test #1) therefore means:
// same (seed_base, worker_idx, tick) -> byte-identical processed output.
struct WorkerConfig {
    // worker_idx is the physical process identity. logical_ring is the ring
    // this process may publish to; an unassigned standby scans for promotion.
    std::uint32_t worker_idx{0};
    std::uint32_t logical_ring{shared_memory::kUnassignedPhysicalOwner};
    std::uint32_t process_generation{1};
    WorkerRole role{WorkerRole::kHot};
    std::uint64_t ticks{0};
    std::chrono::milliseconds tick_interval{10};
    std::chrono::microseconds cpu_budget{1000};
    std::uint64_t seed_base{0x9E3779B97F4A7C15ULL};
    // T-0027 (DEC-0012 #5): opt-in install of the SIGUSR2 corruption hook.
    // Production default is false; the CLI flag or env var opts in (see
    // corruption_hook_opt_in).
    bool corrupt_hook{false};
};

// T-0027 (DEC-0012 #5): single decision point for the corruption-hook opt-in:
// the CLI flag (--corrupt-hook) or SAFETY_CRIT_CORRUPT_HOOK=1. Anything else
// keeps the production default (no handler installed).
bool corruption_hook_opt_in(bool cli_flag, const char* env_value);

// Validation boundary (CLI parsing in T2.3, tests here): a config is valid
// when the worker index names an existing region slot, the tick count is
// positive, and both durations are strictly positive. Returns false and
// leaves `config` untouched otherwise.
bool validate_config(const WorkerConfig& config);

}  // namespace safety_crit::workers

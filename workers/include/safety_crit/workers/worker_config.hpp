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
    std::uint32_t worker_idx{0};
    WorkerRole role{WorkerRole::kHot};
    std::uint64_t ticks{0};
    std::chrono::milliseconds tick_interval{10};
    std::chrono::microseconds cpu_budget{1000};
    std::uint64_t seed_base{0x9E3779B97F4A7C15ULL};
};

// Validation boundary (CLI parsing in T2.3, tests here): a config is valid
// when the worker index names an existing region slot, the tick count is
// positive, and both durations are strictly positive. Returns false and
// leaves `config` untouched otherwise.
bool validate_config(const WorkerConfig& config);

}  // namespace safety_crit::workers

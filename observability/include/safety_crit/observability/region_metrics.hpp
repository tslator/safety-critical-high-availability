// Read-only region metrics collector (T-0041, DEC-0014 §8/§11).
//
// Bridges the shared-memory region to a MetricsRegistry: declares the DEC-0014
// §8 region families and samples them with one atomic load per value through
// the sanctioned observer surface only (WorkerStatusCell.status acquire loads,
// RingBuffer::pushed() / corruption_count(), read_ownership()). No CAS, no
// store, no pop anywhere: a region write from this code is a review blocker
// (DEC-0014 §11). verify_identity() succeeding after collection is the
// read-only proof, pinned by tests.
//
// worker_status metric mapping (DEC-0014 §8; pinned by tests):
//   IDLE=0, RUNNING=1, CRASHED=2, RECOVERING=3, DEGRADED=4.
// The in-region status cell is a bitset, so multiple bits can be set at once;
// they resolve by precedence DEGRADED > RECOVERING > CRASHED > RUNNING > IDLE.
// A zero word (worker never started) maps to 0 (IDLE).
//
// Ownership epochs: read_ownership() fails for unassigned (epoch 0) or
// mid-transfer (odd epoch) rings; the collector then keeps the last reported
// epoch for that ring (absent until the first successful read) and the cycle
// still succeeds — a scrape never blocks and never fabricates an epoch.

#pragma once

#include <cstdint>
#include <system_error>

#include "safety_crit/observability/metrics.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace safety_crit::observability {

// Maps one raw worker status word to its DEC-0014 §8 metric value (see the
// header comment for the precedence table). Always succeeds; exposed for
// tests to pin the mapping without a region.
double worker_status_metric_value(std::uint64_t status_word);

// Declares the region families on `registry` (DEC-0014 §8):
//   worker_status{worker}          gauge
//   ring_buffer_sequence{worker}   gauge
//   ring_corruptions_total{worker} counter
//   ownership_epoch{ring}          gauge
// Labels are decimal logical indices ("0".."kMaxWorkers-1"). False with the
// registry's error (EEXIST/EINVAL) on declaration violations.
bool declare_region_metrics(MetricsRegistry& registry, std::error_code& ec);

// Samples every region value into `registry` (families must have been
// declared; ENOENT otherwise). Pure read path: each value is one atomic load
// (status acquire; ring counters relaxed, observability-only words; epochs
// through read_ownership). False only on registry contract violations.
bool collect_region_metrics(const safety_crit::shared_memory::SharedRegion& region,
                            MetricsRegistry& registry, std::error_code& ec);

}  // namespace safety_crit::observability

// T-0034 (DEC-0014 §4): worker lifecycle event vocabulary.
//
// Published vocabulary (additions additive; renames/removals require a
// decision, DEC-0010 rule carried into DEC-0014 maintenance rules):
//
//   worker_started           info  "role":"hot","ring":<N> |
//                                      "role":"standby"[,"ring":<N>]
//   worker_stopped           info  "role":"hot","reason":"completed|
//                                      stop_signal|ownership_lost","ticks":<N>,
//                                      "overruns":<N> |
//                                      "role":"standby","reason":
//                                      "stop_signal","polls":<N>
//   worker_deadline_overrun  warn  "role":"hot","overruns":<N>
//
// Workers emit to the consolidated event log ONLY -- never to stdout
// (DEC-0009 #6 keeps workers silent there; DEC-0014 §4 grants workers the
// log without stdout). All emission happens between ticks, outside the
// ring hot path. `instance` is the worker index.
#pragma once

#include <cstdint>
#include <string>

#include "safety_crit/workers/worker_config.hpp"

namespace safety_crit::workers {

inline constexpr const char* kEventWorkerStarted = "worker_started";
inline constexpr const char* kEventWorkerStopped = "worker_stopped";
inline constexpr const char* kEventWorkerDeadlineOverrun = "worker_deadline_overrun";

// Stop reasons for the hot role (worker_stopped "reason" field).
inline constexpr const char* kStopReasonCompleted = "completed";
inline constexpr const char* kStopReasonSignal = "stop_signal";
inline constexpr const char* kStopReasonOwnershipLost = "ownership_lost";

// Extra-field builders (pinned field sets and orders).
std::string worker_started_fields(WorkerRole role, bool ring_assigned,
                                  std::uint32_t ring);
std::string worker_stopped_hot_fields(const char* reason, std::uint64_t ticks,
                                      std::uint64_t overruns);
std::string worker_stopped_standby_fields(std::uint64_t polls);
std::string worker_deadline_overrun_fields(std::uint64_t overruns);

}  // namespace safety_crit::workers

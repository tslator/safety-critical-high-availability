// Replay-log schema (T-0028/T-0030, DEC-0012 #8).
//
// The perturbation harness appends one JSON object per line:
//
//   {"ts":<ns>,"category":"<category>","target":<pid>,"params":{...}}
//
//   - ts:      CLOCK_REALTIME nanoseconds at action time. Wall-clock
//              timestamps and pids are EXCLUDED from replay comparison
//              (DEC-0012 #8: determinism is semantic, not bit-identical).
//   - category: replay-comparison categories (DEC-0012 #8):
//              "crash" | "stall" | "corrupt" | "double-fault" |
//              "supervisor-kill". The harness additionally emits
//              "recover-stall" bookkeeping records; replay ignores them.
//   - target:  pid_t of the signal recipient.
//   - params:  JSON object; empty {} unless the action carries extra data
//              (double-fault records the second victim as
//              {"second":<pid>}).
//
// Replay re-issues the signals at relative time offsets against a fresh
// topology; the Phase 5 requirement is one deterministic S1 replay
// (T-0030). The broader record()/replay() engine is Phase 5b.
//
// NEVER FOR PRODUCTION USE (DEC-0007 destroy() pattern).
#pragma once

#include <cstddef>

namespace safety_crit::perturb {

inline constexpr const char* kReplayCategories[] = {
    "crash", "stall", "corrupt", "double-fault", "supervisor-kill",
};
inline constexpr std::size_t kReplayCategoryCount = 5;

}  // namespace safety_crit::perturb

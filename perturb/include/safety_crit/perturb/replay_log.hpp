// Replay-log schema (T-0030, DEC-0012 #8) and versioned parsing.
//
// Every harness-emitted stream is versioned by a header record, one per
// CLI invocation:
//
//   {"schema":1,"ts":<ns>,"category":"_header","target":0,"params":{}}
//
// followed by action records:
//
//   {"ts":<ns>,"category":"<category>","target":<pid>,"params":{...}}
//
//   - ts:      CLOCK_REALTIME nanoseconds at action time. Replay re-issues
//              signals at RELATIVE offsets (ts - first action ts); wall
//              clock timestamps and pids are EXCLUDED from comparison
//              (DEC-0012 #8: determinism is semantic, not bit-identical).
//   - category: replay-comparison categories (DEC-0012 #8):
//              "crash" | "stall" | "corrupt" | "double-fault" |
//              "supervisor-kill" (plus "recover-stall" and
//              "supervisor-exit" harness bookkeeping, replayed verbatim).
//   - target:  pid_t of the signal recipient at record time; remapped at
//              replay time via --target-remap <from>=<pid>.
//   - params:  JSON object; double-fault records the second victim as
//              {"second":<pid>} (also remapped).
//
// Parsers must ignore header records for event-sequence purposes and
// reject files containing no schema header (unversioned/legacy input).
// NEVER FOR PRODUCTION USE (DEC-0007 destroy() pattern).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace safety_crit::perturb {

inline constexpr std::uint32_t kReplaySchemaVersion = 1u;

inline constexpr const char* kReplayCategories[] = {
    "crash", "stall", "recover-stall", "corrupt", "double-fault",
    "supervisor-kill", "supervisor-exit",
};
inline constexpr std::size_t kReplayCategoryCount = 7;

// One parsed log line. `category` is the raw string (a "_header" marker is
// surfaced as a record with is_header set); `second_target` is 0 unless
// params carried {"second":N}.
struct ReplayEntry {
    std::uint64_t ts_ns{0};
    std::string category{};
    std::int64_t target{0};
    std::int64_t second_target{0};
    bool is_header{false};
};

// Parses a whole replay log. Requires at least one schema header record;
// unknown fields are ignored, malformed action records are rejected with a
// description in `error`.
bool parse_replay_log(std::string_view text, std::vector<ReplayEntry>& out,
                      std::string& error);

}  // namespace safety_crit::perturb

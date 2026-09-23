// Perturbation harness (T-0028, DEC-0012 #7).
//
// NEVER FOR PRODUCTION USE. This module sends fault signals (SIGSEGV,
// SIGSTOP, SIGKILL, SIGUSR2) at target processes and records every action
// as a JSON-lines replay-log entry. It exists solely as the fault-injection
// surface for Phase 5 scenarios, following the same opt-in discipline as the
// DEC-0007 test-only destroy() and the DEC-0012 #5 corruption hook.
//
// Error idiom: Phase 2 deviation style (bool fn(..., std::error_code& ec))
// because the pinned container's libstdc++ 12 predates std::expected; the
// deviation is recorded in NOTES.md and retires when libstdc++ reaches 13.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/types.h>

namespace safety_crit::perturb {

// One category per harness action. The replay-log comparison categories are
// the DEC-0012 #8 subset (see replay_log.hpp); "recover-stall" is a
// harness-only bookkeeping record, never replayed.
enum class Category : std::uint8_t {
    kCrash,
    kStall,
    kRecoverStall,
    kCorrupt,
    kDoubleFault,
    kSupervisorKill,
};

const char* to_string(Category category);
bool category_from_string(std::string_view name, Category& out);

// One replay-log record: {ts_ns, category, target_pid, params}. `params` is
// either empty (rendered as {}) or a pre-formatted JSON object body.
struct Record {
    std::uint64_t ts_ns{0};
    Category category{Category::kCrash};
    pid_t target_pid{0};
    std::string params{};
};

// Formats one record as a single JSON line (schema: replay_log.hpp).
bool format_record_line(const Record& record, std::string& out);

// Caller-supplied sink for emitted records. Default: nullptr (disabled --
// actions run silently). The sink must outlive every emit; the harness never
// closes it.
void set_record_sink(std::FILE* sink);

// Formats and writes one record to the sink. False when disabled or the
// write fails. Never sends signals.
bool emit_record(const Record& record);

// Actions (DEC-0012 #1/#3/#5). Each sends its signal(s) first and records
// exactly one line on success; a failed action records nothing and sets
// `ec` from errno.

// SIGSEGV at the target; falls back to SIGKILL if SIGSEGV cannot be
// delivered.
bool crash(pid_t target, std::error_code& ec);
// SIGSTOP (stall injection; DEC-0012 #1).
bool stall(pid_t target, std::error_code& ec);
// SIGCONT (release a harness-injected stall).
bool recover_stall(pid_t target, std::error_code& ec);
// SIGUSR2 (poison-next-slot hook; target must have opted in via
// --corrupt-hook, T-0027/DEC-0012 #5).
bool corrupt_next_slot(pid_t target, std::error_code& ec);
// SIGKILL to both targets with no gap (double fault; DEC-0012 #4).
bool double_fault(pid_t first, pid_t second, std::error_code& ec);
// SIGKILL at the (container PID 1) supervisor (DEC-0012 #6).
bool kill_supervisor(pid_t target, std::error_code& ec);

// Parsed CLI invocation: `perturb <category> --target <pid>
// [--target2 <pid>] [--out <path>]`.
struct Invocation {
    Category category{Category::kCrash};
    pid_t target{-1};
    pid_t target2{-1};
    std::string out_path{};  // empty means stdout
};
bool parse_invocation(int argc, const char* const* argv, Invocation& out, std::string& error);

// Executes a parsed invocation: opens the sink (append) from out_path when
// given, performs the action, returns 0 on success, 1 on runtime failure.
int run_invocation(const Invocation& invocation);

}  // namespace safety_crit::perturb

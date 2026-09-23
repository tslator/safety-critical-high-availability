#include "safety_crit/perturb/harness.hpp"

#include <cerrno>
#include <charconv>
#include <csignal>
#include <ctime>
#include <string_view>

namespace safety_crit::perturb {
namespace {

std::FILE* g_sink = nullptr;

std::uint64_t realtime_ns() {
    struct timespec ts {};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

int send_signal(pid_t target, int signum, std::error_code& ec) {
    if (::kill(target, signum) == 0) {
        return 0;
    }
    ec = std::error_code(errno, std::generic_category());
    return -1;
}

bool record_action(Category category, pid_t target, std::string params) {
    if (g_sink == nullptr) {
        return true;  // sink disabled: recording is a no-op, action stands
    }
    Record entry;
    entry.ts_ns = realtime_ns();
    entry.category = category;
    entry.target_pid = target;
    entry.params = std::move(params);
    return emit_record(entry);
}

bool parse_pid(std::string_view text, pid_t& out) {
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && out > 0;
}

}  // namespace

const char* to_string(Category category) {
    switch (category) {
        case Category::kCrash:
            return "crash";
        case Category::kStall:
            return "stall";
        case Category::kRecoverStall:
            return "recover-stall";
        case Category::kCorrupt:
            return "corrupt";
        case Category::kDoubleFault:
            return "double-fault";
        case Category::kSupervisorKill:
            return "supervisor-kill";
        case Category::kSupervisorExit:
            return "supervisor-exit";
    }
    return "unknown";
}

bool category_from_string(std::string_view name, Category& out) {
    for (const Category candidate : {Category::kCrash, Category::kStall,
                                     Category::kRecoverStall, Category::kCorrupt,
                                     Category::kDoubleFault, Category::kSupervisorKill,
                                     Category::kSupervisorExit}) {
        if (name == to_string(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

bool format_record_line(const Record& record, std::string& out) {
    out = "{\"ts\":" + std::to_string(record.ts_ns) + ",\"category\":\"" +
          to_string(record.category) + "\",\"target\":" + std::to_string(record.target_pid) +
          ",\"params\":" + (record.params.empty() ? std::string("{}") : record.params) + "}\n";
    return true;
}

void set_record_sink(std::FILE* sink) {
    g_sink = sink;
}

bool emit_record(const Record& record) {
    if (g_sink == nullptr) {
        return false;
    }
    std::string line;
    format_record_line(record, line);
    const std::size_t written =
        std::fwrite(line.data(), 1, line.size(), g_sink);
    if (written != line.size()) {
        return false;
    }
    std::fflush(g_sink);
    return true;
}

bool crash(pid_t target, std::error_code& ec) {
    ec = std::error_code{};
    if (send_signal(target, SIGSEGV, ec) != 0) {
        // SIGSEGV undeliverable (permissive fallback path, DEC-0012 #1).
        if (send_signal(target, SIGKILL, ec) != 0) {
            return false;
        }
    }
    return record_action(Category::kCrash, target, std::string{});
}

bool stall(pid_t target, std::error_code& ec) {
    ec = std::error_code{};
    if (send_signal(target, SIGSTOP, ec) != 0) {
        return false;
    }
    return record_action(Category::kStall, target, std::string{});
}

bool recover_stall(pid_t target, std::error_code& ec) {
    ec = std::error_code{};
    if (send_signal(target, SIGCONT, ec) != 0) {
        return false;
    }
    return record_action(Category::kRecoverStall, target, std::string{});
}

bool corrupt_next_slot(pid_t target, std::error_code& ec) {
    ec = std::error_code{};
    if (send_signal(target, SIGUSR2, ec) != 0) {
        return false;
    }
    return record_action(Category::kCorrupt, target, std::string{});
}

bool double_fault(pid_t first, pid_t second, std::error_code& ec) {
    ec = std::error_code{};
    if (send_signal(first, SIGKILL, ec) != 0) {
        return false;
    }
    if (send_signal(second, SIGKILL, ec) != 0) {
        return false;
    }
    return record_action(Category::kDoubleFault, first,
                         "{\"second\":" + std::to_string(second) + "}");
}

bool kill_supervisor(pid_t target, std::error_code& ec) {
    ec = std::error_code{};
    if (send_signal(target, SIGKILL, ec) != 0) {
        return false;
    }
    return record_action(Category::kSupervisorKill, target, std::string{});
}

bool exit_supervisor(pid_t target, std::error_code& ec) {
    ec = std::error_code{};
    if (send_signal(target, SIGTERM, ec) != 0) {
        return false;
    }
    return record_action(Category::kSupervisorExit, target, std::string{});
}

bool replay_entries(const std::vector<ReplayEntry>& entries,
                    const std::vector<std::pair<pid_t, pid_t>>& remap, std::error_code& ec) {
    auto resolve = [&](std::int64_t original) -> pid_t {
        for (const auto& pair : remap) {
            if (pair.first == original) {
                return pair.second;
            }
        }
        return static_cast<pid_t>(original);
    };
    std::uint64_t base_ts = 0;
    bool base_seen = false;
    for (const ReplayEntry& entry : entries) {
        if (entry.is_header) {
            continue;
        }
        if (!base_seen) {
            base_ts = entry.ts_ns;
            base_seen = true;
        } else if (entry.ts_ns > base_ts) {
            const auto ms = static_cast<long long>((entry.ts_ns - base_ts) / 1000000ULL);
            const ::timespec sleep_for{ms / 1000, (ms % 1000) * 1000000L};
            ::nanosleep(&sleep_for, nullptr);
        }
        const pid_t target = resolve(entry.target);
        Category category{};
        if (!category_from_string(entry.category, category)) {
            ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        switch (category) {
            case Category::kCrash:
                if (!crash(target, ec)) {
                    return false;
                }
                break;
            case Category::kStall:
                if (!stall(target, ec)) {
                    return false;
                }
                break;
            case Category::kRecoverStall:
                if (!recover_stall(target, ec)) {
                    return false;
                }
                break;
            case Category::kCorrupt:
                if (!corrupt_next_slot(target, ec)) {
                    return false;
                }
                break;
            case Category::kDoubleFault:
                if (!double_fault(target, resolve(entry.second_target), ec)) {
                    return false;
                }
                break;
            case Category::kSupervisorKill:
                if (!kill_supervisor(target, ec)) {
                    return false;
                }
                break;
            case Category::kSupervisorExit:
                if (!exit_supervisor(target, ec)) {
                    return false;
                }
                break;
        }
    }
    return true;
}

bool parse_invocation(int argc, const char* const* argv, Invocation& out, std::string& error) {
    if (argc < 1 || !category_from_string(argv[0], out.category)) {
        error = "perturb: unknown or missing category (crash|stall|recover-stall|corrupt|"
                "double-fault|supervisor-kill|supervisor-exit)";
        return false;
    }
    bool target_seen = false;
    bool target2_seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--target") {
            if (++i >= argc || !parse_pid(argv[i], out.target)) {
                error = "perturb: --target requires a positive integer pid";
                return false;
            }
            target_seen = true;
        } else if (arg == "--target2") {
            if (++i >= argc || !parse_pid(argv[i], out.target2)) {
                error = "perturb: --target2 requires a positive integer pid";
                return false;
            }
            target2_seen = true;
        } else if (arg == "--out") {
            if (++i >= argc || argv[i][0] == '\0') {
                error = "perturb: --out requires a file path";
                return false;
            }
            out.out_path = argv[i];
        } else {
            error = "perturb: unknown argument: " + std::string(arg);
            return false;
        }
    }
    if (!target_seen) {
        error = "perturb: --target is required";
        return false;
    }
    if (out.category == Category::kDoubleFault && !target2_seen) {
        error = "perturb: double-fault requires --target2";
        return false;
    }
    return true;
}

int run_invocation(const Invocation& invocation) {
    std::FILE* sink = nullptr;
    if (!invocation.out_path.empty()) {
        sink = std::fopen(invocation.out_path.c_str(), "a");
        if (sink == nullptr) {
            std::fprintf(stderr, "perturb: cannot open '%s'\n", invocation.out_path.c_str());
            return 1;
        }
    } else {
        sink = stdout;
    }
    set_record_sink(sink);
    // T-0030: every harness stream is versioned by a schema header record
    // (replay_log.hpp).
    {
        const std::string line = "{\"schema\":" + std::to_string(kReplaySchemaVersion) +
                                 ",\"ts\":" + std::to_string(realtime_ns()) +
                                 ",\"category\":\"_header\",\"target\":0,\"params\":{}}\n";
        (void)std::fwrite(line.data(), 1, line.size(), sink);
        std::fflush(sink);
    }
    std::error_code ec;
    bool ok = false;
    switch (invocation.category) {
        case Category::kCrash:
            ok = crash(invocation.target, ec);
            break;
        case Category::kStall:
            ok = stall(invocation.target, ec);
            break;
        case Category::kRecoverStall:
            ok = recover_stall(invocation.target, ec);
            break;
        case Category::kCorrupt:
            ok = corrupt_next_slot(invocation.target, ec);
            break;
        case Category::kDoubleFault:
            ok = double_fault(invocation.target, invocation.target2, ec);
            break;
        case Category::kSupervisorKill:
            ok = kill_supervisor(invocation.target, ec);
            break;
        case Category::kSupervisorExit:
            ok = exit_supervisor(invocation.target, ec);
            break;
    }
    set_record_sink(nullptr);
    if (sink != stdout) {
        std::fclose(sink);
    }
    if (!ok) {
        std::fprintf(stderr, "perturb: action failed (errno %d)\n", ec.value());
        return 1;
    }
    return 0;
}

}  // namespace safety_crit::perturb

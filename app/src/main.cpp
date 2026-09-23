#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "safety_crit/monitors/monitor_config.hpp"
#include "safety_crit/monitors/monitor_entry.hpp"
#include "safety_crit/monitors/pidfile_liveness.hpp"
#include "safety_crit/supervisor/supervisor.hpp"
#include "safety_crit/workers/pidfile.hpp"
#include "safety_crit/workers/worker_config.hpp"
#include "safety_crit/workers/worker_entry.hpp"

#ifndef SAFETY_CRIT_COMPILER_NAME
#define SAFETY_CRIT_COMPILER_NAME "Unknown Compiler"
#endif

namespace {

void print_usage(std::ostream& out) {
    out << "usage: safety-critical-ha --version\n"
        << "       safety-critical-ha worker --id <a|b|c> [--role hot|standby]\n"
        << "           [--logical-id <a|b|c>] [--generation N] [--corrupt-hook]\n"
        << "           [--ticks N] [--tick-interval-ms MS] [--budget-us US]\n"
        << "           [--region NAME] [--pid-dir DIR]\n"
        << "       safety-critical-ha monitor [--interval-ms MS]\n"
        << "           [--stall-threshold-ms MS] [--region NAME]\n"
        << "           [--pid-dir DIR] [--polls N]\n"
        << "       safety-critical-ha supervisor [--runtime-ms MS]\n"
        << "           [--stall-grace-ms MS] [--region NAME] [--pid-dir DIR]\n"
        << "           [--ticks N]\n";
}

bool parse_u64(std::string_view text, std::uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    const std::string s(text);
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size()) {
        return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
}

// Hand-rolled parsing (DEC-0009 #5): no external CLI dependency.
int run_worker_command(int argc, char* argv[]) {
    safety_crit::workers::WorkerConfig cfg{};
    cfg.worker_idx = 0xFFFFFFFFu;  // --id is mandatory
    const char* region = safety_crit::workers::kDefaultRegionName;
    const char* pid_dir = safety_crit::workers::kDefaultPidDir;
    bool id_seen = false;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const auto next_value = [&](std::string_view& value) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "worker: missing value for " << arg << "\n";
                return false;
            }
            value = argv[++i];
            return true;
        };
        std::string_view value;
        if (arg == "--corrupt-hook") {
            // T-0027 (DEC-0012 #5): valueless opt-in flag for the SIGUSR2
            // corruption hook (test surface, never for production use).
            cfg.corrupt_hook = true;
        } else if (arg == "--id") {
            if (!next_value(value)) {
                return 2;
            }
            if (value == "a") {
                cfg.worker_idx = 0;
            } else if (value == "b") {
                cfg.worker_idx = 1;
            } else if (value == "c") {
                cfg.worker_idx = 2;
            } else {
                std::cerr << "worker: --id must be a, b, or c\n";
                return 2;
            }
            id_seen = true;
        } else if (arg == "--role") {
            if (!next_value(value)) {
                return 2;
            }
            if (value == "hot") {
                cfg.role = safety_crit::workers::WorkerRole::kHot;
            } else if (value == "standby") {
                cfg.role = safety_crit::workers::WorkerRole::kStandby;
            } else {
                std::cerr << "worker: --role must be hot or standby\n";
                return 2;
            }
        } else if (arg == "--logical-id") {
            if (!next_value(value)) {
                return 2;
            }
            if (value == "a") {
                cfg.logical_ring = 0;
            } else if (value == "b") {
                cfg.logical_ring = 1;
            } else if (value == "c") {
                cfg.logical_ring = 2;
            } else {
                std::cerr << "worker: --logical-id must be a, b, or c\n";
                return 2;
            }
        } else if (arg == "--generation") {
            std::uint64_t generation = 0;
            if (!next_value(value) || !parse_u64(value, generation) || generation == 0u ||
                generation > UINT32_MAX) {
                std::cerr << "worker: --generation requires a positive integer\n";
                return 2;
            }
            cfg.process_generation = static_cast<std::uint32_t>(generation);
        } else if (arg == "--ticks") {
            if (!next_value(value) || !parse_u64(value, cfg.ticks)) {
                std::cerr << "worker: --ticks requires a positive integer\n";
                return 2;
            }
        } else if (arg == "--tick-interval-ms") {
            std::uint64_t ms = 0;
            if (!next_value(value) || !parse_u64(value, ms) || ms == 0u ||
                ms > 3600000u) {
                std::cerr << "worker: --tick-interval-ms requires an integer (1..3600000)\n";
                return 2;
            }
            cfg.tick_interval = std::chrono::milliseconds(ms);
        } else if (arg == "--budget-us") {
            std::uint64_t us = 0;
            if (!next_value(value) || !parse_u64(value, us) || us == 0u) {
                std::cerr << "worker: --budget-us requires a positive integer\n";
                return 2;
            }
            cfg.cpu_budget = std::chrono::microseconds(us);
        } else if (arg == "--region") {
            if (!next_value(value) || value.size() < 2 || value[0] != '/') {
                std::cerr << "worker: --region requires an absolute shm name (e.g. "
                             "/safety_crit_region)\n";
                return 2;
            }
            region = argv[i];
        } else if (arg == "--pid-dir") {
            if (!next_value(value) || value.empty()) {
                std::cerr << "worker: --pid-dir requires a directory path\n";
                return 2;
            }
            pid_dir = argv[i];
        } else {
            std::cerr << "worker: unknown option: " << arg << "\n";
            return 2;
        }
    }

    if (!id_seen) {
        std::cerr << "worker: --id is required\n";
        return 2;
    }
    if (cfg.ticks == 0u) {
        cfg.ticks = 1000;  // documented default for demos
    }
    return safety_crit::workers::run_worker(cfg, region, pid_dir);
}

// Hand-rolled parsing (DEC-0009 #5 pattern): no external CLI dependency.
int run_monitor_command(int argc, char* argv[]) {
    safety_crit::monitors::MonitorConfig cfg{};
    const char* region = safety_crit::workers::kDefaultRegionName;
    const char* pid_dir = safety_crit::monitors::kDefaultPidDir;
    std::uint64_t polls = 0;  // 0: run until stopped

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const auto next_value = [&](std::string_view& value) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "monitor: missing value for " << arg << "\n";
                return false;
            }
            value = argv[++i];
            return true;
        };
        std::string_view value;
        if (arg == "--interval-ms") {
            std::uint64_t ms = 0;
            if (!next_value(value) || !parse_u64(value, ms) || ms == 0u || ms > 3600000u) {
                std::cerr << "monitor: --interval-ms requires an integer (1..3600000)\n";
                return 2;
            }
            cfg.poll_interval = std::chrono::milliseconds(ms);
        } else if (arg == "--stall-threshold-ms") {
            std::uint64_t ms = 0;
            if (!next_value(value) || !parse_u64(value, ms) || ms == 0u || ms > 3600000u) {
                std::cerr << "monitor: --stall-threshold-ms requires an integer (1..3600000)\n";
                return 2;
            }
            cfg.stall_threshold = std::chrono::milliseconds(ms);
        } else if (arg == "--polls") {
            if (!next_value(value) || !parse_u64(value, polls) || polls == 0u) {
                std::cerr << "monitor: --polls requires a positive integer\n";
                return 2;
            }
        } else if (arg == "--region") {
            if (!next_value(value) || value.size() < 2 || value[0] != '/') {
                std::cerr << "monitor: --region requires an absolute shm name (e.g. "
                             "/safety_crit_region)\n";
                return 2;
            }
            region = argv[i];
        } else if (arg == "--pid-dir") {
            if (!next_value(value) || value.empty()) {
                std::cerr << "monitor: --pid-dir requires a directory path\n";
                return 2;
            }
            pid_dir = argv[i];
        } else {
            std::cerr << "monitor: unknown option: " << arg << "\n";
            return 2;
        }
    }

    if (!safety_crit::monitors::validate_config(cfg)) {
        std::cerr << "monitor: --stall-threshold-ms must be >= --interval-ms\n";
        return 2;
    }
    return safety_crit::monitors::run_monitor(cfg, region, pid_dir, polls);
}

int run_supervisor_command(int argc, char* argv[]) {
    safety_crit::supervisor::SupervisorConfig cfg{};
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (i + 1 >= argc) {
            std::cerr << "supervisor: missing value for " << arg << "\n";
            return 2;
        }
        const std::string_view value(argv[++i]);
        std::uint64_t parsed = 0;
        if (arg == "--runtime-ms") {
            if (!parse_u64(value, parsed)) {
                return 2;
            }
            cfg.runtime_ms = parsed;
        } else if (arg == "--stall-grace-ms") {
            if (!parse_u64(value, parsed) || parsed > 3600000u) {
                std::cerr << "supervisor: --stall-grace-ms requires an integer (0..3600000)\n";
                return 2;
            }
            cfg.stall_grace_ms = parsed;
        } else if (arg == "--ticks") {
            if (!parse_u64(value, parsed) || parsed == 0) {
                return 2;
            }
            cfg.worker_ticks = parsed;
        } else if (arg == "--region" && value.size() >= 2 && value[0] == '/') {
            cfg.region_name = argv[i];
        } else if (arg == "--pid-dir" && !value.empty()) {
            cfg.pid_dir = argv[i];
        } else {
            std::cerr << "supervisor: unknown or invalid option: " << arg << "\n";
            return 2;
        }
    }
    return safety_crit::supervisor::run_supervisor(cfg);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "safety-critical-ha version 0.1.0\n";
        std::cout << "Compiler: " << SAFETY_CRIT_COMPILER_NAME << "\n";
        return 0;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "worker") {
        return run_worker_command(argc, argv);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "monitor") {
        return run_monitor_command(argc, argv);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "supervisor") {
        return run_supervisor_command(argc, argv);
    }
    print_usage(std::cerr);
    return 1;
}

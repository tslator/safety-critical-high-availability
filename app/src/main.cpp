#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "safety_crit/workers/worker_config.hpp"
#include "safety_crit/workers/worker_entry.hpp"

#ifndef SAFETY_CRIT_COMPILER_NAME
#define SAFETY_CRIT_COMPILER_NAME "Unknown Compiler"
#endif

namespace {

void print_usage(std::ostream& out) {
    out << "usage: safety-critical-ha --version\n"
        << "       safety-critical-ha worker --id <a|b|c> [--role hot|standby]\n"
        << "           [--ticks N] [--tick-interval-ms MS] [--budget-us US]\n"
        << "           [--region NAME]\n";
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
        if (arg == "--id") {
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
    return safety_crit::workers::run_worker(cfg, region);
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
    print_usage(std::cerr);
    return 1;
}

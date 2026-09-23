#include "safety_crit/workers/worker_config.hpp"

#include <cstdlib>
#include <string_view>

namespace safety_crit::workers {

bool validate_config(const WorkerConfig& config) {
    if (config.worker_idx >= shared_memory::kMaxWorkers) {
        return false;
    }
    if (config.logical_ring != shared_memory::kUnassignedPhysicalOwner &&
        config.logical_ring >= shared_memory::kMaxWorkers) {
        return false;
    }
    if (config.process_generation == 0u) {
        return false;
    }
    if (config.ticks == 0u) {
        return false;
    }
    if (config.tick_interval <= std::chrono::milliseconds::zero()) {
        return false;
    }
    if (config.cpu_budget <= std::chrono::microseconds::zero()) {
        return false;
    }
    return true;
}

bool corruption_hook_opt_in(bool cli_flag, const char* env_value) {
    return cli_flag || (env_value != nullptr && std::string_view(env_value) == "1");
}

}  // namespace safety_crit::workers

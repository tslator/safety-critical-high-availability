#include "safety_crit/monitors/monitor_config.hpp"

namespace safety_crit::monitors {

bool validate_config(const MonitorConfig& config) {
    if (config.poll_interval <= std::chrono::milliseconds::zero()) {
        return false;
    }
    if (config.stall_threshold <= std::chrono::milliseconds::zero()) {
        return false;
    }
    if (config.stall_threshold < config.poll_interval) {
        return false;
    }
    if (config.handoff_grace <= std::chrono::milliseconds::zero()) {
        return false;
    }
    return config.handoff_grace >= config.stall_threshold;
}

}  // namespace safety_crit::monitors

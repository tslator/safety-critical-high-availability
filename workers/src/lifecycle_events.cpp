#include "safety_crit/workers/lifecycle_events.hpp"

#include <cstdio>

namespace safety_crit::workers {

std::string worker_started_fields(WorkerRole role, bool ring_assigned, std::uint32_t ring) {
    char buf[64];
    if (role == WorkerRole::kHot) {
        const int n = std::snprintf(buf, sizeof(buf), "\"role\":\"hot\",\"ring\":%u", ring);
        return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
    }
    if (ring_assigned) {
        const int n = std::snprintf(buf, sizeof(buf), "\"role\":\"standby\",\"ring\":%u", ring);
        return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
    }
    return std::string{"\"role\":\"standby\""};
}

std::string worker_stopped_hot_fields(const char* reason, std::uint64_t ticks,
                                      std::uint64_t overruns) {
    char buf[128];
    const int n = std::snprintf(buf, sizeof(buf),
                                "\"role\":\"hot\",\"reason\":\"%s\",\"ticks\":%llu,"
                                "\"overruns\":%llu",
                                reason, static_cast<unsigned long long>(ticks),
                                static_cast<unsigned long long>(overruns));
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

std::string worker_stopped_standby_fields(std::uint64_t polls) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf),
                                "\"role\":\"standby\",\"reason\":\"stop_signal\",\"polls\":%llu",
                                static_cast<unsigned long long>(polls));
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

std::string worker_deadline_overrun_fields(std::uint64_t overruns) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "\"role\":\"hot\",\"overruns\":%llu",
                                static_cast<unsigned long long>(overruns));
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

}  // namespace safety_crit::workers

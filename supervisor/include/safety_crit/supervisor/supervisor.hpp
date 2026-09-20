#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace safety_crit::supervisor {

enum class MonitorLineKind : std::uint8_t {
    kAlert,
    kReport,
};

struct MonitorLine {
    MonitorLineKind kind{MonitorLineKind::kAlert};
    std::int64_t timestamp_ns{0};
    std::uint32_t worker{0};
    std::string_view event{};
};

// Validates one complete newline-delimited monitor record. Unknown fields,
// unknown events, bad workers, and truncated records are rejected.
bool parse_monitor_line(std::string_view line, MonitorLine& out);

struct SupervisorConfig {
    const char* region_name{"/safety_crit_region"};
    std::filesystem::path pid_dir{"/run/safety-critical-ha"};
    std::uint64_t worker_ticks{1000};
    std::uint64_t runtime_ms{0};  // zero means run until SIGTERM or child exit
};

enum class SupervisorState : std::uint8_t {
    kIdle,
    kLaunching,
    kRunning,
    kFailoverDetected,
    kRecovering,
    kDegraded,
    kFailsafe,
};

// Creates/verifies shared memory, launches monitor plus A/B hot and C standby,
// forwards validated monitor alerts to stdout, then terminates and reaps every
// child. Returns zero after an orderly shutdown, nonzero on setup failure.
int run_supervisor(const SupervisorConfig& config);

}  // namespace safety_crit::supervisor

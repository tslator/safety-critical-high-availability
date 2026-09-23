#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>

#include "safety_crit/shared_memory/shared_region.hpp"

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

struct OutputWitness {
    std::uint64_t next_sequence{0};
    std::uint64_t last_epoch{0};
    std::uint64_t records{0};
    std::uint64_t corruptions{0};
    bool first_post_failover{false};
};

// Validates one complete newline-delimited monitor record. Unknown fields,
// unknown events, bad workers, and truncated records are rejected.
bool parse_monitor_line(std::string_view line, MonitorLine& out);

// Drains committed records outside the ring hot path. The transport position
// is the authoritative sequence witness; corruption skips are counted and
// tolerated (T-0027, sequence continuity carries across the skip), while
// ownership read failures and real sequence gaps are rejected.
bool drain_output_witness(shared_memory::SharedRegion& region, std::size_t logical_ring,
                          OutputWitness& witness);

struct SupervisorConfig {
    const char* region_name{"/safety_crit_region"};
    std::filesystem::path pid_dir{"/run/safety-critical-ha"};
    // Hot workers run until SIGTERM/SIGINT by default (T-0021: supervisor is
    // a long-lived Compose service). Tests set an explicit bounded budget.
    std::uint64_t worker_ticks{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t runtime_ms{0};  // zero means run until SIGTERM or child exit
    // T-0025 (DEC-0012 #3): grace between the bounded SIGCONT and SIGKILL
    // escalation for a stalled ring, exposed via --stall-grace-ms.
    std::uint64_t stall_grace_ms{200};
    // T-0032: bounded window after a reap-observed crash during which the
    // promoted owner is expected to take its ring over. Monitor stall alerts
    // arriving in this window describe the handoff, not a wedged worker, and
    // must not arm the SIGCONT/SIGKILL path (the grace also bounds the window
    // in the unlikely case the first post-failover record never arrives).
    std::uint64_t handoff_grace_ms{750};
};

enum class SupervisorState : std::uint8_t {
    kIdle,
    kLaunching,
    kRunning,
    kStalledRecovering,
    kFailoverDetected,
    kRecovering,
    kDegraded,
    kFailsafe,
};

// T-0025 (DEC-0012 #3): per-logical-ring stall recovery state machine. On a
// worker_stalled alert the supervisor issues exactly one bounded SIGCONT
// (begin() returning true) and then watches the ring tail each loop
// (observe()): tail change means recovered, grace expiry means escalate to
// SIGKILL through the existing crash-recovery path. A second alert on an
// already-recovering ring is a no-op and never resets the grace timer.
// Outside the ring hot path; time is injected so unit tests are
// deterministic.
enum class StallRecoveryOutcome : std::uint8_t {
    kNone,
    kRecovered,
    kEscalate,
};

struct StallRecoveryEvent {
    StallRecoveryOutcome outcome{StallRecoveryOutcome::kNone};
    std::uint32_t physical_worker{0};
    std::uint64_t epoch{0};
};

class StallRecoveryTracker {
public:
    using time_point = std::chrono::steady_clock::time_point;

    explicit StallRecoveryTracker(std::chrono::milliseconds grace_period);

    // Starts recovery for one physical worker. Returns true when recovery
    // begins (the caller must send one SIGCONT); false is a no-op because a
    // stall episode for this worker is already in flight.
    bool begin(std::uint32_t physical_worker, std::uint64_t epoch, std::uint64_t tail,
               time_point now);

    // One loop observation of the ring tail. kRecovered and kEscalate are
    // terminal: they clear the slot and re-arm the ring for a future
    // episode.
    StallRecoveryEvent observe(std::uint32_t physical_worker, std::uint64_t tail,
                               time_point now);

    bool recovering(std::uint32_t physical_worker) const;

private:
    struct Slot {
        bool active{false};
        std::uint64_t epoch{0};
        std::uint64_t baseline_tail{0};
        time_point started_at{};
    };

    std::chrono::milliseconds grace_period_{0};
    std::array<Slot, shared_memory::kMaxWorkers> slots_{};
};

// Creates/verifies shared memory, launches monitor plus A/B hot and C standby,
// forwards validated monitor alerts to stdout, then terminates and reaps every
// child. Returns zero after an orderly shutdown, nonzero on setup failure.
int run_supervisor(const SupervisorConfig& config);

}  // namespace safety_crit::supervisor

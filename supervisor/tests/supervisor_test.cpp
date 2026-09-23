#include "test_framework.hpp"

#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#include <thread>

#include "safety_crit/supervisor/supervisor.hpp"
#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/workers/workload.hpp"

using safety_crit::supervisor::MonitorLine;
using safety_crit::supervisor::MonitorLineKind;
using safety_crit::supervisor::parse_monitor_line;
using safety_crit::supervisor::StallRecoveryEvent;
using safety_crit::supervisor::StallRecoveryOutcome;
using safety_crit::supervisor::StallRecoveryTracker;

namespace {

using clock_type = std::chrono::steady_clock;

// Reads the captured supervisor stdout pipe until `needle` appears or the
// timeout expires. Returns false on EOF before the needle is found.
bool wait_for_output(int fd, std::string& buffer, std::string_view needle,
                     std::chrono::milliseconds timeout) {
    const auto deadline = clock_type::now() + timeout;
    while (buffer.find(needle) == std::string::npos) {
        struct pollfd descriptor{fd, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 20);
        if (ready > 0) {
            char chunk[512];
            const ssize_t count = ::read(fd, chunk, sizeof(chunk));
            if (count > 0) {
                buffer.append(chunk, static_cast<std::size_t>(count));
            } else if (count == 0) {
                break;  // EOF
            }
        }
        if (clock_type::now() >= deadline) {
            break;
        }
    }
    return buffer.find(needle) != std::string::npos;
}

// Drains a captured stdout pipe to EOF (safe once the writer process has
// been reaped) and returns everything read.
std::string drain_pipe(int fd) {
    std::string out;
    for (;;) {
        char chunk[512];
        const ssize_t count = ::read(fd, chunk, sizeof(chunk));
        if (count > 0) {
            out.append(chunk, static_cast<std::size_t>(count));
        } else {
            break;
        }
    }
    return out;
}

// Waits for the worker pidfile and returns the recorded pid (0 on timeout).
long worker_pid(const std::filesystem::path& pid_dir, const char* name,
                std::chrono::milliseconds timeout) {
    const auto deadline = clock_type::now() + timeout;
    while (clock_type::now() < deadline) {
        std::error_code ec;
        const auto pid_path = pid_dir / name;
        if (std::filesystem::exists(pid_path, ec)) {
            std::ifstream input(pid_path);
            long pid = 0;
            input >> pid;
            if (pid > 0) {
                return pid;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return 0;
}

}  // namespace

SAFETY_CRIT_TEST_CASE(Supervisor, StallRecoveryRecoversOnTailAdvance) {
    StallRecoveryTracker tracker{std::chrono::milliseconds(200)};
    const auto t0 = clock_type::now();
    SAFETY_CRIT_ASSERT(tracker.recovering(0u) == false);
    SAFETY_CRIT_ASSERT(tracker.begin(0u, 3u, 100u, t0));  // caller issues one SIGCONT
    SAFETY_CRIT_ASSERT(tracker.recovering(0u));

    StallRecoveryEvent event = tracker.observe(0u, 100u, t0 + std::chrono::milliseconds(50));
    SAFETY_CRIT_ASSERT(event.outcome == StallRecoveryOutcome::kNone);

    // Tail advanced after the (hypothetical) SIGCONT: recovery completes.
    event = tracker.observe(0u, 101u, t0 + std::chrono::milliseconds(90));
    SAFETY_CRIT_ASSERT(event.outcome == StallRecoveryOutcome::kRecovered);
    SAFETY_CRIT_ASSERT(event.physical_worker == 0u);
    SAFETY_CRIT_ASSERT(event.epoch == 3u);
    SAFETY_CRIT_ASSERT(tracker.recovering(0u) == false);

    event = tracker.observe(0u, 101u, t0 + std::chrono::milliseconds(300));
    SAFETY_CRIT_ASSERT(event.outcome == StallRecoveryOutcome::kNone);
}

SAFETY_CRIT_TEST_CASE(Supervisor, StallRecoveryEscalatesAfterGracePeriod) {
    StallRecoveryTracker tracker{std::chrono::milliseconds(200)};
    const auto t0 = clock_type::now();
    SAFETY_CRIT_ASSERT(tracker.begin(0u, 5u, 77u, t0));

    StallRecoveryEvent event = tracker.observe(0u, 77u, t0 + std::chrono::milliseconds(200));
    SAFETY_CRIT_ASSERT(event.outcome == StallRecoveryOutcome::kNone);  // not strictly past grace

    event = tracker.observe(0u, 77u, t0 + std::chrono::milliseconds(201));
    SAFETY_CRIT_ASSERT(event.outcome == StallRecoveryOutcome::kEscalate);  // caller SIGKILLs
    SAFETY_CRIT_ASSERT(event.physical_worker == 0u);
    SAFETY_CRIT_ASSERT(event.epoch == 5u);
    SAFETY_CRIT_ASSERT(tracker.recovering(0u) == false);

    event = tracker.observe(0u, 77u, t0 + std::chrono::milliseconds(400));
    SAFETY_CRIT_ASSERT(event.outcome == StallRecoveryOutcome::kNone);
}

SAFETY_CRIT_TEST_CASE(Supervisor, StallRecoveryIdempotentPerEpoch) {
    StallRecoveryTracker tracker{std::chrono::milliseconds(200)};
    const auto t0 = clock_type::now();
    SAFETY_CRIT_ASSERT(tracker.begin(0u, 4u, 10u, t0));
    // A second stall alert on an already-recovering ring is a no-op and does
    // not reset the grace timer: escalation still fires off the original t0.
    SAFETY_CRIT_ASSERT(tracker.begin(0u, 4u, 10u, t0 + std::chrono::milliseconds(150)) == false);
    // Tracking is per logical ring: a different physical worker is independent.
    SAFETY_CRIT_ASSERT(tracker.begin(1u, 4u, 20u, t0));
    SAFETY_CRIT_ASSERT(tracker.begin(1u, 4u, 20u, t0 + std::chrono::milliseconds(10)) == false);

    StallRecoveryEvent event = tracker.observe(0u, 10u, t0 + std::chrono::milliseconds(201));
    SAFETY_CRIT_ASSERT(event.outcome == StallRecoveryOutcome::kEscalate);
    SAFETY_CRIT_ASSERT(event.epoch == 4u);
    // The other ring is still recovering on its own timer.
    SAFETY_CRIT_ASSERT(tracker.recovering(1u));

    // A terminal outcome re-arms the ring for a future episode (new epoch).
    SAFETY_CRIT_ASSERT(tracker.begin(0u, 5u, 10u, t0 + std::chrono::milliseconds(250)));
}

SAFETY_CRIT_TEST_CASE(Supervisor, StallRecoveryGracePeriodIsConfigurable) {
    StallRecoveryTracker fast{std::chrono::milliseconds(20)};
    StallRecoveryTracker slow{std::chrono::milliseconds(500)};
    const auto t0 = clock_type::now();
    SAFETY_CRIT_ASSERT(fast.begin(0u, 1u, 0u, t0));
    SAFETY_CRIT_ASSERT(slow.begin(0u, 1u, 0u, t0));

    SAFETY_CRIT_ASSERT(fast.observe(0u, 0u, t0 + std::chrono::milliseconds(50)).outcome ==
                       StallRecoveryOutcome::kEscalate);
    SAFETY_CRIT_ASSERT(slow.observe(0u, 0u, t0 + std::chrono::milliseconds(50)).outcome ==
                       StallRecoveryOutcome::kNone);
}

SAFETY_CRIT_TEST_CASE(Supervisor, RecoversStalledWorkerWithBoundedSigcont) {
    const std::string region_name = "/sc_t0025r_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0025r_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    int fds[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(fds) == 0);
    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 2500;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }
    ::close(fds[1]);

    const long worker = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                   std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(worker > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(worker), SIGSTOP) == 0);

    std::string output;
    SAFETY_CRIT_ASSERT(wait_for_output(fds[0], output, "worker_stalled",
                                       std::chrono::milliseconds(1000)));
    SAFETY_CRIT_ASSERT(wait_for_output(fds[0], output,
                                       "supervisor: stall recovered for physical 0 at epoch",
                                       std::chrono::milliseconds(1000)));

    (void)::kill(static_cast<pid_t>(worker), SIGCONT);
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);
    ::close(fds[0]);
    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

SAFETY_CRIT_TEST_CASE(Supervisor, EscalatesStalledWorkerToCrashRecovery) {
    const std::string region_name = "/sc_t0025e_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0025e_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    int fds[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(fds) == 0);
    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 2500;
        config.stall_grace_ms = 20;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }
    ::close(fds[1]);

    const long worker = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                   std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(worker > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(worker), SIGSTOP) == 0);

    // Keep the worker stopped against the supervisor's bounded SIGCONT so the
    // ring tail can never advance, forcing the escalation to SIGKILL.
    std::string output;
    bool escalated = false;
    const auto deadline = clock_type::now() + std::chrono::milliseconds(1500);
    while (!escalated && clock_type::now() < deadline) {
        (void)::kill(static_cast<pid_t>(worker), SIGSTOP);
        escalated = wait_for_output(fds[0], output,
                                    "supervisor: stall escalation for physical 0 at epoch",
                                    std::chrono::milliseconds(5));
    }
    SAFETY_CRIT_ASSERT(escalated);

    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);
    ::close(fds[0]);

    // The SIGKILL escalation went through the existing crash-recovery path:
    // standby (physical 2) owns logical ring 0.
    auto region = safety_crit::shared_memory::SharedRegionHandle::create_or_open(
        region_name.c_str());
    SAFETY_CRIT_ASSERT(region.ok());
    safety_crit::shared_memory::OwnershipToken ownership;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*region.get(), 0u, ownership));
    SAFETY_CRIT_ASSERT(ownership.physical_owner == 2u);
    region.detach();
    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

SAFETY_CRIT_TEST_CASE(Supervisor, DegradedStatusFlagReservedAndDistinct) {
    using safety_crit::shared_memory::WorkerStatusFlag;
    constexpr std::uint64_t degraded = safety_crit::shared_memory::to_bits(WorkerStatusFlag::kDegraded);
    SAFETY_CRIT_ASSERT(degraded == (1ULL << 5));
    SAFETY_CRIT_ASSERT((degraded & safety_crit::shared_memory::to_bits(WorkerStatusFlag::kRunning)) == 0u);
    SAFETY_CRIT_ASSERT((degraded & safety_crit::shared_memory::to_bits(WorkerStatusFlag::kIdle)) == 0u);
    SAFETY_CRIT_ASSERT((degraded & safety_crit::shared_memory::to_bits(WorkerStatusFlag::kCrashed)) == 0u);
    SAFETY_CRIT_ASSERT((degraded & safety_crit::shared_memory::to_bits(WorkerStatusFlag::kRecovering)) == 0u);
    SAFETY_CRIT_ASSERT((degraded & safety_crit::shared_memory::to_bits(WorkerStatusFlag::kOverrun)) == 0u);
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::has_flag(degraded, WorkerStatusFlag::kDegraded));
}

SAFETY_CRIT_TEST_CASE(Supervisor, PromotesStandbyOnPhysicalBCrash) {
    const std::string region_name = "/sc_t0026b_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0026b_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    int fds[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(fds) == 0);
    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 700;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }
    ::close(fds[1]);

    const long worker = worker_pid(pid_dir, "safety_crit_worker_1.pid",
                                   std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(worker > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(worker), SIGKILL) == 0);

    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);  // single crash: recoverable, not degraded
    drain_pipe(fds[0]);
    ::close(fds[0]);

    auto region = safety_crit::shared_memory::SharedRegionHandle::create_or_open(
        region_name.c_str());
    SAFETY_CRIT_ASSERT(region.ok());
    safety_crit::shared_memory::OwnershipToken ownership;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*region.get(), 1u, ownership));
    SAFETY_CRIT_ASSERT(ownership.physical_owner == 2u);  // C promotes to logical ring B
    SAFETY_CRIT_ASSERT(!safety_crit::shared_memory::load_has_flag(
        region.get()->worker_status[1].status,
        safety_crit::shared_memory::WorkerStatusFlag::kDegraded));
    region.detach();
    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

SAFETY_CRIT_TEST_CASE(Supervisor, DegradesSecondRingOnSimultaneousCrash) {
    const std::string region_name = "/sc_t0026d_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0026d_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    int fds[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(fds) == 0);
    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 700;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }
    ::close(fds[1]);

    const long hot_a = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                  std::chrono::milliseconds(1500));
    const long hot_b = worker_pid(pid_dir, "safety_crit_worker_1.pid",
                                  std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(hot_a > 0);
    SAFETY_CRIT_ASSERT(hot_b > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Double fault: no gap for the supervisor to react between the crashes.
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(hot_a), SIGKILL) == 0);
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(hot_b), SIGKILL) == 0);

    // While degraded, both replacement physical processes run as standby.
    bool replacements_live = false;
    const auto deadline = clock_type::now() + std::chrono::milliseconds(600);
    while (clock_type::now() < deadline) {
        const long replacement_a = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                              std::chrono::milliseconds(50));
        const long replacement_b = worker_pid(pid_dir, "safety_crit_worker_1.pid",
                                              std::chrono::milliseconds(50));
        if (replacement_a > 0 && replacement_a != hot_a && replacement_b > 0 &&
            replacement_b != hot_b && ::kill(static_cast<pid_t>(replacement_a), 0) == 0 &&
            ::kill(static_cast<pid_t>(replacement_b), 0) == 0) {
            replacements_live = true;
            break;
        }
    }
    SAFETY_CRIT_ASSERT(replacements_live);

    // DEGRADED is asserted while the supervisor lives: it re-asserts the
    // status bit each loop, so a standby restart cannot erase it.
    bool degraded_bit_visible = false;
    {
        auto live = safety_crit::shared_memory::SharedRegionHandle::create_or_open(
            region_name.c_str());
        SAFETY_CRIT_ASSERT(live.ok());
        const auto bit_deadline = clock_type::now() + std::chrono::milliseconds(300);
        while (clock_type::now() < bit_deadline && !degraded_bit_visible) {
            degraded_bit_visible = safety_crit::shared_memory::load_has_flag(
                live.get()->worker_status[1].status,
                safety_crit::shared_memory::WorkerStatusFlag::kDegraded);
            if (!degraded_bit_visible) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        live.detach();
    }
    SAFETY_CRIT_ASSERT(degraded_bit_visible);

    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 4);  // DEGRADED shutdown
    std::string output = drain_pipe(fds[0]);
    ::close(fds[0]);

    // Exactly one stdout degradation event for logical ring 1.
    std::size_t degraded_events = 0;
    std::size_t pos = 0;
    while ((pos = output.find("supervisor: logical ring 1 degraded (reason=", pos)) !=
           std::string::npos) {
        ++degraded_events;
        ++pos;
    }
    SAFETY_CRIT_ASSERT(degraded_events == 1u);
    SAFETY_CRIT_ASSERT(output.find("logical ring 0 degraded") == std::string::npos);

    auto region = safety_crit::shared_memory::SharedRegionHandle::create_or_open(
        region_name.c_str());
    SAFETY_CRIT_ASSERT(region.ok());
    safety_crit::shared_memory::OwnershipToken ownership;
    // Deterministic tie-break: C promotes the lowest logical-ring index...
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*region.get(), 0u, ownership));
    SAFETY_CRIT_ASSERT(ownership.physical_owner == 2u);
    // ...and the other ring keeps its dead owner (DEGRADED bit asserted
    // above, during the supervisor's lifetime).
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*region.get(), 1u, ownership));
    SAFETY_CRIT_ASSERT(ownership.physical_owner == 1u);
    region.detach();
    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

SAFETY_CRIT_TEST_CASE(Supervisor, CrashRecoveryIdempotentForRepeatingKill) {
    const std::string region_name = "/sc_t0026i_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0026i_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 700;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }

    const long hot_a = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                  std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(hot_a > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // A second SIGKILL on an already-crashed (handled) worker must not
    // trigger a second promotion or drive the supervisor into failsafe.
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(hot_a), SIGKILL) == 0);
    (void)::kill(static_cast<pid_t>(hot_a), SIGKILL);

    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);

    auto region = safety_crit::shared_memory::SharedRegionHandle::create_or_open(
        region_name.c_str());
    SAFETY_CRIT_ASSERT(region.ok());
    safety_crit::shared_memory::OwnershipToken ownership;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*region.get(), 0u, ownership));
    SAFETY_CRIT_ASSERT(ownership.physical_owner == 2u);
    region.detach();
    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

SAFETY_CRIT_TEST_CASE(Supervisor, ParsesPublishedAlert) {
    MonitorLine line;
    SAFETY_CRIT_ASSERT(parse_monitor_line(
        R"({"ts":42,"level":"error","component":"monitor","event":"worker_crashed","worker":1})",
        line));
    SAFETY_CRIT_ASSERT(line.kind == MonitorLineKind::kAlert);
    SAFETY_CRIT_ASSERT(line.timestamp_ns == 42);
    SAFETY_CRIT_ASSERT(line.worker == 1);
    SAFETY_CRIT_ASSERT(line.event == "worker_crashed");
}

SAFETY_CRIT_TEST_CASE(Supervisor, RejectsMalformedUnknownAndPartialInput) {
    MonitorLine line;
    SAFETY_CRIT_ASSERT(!parse_monitor_line("{\"ts\":42", line));
    SAFETY_CRIT_ASSERT(!parse_monitor_line(
        R"({"ts":42,"level":"info","component":"monitor","event":"unknown","worker":1})",
        line));
    SAFETY_CRIT_ASSERT(!parse_monitor_line(
        R"({"ts":42,"level":"info","component":"other","event":"worker_idle","worker":1})",
        line));
    SAFETY_CRIT_ASSERT(!parse_monitor_line(
        R"({"ts":42,"level":"info","component":"monitor","event":"worker_idle","worker":4})",
        line));
}

SAFETY_CRIT_TEST_CASE(Supervisor, AcceptsShutdownReport) {
    MonitorLine line;
    SAFETY_CRIT_ASSERT(parse_monitor_line(
        R"({"ts":42,"level":"info","component":"monitor","event":"monitor_report","polls":1,"alerts":{},"workers":[]})",
        line));
    SAFETY_CRIT_ASSERT(line.kind == MonitorLineKind::kReport);
}

SAFETY_CRIT_TEST_CASE(Supervisor, DrainsContinuousCrcCheckedOutput) {
    safety_crit::shared_memory::SharedRegion region{};
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::initialize(region));
    safety_crit::workers::ProcessedData first{};
    first.worker_idx = 0u;
    first.tick = 4u;
    safety_crit::workers::ProcessedData second = first;
    second.tick = 5u;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::push(region, 0u, first));
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::push(region, 0u, second));
    safety_crit::supervisor::OutputWitness witness;
    SAFETY_CRIT_ASSERT(safety_crit::supervisor::drain_output_witness(region, 0u, witness));
    SAFETY_CRIT_ASSERT(witness.records == 2u);
    SAFETY_CRIT_ASSERT(witness.next_sequence == 2u);
    SAFETY_CRIT_ASSERT(witness.corruptions == 0u);
    SAFETY_CRIT_ASSERT(!witness.first_post_failover);
    SAFETY_CRIT_ASSERT(safety_crit::supervisor::drain_output_witness(region, 0u, witness));
}

SAFETY_CRIT_TEST_CASE(Supervisor, WitnessCountsCorruptionSkipsAcrossSequence) {
    safety_crit::shared_memory::SharedRegion region{};
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::initialize(region));
    safety_crit::workers::ProcessedData first{};
    first.worker_idx = 0u;
    first.tick = 4u;
    safety_crit::workers::ProcessedData poisoned = first;
    poisoned.tick = 5u;
    safety_crit::workers::ProcessedData third = first;
    third.tick = 6u;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::push(region, 0u, first));
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::push_with_bad_crc(region, 0u, poisoned));
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::push(region, 0u, third));

    safety_crit::supervisor::OutputWitness witness;
    SAFETY_CRIT_ASSERT(safety_crit::supervisor::drain_output_witness(region, 0u, witness));
    SAFETY_CRIT_ASSERT(witness.records == 2u);
    SAFETY_CRIT_ASSERT(witness.corruptions == 1u);
    // Sequence continuity carries across the skip: 0 delivered, 1 skipped,
    // 2 delivered -> next expected is 3.
    SAFETY_CRIT_ASSERT(witness.next_sequence == 3u);
}

SAFETY_CRIT_TEST_CASE(Supervisor, LaunchesAndReapsTopology) {
    const std::string region_name = "/sc_t0017_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0017_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    safety_crit::supervisor::SupervisorConfig config;
    config.region_name = region_name.c_str();
    config.pid_dir = pid_dir;
    config.worker_ticks = 1000;
    config.runtime_ms = 150;
    SAFETY_CRIT_ASSERT(safety_crit::supervisor::run_supervisor(config) == 0);

    SAFETY_CRIT_ASSERT(!std::filesystem::exists(
        pid_dir / "safety_crit_worker_0.pid"));
    SAFETY_CRIT_ASSERT(!std::filesystem::exists(
        pid_dir / "safety_crit_worker_1.pid"));
    SAFETY_CRIT_ASSERT(!std::filesystem::exists(
        pid_dir / "safety_crit_worker_2.pid"));
    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

SAFETY_CRIT_TEST_CASE(Supervisor, PromotesStandbyAndStartsReplacement) {
    const std::string region_name = "/sc_t0018_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0018_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 1000;
        config.runtime_ms = 700;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }

    const auto crash_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    bool crashed = false;
    while (std::chrono::steady_clock::now() < crash_deadline) {
        std::error_code ec;
        const auto pid_path = pid_dir / "safety_crit_worker_0.pid";
        if (std::filesystem::exists(pid_path, ec)) {
            std::ifstream input(pid_path);
            long worker_pid = 0;
            input >> worker_pid;
            if (worker_pid > 0 && ::kill(static_cast<pid_t>(worker_pid), SIGUSR1) == 0) {
                crashed = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    SAFETY_CRIT_ASSERT(crashed);
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);

    auto region = safety_crit::shared_memory::SharedRegionHandle::create_or_open(region_name.c_str());
    SAFETY_CRIT_ASSERT(region.ok());
    safety_crit::shared_memory::OwnershipToken ownership;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*region.get(), 0, ownership));
    SAFETY_CRIT_ASSERT(ownership.physical_owner == 2u);
    SAFETY_CRIT_ASSERT(ownership.process_generation == 1u);
    region.detach();
    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

SAFETY_CRIT_TEST_CASE(Supervisor, CorruptedPushObservedInOutputWitness) {
    const std::string region_name = "/sc_t0027c_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0027c_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    int fds[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(fds) == 0);
    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        ::setenv("SAFETY_CRIT_CORRUPT_HOOK", "1", 1);
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 1200;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }
    ::close(fds[1]);

    const long worker = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                   std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(worker > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Poison exactly one push (SIGUSR2 handler sets the flag once).
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(worker), SIGUSR2) == 0);

    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);  // no CRC assertion failure
    std::string output = drain_pipe(fds[0]);
    ::close(fds[0]);

    const std::size_t at = output.find("a_corruptions=");
    SAFETY_CRIT_ASSERT(at != std::string::npos);
    unsigned long long corruptions = 0;
    SAFETY_CRIT_ASSERT(sscanf(output.c_str() + at, "a_corruptions=%llu", &corruptions) == 1);
    SAFETY_CRIT_ASSERT(corruptions >= 1ull);
    // Normal operation resumed: records kept flowing to the witness.
    const std::size_t rec_at = output.find("a_records=");
    SAFETY_CRIT_ASSERT(rec_at != std::string::npos);
    unsigned long long records = 0;
    SAFETY_CRIT_ASSERT(sscanf(output.c_str() + rec_at, "a_records=%llu", &records) == 1);
    SAFETY_CRIT_ASSERT(records >= 50ull);

    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

// T-0032 regression (hosted CI run 35865376613). Crashing the hot worker used
// to leave the supervisor in kRunning for the whole failover handoff: the
// monitor never reports the crash (the replacement process stores its own IDLE
// status word into the crashed slot before the monitor's next poll, masking
// the crash edge), so nothing advanced the state, and a monitor stall alert --
// armed on the promoted owner before it could commit anything on its new ring
// -- drove the bounded SIGCONT path. Depending on whether the first
// post-failover commit landed before that 100 ms window expired, the run
// either logged a spurious "stall recovered" (breaking the S1-R determinism
// contract) or escalated to SIGKILL against the worker recovering the ring.
SAFETY_CRIT_TEST_CASE(Supervisor, CrashRecordsFailoverWithoutStallArtifacts) {
    const std::string region_name = "/sc_t0032c_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0032c_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    int fds[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(fds) == 0);
    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 2500;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }
    ::close(fds[1]);

    const long hot = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(hot > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // SIGKILL (the perturb harness' fallback, DEC-0012 #1): the status word
    // stays RUNNING and the slot's pidfile is recycled by the replacement, so
    // this is the hardest case for the monitor's slot attribution.
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(hot), SIGKILL) == 0);

    std::string output;
    SAFETY_CRIT_ASSERT(wait_for_output(fds[0], output, "first post-failover record observed",
                                       std::chrono::milliseconds(2000)));

    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);
    output += drain_pipe(fds[0]);
    ::close(fds[0]);

    // Failover is recorded from reap, not from the (masked) monitor alert.
    SAFETY_CRIT_ASSERT(output.find("supervisor: shutdown state=4") != std::string::npos);
    // No stall artifacts of any kind during the handoff.
    SAFETY_CRIT_ASSERT(output.find("stall recovered") == std::string::npos);
    SAFETY_CRIT_ASSERT(output.find("stall escalation") == std::string::npos);
    SAFETY_CRIT_ASSERT(output.find("\"event\":\"worker_stalled\"") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

// T-0032: the handoff window is a window, not a permanent stall exemption.
// With the window bounded to zero, a stall injected on the promoted owner
// after it has committed still arms the bounded SIGCONT recovery path, i.e.
// kFailoverDetected is admitted by the stall handler once the handoff is over.
SAFETY_CRIT_TEST_CASE(Supervisor, StallAfterHandoffWindowStillRecovers) {
    const std::string region_name = "/sc_t0032s_" + std::to_string(static_cast<long>(::getpid()));
    const auto pid_dir = std::filesystem::temp_directory_path() /
                         ("sc_t0032s_pid_" + std::to_string(static_cast<long>(::getpid())));
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());

    int fds[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(fds) == 0);
    const pid_t supervisor = ::fork();
    SAFETY_CRIT_ASSERT(supervisor >= 0);
    if (supervisor == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        safety_crit::supervisor::SupervisorConfig config;
        config.region_name = region_name.c_str();
        config.pid_dir = pid_dir;
        config.worker_ticks = 100000;
        config.runtime_ms = 4000;
        config.handoff_grace_ms = 0;
        ::_exit(safety_crit::supervisor::run_supervisor(config));
    }
    ::close(fds[1]);

    const long hot = worker_pid(pid_dir, "safety_crit_worker_0.pid",
                                std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(hot > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // SIGKILL (the perturb harness' fallback, DEC-0012 #1): the status word
    // stays RUNNING and the slot's pidfile is recycled by the replacement, so
    // this is the hardest case for the monitor's slot attribution.
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(hot), SIGKILL) == 0);

    std::string output;
    SAFETY_CRIT_ASSERT(wait_for_output(fds[0], output, "first post-failover record observed",
                                       std::chrono::milliseconds(2000)));

    // Freeze the promoted owner (physical 2): its ring tail stops, the monitor
    // reports a real stall, and the supervisor recovers it with SIGCONT.
    const long promoted = worker_pid(pid_dir, "safety_crit_worker_2.pid",
                                     std::chrono::milliseconds(1500));
    SAFETY_CRIT_ASSERT(promoted > 0);
    SAFETY_CRIT_ASSERT(::kill(static_cast<pid_t>(promoted), SIGSTOP) == 0);
    SAFETY_CRIT_ASSERT(wait_for_output(fds[0], output,
                                       "supervisor: stall recovered for physical 2 at epoch",
                                       std::chrono::milliseconds(2000)));

    (void)::kill(static_cast<pid_t>(promoted), SIGCONT);
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(supervisor, &status, 0) == supervisor);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    (void)::kill(static_cast<pid_t>(promoted), SIGCONT);
    ::close(fds[0]);

    std::error_code ec;
    std::filesystem::remove_all(pid_dir, ec);
    (void)safety_crit::shared_memory::SharedRegionHandle::destroy(region_name.c_str());
}

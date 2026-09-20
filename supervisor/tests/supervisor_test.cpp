#include "test_framework.hpp"

#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <thread>

#include "safety_crit/supervisor/supervisor.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/workers/workload.hpp"

using safety_crit::supervisor::MonitorLine;
using safety_crit::supervisor::MonitorLineKind;
using safety_crit::supervisor::parse_monitor_line;

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

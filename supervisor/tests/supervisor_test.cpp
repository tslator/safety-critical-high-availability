#include "test_framework.hpp"

#include <string>
#include <filesystem>
#include <unistd.h>

#include "safety_crit/supervisor/supervisor.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"

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

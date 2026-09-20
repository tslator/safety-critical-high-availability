// Phase 3, T3.3 (T-0014): fork-based monitor integration tests -- real
// worker processes, real pidfile liveness, and a forked monitor child whose
// stdout JSON lines are captured and asserted. Plain build only under
// sanitizers (fork is unreliable there; G1.4/G2.3 precedent) -- the
// in-process loop suite carries the sanitizer signal.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // fork / waitpid feature-test macro
#endif

#include "test_framework.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "safety_crit/monitors/monitor_config.hpp"
#include "safety_crit/monitors/monitor_entry.hpp"
#include "safety_crit/monitors/pidfile_liveness.hpp"
#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/workers/pidfile.hpp"
#include "safety_crit/workers/worker_config.hpp"
#include "safety_crit/workers/worker_entry.hpp"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAFETY_CRIT_P3_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SAFETY_CRIT_P3_SANITIZED 1
#endif
#endif
#ifndef SAFETY_CRIT_P3_SANITIZED
#define SAFETY_CRIT_P3_SANITIZED 0
#endif

namespace {
#if SAFETY_CRIT_P3_SANITIZED

// Forked children of a sanitized process hit runtime-fatal errors; the
// cross-process scenarios run in the plain build. Sanitizer coverage of the
// monitor logic comes from monitors_health_test / monitors_loop_test.
SAFETY_CRIT_TEST_CASE(monitors_integration, SkippedUnderSanitizers) {
    SAFETY_CRIT_ASSERT(true);
}

#else

using safety_crit::monitors::MonitorConfig;
using safety_crit::shared_memory::SharedRegion;
using safety_crit::shared_memory::SharedRegionHandle;
using safety_crit::shared_memory::WorkerStatusFlag;
using safety_crit::workers::WorkerConfig;
using safety_crit::workers::WorkerRole;

constexpr std::uint64_t kTestSeed = 0xB16B00B5DAD10ADEULL;

std::string test_region_name(const char* case_tag) {
    return std::string("/sc_p3_itg_") + case_tag + "_" +
           std::to_string(static_cast<long>(::getpid()));
}

class Fixture {
public:
    explicit Fixture(const char* case_tag)
        : name_(test_region_name(case_tag)),
          pid_dir_(std::filesystem::temp_directory_path() /
                   (std::string{"sc_p3_itg_"} + case_tag + "_" +
                    std::to_string(static_cast<long>(::getpid())))),
          out_path_(std::filesystem::temp_directory_path() /
                    (std::string{"sc_p3_itg_"} + case_tag + "_" +
                     std::to_string(static_cast<long>(::getpid())) + ".out")) {
        SharedRegionHandle::destroy(name_.c_str());
        handle_ = SharedRegionHandle::create_or_open(name_.c_str());
        std::error_code ec;
        std::filesystem::remove_all(pid_dir_, ec);
        std::filesystem::remove(out_path_, ec);
    }
    ~Fixture() {
        if (handle_.ok()) {
            handle_.detach();
        }
        SharedRegionHandle::destroy(name_.c_str());
        std::error_code ec;
        std::filesystem::remove_all(pid_dir_, ec);
        std::filesystem::remove(out_path_, ec);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] bool ok() const { return handle_.ok(); }
    [[nodiscard]] SharedRegion* region() const { return handle_.get(); }
    [[nodiscard]] const char* name() const { return name_.c_str(); }
    [[nodiscard]] std::string pid_dir() const { return pid_dir_.string(); }
    [[nodiscard]] std::string out_path() const { return out_path_.string(); }

private:
    std::string name_;
    std::filesystem::path pid_dir_;
    std::filesystem::path out_path_;
    SharedRegionHandle handle_;
};

WorkerConfig hot_config(std::uint32_t idx, std::uint64_t ticks,
                        std::chrono::milliseconds interval) {
    WorkerConfig cfg{};
    cfg.worker_idx = idx;
    cfg.role = WorkerRole::kHot;
    cfg.ticks = ticks;
    cfg.tick_interval = interval;
    cfg.cpu_budget = std::chrono::microseconds(50000);
    cfg.seed_base = kTestSeed;
    return cfg;
}

pid_t fork_worker(const WorkerConfig& cfg, const char* region_name, const std::string& pid_dir) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::freopen("/dev/null", "w", stdout);
        ::freopen("/dev/null", "w", stderr);
        const int rc = safety_crit::workers::run_worker(cfg, region_name, pid_dir.c_str());
        ::_exit(rc);
    }
    return pid;
}

// Forked monitor child: stdout captured to the fixture output file.
pid_t fork_monitor(const Fixture& fx, std::uint64_t threshold_ms) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::freopen(fx.out_path().c_str(), "w", stdout);
        ::freopen("/dev/null", "w", stderr);
        MonitorConfig cfg{};
        cfg.poll_interval = std::chrono::milliseconds(10);
        cfg.stall_threshold = std::chrono::milliseconds(threshold_ms);
        const int rc = safety_crit::monitors::run_monitor(cfg, fx.name(), fx.pid_dir().c_str(), 0);
        ::_exit(rc);
    }
    return pid;
}

int timed_wait(pid_t pid, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        int status = 0;
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return -1;
}

bool wait_running(Fixture& fx, std::size_t idx, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        if (safety_crit::shared_memory::load_has_flag(fx.region()->worker_status[idx].status,
                                                      WorkerStatusFlag::kRunning)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool has_event(const std::string& log, const std::string& event) {
    return log.find("\"event\":\"" + event + "\"") != std::string::npos;
}

// Every emitted line must be a monitor JSON line (structural smoke on the
// captured stream; exact shapes are unit-tested in monitors_loop_test).
void expect_monitor_lines(const std::string& log) {
    SAFETY_CRIT_ASSERT(!log.empty());
    std::size_t line_start = 0;
    std::size_t lines = 0;
    while (line_start < log.size()) {
        const std::size_t nl = log.find('\n', line_start);
        const std::size_t end = nl == std::string::npos ? log.size() : nl;
        if (end > line_start) {
            const std::string line = log.substr(line_start, end - line_start);
            SAFETY_CRIT_ASSERT(line.rfind("{\"ts\":", 0) == 0);
            SAFETY_CRIT_ASSERT(line.find("\"component\":\"monitor\"") != std::string::npos);
            ++lines;
        }
        if (nl == std::string::npos) {
            break;
        }
        line_start = nl + 1;
    }
    SAFETY_CRIT_ASSERT(lines >= 1u);
}

SAFETY_CRIT_TEST_CASE(monitors_integration, CrashDetectedEndToEnd) {
    // Hot worker runs; SIGKILL (the un-cooperative death the status word
    // cannot express); the monitor must observe CRASHED through pidfile
    // liveness within a few polls, exactly once.
    Fixture fx("crash");
    SAFETY_CRIT_ASSERT(fx.ok());
    const WorkerConfig wcfg = hot_config(0, 1000000, std::chrono::milliseconds(5));
    const pid_t worker = fork_worker(wcfg, fx.name(), fx.pid_dir());
    SAFETY_CRIT_ASSERT(worker > 0);
    SAFETY_CRIT_ASSERT(wait_running(fx, 0, 3000));

    // The pidfile names exactly the running worker pid (contract pinned).
    SAFETY_CRIT_ASSERT(safety_crit::monitors::read_worker_pid(fx.pid_dir(), 0) == worker);

    const pid_t monitor = fork_monitor(fx, 100);
    SAFETY_CRIT_ASSERT(monitor > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    SAFETY_CRIT_ASSERT(::kill(worker, SIGKILL) == 0);
    int wstatus = 0;
    ::waitpid(worker, &wstatus, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // >= 10 polls

    SAFETY_CRIT_ASSERT(::kill(monitor, SIGTERM) == 0);
    const int mstatus = timed_wait(monitor, 3000);
    SAFETY_CRIT_ASSERT(mstatus >= 0);
    SAFETY_CRIT_ASSERT(WEXITSTATUS(mstatus) == 0);

    const std::string log = read_file(fx.out_path());
    expect_monitor_lines(log);
    SAFETY_CRIT_ASSERT(has_event(log, "worker_running"));
    SAFETY_CRIT_ASSERT(has_event(log, "worker_crashed"));
    SAFETY_CRIT_ASSERT(has_event(log, "monitor_report"));
    SAFETY_CRIT_ASSERT(log.find("\"worker_crashed\":1") != std::string::npos);
    // SIGKILL skipped pidfile cleanup: the stale file is left behind for
    // readers to reject via liveness, never trusted.
    SAFETY_CRIT_ASSERT(std::filesystem::exists(
        safety_crit::workers::worker_pid_path(fx.pid_dir(), 0)));
    SAFETY_CRIT_ASSERT(!safety_crit::monitors::worker_process_alive(fx.pid_dir(), 0));
}

SAFETY_CRIT_TEST_CASE(monitors_integration, CleanExitIsIdleNoCrash) {
    // Worker completes its tick budget and exits cleanly (IDLE published,
    // pidfile removed): the monitor must log idle, never a crash.
    Fixture fx("clean");
    SAFETY_CRIT_ASSERT(fx.ok());
    const WorkerConfig wcfg = hot_config(1, 30, std::chrono::milliseconds(5));
    const pid_t worker = fork_worker(wcfg, fx.name(), fx.pid_dir());
    SAFETY_CRIT_ASSERT(worker > 0);
    SAFETY_CRIT_ASSERT(wait_running(fx, 1, 3000));

    const pid_t monitor = fork_monitor(fx, 100);
    SAFETY_CRIT_ASSERT(monitor > 0);

    SAFETY_CRIT_ASSERT(timed_wait(worker, 5000) >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // monitor notices

    SAFETY_CRIT_ASSERT(::kill(monitor, SIGTERM) == 0);
    const int mstatus = timed_wait(monitor, 3000);
    SAFETY_CRIT_ASSERT(mstatus >= 0);
    SAFETY_CRIT_ASSERT(WEXITSTATUS(mstatus) == 0);

    const std::string log = read_file(fx.out_path());
    expect_monitor_lines(log);
    SAFETY_CRIT_ASSERT(has_event(log, "worker_running"));
    SAFETY_CRIT_ASSERT(has_event(log, "worker_idle"));
    SAFETY_CRIT_ASSERT(!has_event(log, "worker_crashed"));
    SAFETY_CRIT_ASSERT(log.find("\"worker_crashed\":0") != std::string::npos);
    // Clean exit removed the pidfile.
    SAFETY_CRIT_ASSERT(!std::filesystem::exists(
        safety_crit::workers::worker_pid_path(fx.pid_dir(), 1)));
}

SAFETY_CRIT_TEST_CASE(monitors_integration, StallDetectedAndRecoveredEndToEnd) {
    // SIGSTOP freezes a live worker (status stays RUNNING, counters frozen,
    // process still exists so liveness passes): the monitor must report
    // STALLED, not CRASHED. SIGCONT resumes it: exactly one recovery.
    Fixture fx("stall");
    SAFETY_CRIT_ASSERT(fx.ok());
    const WorkerConfig wcfg = hot_config(2, 1000000, std::chrono::milliseconds(5));
    const pid_t worker = fork_worker(wcfg, fx.name(), fx.pid_dir());
    SAFETY_CRIT_ASSERT(worker > 0);
    SAFETY_CRIT_ASSERT(wait_running(fx, 2, 3000));

    const pid_t monitor = fork_monitor(fx, 150);
    SAFETY_CRIT_ASSERT(monitor > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SAFETY_CRIT_ASSERT(::kill(worker, SIGSTOP) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // > 3x threshold

    SAFETY_CRIT_ASSERT(::kill(worker, SIGCONT) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // resume observed

    SAFETY_CRIT_ASSERT(::kill(monitor, SIGTERM) == 0);
    const int mstatus = timed_wait(monitor, 3000);
    SAFETY_CRIT_ASSERT(mstatus >= 0);
    SAFETY_CRIT_ASSERT(WEXITSTATUS(mstatus) == 0);
    ::kill(worker, SIGKILL);  // finish the frozen-tick budget the hard way
    ::waitpid(worker, nullptr, 0);

    const std::string log = read_file(fx.out_path());
    expect_monitor_lines(log);
    SAFETY_CRIT_ASSERT(has_event(log, "worker_stalled"));
    SAFETY_CRIT_ASSERT(has_event(log, "worker_recovered"));
    SAFETY_CRIT_ASSERT(!has_event(log, "worker_crashed"));  // alive: never a crash
}

SAFETY_CRIT_TEST_CASE(monitors_integration, StandbyReportedIdleAliveNoCrash) {
    // Standby worker (IDLE while alive, touches no ring): monitor reports
    // it idle exactly once, sees no crash, and never writes the region.
    Fixture fx("standby");
    SAFETY_CRIT_ASSERT(fx.ok());
    WorkerConfig wcfg = hot_config(0, 1, std::chrono::milliseconds(1));
    wcfg.role = WorkerRole::kStandby;
    const pid_t worker = fork_worker(wcfg, fx.name(), fx.pid_dir());
    SAFETY_CRIT_ASSERT(worker > 0);

    const pid_t monitor = fork_monitor(fx, 100);
    SAFETY_CRIT_ASSERT(monitor > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    SAFETY_CRIT_ASSERT(::kill(monitor, SIGTERM) == 0);
    const int mstatus = timed_wait(monitor, 3000);
    SAFETY_CRIT_ASSERT(mstatus >= 0);
    SAFETY_CRIT_ASSERT(WEXITSTATUS(mstatus) == 0);
    SAFETY_CRIT_ASSERT(::kill(worker, SIGTERM) == 0);
    SAFETY_CRIT_ASSERT(timed_wait(worker, 3000) >= 0);

    const std::string log = read_file(fx.out_path());
    expect_monitor_lines(log);
    SAFETY_CRIT_ASSERT(has_event(log, "worker_idle"));
    SAFETY_CRIT_ASSERT(!has_event(log, "worker_crashed"));
    SAFETY_CRIT_ASSERT(fx.region()->rings[0].consumed() == 0u);
}

#endif  // SAFETY_CRIT_P3_SANITIZED
}  // namespace

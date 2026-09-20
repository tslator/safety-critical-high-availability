// Phase 2, T2.3 (T-0011): fork-based multi-process integration tests.
// Plain build only under sanitizers (fork is unreliable there; G1.4
// precedent) -- the in-process loop tests carry the sanitizer signal.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // fork / waitpid feature-test macro
#endif

#include "test_framework.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "safety_crit/shared_memory/atomic_flags.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/workers/signals.hpp"
#include "safety_crit/workers/worker_config.hpp"
#include "safety_crit/workers/worker_entry.hpp"
#include "safety_crit/workers/workload.hpp"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAFETY_CRIT_P2_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SAFETY_CRIT_P2_SANITIZED 1
#endif
#endif
#ifndef SAFETY_CRIT_P2_SANITIZED
#define SAFETY_CRIT_P2_SANITIZED 0
#endif

namespace {
#if SAFETY_CRIT_P2_SANITIZED

// Forked children of a sanitized process hit runtime-fatal errors; the
// cross-process scenarios run in the plain build (same convention as the
// G1.4 fork test). Sanitizer coverage of the worker logic comes from the
// loop/core suites, which execute everything in-process.
SAFETY_CRIT_TEST_CASE(workers_integration, SkippedUnderSanitizers) {
    SAFETY_CRIT_ASSERT(true);
}

#else

using safety_crit::shared_memory::SharedRegion;
using safety_crit::shared_memory::SharedRegionHandle;
using safety_crit::shared_memory::WorkerStatusFlag;
using safety_crit::workers::ProcessedData;
using safety_crit::workers::WorkerConfig;
using safety_crit::workers::WorkerRole;

constexpr std::uint64_t kTestSeed = 0xB16B00B5DAD10ADEULL;

std::string test_region_name(const char* case_tag) {
    return std::string("/sc_p2_itg_") + case_tag + "_" +
           std::to_string(static_cast<long>(::getpid()));
}

// Creates a fresh uniquely-named region; destroys (shm_unlinks) it on scope
// exit. Pre-destroys in case a crashed previous run left a stale object.
class RegionFixture {
public:
    explicit RegionFixture(const char* case_tag) : name_(test_region_name(case_tag)) {
        SharedRegionHandle::destroy(name_.c_str());
        handle_ = SharedRegionHandle::create_or_open(name_.c_str());
    }
    ~RegionFixture() {
        if (handle_.ok()) {
            handle_.detach();
        }
        SharedRegionHandle::destroy(name_.c_str());
    }
    RegionFixture(const RegionFixture&) = delete;
    RegionFixture& operator=(const RegionFixture&) = delete;

    [[nodiscard]] bool ok() const { return handle_.ok(); }
    [[nodiscard]] SharedRegion* region() const { return handle_.get(); }
    [[nodiscard]] const char* name() const { return name_.c_str(); }

private:
    std::string name_;
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

// fork() a worker running run_worker with `cfg`; returns the child pid.
// Child output is silenced; the child _exit()s with run_worker's code.
pid_t fork_worker(const WorkerConfig& cfg, const char* region_name) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::freopen("/dev/null", "w", stdout);
        ::freopen("/dev/null", "w", stderr);
        const int rc = safety_crit::workers::run_worker(cfg, region_name);
        ::_exit(rc);
    }
    return pid;
}

// waitpid with timeout; -1 means timed out (child then SIGKILLed + reaped).
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

std::vector<ProcessedData> drain(SharedRegion& region, std::size_t worker_idx) {
    std::vector<ProcessedData> out;
    ProcessedData p{};
    while (region.rings[worker_idx].try_pop(p)) {
        out.push_back(p);
    }
    return out;
}

// Drained payloads must be exactly ticks 0..n-1, in order, with payloads
// matching the deterministic workload recomputation (plan: no data loss, no
// duplication, idempotency across process boundaries).
bool drained_is_valid_prefix(const std::vector<ProcessedData>& got, std::uint32_t worker_idx,
                             std::size_t* out_n = nullptr) {
    for (std::size_t i = 0; i < got.size(); ++i) {
        const ProcessedData expected =
            safety_crit::workers::process_sensor_data(
                safety_crit::workers::generate_raw_tick(kTestSeed, worker_idx, i));
        if (!safety_crit::workers::processed_equal(got[i], expected)) {
            return false;
        }
    }
    if (out_n != nullptr) {
        *out_n = got.size();
    }
    return true;
}

SAFETY_CRIT_TEST_CASE(workers_integration, RingDrainRoundTrip) {
    // (a) Hot worker pushes 200 ticks; a concurrent parent drain observes
    // every tick exactly once, in order, pipeline-correct (G2.3 core).
    RegionFixture fixture("drain");
    SAFETY_CRIT_ASSERT(fixture.ok());
    const WorkerConfig cfg = hot_config(0, 200, std::chrono::milliseconds(1));

    const pid_t child = fork_worker(cfg, fixture.name());
    SAFETY_CRIT_ASSERT(child > 0);

    std::vector<ProcessedData> got;
    int status = -1;
    for (int waited = 0; waited < 10000 && status < 0; waited += 5) {
        auto chunk = drain(*fixture.region(), 0);
        got.insert(got.end(), chunk.begin(), chunk.end());
        int s = 0;
        if (::waitpid(child, &s, WNOHANG) == child) {
            status = s;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto tail = drain(*fixture.region(), 0);
    got.insert(got.end(), tail.begin(), tail.end());
    if (status < 0) {
        ::kill(child, SIGKILL);
        ::waitpid(child, &status, 0);
    }

    SAFETY_CRIT_ASSERT(status >= 0);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);
    SAFETY_CRIT_ASSERT(got.size() == 200u);
    SAFETY_CRIT_ASSERT(drained_is_valid_prefix(got, 0));
    SAFETY_CRIT_ASSERT(fixture.region()->rings[0].empty());
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::load_has_flag(
        fixture.region()->worker_status[0].status, WorkerStatusFlag::kIdle));
    SAFETY_CRIT_ASSERT(!safety_crit::shared_memory::load_has_flag(
        fixture.region()->worker_status[0].status, WorkerStatusFlag::kRunning));
}

SAFETY_CRIT_TEST_CASE(workers_integration, SigtermCleanStop) {
    // (b) SIGTERM mid-run: child exits 0 within one tick + interval; all
    // committed outputs drain as a valid, gap-free tick prefix; the ring is
    // quiescent and the worker leaves IDLE behind.
    RegionFixture fixture("term");
    SAFETY_CRIT_ASSERT(fixture.ok());
    const WorkerConfig cfg = hot_config(1, 1000000, std::chrono::milliseconds(5));

    const pid_t child = fork_worker(cfg, fixture.name());
    SAFETY_CRIT_ASSERT(child > 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    SAFETY_CRIT_ASSERT(::kill(child, SIGTERM) == 0);
    const int status = timed_wait(child, 3000);
    SAFETY_CRIT_ASSERT(status >= 0);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);

    const std::vector<ProcessedData> got = drain(*fixture.region(), 1);
    SAFETY_CRIT_ASSERT(!got.empty());               // it was actually running
    SAFETY_CRIT_ASSERT(got.size() < 1000000u);      // and it stopped early
    SAFETY_CRIT_ASSERT(drained_is_valid_prefix(got, 1));
    SAFETY_CRIT_ASSERT(fixture.region()->rings[1].pushed() ==
                       fixture.region()->rings[1].consumed());
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::load_has_flag(
        fixture.region()->worker_status[1].status, WorkerStatusFlag::kIdle));
}

SAFETY_CRIT_TEST_CASE(workers_integration, Sigusr1CrashHook) {
    // (c) SIGUSR1 forced crash: exit via _exit(kCrashExitCode), no cleanup.
    // The status word keeps stale RUNNING (only a supervisor can declare
    // CRASHED -- Phase 4), and the ring may hold an in-flight claim from the
    // interrupted tick: committed outputs must still drain as a valid tick
    // prefix (Phase 1's protocol guarantee under crash).
    RegionFixture fixture("usr1");
    SAFETY_CRIT_ASSERT(fixture.ok());
    const WorkerConfig cfg = hot_config(2, 1000000, std::chrono::milliseconds(5));

    const pid_t child = fork_worker(cfg, fixture.name());
    SAFETY_CRIT_ASSERT(child > 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SAFETY_CRIT_ASSERT(::kill(child, SIGUSR1) == 0);
    const int status = timed_wait(child, 3000);
    SAFETY_CRIT_ASSERT(status >= 0);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == safety_crit::workers::signals::kCrashExitCode);

    const std::vector<ProcessedData> got = drain(*fixture.region(), 2);
    SAFETY_CRIT_ASSERT(drained_is_valid_prefix(got, 2));
    SAFETY_CRIT_ASSERT(fixture.region()->rings[2].pushed() >=
                       fixture.region()->rings[2].consumed());
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::load_has_flag(
        fixture.region()->worker_status[2].status, WorkerStatusFlag::kRunning));
}

SAFETY_CRIT_TEST_CASE(workers_integration, StandbyPassivity) {
    // (d) Standby: polls status flags, touches no ring, exits 0 on SIGTERM.
    RegionFixture fixture("standby");
    SAFETY_CRIT_ASSERT(fixture.ok());
    WorkerConfig cfg = hot_config(0, 100, std::chrono::milliseconds(1));
    cfg.role = WorkerRole::kStandby;

    const pid_t child = fork_worker(cfg, fixture.name());
    SAFETY_CRIT_ASSERT(child > 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    SAFETY_CRIT_ASSERT(::kill(child, SIGTERM) == 0);
    const int status = timed_wait(child, 3000);
    SAFETY_CRIT_ASSERT(status >= 0);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);

    SAFETY_CRIT_ASSERT(fixture.region()->rings[0].pushed() == 0u);
    SAFETY_CRIT_ASSERT(fixture.region()->rings[0].consumed() == 0u);
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::load_has_flag(
        fixture.region()->worker_status[0].status, WorkerStatusFlag::kIdle));
}

SAFETY_CRIT_TEST_CASE(workers_integration, StaleGenerationCannotPublish) {
    RegionFixture fixture("stale_generation");
    SAFETY_CRIT_ASSERT(fixture.ok());
    WorkerConfig cfg = hot_config(0, 100, std::chrono::milliseconds(1));
    cfg.logical_ring = 0;
    cfg.process_generation = 1;

    safety_crit::shared_memory::OwnershipToken current;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*fixture.region(), 0, current));
    safety_crit::shared_memory::OwnershipToken replacement;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::transfer_ownership(
        *fixture.region(), 0, current, 2, 2, replacement));

    safety_crit::shared_memory::OwnershipToken acknowledged;
    SAFETY_CRIT_ASSERT(!safety_crit::workers::acknowledge_ownership(
        *fixture.region(), cfg, acknowledged));
    SAFETY_CRIT_ASSERT(fixture.region()->rings[0].pushed() == 0u);
}

SAFETY_CRIT_TEST_CASE(workers_integration, PromotionAcknowledgementUsesNewGeneration) {
    RegionFixture fixture("promotion_ack");
    SAFETY_CRIT_ASSERT(fixture.ok());

    WorkerConfig promoted = hot_config(2, 1, std::chrono::milliseconds(1));
    promoted.role = WorkerRole::kStandby;
    promoted.logical_ring = 0;
    promoted.process_generation = 7;

    safety_crit::shared_memory::OwnershipToken initial;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::read_ownership(*fixture.region(), 0, initial));
    safety_crit::shared_memory::OwnershipToken replacement;
    SAFETY_CRIT_ASSERT(safety_crit::shared_memory::transfer_ownership(
        *fixture.region(), 0, initial, promoted.worker_idx, promoted.process_generation,
        replacement));

    safety_crit::shared_memory::OwnershipToken acknowledged;
    SAFETY_CRIT_ASSERT(safety_crit::workers::acknowledge_ownership(
        *fixture.region(), promoted, acknowledged));
    SAFETY_CRIT_ASSERT(acknowledged.logical_ring == 0u);
    SAFETY_CRIT_ASSERT(acknowledged.epoch == replacement.epoch);
}

#endif  // SAFETY_CRIT_P2_SANITIZED
}  // namespace

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "safety_crit/supervisor/supervisor.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

#include "safety_crit/monitors/health.hpp"
#include "safety_crit/monitors/monitor_config.hpp"
#include "safety_crit/monitors/monitor_entry.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/runtime/scheduling.hpp"
#include "safety_crit/workers/worker_config.hpp"
#include "safety_crit/workers/worker_entry.hpp"
#include "safety_crit/workers/workload.hpp"

namespace safety_crit::supervisor {
namespace {

volatile std::sig_atomic_t g_stop = 0;
void stop_handler(int) { g_stop = 1; }

bool has_prefix(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool parse_number(std::string_view value, std::int64_t& out) {
    if (value.empty()) {
        return false;
    }
    const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool known_alert(std::string_view event) {
    return event == "worker_crashed" || event == "worker_stalled" ||
           event == "worker_recovered" || event == "worker_overrun" ||
           event == "worker_idle" || event == "worker_running";
}

bool parse_alert(std::string_view line, MonitorLine& out) {
    constexpr std::string_view prefix =
        "{\"ts\":";
    if (!has_prefix(line, prefix) || line.back() != '}') {
        return false;
    }
    line.remove_suffix(1);
    const std::size_t level = line.find(",\"level\":\"");
    const std::size_t component = line.find(",\"component\":\"monitor\"");
    const std::size_t event = line.find(",\"event\":\"");
    const std::size_t worker = line.find(",\"worker\":");
    if (level == std::string_view::npos || component == std::string_view::npos ||
        event == std::string_view::npos || worker == std::string_view::npos ||
        level < prefix.size() || component <= level || event <= component || worker <= event) {
        return false;
    }
    std::int64_t timestamp = 0;
    const std::size_t level_start = level + std::string_view(",\"level\":\"").size();
    const std::size_t level_end = line.find('"', level_start);
    if (!parse_number(line.substr(prefix.size(), level - prefix.size()), timestamp) ||
        timestamp < 0 || level_end == std::string_view::npos ||
        (line.substr(level_start, level_end - level_start) != "error" &&
         line.substr(level_start, level_end - level_start) != "warn" &&
         line.substr(level_start, level_end - level_start) != "info")) {
        return false;
    }
    const std::size_t event_start = event + std::string_view(",\"event\":\"").size();
    const std::size_t event_end = line.find('"', event_start);
    if (event_end == std::string_view::npos || !known_alert(line.substr(event_start, event_end - event_start))) {
        return false;
    }
    const std::size_t worker_start = worker + std::string_view(",\"worker\":").size();
    std::int64_t worker_number = 0;
    if (!parse_number(line.substr(worker_start), worker_number) || worker_number < 0 ||
        worker_number >= 3) {
        return false;
    }
    out = MonitorLine{MonitorLineKind::kAlert, timestamp,
                      static_cast<std::uint32_t>(worker_number),
                      line.substr(event_start, event_end - event_start)};
    return true;
}

bool parse_report(std::string_view line, MonitorLine& out) {
    constexpr std::string_view marker =
        "\"event\":\"monitor_report\"";
    if (line.find(marker) == std::string_view::npos || line.back() != '}') {
        return false;
    }
    out = MonitorLine{MonitorLineKind::kReport, 0, 0, "monitor_report"};
    return true;
}

struct Child {
    pid_t pid{-1};
    std::uint32_t physical_worker{shared_memory::kUnassignedPhysicalOwner};
    bool worker{false};
    bool handled{false};
};

void terminate_and_reap(std::vector<Child>& children) {
    for (const Child child : children) {
        if (child.pid > 0) {
            (void)::kill(child.pid, SIGTERM);
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    for (const Child child : children) {
        if (child.pid <= 0) {
            continue;
        }
        int status = 0;
        while (::waitpid(child.pid, &status, WNOHANG) == 0 &&
               std::chrono::steady_clock::now() < deadline) {
            ::usleep(1000);
        }
        if (::kill(child.pid, 0) == 0) {
            (void)::kill(child.pid, SIGKILL);
            (void)::waitpid(child.pid, &status, 0);
        }
    }
}

pid_t launch_worker(const workers::WorkerConfig& config, const char* region,
                    const char* pid_dir) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        const int rc = workers::run_worker(config, region, pid_dir);
        ::_exit(rc);
    }
    return pid;
}

Child* find_worker(std::vector<Child>& children, std::uint32_t physical_worker) {
    for (Child& child : children) {
        if (child.worker && child.physical_worker == physical_worker && !child.handled) {
            return &child;
        }
    }
    return nullptr;
}

// Topology constant (DEC-0011 #2): physical C is the only standby.
constexpr std::uint32_t kStandbyPhysical = 2u;

// T-0026 (DEC-0012 #4): crash recovery for ANY physical worker. A crash of
// the owner of a logical ring promotes the standby physical process unless
// the standby already owns a ring (double fault): the ring is then marked
// DEGRADED in its owner's worker_status cell (terminal for that ring) and
// announced exactly once. The crashed physical process always restarts as
// standby.
bool recover_worker_crash(shared_memory::SharedRegion& region,
                          std::vector<Child>& children,
                          std::uint32_t physical_worker,
                          const char* region_name, const char* pid_dir,
                          std::uint32_t& next_generation,
                          std::array<bool, shared_memory::kMaxWorkers>& degraded_rings) {
    Child* crashed = find_worker(children, physical_worker);
    if (crashed == nullptr) {
        return true;
    }
    crashed->handled = true;

    const auto owned = monitors::owned_logical_ring(region, physical_worker);
    if (owned.has_value()) {
        const std::size_t logical = *owned;
        shared_memory::OwnershipToken expected;
        if (!shared_memory::read_ownership(region, logical, expected) ||
            expected.physical_owner != physical_worker ||
            !shared_memory::verify_worker_ring(region, logical)) {
            return false;
        }
        Child* standby = find_worker(children, kStandbyPhysical);
        // Region initialization hands every ring to its home physical, so
        // "busy" means the standby owns a ring that is NOT its own home
        // index, i.e. it has already been promoted to a hot logical ring.
        const auto owned_by_standby =
            monitors::owned_logical_ring(region, kStandbyPhysical);
        const bool standby_busy =
            owned_by_standby.has_value() && *owned_by_standby != kStandbyPhysical;
        if (standby != nullptr && standby->pid > 0 && !standby_busy) {
            shared_memory::OwnershipToken replacement_token;
            if (!shared_memory::transfer_ownership(region, logical, expected,
                                                    kStandbyPhysical, 1u,
                                                    replacement_token)) {
                return false;
            }
        } else if (!degraded_rings[physical_worker]) {
            // No promotable standby: DEGRADED is terminal for this ring
            // within this supervisor lifetime; announce exactly once and
            // keep re-asserting the bit (the status word belongs to the
            // physical process and a standby restart stores over it).
            shared_memory::set_flag(region.worker_status[physical_worker].status,
                                    shared_memory::WorkerStatusFlag::kDegraded);
            std::printf("supervisor: logical ring %zu degraded (reason=standby_exhausted)\n",
                        logical);
            std::fflush(stdout);
            degraded_rings[physical_worker] = true;
        }
    }

    const std::uint32_t replacement_generation = ++next_generation;
    workers::WorkerConfig replacement{};
    replacement.worker_idx = physical_worker;
    replacement.logical_ring = shared_memory::kUnassignedPhysicalOwner;
    replacement.process_generation = replacement_generation;
    replacement.role = workers::WorkerRole::kStandby;
    replacement.ticks = 1000u;
    const pid_t replacement_pid = launch_worker(replacement, region_name, pid_dir);
    if (replacement_pid < 0) {
        return false;
    }
    children.push_back({replacement_pid, physical_worker, true, false});
    return true;
}

// Returns false only when recovery itself failed (failsafe). `crash_reaped`
// reports whether THIS iteration reaped a crashed child, which is what opens
// the T-0032 handoff window -- the timing witness below stays gated on the
// first crash of the process lifetime.
bool reap_crashed_workers(shared_memory::SharedRegion& region,
                          std::vector<Child>& children,
                          const char* region_name, const char* pid_dir,
                          std::uint32_t& next_generation,
                          std::chrono::steady_clock::time_point* crash_observed,
                          std::array<bool, shared_memory::kMaxWorkers>& degraded_rings,
                          bool* crash_reaped = nullptr) {
    for (std::size_t index = 0; index < children.size(); ++index) {
        Child& child = children[index];
        if (!child.worker || child.pid <= 0 || child.handled) {
            continue;
        }
        int status = 0;
        if (::waitpid(child.pid, &status, WNOHANG) != child.pid) {
            continue;
        }
        child.pid = -1;
        if (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0)) {
            if (crash_observed != nullptr && crash_observed->time_since_epoch().count() == 0) {
                *crash_observed = std::chrono::steady_clock::now();
            }
            if (crash_reaped != nullptr) {
                *crash_reaped = true;
            }
            if (!recover_worker_crash(region, children, child.physical_worker,
                                       region_name, pid_dir, next_generation,
                                       degraded_rings)) {
                return false;
            }
        } else {
            child.handled = true;
        }
    }
    return true;
}

// T-0025: tail and epoch of the logical ring a physical worker owns
// (T-0023 attribution), for the stall recovery tracker.
bool owned_ring_state(shared_memory::SharedRegion& region, std::uint32_t physical_worker,
                      std::uint64_t& tail, std::uint64_t& epoch) {
    const auto owned = monitors::owned_logical_ring(region, physical_worker);
    const std::size_t logical = owned.value_or(physical_worker);
    shared_memory::OwnershipToken token;
    if (!shared_memory::read_ownership(region, logical, token)) {
        return false;
    }
    epoch = token.epoch;
    tail = region.rings[logical].tail_.load(std::memory_order_acquire);
    return true;
}

}  // namespace

bool parse_monitor_line(std::string_view line, MonitorLine& out) {
    return parse_alert(line, out) || parse_report(line, out);
}

bool drain_output_witness(shared_memory::SharedRegion& region, std::size_t logical_ring,
                          OutputWitness& witness) {
    if (logical_ring >= shared_memory::kMaxWorkers) {
        return false;
    }
    shared_memory::OwnershipToken ownership;
    if (!shared_memory::read_ownership(region, logical_ring, ownership)) {
        return false;
    }
    if (witness.last_epoch != 0u && ownership.epoch < witness.last_epoch) {
        return false;
    }
    auto& ring = region.rings[logical_ring];
    while (true) {
        const std::uint64_t corruption_before = ring.corruption_count();
        workers::ProcessedData record;
        if (!ring.try_pop(record)) {
            if (ring.corruption_count() != corruption_before) {
                // T-0027 (DEC-0012 #5): the ring's skip-and-count path
                // consumed a corrupted slot. Tolerate and count it; the
                // sequence continuity invariant carries across the skip.
                ++witness.corruptions;
                ++witness.next_sequence;
                continue;
            }
            break;  // empty
        }
        const std::uint64_t sequence = ring.consumed() - 1u;
        if (witness.records != 0u && sequence != witness.next_sequence) {
            return false;
        }
        witness.next_sequence = sequence + 1u;
        ++witness.records;
    }
    witness.last_epoch = ownership.epoch;
    return true;
}

StallRecoveryTracker::StallRecoveryTracker(std::chrono::milliseconds grace_period)
    : grace_period_(grace_period) {}

bool StallRecoveryTracker::begin(std::uint32_t physical_worker, std::uint64_t epoch,
                                 std::uint64_t tail, time_point now) {
    if (physical_worker >= slots_.size()) {
        return false;
    }
    Slot& slot = slots_[physical_worker];
    if (slot.active) {
        return false;  // idempotent per epoch: grace timer is not reset
    }
    slot.active = true;
    slot.epoch = epoch;
    slot.baseline_tail = tail;
    slot.started_at = now;
    return true;
}

StallRecoveryEvent StallRecoveryTracker::observe(std::uint32_t physical_worker,
                                                 std::uint64_t tail, time_point now) {
    StallRecoveryEvent event;
    if (physical_worker >= slots_.size()) {
        return event;
    }
    Slot& slot = slots_[physical_worker];
    if (!slot.active) {
        return event;
    }
    event.physical_worker = physical_worker;
    event.epoch = slot.epoch;
    if (tail != slot.baseline_tail) {
        slot.active = false;  // ring moved: the SIGCONT worked
        event.outcome = StallRecoveryOutcome::kRecovered;
    } else if (now - slot.started_at > grace_period_) {
        slot.active = false;  // grace expired: caller escalates to SIGKILL
        event.outcome = StallRecoveryOutcome::kEscalate;
    }
    return event;
}

bool StallRecoveryTracker::recovering(std::uint32_t physical_worker) const {
    if (physical_worker >= slots_.size()) {
        return false;
    }
    return slots_[physical_worker].active;
}

int run_supervisor(const SupervisorConfig& config) {
    const runtime::SchedulingResult scheduling =
        runtime::apply_scheduling(runtime::ProcessRole::kSupervisor);
    if (scheduling.fallback) {
        std::fprintf(stderr, "supervisor: scheduling fallback (errno %d)\n",
                     scheduling.error_number);
    }
    if (config.region_name == nullptr || config.pid_dir.empty()) {
        return 2;
    }
    shared_memory::SharedRegionHandle region =
        shared_memory::SharedRegionHandle::create_or_open(config.region_name);
    if (!region.ok() || !shared_memory::verify_identity(*region.get())) {
        return 1;
    }
    std::error_code directory_error;
    (void)std::filesystem::create_directories(config.pid_dir, directory_error);
    if (directory_error && !std::filesystem::is_directory(config.pid_dir)) {
        return 1;
    }

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        return 1;
    }
    std::vector<Child> children;
    SupervisorState state = SupervisorState::kLaunching;
    std::uint32_t next_generation = 1u;
    const pid_t monitor = ::fork();
    if (monitor == 0) {
        ::close(pipe_fds[0]);
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::close(pipe_fds[1]);
        monitors::MonitorConfig monitor_config{};
        const int rc = monitors::run_monitor(monitor_config, config.region_name,
                                              config.pid_dir.c_str());
        ::_exit(rc);
    }
    if (monitor < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return 1;
    }
    ::close(pipe_fds[1]);
    children.push_back({monitor, shared_memory::kUnassignedPhysicalOwner, false, false});

    workers::WorkerConfig hot_a{};
    hot_a.worker_idx = 0;
    hot_a.logical_ring = 0;
    hot_a.ticks = config.worker_ticks;
    workers::WorkerConfig hot_b = hot_a;
    hot_b.worker_idx = 1;
    hot_b.logical_ring = 1;
    workers::WorkerConfig standby = hot_a;
    standby.worker_idx = 2;
    standby.logical_ring = shared_memory::kUnassignedPhysicalOwner;
    standby.role = workers::WorkerRole::kStandby;
    for (const workers::WorkerConfig worker : {hot_a, hot_b, standby}) {
        const pid_t pid = launch_worker(worker, config.region_name, config.pid_dir.c_str());
        if (pid < 0) {
            g_stop = 1;
            break;
        }
        children.push_back({pid, worker.worker_idx, true, false});
    }

    struct sigaction action{};
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    struct sigaction old_term{}, old_int{};
    sigaction(SIGTERM, &action, &old_term);
    sigaction(SIGINT, &action, &old_int);

    const auto deadline = config.runtime_ms == 0
                               ? std::chrono::steady_clock::time_point::max()
                               : std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(config.runtime_ms);
    std::string pending;
    std::string last_line;
    bool input_error = false;
    OutputWitness output_witness_a;
    OutputWitness output_witness_b;
    std::uint64_t records_before_failover = 0;
    // T-0022 (G4.3): the "injected worker fault" start-of-clock is approximated
    // by the supervisor's reap observation of the crash, which is the earliest
    // supervisor-observable signal. Elapsed ms to first post-failover record
    // is emitted once on stdout for phase evidence.
    std::chrono::steady_clock::time_point failover_detected{};
    bool failover_timing_emitted = false;
    // T-0032 (failover handoff blind spot): opened by the iteration that
    // reaps the crash, closed by the first post-failover commit (or by
    // config.handoff_grace_ms, so the window is bounded even if the ring
    // never commits again). While open, the promoted owner is by definition
    // not yet producing records: monitor stall alerts in this window are
    // handoff artifacts and arming SIGCONT/SIGKILL from them would kill the
    // worker that is in the middle of recovering the ring.
    bool post_failover_handoff_open = false;
    std::chrono::steady_clock::time_point post_failover_handoff_deadline{};
    char buffer[512];
    state = SupervisorState::kRunning;
    bool recovery_failed = false;
    StallRecoveryTracker stall_tracker{std::chrono::milliseconds(config.stall_grace_ms)};
    std::array<bool, shared_memory::kMaxWorkers> degraded_rings{};
    while (!g_stop && !recovery_failed && std::chrono::steady_clock::now() < deadline) {
        const bool failover_previously_detected = failover_detected.time_since_epoch().count() != 0;
        bool crash_reaped = false;
        if (!reap_crashed_workers(*region.get(), children, config.region_name,
                                  config.pid_dir.c_str(), next_generation, &failover_detected,
                                  degraded_rings, &crash_reaped)) {
            state = SupervisorState::kFailsafe;
            recovery_failed = true;
            break;
        }
        // Management-plane authority (T-0026): the supervisor re-asserts the
        // DEGRADED bit of every degraded ring each loop, so a standby
        // restart storing its own status word cannot silently erase it.
        for (std::size_t ring = 0; ring < shared_memory::kMaxWorkers; ++ring) {
            if (degraded_rings[ring]) {
                shared_memory::set_flag(region.get()->worker_status[ring].status,
                                        shared_memory::WorkerStatusFlag::kDegraded);
                state = SupervisorState::kDegraded;
            }
        }
        if (!drain_output_witness(*region.get(), 0u, output_witness_a) ||
            !drain_output_witness(*region.get(), 1u, output_witness_b)) {
            state = SupervisorState::kFailsafe;
            recovery_failed = true;
            break;
        }
        // Snapshot the drain position on the same iteration that reap moved
        // ownership so first-post-failover only counts records committed
        // AFTER the crash (A's residual drain here is excluded).
        // T-0032: reap is the authoritative failover trigger (the monitor
        // cannot report it -- the replacement process stores its own IDLE
        // status into the crashed slot's status word before the monitor's next
        // poll, so the crash edge is masked). Record it here rather than
        // waiting for an alert that never arrives, and open the handoff window
        // for EVERY reaped crash (a double fault must not run unshielded).
        if (crash_reaped) {
            post_failover_handoff_open = true;
            // Clock starts here, not at `failover_detected`: that timestamp is
            // recorded once per supervisor lifetime (it anchors the timing
            // witness), so a second crash would inherit an expired deadline.
            post_failover_handoff_deadline = std::chrono::steady_clock::now() +
                                             std::chrono::milliseconds(config.handoff_grace_ms);
            if (state != SupervisorState::kDegraded) {
                state = SupervisorState::kFailoverDetected;
            }
        }
        if (!failover_previously_detected &&
            failover_detected.time_since_epoch().count() != 0 && records_before_failover == 0u) {
            records_before_failover = output_witness_a.records;
        }
        shared_memory::OwnershipToken logical_a;
        if (shared_memory::read_ownership(*region.get(), 0u, logical_a) &&
            logical_a.physical_owner == 2u && logical_a.epoch > 2u &&
            output_witness_a.records > records_before_failover) {
            if (!output_witness_a.first_post_failover) {
                // Only emit the timing when this process actually observed
                // the crash via reap; a stale region carried over from a
                // previous run has ownership already moved and would
                // otherwise produce a nonsensical delta against a zero clock.
                if (failover_detected.time_since_epoch().count() != 0) {
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - failover_detected)
                                        .count();
                    std::printf("supervisor: first post-failover record observed in %lld ms\n",
                                static_cast<long long>(ms));
                    std::fflush(stdout);
                }
                failover_timing_emitted = true;
            }
            output_witness_a.first_post_failover = true;
            // T-0032: the promoted owner is producing again; monitor stall
            // alerts from here on describe real stalls.
            post_failover_handoff_open = false;
        }
        // T-0032: bounded handoff window -- if the ring never commits, the
        // window still closes and normal stall detection resumes.
        if (post_failover_handoff_open && std::chrono::steady_clock::now() >
                                              post_failover_handoff_deadline) {
            post_failover_handoff_open = false;
        }
        // T-0025 (DEC-0012 #3): bounded stall recovery. One SIGCONT was
        // issued when the alert arrived; every loop either sees the ring
        // tail move (recovered) or the grace expire (escalate to SIGKILL;
        // the next reap iteration runs the existing crash-recovery path).
        for (std::size_t physical = 0; physical < shared_memory::kMaxWorkers; ++physical) {
            const auto physical_worker = static_cast<std::uint32_t>(physical);
            if (!stall_tracker.recovering(physical_worker)) {
                continue;
            }
            std::uint64_t tail = 0;
            std::uint64_t epoch = 0;
            if (!owned_ring_state(*region.get(), physical_worker, tail, epoch)) {
                continue;
            }
            const StallRecoveryEvent event = stall_tracker.observe(
                physical_worker, tail, std::chrono::steady_clock::now());
            if (event.outcome == StallRecoveryOutcome::kRecovered) {
                if (state != SupervisorState::kDegraded) {
                    state = SupervisorState::kRunning;
                }
                std::printf("supervisor: stall recovered for physical %u at epoch %llu\n",
                            event.physical_worker,
                            static_cast<unsigned long long>(event.epoch));
                std::fflush(stdout);
            } else if (event.outcome == StallRecoveryOutcome::kEscalate) {
                std::printf("supervisor: stall escalation for physical %u at epoch %llu\n",
                            event.physical_worker,
                            static_cast<unsigned long long>(event.epoch));
                std::fflush(stdout);
                Child* stalled = find_worker(children, physical_worker);
                if (stalled != nullptr && stalled->pid > 0) {
                    (void)::kill(stalled->pid, SIGKILL);
                }
                state = SupervisorState::kFailoverDetected;
            }
        }
        struct pollfd descriptor{pipe_fds[0], POLLIN | POLLHUP, 0};
        const int ready = ::poll(&descriptor, 1, 10);
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            const ssize_t count = ::read(pipe_fds[0], buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            pending.append(buffer, static_cast<std::size_t>(count));
            std::size_t newline = 0;
            while ((newline = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                MonitorLine parsed;
                if (!parse_monitor_line(line, parsed) || line == last_line) {
                    input_error = true;
                    continue;
                }
                last_line = line;
                if (parsed.kind == MonitorLineKind::kAlert &&
                    parsed.event == "worker_crashed") {
                    // DEGRADED is sticky for the supervisor lifetime (the
                    // degraded ring is terminal); a crash alert must not
                    // mask it.
                    if (state != SupervisorState::kDegraded) {
                        state = SupervisorState::kFailoverDetected;
                    }
                    // Records snapshot lives with reap_crashed_workers (the
                    // actual failover trigger), not here, so drain position
                    // and ownership transfer stay consistent.
                }
                if (parsed.kind == MonitorLineKind::kAlert &&
                    parsed.event == "worker_stalled" &&
                    // T-0032: during a failover handoff the promoted owner has
                    // not committed yet by design; arming the SIGCONT/SIGKILL
                    // path from that alert SIGKILLs the worker that is in the
                    // middle of recovering the ring. kFailoverDetected is
                    // admitted (it is the post-failover steady state, T-0032)
                    // so genuine stalls after the window still recover;
                    // kDegraded stays sticky and terminal as before.
                    !post_failover_handoff_open &&
                    (state == SupervisorState::kRunning ||
                     state == SupervisorState::kStalledRecovering ||
                     state == SupervisorState::kFailoverDetected)) {
                    std::uint64_t tail = 0;
                    std::uint64_t epoch = 0;
                    if (owned_ring_state(*region.get(), parsed.worker, tail, epoch) &&
                        stall_tracker.begin(parsed.worker, epoch, tail,
                                            std::chrono::steady_clock::now())) {
                        Child* stalled = find_worker(children, parsed.worker);
                        if (stalled != nullptr && stalled->pid > 0) {
                            (void)::kill(stalled->pid, SIGCONT);
                        }
                        state = SupervisorState::kStalledRecovering;
                    }
                }
                std::fwrite(line.data(), 1, line.size(), stdout);
                std::fputc('\n', stdout);
                std::fflush(stdout);
            }
        }
    }
    if (!pending.empty()) {
        input_error = true;  // EOF/termination in the middle of a record.
    }
    ::close(pipe_fds[0]);
    terminate_and_reap(children);
    sigaction(SIGTERM, &old_term, nullptr);
    sigaction(SIGINT, &old_int, nullptr);
    // T-0022 (Phase 4 exit evidence): the shutdown summary captures the drain
    // witness state (records, corruptions, first-post-failover) and the exit
    // reason so integration runs can verify continuity and clean shutdown
    // without attaching a second consumer to the ring.
    std::printf(
        "supervisor: shutdown state=%d a_records=%llu a_corruptions=%llu a_first_post_failover=%d "
        "b_records=%llu b_corruptions=%llu b_first_post_failover=%d failover_timing_emitted=%d\n",
        static_cast<int>(state),
        static_cast<unsigned long long>(output_witness_a.records),
        static_cast<unsigned long long>(output_witness_a.corruptions),
        output_witness_a.first_post_failover ? 1 : 0,
        static_cast<unsigned long long>(output_witness_b.records),
        static_cast<unsigned long long>(output_witness_b.corruptions),
        output_witness_b.first_post_failover ? 1 : 0,
        failover_timing_emitted ? 1 : 0);
    std::fflush(stdout);
    region.detach();
    if (state == SupervisorState::kDegraded || state == SupervisorState::kFailsafe) {
        return 4;
    }
    return input_error ? 3 : 0;
}

}  // namespace safety_crit::supervisor

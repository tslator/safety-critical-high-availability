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

#include "safety_crit/monitors/monitor_config.hpp"
#include "safety_crit/monitors/monitor_entry.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"
#include "safety_crit/workers/worker_config.hpp"
#include "safety_crit/workers/worker_entry.hpp"

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

}  // namespace

bool parse_monitor_line(std::string_view line, MonitorLine& out) {
    return parse_alert(line, out) || parse_report(line, out);
}

int run_supervisor(const SupervisorConfig& config) {
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
    children.push_back({monitor});

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
        children.push_back({pid});
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
    char buffer[512];
    while (!g_stop && std::chrono::steady_clock::now() < deadline) {
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
    region.detach();
    return input_error ? 3 : 0;
}

}  // namespace safety_crit::supervisor

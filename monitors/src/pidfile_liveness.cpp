#include "safety_crit/monitors/pidfile_liveness.hpp"

#include <cerrno>
#include <csignal>
#include <string>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

namespace safety_crit::monitors {

std::filesystem::path worker_pid_path(const std::filesystem::path& dir, std::size_t worker_idx) {
    return dir / ("safety_crit_worker_" + std::to_string(worker_idx) + ".pid");
}

std::int64_t read_worker_pid(const std::filesystem::path& dir, std::size_t worker_idx) {
    const std::filesystem::path path = worker_pid_path(dir, worker_idx);
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;  // missing or unreadable: dead verdict
    }
    char buf[32];
    ssize_t n = 0;
    do {
        n = ::read(fd, buf, sizeof(buf) - 1);
    } while (n < 0 && errno == EINTR);
    ::close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';

    errno = 0;
    char* end = nullptr;
    const long long pid = std::strtoll(buf, &end, 10);
    if (errno != 0 || end == buf) {
        return 0;  // malformed
    }
    if (pid <= 0 || pid > kPidMax) {
        return 0;  // out of legal range: stale garbage
    }
    return static_cast<std::int64_t>(pid);
}

bool worker_process_alive(const std::filesystem::path& dir, std::size_t worker_idx) {
    const std::int64_t pid = read_worker_pid(dir, worker_idx);
    if (pid == 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;  // exists but not ours: still alive
}

}  // namespace safety_crit::monitors

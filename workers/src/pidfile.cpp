#include "safety_crit/workers/pidfile.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <string>
#include <fcntl.h>
#include <unistd.h>

namespace safety_crit::workers {

std::filesystem::path worker_pid_path(const std::filesystem::path& dir, std::uint32_t worker_idx) {
    return dir / ("safety_crit_worker_" + std::to_string(worker_idx) + ".pid");
}

bool write_worker_pidfile(const std::filesystem::path& dir, std::uint32_t worker_idx) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec && !std::filesystem::is_directory(dir)) {
        return false;
    }
    const std::filesystem::path path = worker_pid_path(dir, worker_idx);
    // Temp name embeds the writing pid: two racing writers never share a
    // temp file, and rename() publishes the final name atomically.
    const std::filesystem::path tmp =
        path.string() + "." + std::to_string(static_cast<long>(::getpid())) + ".tmp";

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    const std::string content = std::to_string(static_cast<long>(::getpid())) + "\n";
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t n = ::write(fd, content.data() + written, content.size() - written);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            ::close(fd);
            ::unlink(tmp.c_str());
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    if (::close(fd) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

void remove_worker_pidfile(const std::filesystem::path& dir, std::uint32_t worker_idx) {
    const std::filesystem::path path = worker_pid_path(dir, worker_idx);
    std::error_code ec;
    std::filesystem::remove(path, ec);  // best-effort by design
}

}  // namespace safety_crit::workers

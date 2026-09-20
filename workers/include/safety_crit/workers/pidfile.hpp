#pragma once

#include <cstdint>
#include <filesystem>

namespace safety_crit::workers {

// Worker pidfile contract (T3.2, DEC-0010 #2). One file per worker at
// `<dir>/safety_crit_worker_<idx>.pid`, content is the decimal pid plus a
// trailing newline, published atomically (temp file + rename) so a reader
// never sees a truncated file. The writer (worker) creates the file right
// after region attach and removes it on clean exit only; a crash leaves a
// stale file, which the reader must reject through its liveness check --
// file existence alone is never proof of life. The reader side lives in
// `safety_crit/monitors/pidfile_liveness.hpp`; the fork integration tests
// pin the end-to-end contract.
inline constexpr const char* kDefaultPidDir = "/tmp";

// Canonical path for one worker's pidfile.
std::filesystem::path worker_pid_path(const std::filesystem::path& dir, std::uint32_t worker_idx);

// Publishes the calling process's pid for `worker_idx`. Returns false on
// any failure (directory cannot be created, open/write/rename error);
// callers treat failure as fatal because a missing pidfile makes a live
// worker indistinguishable from a crashed one for the monitor.
bool write_worker_pidfile(const std::filesystem::path& dir, std::uint32_t worker_idx);

// Removes the pidfile if present. Ignores all errors: on the clean-exit
// path the worst outcome is a stale file, which the reader rejects.
void remove_worker_pidfile(const std::filesystem::path& dir, std::uint32_t worker_idx);

}  // namespace safety_crit::workers

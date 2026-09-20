#pragma once

#include <cstdint>
#include <filesystem>

namespace safety_crit::monitors {

// Reader side of the worker pidfile contract (T3.2, DEC-0010 #2); the
// writer and the path/format specification live in
// `safety_crit/workers/pidfile.hpp`. Existence alone is never proof of
// life: the pid is parsed, range-checked, and liveness-probed with
// kill(pid, 0); stale or malformed pidfiles are rejected (treated dead).
// The pid-reuse window (pid wrap) is the known accepted limitation.

// Must match the writer-side default (workers::kDefaultPidDir).
inline constexpr const char* kDefaultPidDir = "/tmp";

// Generous Linux pid ceiling (kernel default max 4194304); anything beyond
// is treated as a malformed file.
inline constexpr std::int64_t kPidMax = 4194304;

// Canonical path for one worker's pidfile. Must stay byte-identical to the
// writer-side naming (pinned by the fork integration tests).
std::filesystem::path worker_pid_path(const std::filesystem::path& dir, std::size_t worker_idx);

// Reads and validates the pidfile. Returns the pid (> 0) or 0 for
// missing/unreadable/malformed/out-of-range content.
std::int64_t read_worker_pid(const std::filesystem::path& dir, std::size_t worker_idx);

// Liveness verdict for one worker: pidfile parses AND kill(pid, 0)
// succeeds. EPERM (process exists, owned by another uid) counts as alive.
bool worker_process_alive(const std::filesystem::path& dir, std::size_t worker_idx);

}  // namespace safety_crit::monitors

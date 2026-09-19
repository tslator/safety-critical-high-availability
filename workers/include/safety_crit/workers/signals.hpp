#pragma once

#include <atomic>
#include <csignal>

namespace safety_crit::workers::signals {

// Process-global signal state (T2.2, DEC-0009 #3). The handler writes ONLY
// the volatile std::sig_atomic_t flag (async-signal-safe); the work loop
// consumes it between ticks. Nothing non-trivial happens in a handler.
struct SignalState {
    volatile std::sig_atomic_t stop_requested{0};
};

// SIGUSR1 forced-crash exit code (128 + signal, the shell convention).
// The crash hook terminates via _exit() -- async-signal-safe, immediate,
// no cleanup: this is the fault-injection surface Phase 5 drives.
inline constexpr int kCrashExitCode = 128 + 10;

// Install handlers: SIGTERM/SIGINT -> store stop_requested;
// SIGUSR1 -> _exit(kCrashExitCode). Returns false if sigaction fails.
// `state` must outlive the process (or at least every installed handler).
bool install(SignalState& state);

// Restore default dispositions for the three handled signals and detach the
// state pointer. Intended for tests/tests-teardown only.
void uninstall();

// Observability for tests: current handler-query after install() (whether a
// custom disposition is installed for SIGTERM/SIGINT/SIGUSR1).
struct HandlerSnapshot {
    bool stop_custom{false};
    bool crash_custom{false};
};
HandlerSnapshot query_handlers();

}  // namespace safety_crit::workers::signals

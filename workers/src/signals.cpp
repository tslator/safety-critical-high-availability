#include "safety_crit/workers/signals.hpp"

#include <csignal>
#include <cstdlib>

namespace {
using safety_crit::workers::signals::kCrashExitCode;
using safety_crit::workers::signals::SignalState;

// The handler may read this pointer (atomic, lock-free) and store a
// sig_atomic_t through it -- both async-signal-safe operations. The state
// object itself is owned by the caller and must outlive the handlers.
std::atomic<SignalState*> g_state{nullptr};

void stop_handler(int) {
    SignalState* state = g_state.load(std::memory_order_relaxed);
    if (state != nullptr) {
        state->stop_requested = 1;
    }
}

void crash_handler(int) {
    // Forced-crash hook (DEC-0009 #3): terminate immediately, no cleanup.
    // std::_Exit is async-signal-safe.
    std::_Exit(kCrashExitCode);
}

bool set_disposition(int signum, void (*handler)(int)) {
    struct sigaction action {};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;  // no SA_RESTART: a stop must break through pacing
    return sigaction(signum, &action, nullptr) == 0;
}

bool is_custom_disposition(int signum) {
    struct sigaction current {};
    if (sigaction(signum, nullptr, &current) != 0) {
        return false;
    }
    return current.sa_handler != SIG_DFL && current.sa_handler != SIG_IGN;
}
}  // namespace

namespace safety_crit::workers::signals {

bool install(SignalState& state) {
    g_state.store(&state, std::memory_order_relaxed);
    if (!set_disposition(SIGTERM, stop_handler)) {
        return false;
    }
    if (!set_disposition(SIGINT, stop_handler)) {
        return false;
    }
    return set_disposition(SIGUSR1, crash_handler);
}

void uninstall() {
    set_disposition(SIGTERM, SIG_DFL);
    set_disposition(SIGINT, SIG_DFL);
    set_disposition(SIGUSR1, SIG_DFL);
    g_state.store(nullptr, std::memory_order_relaxed);
}

HandlerSnapshot query_handlers() {
    HandlerSnapshot snap{};
    snap.stop_custom = is_custom_disposition(SIGTERM) && is_custom_disposition(SIGINT);
    snap.crash_custom = is_custom_disposition(SIGUSR1);
    return snap;
}

}  // namespace safety_crit::workers::signals

#include "test_framework.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <system_error>
#include <unistd.h>
#include <sys/wait.h>

#include "safety_crit/perturb/harness.hpp"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAFETY_CRIT_PERT_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SAFETY_CRIT_PERT_SANITIZED 1
#endif
#endif
#ifndef SAFETY_CRIT_PERT_SANITIZED
#define SAFETY_CRIT_PERT_SANITIZED 0
#endif

namespace {

using namespace safety_crit::perturb;

#if !SAFETY_CRIT_PERT_SANITIZED
pid_t spawn_sleeper() {
    const pid_t pid = ::fork();
    SAFETY_CRIT_ASSERT(pid >= 0);
    if (pid == 0) {
        for (;;) {
            ::pause();
        }
    }
    return pid;
}
#endif  // !SAFETY_CRIT_PERT_SANITIZED

#if !SAFETY_CRIT_PERT_SANITIZED
pid_t spawn_usr2_exiter() {
    int ready[2] = {-1, -1};
    SAFETY_CRIT_ASSERT(::pipe(ready) == 0);
    const pid_t pid = ::fork();
    SAFETY_CRIT_ASSERT(pid >= 0);
    if (pid == 0) {
        ::close(ready[0]);
        struct sigaction action{};
        action.sa_handler = [](int) { ::_exit(42); };
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(SIGUSR2, &action, nullptr);
        const char ready_byte = 'r';
        (void)::write(ready[1], &ready_byte, 1);
        ::close(ready[1]);
        for (;;) {
            ::pause();
        }
    }
    ::close(ready[1]);
    char ready_byte = 0;
    SAFETY_CRIT_ASSERT(::read(ready[0], &ready_byte, 1) == 1);
    ::close(ready[0]);
    return pid;
}
#endif  // !SAFETY_CRIT_PERT_SANITIZED

class CapturedSink {
public:
    CapturedSink() : buffer_(::tmpfile()) {
        SAFETY_CRIT_ASSERT(buffer_ != nullptr);
        set_record_sink(buffer_);
    }
    ~CapturedSink() {
        set_record_sink(nullptr);
        ::fclose(buffer_);
    }
    std::string text() {
        ::fflush(buffer_);
        ::fseek(buffer_, 0, SEEK_SET);
        std::string out;
        char chunk[256];
        std::size_t n = 0;
        while ((n = std::fread(chunk, 1, sizeof(chunk), buffer_)) > 0) {
            out.append(chunk, n);
        }
        ::fseek(buffer_, 0, SEEK_END);
        return out;
    }

private:
    FILE* buffer_{nullptr};
};

constexpr pid_t kImpossiblePid = 0x7FFFFFFF;

}  // namespace

#if !SAFETY_CRIT_PERT_SANITIZED
SAFETY_CRIT_TEST_CASE(Perturb, CrashSendsSegvAndRecords) {
    CapturedSink sink;
    const pid_t target = spawn_sleeper();
    std::error_code ec;
    SAFETY_CRIT_ASSERT(crash(target, ec));
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, 0) == target);
    SAFETY_CRIT_ASSERT(WIFSIGNALED(status));
    SAFETY_CRIT_ASSERT(WTERMSIG(status) == SIGSEGV);
    const std::string log = sink.text();
    SAFETY_CRIT_ASSERT(log.find("\"category\":\"crash\"") != std::string::npos);
    SAFETY_CRIT_ASSERT(log.find("\"target\":" + std::to_string(target)) != std::string::npos);
    SAFETY_CRIT_ASSERT(log.back() == '\n');
}

SAFETY_CRIT_TEST_CASE(Perturb, StallAndRecoverStallRoundTrip) {
    CapturedSink sink;
    const pid_t target = spawn_sleeper();
    std::error_code ec;
    SAFETY_CRIT_ASSERT(stall(target, ec));
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, WUNTRACED) == target);
    SAFETY_CRIT_ASSERT(WIFSTOPPED(status));
    SAFETY_CRIT_ASSERT(recover_stall(target, ec));
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, WCONTINUED) == target);
    SAFETY_CRIT_ASSERT(WIFCONTINUED(status));
    SAFETY_CRIT_ASSERT(::kill(target, SIGKILL) == 0);
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, 0) == target);
    const std::string log = sink.text();
    SAFETY_CRIT_ASSERT(log.find("\"category\":\"stall\"") != std::string::npos);
    SAFETY_CRIT_ASSERT(log.find("\"category\":\"recover-stall\"") != std::string::npos);
}

SAFETY_CRIT_TEST_CASE(Perturb, CorruptNextSlotSendsUsr2) {
    CapturedSink sink;
    const pid_t target = spawn_usr2_exiter();
    std::error_code ec;
    SAFETY_CRIT_ASSERT(corrupt_next_slot(target, ec));
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, 0) == target);
    SAFETY_CRIT_ASSERT(WIFEXITED(status));
    SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 42);
    const std::string log = sink.text();
    SAFETY_CRIT_ASSERT(log.find("\"category\":\"corrupt\"") != std::string::npos);
}

SAFETY_CRIT_TEST_CASE(Perturb, DoubleFaultKillsBothWithOneRecord) {
    CapturedSink sink;
    const pid_t first = spawn_sleeper();
    const pid_t second = spawn_sleeper();
    std::error_code ec;
    SAFETY_CRIT_ASSERT(double_fault(first, second, ec));
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(first, &status, 0) == first);
    SAFETY_CRIT_ASSERT(WIFSIGNALED(status));
    SAFETY_CRIT_ASSERT(WTERMSIG(status) == SIGKILL);
    SAFETY_CRIT_ASSERT(::waitpid(second, &status, 0) == second);
    SAFETY_CRIT_ASSERT(WIFSIGNALED(status));
    SAFETY_CRIT_ASSERT(WTERMSIG(status) == SIGKILL);
    const std::string log = sink.text();
    SAFETY_CRIT_ASSERT(log.find("\"category\":\"double-fault\"") != std::string::npos);
    SAFETY_CRIT_ASSERT(log.find("\"second\":" + std::to_string(second)) != std::string::npos);
    SAFETY_CRIT_ASSERT(std::count(log.begin(), log.end(), '\n') == 1);
}

SAFETY_CRIT_TEST_CASE(Perturb, KillSupervisorSendsSigkill) {
    CapturedSink sink;
    const pid_t target = spawn_sleeper();
    std::error_code ec;
    SAFETY_CRIT_ASSERT(kill_supervisor(target, ec));
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, 0) == target);
    SAFETY_CRIT_ASSERT(WIFSIGNALED(status));
    SAFETY_CRIT_ASSERT(WTERMSIG(status) == SIGKILL);
    SAFETY_CRIT_ASSERT(sink.text().find("\"category\":\"supervisor-kill\"") != std::string::npos);
}
#endif  // !SAFETY_CRIT_PERT_SANITIZED

SAFETY_CRIT_TEST_CASE(Perturb, InvalidTargetSetsErrorCode) {
    CapturedSink sink;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(!crash(kImpossiblePid, ec));
    SAFETY_CRIT_ASSERT(ec == std::errc::no_such_process);
    SAFETY_CRIT_ASSERT(!stall(kImpossiblePid, ec));
    SAFETY_CRIT_ASSERT(!corrupt_next_slot(kImpossiblePid, ec));
    SAFETY_CRIT_ASSERT(!double_fault(kImpossiblePid, kImpossiblePid - 1, ec));
    SAFETY_CRIT_ASSERT(!kill_supervisor(kImpossiblePid, ec));
    SAFETY_CRIT_ASSERT(sink.text().empty());  // failed actions record nothing
}

#if !SAFETY_CRIT_PERT_SANITIZED
SAFETY_CRIT_TEST_CASE(Perturb, SinkDisabledByDefault) {
    set_record_sink(nullptr);
    const pid_t target = spawn_sleeper();
    std::error_code ec;
    SAFETY_CRIT_ASSERT(stall(target, ec));
    int status = 0;
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, WUNTRACED) == target);
    SAFETY_CRIT_ASSERT(::kill(target, SIGKILL) == 0);
    SAFETY_CRIT_ASSERT(::waitpid(target, &status, 0) == target);
    // No sink: actions succeed silently and nothing can be observed.
    SAFETY_CRIT_ASSERT(true);
}
#endif  // !SAFETY_CRIT_PERT_SANITIZED

SAFETY_CRIT_TEST_CASE(Perturb, FormatsRecordLine) {
    std::string line;
    SAFETY_CRIT_ASSERT(format_record_line(
        Record{42, Category::kCrash, 7, std::string{}}, line));
    SAFETY_CRIT_ASSERT(line == "{\"ts\":42,\"category\":\"crash\",\"target\":7,\"params\":{}}\n");
    SAFETY_CRIT_ASSERT(format_record_line(
        Record{7, Category::kDoubleFault, 3, "{\"second\":9}"}, line));
    SAFETY_CRIT_ASSERT(
        line == "{\"ts\":7,\"category\":\"double-fault\",\"target\":3,\"params\":{\"second\":9}}\n");
}

SAFETY_CRIT_TEST_CASE(Perturb, ParsesInvocationArguments) {
    Invocation invocation;
    std::string error;
    {
        const char* argv[] = {"crash", "--target", "123"};
        SAFETY_CRIT_ASSERT(parse_invocation(3, argv, invocation, error));
        SAFETY_CRIT_ASSERT(invocation.category == Category::kCrash);
        SAFETY_CRIT_ASSERT(invocation.target == 123);
        SAFETY_CRIT_ASSERT(invocation.out_path.empty());
    }
    {
        const char* argv[] = {"double-fault", "--target", "10", "--target2", "20",
                              "--out", "/tmp/x.jsonl"};
        SAFETY_CRIT_ASSERT(parse_invocation(7, argv, invocation, error));
        SAFETY_CRIT_ASSERT(invocation.category == Category::kDoubleFault);
        SAFETY_CRIT_ASSERT(invocation.target == 10);
        SAFETY_CRIT_ASSERT(invocation.target2 == 20);
        SAFETY_CRIT_ASSERT(invocation.out_path == "/tmp/x.jsonl");
    }
    {
        const char* argv[] = {"stall"};  // missing --target
        SAFETY_CRIT_ASSERT(!parse_invocation(1, argv, invocation, error));
    }
    {
        const char* argv[] = {"nope", "--target", "1"};  // unknown category
        SAFETY_CRIT_ASSERT(!parse_invocation(3, argv, invocation, error));
    }
    {
        const char* argv[] = {"crash", "--target", "abc"};  // non-numeric pid
        SAFETY_CRIT_ASSERT(!parse_invocation(3, argv, invocation, error));
    }
    {
        const char* argv[] = {"crash", "--target", "1", "extra"};  // trailing token
        SAFETY_CRIT_ASSERT(!parse_invocation(4, argv, invocation, error));
    }
}

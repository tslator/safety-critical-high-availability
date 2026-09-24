#include "test_framework.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "safety_crit/observability/event_log.hpp"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAFETY_CRIT_OBS_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SAFETY_CRIT_OBS_SANITIZED 1
#endif
#endif
#ifndef SAFETY_CRIT_OBS_SANITIZED
#define SAFETY_CRIT_OBS_SANITIZED 0
#endif

namespace {

using namespace safety_crit::observability;

std::string temp_path(const char* tag) {
    return "/tmp/safety_crit_ha_eventlog_" + std::to_string(static_cast<long>(::getpid())) + "_" +
           tag + ".jsonl";
}

class TempLog {
public:
    explicit TempLog(const char* tag) : path_(temp_path(tag)) {
        ::unlink(path_.c_str());
    }
    ~TempLog() { ::unlink(path_.c_str()); }
    TempLog(const TempLog&) = delete;
    TempLog& operator=(const TempLog&) = delete;
    const std::string& path() const { return path_; }

private:
    std::string path_{};
};

// Writer options with a fixed clock and an fsync counter (the injected
// durability policy required by T-0033).
struct TestPolicy {
    EventLogWriterOptions options{};
    int sync_count{0};
    std::uint64_t ts{1000000000ULL};

    TestPolicy() {
        options.now = [this]() { return ts; };
        options.sync = [this](std::error_code&) {
            ++sync_count;
            return true;
        };
    }
};

std::size_t file_size(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 ? static_cast<std::size_t>(st.st_size) : 0;
}

void append_raw(const std::string& path, const std::string& bytes) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    SAFETY_CRIT_ASSERT(fd >= 0);
    const ssize_t written = ::write(fd, bytes.data(), bytes.size());
    SAFETY_CRIT_ASSERT(written == static_cast<ssize_t>(bytes.size()));
    ::close(fd);
}

// Drains a reader to kEnd, collecting the kOk record count; asserts no
// kError status was seen.
std::size_t drain(EventLogReader& reader, std::size_t& incomplete_count) {
    std::size_t ok = 0;
    incomplete_count = 0;
    for (;;) {
        EventRecord record{};
        std::string error;
        const ReadStatus status = reader.read_next(record, error);
        if (status == ReadStatus::kError) {
            SAFETY_CRIT_ASSERT(!"unexpected reader error");
            return ok;
        }
        if (status == ReadStatus::kEnd) {
            return ok;
        }
        if (status == ReadStatus::kIncomplete) {
            ++incomplete_count;
            return ok;
        }
        ++ok;
    }
}

}  // namespace

SAFETY_CRIT_TEST_CASE(EventLog, HappyPathWriteRead) {
    TempLog log("happy");
    TestPolicy policy{};
    std::error_code ec;
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(log.path(), "worker", policy.options, ec));
    SAFETY_CRIT_ASSERT(writer.next_seq() == 1);
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "worker_started", "\"ring\":0", ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kWarn, "worker_deadline_overrun", "\"ticks\":7", ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kError, "worker_stopped", "\"reason\":\"sigterm\"",
                                     ec));
    SAFETY_CRIT_ASSERT(writer.next_seq() == 4);

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    std::size_t incomplete = 0;
    SAFETY_CRIT_ASSERT(drain(reader, incomplete) == 3);
    SAFETY_CRIT_ASSERT(incomplete == 0);
    const SequenceState* state = reader.state_for("worker");
    SAFETY_CRIT_ASSERT(state != nullptr);
    SAFETY_CRIT_ASSERT(state->expected_seq == 4);
    SAFETY_CRIT_ASSERT(state->gaps == 0);
    SAFETY_CRIT_ASSERT(reader.gaps_total() == 0);
}

SAFETY_CRIT_TEST_CASE(EventLog, RecordFieldsRoundTrip) {
    TempLog log("fields");
    TestPolicy policy{};
    policy.ts = 1234567890123ULL;
    std::error_code ec;
    EventLogWriterOptions options = policy.options;
    options.instance = 3;
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(log.path(), "worker", options, ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kError, "failover_recovered",
                                     "\"latency_ms\":42", ec));

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    EventRecord record{};
    std::string error;
    SAFETY_CRIT_ASSERT(reader.read_next(record, error) == ReadStatus::kOk);
    SAFETY_CRIT_ASSERT(record.ts_ns == policy.ts);
    SAFETY_CRIT_ASSERT(record.level == LogLevel::kError);
    SAFETY_CRIT_ASSERT(record.component == "worker");
    SAFETY_CRIT_ASSERT(record.seq == 1);
    SAFETY_CRIT_ASSERT(record.event == "failover_recovered");
    SAFETY_CRIT_ASSERT(record.instance.has_value());
    SAFETY_CRIT_ASSERT(record.instance.value() == 3);
    SAFETY_CRIT_ASSERT(record.raw.find("\"latency_ms\":42") != std::string::npos);
    SAFETY_CRIT_ASSERT(reader.state_for("worker/3") != nullptr);
}

SAFETY_CRIT_TEST_CASE(EventLog, OversizedRecordRejected) {
    TempLog log("oversize");
    TestPolicy policy{};
    std::error_code ec;
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(log.path(), "monitor", policy.options, ec));
    const std::string huge_event(kMaxEventRecordBytes, 'x');
    SAFETY_CRIT_ASSERT(!writer.append(LogLevel::kInfo, huge_event, "", ec));
    SAFETY_CRIT_ASSERT(ec == std::errc::file_too_large);
    // Slightly-too-small-is-ok check: an extra-field payload that pushes the
    // record past 4096 bytes must also be rejected.
    const std::string big_extra = "\"pad\":\"" + std::string(kMaxEventRecordBytes, 'p') + "\"";
    SAFETY_CRIT_ASSERT(!writer.append(LogLevel::kInfo, "tick", big_extra, ec));
    // Rejection never splits a record and never burns a sequence number.
    SAFETY_CRIT_ASSERT(file_size(log.path()) == 0);
    SAFETY_CRIT_ASSERT(writer.next_seq() == 1);
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "tick", "", ec));
    SAFETY_CRIT_ASSERT(writer.next_seq() == 2);
    SAFETY_CRIT_ASSERT(file_size(log.path()) > 0);
}

SAFETY_CRIT_TEST_CASE(EventLog, SeqRecoveryAfterReopen) {
    TempLog log("reopen");
    TestPolicy policy{};
    std::error_code ec;
    {
        EventLogWriter first{};
        SAFETY_CRIT_ASSERT(first.open(log.path(), "supervisor", policy.options, ec));
        SAFETY_CRIT_ASSERT(first.append(LogLevel::kInfo, "failover_started", "", ec));
        SAFETY_CRIT_ASSERT(first.append(LogLevel::kInfo, "ring_degraded", "", ec));
        SAFETY_CRIT_ASSERT(first.append(LogLevel::kInfo, "failover_recovered", "", ec));
    }
    // Simulated restart: a fresh writer recovers the sequence from the file.
    EventLogWriter second{};
    SAFETY_CRIT_ASSERT(second.open(log.path(), "supervisor", policy.options, ec));
    SAFETY_CRIT_ASSERT(second.next_seq() == 4);
    SAFETY_CRIT_ASSERT(second.append(LogLevel::kInfo, "shutdown_summary", "", ec));

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    std::size_t incomplete = 0;
    SAFETY_CRIT_ASSERT(drain(reader, incomplete) == 4);
    const SequenceState* state = reader.state_for("supervisor");
    SAFETY_CRIT_ASSERT(state != nullptr);
    SAFETY_CRIT_ASSERT(state->gaps == 0);
    SAFETY_CRIT_ASSERT(state->missed == 0);
    SAFETY_CRIT_ASSERT(state->expected_seq == 5);
}

SAFETY_CRIT_TEST_CASE(EventLog, InstanceKeysRecoverIndependently) {
    TempLog log("instances");
    TestPolicy policy{};
    std::error_code ec;
    EventLogWriterOptions w0 = policy.options;
    w0.instance = 0;
    EventLogWriterOptions w1 = policy.options;
    w1.instance = 1;
    {
        EventLogWriter a{};
        SAFETY_CRIT_ASSERT(a.open(log.path(), "worker", w0, ec));
        SAFETY_CRIT_ASSERT(a.append(LogLevel::kInfo, "worker_started", "", ec));
        EventLogWriter b{};
        SAFETY_CRIT_ASSERT(b.open(log.path(), "worker", w1, ec));
        SAFETY_CRIT_ASSERT(b.next_seq() == 1);  // not polluted by instance 0
        SAFETY_CRIT_ASSERT(b.append(LogLevel::kInfo, "worker_started", "", ec));
        SAFETY_CRIT_ASSERT(b.append(LogLevel::kInfo, "tick", "", ec));
    }
    EventLogWriter a2{};
    SAFETY_CRIT_ASSERT(a2.open(log.path(), "worker", w0, ec));
    SAFETY_CRIT_ASSERT(a2.next_seq() == 2);  // only instance 0's one record
    SAFETY_CRIT_ASSERT(a2.append(LogLevel::kInfo, "tick", "", ec));

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    std::size_t incomplete = 0;
    SAFETY_CRIT_ASSERT(drain(reader, incomplete) == 4);
    const SequenceState* s0 = reader.state_for("worker/0");
    const SequenceState* s1 = reader.state_for("worker/1");
    SAFETY_CRIT_ASSERT(s0 != nullptr);
    SAFETY_CRIT_ASSERT(s1 != nullptr);
    SAFETY_CRIT_ASSERT(s0->gaps == 0);
    SAFETY_CRIT_ASSERT(s0->expected_seq == 3);
    SAFETY_CRIT_ASSERT(s1->gaps == 0);
    SAFETY_CRIT_ASSERT(s1->expected_seq == 3);
    SAFETY_CRIT_ASSERT(reader.state_for("worker") == nullptr);
}

SAFETY_CRIT_TEST_CASE(EventLog, GapDetection) {
    TempLog log("gap");
    // Hand-crafted file with seq 1, 2, 5 (3 and 4 missing) and a second
    // component with a clean sequence.
    append_raw(log.path(),
               "{\"schema\":1,\"ts\":1,\"level\":\"info\",\"component\":\"monitor\",\"seq\":1,"
               "\"event\":\"worker_crashed\"}\n"
               "{\"schema\":1,\"ts\":2,\"level\":\"info\",\"component\":\"monitor\",\"seq\":2,"
               "\"event\":\"worker_recovered\"}\n"
               "{\"schema\":1,\"ts\":3,\"level\":\"info\",\"component\":\"monitor\",\"seq\":5,"
               "\"event\":\"worker_stalled\"}\n"
               "{\"schema\":1,\"ts\":4,\"level\":\"info\",\"component\":\"supervisor\",\"seq\":1,"
               "\"event\":\"failover_started\"}\n");
    std::error_code ec;
    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    std::size_t incomplete = 0;
    SAFETY_CRIT_ASSERT(drain(reader, incomplete) == 4);
    const SequenceState* monitor = reader.state_for("monitor");
    SAFETY_CRIT_ASSERT(monitor != nullptr);
    SAFETY_CRIT_ASSERT(monitor->gaps == 1);
    SAFETY_CRIT_ASSERT(monitor->missed == 2);  // seq 3 and 4 dropped
    SAFETY_CRIT_ASSERT(monitor->expected_seq == 6);
    const SequenceState* supervisor = reader.state_for("supervisor");
    SAFETY_CRIT_ASSERT(supervisor != nullptr);
    SAFETY_CRIT_ASSERT(supervisor->gaps == 0);
    SAFETY_CRIT_ASSERT(reader.gaps_total() == 1);
}

SAFETY_CRIT_TEST_CASE(EventLog, TornTrailingLineTolerated) {
    TempLog log("torn");
    TestPolicy policy{};
    std::error_code ec;
    {
        EventLogWriter writer{};
        SAFETY_CRIT_ASSERT(writer.open(log.path(), "monitor", policy.options, ec));
        SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "a", "", ec));
        SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "b", "", ec));
    }
    const std::size_t complete_size = file_size(log.path());
    // Simulate a process killed mid-append: partial line, no newline.
    append_raw(log.path(), "{\"schema\":1,\"ts\":9,\"level\":\"inf");

    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    std::size_t incomplete = 0;
    SAFETY_CRIT_ASSERT(drain(reader, incomplete) == 2);
    SAFETY_CRIT_ASSERT(incomplete == 1);
    SAFETY_CRIT_ASSERT(reader.watermark() == complete_size);

    // Tail-follow: the line completes later and becomes readable from the
    // persisted watermark without re-reading anything.
    append_raw(log.path(), "o\",\"component\":\"monitor\",\"seq\":3,\"event\":\"c\"}\n");
    const std::uint64_t resume = reader.watermark();
    EventLogReader follower{};
    SAFETY_CRIT_ASSERT(follower.open(log.path(), ec));
    SAFETY_CRIT_ASSERT(follower.seek(resume, ec));
    EventRecord record{};
    std::string error;
    SAFETY_CRIT_ASSERT(follower.read_next(record, error) == ReadStatus::kOk);
    SAFETY_CRIT_ASSERT(record.event == "c");
    SAFETY_CRIT_ASSERT(record.seq == 3);
}

SAFETY_CRIT_TEST_CASE(EventLog, EmptyFile) {
    TempLog log("empty");
    append_raw(log.path(), "");  // create only
    std::error_code ec;
    EventLogReader reader{};
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    std::size_t incomplete = 0;
    SAFETY_CRIT_ASSERT(drain(reader, incomplete) == 0);
    SAFETY_CRIT_ASSERT(incomplete == 0);
    SAFETY_CRIT_ASSERT(reader.gaps_total() == 0);

    // A writer opening an empty (or absent) log starts at seq 1.
    TestPolicy policy{};
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(log.path(), "monitor", policy.options, ec));
    SAFETY_CRIT_ASSERT(writer.next_seq() == 1);
    TempLog absent("absent");  // guarantees the path does not exist
    EventLogWriter fresh{};
    SAFETY_CRIT_ASSERT(fresh.open(absent.path(), "monitor", policy.options, ec));
    SAFETY_CRIT_ASSERT(fresh.next_seq() == 1);
}

SAFETY_CRIT_TEST_CASE(EventLog, FsyncPolicyFollowsLevel) {
    TempLog log("fsync");
    TestPolicy policy{};
    std::error_code ec;
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(writer.open(log.path(), "monitor", policy.options, ec));
    SAFETY_CRIT_ASSERT(policy.sync_count == 0);
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "tick", "", ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "tick", "", ec));
    SAFETY_CRIT_ASSERT(policy.sync_count == 0);  // info: no fsync barrier
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kWarn, "worker_stalled", "", ec));
    SAFETY_CRIT_ASSERT(policy.sync_count == 1);  // warn: exactly one
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kError, "worker_crashed", "", ec));
    SAFETY_CRIT_ASSERT(policy.sync_count == 2);  // error: exactly one
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "tick", "", ec));
    SAFETY_CRIT_ASSERT(policy.sync_count == 2);
}

SAFETY_CRIT_TEST_CASE(EventLog, UnsupportedSchemaRejected) {
    TempLog log("schema");
    append_raw(log.path(),
               "{\"schema\":2,\"ts\":1,\"level\":\"info\",\"component\":\"monitor\",\"seq\":1,"
               "\"event\":\"x\"}\n");
    std::string error;
    EventRecord record{};
    SAFETY_CRIT_ASSERT(!parse_event_record(
        "{\"schema\":2,\"ts\":1,\"level\":\"info\",\"component\":\"monitor\",\"seq\":1,"
        "\"event\":\"x\"}",
        record, error));

    // A writer must refuse to open over a log it cannot understand.
    TestPolicy policy{};
    std::error_code ec;
    EventLogWriter writer{};
    SAFETY_CRIT_ASSERT(!writer.open(log.path(), "monitor", policy.options, ec));
    SAFETY_CRIT_ASSERT(ec == std::errc::protocol_error);

    // Unknown extra fields are forward-compatible within schema 1.
    SAFETY_CRIT_ASSERT(parse_event_record(
        "{\"schema\":1,\"ts\":1,\"level\":\"info\",\"component\":\"monitor\",\"seq\":1,"
        "\"event\":\"x\",\"future_field\":[1,2]}",
        record, error));
    SAFETY_CRIT_ASSERT(record.event == "x");
}

#if !SAFETY_CRIT_OBS_SANITIZED
SAFETY_CRIT_TEST_CASE(EventLog, ConcurrentWriterAtomicity) {
    // T-0033 AC: >= 4 processes x >= 100 interleaved records append to one
    // file; the result must parse line-by-line with zero mixed records and
    // gap-free per-instance sequences. Plain builds only (fork idiom per
    // G1.4/G2.3 precedent).
    TempLog log("concurrent");
    constexpr int kWriters = 4;
    constexpr int kRecords = 100;

    for (int w = 0; w < kWriters; ++w) {
        const pid_t pid = ::fork();
        SAFETY_CRIT_ASSERT(pid >= 0);
        if (pid == 0) {
            (void)::prctl(PR_SET_PDEATHSIG, SIGKILL);
            EventLogWriterOptions options{};
            options.instance = w;
            EventLogWriter writer{};
            std::error_code ec;
            int rc = 1;
            if (writer.open(log.path(), "worker", options, ec)) {
                rc = 0;
                for (int i = 0; i < kRecords; ++i) {
                    const std::string extra = "\"payload\":" + std::to_string(i);
                    if (!writer.append(LogLevel::kInfo, "tick", extra, ec)) {
                        rc = 2;
                        break;
                    }
                }
            }
            ::_exit(rc);
        }
    }
    for (int w = 0; w < kWriters; ++w) {
        int status = 0;
        SAFETY_CRIT_ASSERT(::waitpid(-1, &status, 0) > 0);
        SAFETY_CRIT_ASSERT(WIFEXITED(status));
        SAFETY_CRIT_ASSERT(WEXITSTATUS(status) == 0);
    }

    EventLogReader reader{};
    std::error_code ec;
    SAFETY_CRIT_ASSERT(reader.open(log.path(), ec));
    std::size_t incomplete = 0;
    SAFETY_CRIT_ASSERT(drain(reader, incomplete) == kWriters * kRecords);
    SAFETY_CRIT_ASSERT(incomplete == 0);
    SAFETY_CRIT_ASSERT(reader.gaps_total() == 0);
    for (int w = 0; w < kWriters; ++w) {
        const SequenceState* state =
            reader.state_for("worker/" + std::to_string(static_cast<long>(w)));
        SAFETY_CRIT_ASSERT(state != nullptr);
        SAFETY_CRIT_ASSERT(state->gaps == 0);
        SAFETY_CRIT_ASSERT(state->missed == 0);
        SAFETY_CRIT_ASSERT(state->expected_seq == kRecords + 1);
    }
}
#endif  // !SAFETY_CRIT_OBS_SANITIZED

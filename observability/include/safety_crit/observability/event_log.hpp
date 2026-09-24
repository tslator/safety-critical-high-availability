// Certification-grade event log, schema v1 (T-0033, DEC-0014 §3).
//
// One consolidated append-only JSON-lines event log per deployment. Every
// record is one flat JSON object per line, terminated by '\n', the whole
// record (including the newline) at most kMaxEventRecordBytes bytes:
//
//   {"schema":1,"ts":<unix-ns>,"level":"info|warn|error",
//    "component":"monitor|supervisor|worker|observability|perturb",
//    "seq":<uint64, per component instance, starts at 1>,
//    "event":"<name>"[,"instance":<int>][,<extra flat fields>]}
//
//   - schema:   record-level version marker (every record carries it, like
//               the replay-log header idiom); field changes require a
//               version bump and a decision (DEC-0014 maintenance rule).
//   - ts:       CLOCK_REALTIME nanoseconds from the writer's clock
//               (injectable; fake clock in tests).
//   - level:    severity; the writer fsyncs synchronously on
//               level >= warn. Info records rely on process-crash
//               durability only (DEC-0014 §3; host-crash durability of
//               info records is explicitly out of scope).
//   - component: emitting component. `component` + optional `instance`
//               (worker index; omitted when the component is unique) form
//               the sequence key. Keys are per component instance, never
//               per pid, so gaps stay detectable across restarts.
//   - seq:      monotonic per sequence key. A writer recovers its next seq
//               at open by tail-scanning the existing file for its own key,
//               so restarts never reset or duplicate the sequence.
//   - event:    published vocabulary name (additions additive; renames or
//               removals require a decision, DEC-0010 rule).
//   - extra:    flat "key":value pairs owned by the caller (snprintf
//               formatted), comma-separated, no leading comma; the log
//               layer never nests objects in them.
//
// Atomicity: every record is exactly one write() to an O_APPEND | O_CREAT |
// O_CLOEXEC file. POSIX appends of at most kMaxEventRecordBytes (4096,
// PIPE_BUF) are atomic on local filesystems, so concurrent processes cannot
// interleave records. Oversized records are a writer error (never split);
// short writes are a writer error too (never partially committed).
//
// Reader contract: sequential scan with per-key sequence continuity
// validation (gap and out-of-order counts), torn-trailing-line tolerance
// (a final line without '\n' yields kIncomplete, not an error, and the
// watermark stays before it so a tail-following reader retries it), and
// watermark (byte offset past the last complete record read) tracking.
//
// Error handling follows the standing idiom: bool fn(..., T& out,
// std::error_code& ec). This library is single-threaded per process
// instance; no shared state is shared between writer and reader objects.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace safety_crit::observability {

inline constexpr std::uint32_t kEventSchemaVersion = 1u;

// Maximum record size including the trailing newline. Doubles as the
// atomic-append bound (PIPE_BUF) and the writer's fixed format buffer.
inline constexpr std::size_t kMaxEventRecordBytes = 4096u;

enum class LogLevel : std::uint8_t {
    kInfo = 0,
    kWarn = 1,   // fsync barrier
    kError = 2,  // fsync barrier
};

// Canonical level name ("info"|"warn"|"error"); false if `level` is not a
// valid enumerant.
bool level_to_name(LogLevel level, const char*& name);

// Parses a canonical level name; false if unknown.
bool level_from_name(std::string_view name, LogLevel& level);

// One parsed record. `raw` holds the full original line (without the
// trailing newline) for verbatim forwarding and diagnostics.
struct EventRecord {
    std::uint64_t ts_ns{0};
    LogLevel level{LogLevel::kInfo};
    std::string component{};
    std::uint64_t seq{0};
    std::string event{};
    std::optional<std::int64_t> instance{};
    std::string raw{};
};

// Parses one schema v1 record line. Unknown fields are ignored (forward
// compatibility within schema 1); structural violations are rejected with a
// description in `error`.
bool parse_event_record(std::string_view line, EventRecord& record, std::string& error);

// Sequence-continuity state for one key ("component" or "component/instance").
struct SequenceState {
    std::uint64_t expected_seq{1};
    std::uint64_t gaps{0};    // records that jumped ahead or went backwards
    std::uint64_t missed{0};  // sequence numbers skipped by forward jumps
};

// Writer for one component (optionally one instance of it). One writer
// object per process; not thread-safe by design (DEC-0014 §3).
struct EventLogWriterOptions {
    // Sequence-key discriminator emitted as `"instance":N`; empty for
    // singleton components (monitor, supervisor, observability).
    std::optional<std::int64_t> instance{};
    // Injectable clock returning unix nanoseconds; default CLOCK_REALTIME.
    std::function<std::uint64_t()> now{};
    // Injectable durability hook invoked after every record with
    // level >= warn; default ::fsync on the log fd. Observability of the
    // call count is a test requirement, hence injection rather than direct
    // syscall.
    std::function<bool(std::error_code& ec)> sync{};
};

class EventLogWriter {
public:
    EventLogWriter() = default;
    ~EventLogWriter();
    EventLogWriter(const EventLogWriter&) = delete;
    EventLogWriter& operator=(const EventLogWriter&) = delete;

    // Opens (creating if absent) the log at `path` and tail-scans it to
    // recover the next seq for this component instance. A file with an
    // unsupported schema version or a malformed non-tail record is a
    // startup error (ec = EPROTO). Tolerates a torn trailing line.
    bool open(std::string_view path, std::string_view component,
              const EventLogWriterOptions& options, std::error_code& ec);

    // Appends one record. `extra_fields` must be empty or a valid JSON
    // fragment of `"key":value` pairs (caller-formatted; validated to
    // contain no newlines or unbalanced brace). On failure nothing is
    // written and `next_seq()` is unchanged.
    bool append(LogLevel level, std::string_view event, std::string_view extra_fields,
                std::error_code& ec);

    std::uint64_t next_seq() const { return next_seq_; }
    bool is_open() const { return fd_ >= 0; }
    void close();

private:
    int fd_{-1};
    std::string component_{};
    EventLogWriterOptions options_{};
    std::uint64_t next_seq_{1};
};

enum class ReadStatus : std::uint8_t {
    kOk,        // one complete record returned
    kEnd,       // nothing more available now (tail-follow may retry later)
    kIncomplete,  // torn trailing line; watermark unchanged, retry later
    kError,     // I/O error or malformed record (description in `error`)
};

// Sequential reader with continuity validation and tail-follow watermarks.
class EventLogReader {
public:
    EventLogReader() = default;
    ~EventLogReader();
    EventLogReader(const EventLogReader&) = delete;
    EventLogReader& operator=(const EventLogReader&) = delete;

    bool open(std::string_view path, std::error_code& ec);

    // Reads the next complete record, updating per-key continuity state.
    ReadStatus read_next(EventRecord& record, std::string& error);

    // Byte offset just past the last record returned with kOk. A
    // tail-following consumer persists this and reopens/ seeks to it.
    std::uint64_t watermark() const { return watermark_; }

    // Repositions the scan (and drops buffered bytes) to `offset`; the
    // continuity state is kept, so a persisted watermark resumes a
    // gap-counting stream without double counting.
    bool seek(std::uint64_t offset, std::error_code& ec);

    // Continuity access keyed by "component" or "component/<instance>".
    const SequenceState* state_for(std::string_view key) const;
    std::uint64_t gaps_total() const;

    void close();

private:
    static std::string state_key(const EventRecord& record);

    int fd_{-1};
    std::string line_{};      // bytes after the watermark not yet line-delimited
    std::uint64_t watermark_{0};
    bool eof_reached_{false};
    std::map<std::string, SequenceState> states_{};
};

}  // namespace safety_crit::observability

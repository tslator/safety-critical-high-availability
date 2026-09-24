#include "safety_crit/observability/event_log.hpp"

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

namespace safety_crit::observability {
namespace {

bool extract_number(std::string_view line, std::string_view key, std::int64_t& out) {
    const std::size_t at = line.find(key);
    if (at == std::string_view::npos) {
        return false;
    }
    const std::size_t start = at + key.size();
    const auto result = std::from_chars(line.data() + start, line.data() + line.size(), out);
    return result.ec == std::errc{};
}

bool extract_string(std::string_view line, std::string_view key, std::string& out) {
    const std::size_t at = line.find(key);
    if (at == std::string_view::npos) {
        return false;
    }
    // `key` ends at the opening quote of the value.
    const std::size_t start = at + key.size();
    const std::size_t end = line.find('"', start);
    if (end == std::string_view::npos) {
        return false;
    }
    out = std::string(line.substr(start, end - start));
    return true;
}

bool is_component_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == '-';
}

bool is_valid_component(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    for (const char c : value) {
        if (!is_component_char(c)) {
            return false;
        }
    }
    return true;
}

std::uint64_t realtime_ns() {
    struct timespec spec{};
    clock_gettime(CLOCK_REALTIME, &spec);
    return static_cast<std::uint64_t>(spec.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(spec.tv_nsec);
}

}  // namespace

bool level_to_name(LogLevel level, const char*& name) {
    switch (level) {
        case LogLevel::kInfo:
            name = "info";
            return true;
        case LogLevel::kWarn:
            name = "warn";
            return true;
        case LogLevel::kError:
            name = "error";
            return true;
    }
    return false;
}

bool level_from_name(std::string_view name, LogLevel& level) {
    if (name == "info") {
        level = LogLevel::kInfo;
        return true;
    }
    if (name == "warn") {
        level = LogLevel::kWarn;
        return true;
    }
    if (name == "error") {
        level = LogLevel::kError;
        return true;
    }
    return false;
}

bool parse_event_record(std::string_view line, EventRecord& record, std::string& error) {
    record = EventRecord{};
    if (line.empty() || line.front() != '{' || line.back() != '}') {
        error = "event-log: record is not a JSON object";
        return false;
    }
    std::int64_t schema = 0;
    if (!extract_number(line, "\"schema\":", schema) || schema != kEventSchemaVersion) {
        error = "event-log: missing or unsupported schema version";
        return false;
    }
    std::int64_t ts = 0;
    if (!extract_number(line, "\"ts\":", ts)) {
        error = "event-log: missing or malformed \"ts\"";
        return false;
    }
    std::string level_name;
    if (!extract_string(line, "\"level\":\"", level_name)) {
        error = "event-log: missing or malformed \"level\"";
        return false;
    }
    if (!level_from_name(level_name, record.level)) {
        error = "event-log: unknown level \"" + level_name + "\"";
        return false;
    }
    if (!extract_string(line, "\"component\":\"", record.component)) {
        error = "event-log: missing or malformed \"component\"";
        return false;
    }
    std::int64_t seq = 0;
    if (!extract_number(line, "\"seq\":", seq) || seq < 0) {
        error = "event-log: missing or malformed \"seq\"";
        return false;
    }
    if (!extract_string(line, "\"event\":\"", record.event)) {
        error = "event-log: missing or malformed \"event\"";
        return false;
    }
    std::int64_t instance = 0;
    if (extract_number(line, "\"instance\":", instance)) {
        record.instance = instance;
    }
    record.ts_ns = static_cast<std::uint64_t>(ts);
    record.seq = static_cast<std::uint64_t>(seq);
    record.raw = std::string(line);
    return true;
}

EventLogWriter::~EventLogWriter() {
    close();
}

bool EventLogWriter::open(std::string_view path, std::string_view component,
                          const EventLogWriterOptions& options, std::error_code& ec) {
    close();
    if (!is_valid_component(component)) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    // Recovery scan (DEC-0014 §3): the next seq is one past the highest seq
    // already recorded under this component instance's key. A torn trailing
    // line is tolerated; any other structural violation is a startup error.
    std::uint64_t max_seen = 0;
    {
        EventLogReader scan{};
        std::string scan_error;
        if (!scan.open(path, ec)) {
            if (ec == std::errc::no_such_file_or_directory) {
                ec.clear();  // fresh log; sequence starts at 1
                max_seen = 0;
            } else {
                return false;
            }
        } else {
            for (;;) {
                EventRecord record{};
                const ReadStatus status = scan.read_next(record, scan_error);
                if (status == ReadStatus::kEnd || status == ReadStatus::kIncomplete) {
                    break;
                }
                if (status == ReadStatus::kError) {
                    ec = std::make_error_code(std::errc::protocol_error);
                    return false;
                }
                if (record.component == component && record.instance == options.instance &&
                    record.seq > max_seen) {
                    max_seen = record.seq;
                }
            }
        }
    }

    const int fd = ::open(std::string(path).c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC,
                          0644);
    if (fd < 0) {
        ec = std::make_error_code(static_cast<std::errc>(errno));
        return false;
    }
    fd_ = fd;
    component_ = std::string(component);
    options_ = options;
    next_seq_ = max_seen + 1;
    if (!options_.now) {
        options_.now = realtime_ns;
    }
    if (!options_.sync) {
        const int sync_fd = fd_;
        options_.sync = [sync_fd](std::error_code& sync_ec) {
            if (::fsync(sync_fd) != 0) {
                sync_ec = std::make_error_code(static_cast<std::errc>(errno));
                return false;
            }
            return true;
        };
    }
    return true;
}

bool EventLogWriter::append(LogLevel level, std::string_view event, std::string_view extra_fields,
                            std::error_code& ec) {
    if (fd_ < 0) {
        ec = std::make_error_code(std::errc::bad_file_descriptor);
        return false;
    }
    const char* level_name = nullptr;
    if (!level_to_name(level, level_name)) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (!is_valid_component(event)) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    // Extra fields are caller-formatted JSON pairs: reject anything that
    // could break out of the record (newlines) or close it early.
    if (extra_fields.find('\n') != std::string_view::npos ||
        extra_fields.find('}') != std::string_view::npos) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (!extra_fields.empty() && extra_fields.front() != '"') {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    // Optional segments are comma-prefixed so the record stays valid JSON
    // with either, both, or neither present.
    char instance_part[48] = "";
    if (options_.instance) {
        std::snprintf(instance_part, sizeof(instance_part), ",\"instance\":%lld",
                      static_cast<long long>(*options_.instance));
    }
    std::string extra_part;
    if (!extra_fields.empty()) {
        extra_part = "," + std::string(extra_fields);
    }
    const std::uint64_t ts = options_.now();
    const std::uint64_t seq = next_seq_;
    char line[kMaxEventRecordBytes];
    const int n = std::snprintf(line, sizeof(line),
                                "{\"schema\":1,\"ts\":%llu,\"level\":\"%s\",\"component\":\"%s\","
                                "\"seq\":%llu,\"event\":\"%s\"%s%s}\n",
                                static_cast<unsigned long long>(ts), level_name, component_.c_str(),
                                static_cast<unsigned long long>(seq), std::string(event).c_str(),
                                instance_part, extra_part.c_str());
    // Oversized records are an error and are never split (DEC-0014 §3);
    // snprintf reports the length it would have written.
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(line)) {
        ec = std::make_error_code(std::errc::file_too_large);
        return false;
    }

    // Exactly one write() per record for atomic O_APPEND appends; EINTR may
    // retry the identical buffer, any other short write fails the record
    // (never partially committed).
    for (;;) {
        const ssize_t written = ::write(fd_, line, static_cast<std::size_t>(n));
        if (written == static_cast<ssize_t>(n)) {
            break;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        ec = std::make_error_code(written < 0 ? static_cast<std::errc>(errno)
                                              : std::errc::io_error);
        return false;
    }

    if (level >= LogLevel::kWarn) {
        if (!options_.sync(ec)) {
            return false;
        }
    }
    next_seq_ = seq + 1;
    return true;
}

void EventLogWriter::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

EventLogReader::~EventLogReader() {
    close();
}

bool EventLogReader::open(std::string_view path, std::error_code& ec) {
    close();
    const int fd = ::open(std::string(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ec = std::make_error_code(static_cast<std::errc>(errno));
        return false;
    }
    fd_ = fd;
    return true;
}

ReadStatus EventLogReader::read_next(EventRecord& record, std::string& error) {
    for (;;) {
        const std::size_t nl = line_.find('\n');
        if (nl != std::string::npos) {
            const std::string raw = line_.substr(0, nl);
            line_.erase(0, nl + 1);
            if (raw.empty()) {
                continue;  // blank line between records: skip, not a record
            }
            EventRecord parsed{};
            if (!parse_event_record(raw, parsed, error)) {
                return ReadStatus::kError;
            }
            watermark_ += static_cast<std::uint64_t>(raw.size()) + 1;
            SequenceState& state = states_[state_key(parsed)];
            if (parsed.seq >= state.expected_seq) {
                if (parsed.seq > state.expected_seq) {
                    ++state.gaps;
                    state.missed += parsed.seq - state.expected_seq;
                }
                state.expected_seq = parsed.seq + 1;
            } else {
                ++state.gaps;  // out-of-order record for this key
            }
            record = std::move(parsed);
            return ReadStatus::kOk;
        }
        if (eof_reached_) {
            if (!line_.empty()) {
                return ReadStatus::kIncomplete;  // torn trailing line, retryable
            }
            return ReadStatus::kEnd;
        }
        char chunk[4096];
        const ssize_t n = ::read(fd_, chunk, sizeof(chunk));
        if (n == 0) {
            eof_reached_ = true;
            continue;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::strerror(errno);
            return ReadStatus::kError;
        }
        line_.append(chunk, static_cast<std::size_t>(n));
    }
}

bool EventLogReader::seek(std::uint64_t offset, std::error_code& ec) {
    if (fd_ < 0) {
        ec = std::make_error_code(std::errc::bad_file_descriptor);
        return false;
    }
    if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) {
        ec = std::make_error_code(static_cast<std::errc>(errno));
        return false;
    }
    line_.clear();
    watermark_ = offset;
    eof_reached_ = false;
    return true;
}

const SequenceState* EventLogReader::state_for(std::string_view key) const {
    const auto it = states_.find(std::string(key));
    return it == states_.end() ? nullptr : &it->second;
}

std::uint64_t EventLogReader::gaps_total() const {
    std::uint64_t total = 0;
    for (const auto& entry : states_) {
        total += entry.second.gaps;
    }
    return total;
}

void EventLogReader::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    line_.clear();
    watermark_ = 0;
    eof_reached_ = false;
}

std::string EventLogReader::state_key(const EventRecord& record) {
    if (record.instance) {
        return record.component + "/" + std::to_string(*record.instance);
    }
    return record.component;
}

}  // namespace safety_crit::observability

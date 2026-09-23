#include "safety_crit/perturb/replay_log.hpp"

#include <charconv>
#include <string_view>

namespace safety_crit::perturb {
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

}  // namespace

bool parse_replay_log(std::string_view text, std::vector<ReplayEntry>& out, std::string& error) {
    out.clear();
    bool schema_seen = false;
    std::size_t line_no = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::string_view line =
            text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        if (nl == std::string_view::npos) {
            start = text.size() + 1;
        } else {
            start = nl + 1;
        }
        ++line_no;
        if (line.empty()) {
            continue;
        }
        ReplayEntry entry;
        std::int64_t schema = 0;
        if (extract_number(line, "\"schema\":", schema)) {
            if (static_cast<std::uint32_t>(schema) != kReplaySchemaVersion) {
                error = "replay: unsupported schema version at line " + std::to_string(line_no);
                return false;
            }
            schema_seen = true;
            entry.is_header = true;
            std::int64_t header_ts = 0;
            if (extract_number(line, "\"ts\":", header_ts)) {
                entry.ts_ns = static_cast<std::uint64_t>(header_ts);
            }
            out.push_back(std::move(entry));
            continue;
        }
        if (!extract_string(line, "\"category\":\"", entry.category)) {
            error = "replay: malformed record at line " + std::to_string(line_no);
            return false;
        }
        bool known = false;
        for (const char* candidate : kReplayCategories) {
            if (entry.category == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            error = "replay: unknown category at line " + std::to_string(line_no);
            return false;
        }
        std::int64_t ts = 0;
        std::int64_t target = 0;
        if (!extract_number(line, "\"ts\":", ts) || !extract_number(line, "\"target\":", target)) {
            error = "replay: malformed record at line " + std::to_string(line_no);
            return false;
        }
        entry.ts_ns = static_cast<std::uint64_t>(ts);
        entry.target = target;
        std::int64_t second = 0;
        if (extract_number(line, "\"second\":", second)) {
            entry.second_target = second;
        }
        out.push_back(std::move(entry));
    }
    if (!schema_seen) {
        error = "replay: missing schema header record";
        return false;
    }
    return true;
}

}  // namespace safety_crit::perturb

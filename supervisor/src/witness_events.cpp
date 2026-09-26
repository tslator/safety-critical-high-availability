#include "safety_crit/supervisor/witness_events.hpp"

#include <cstdio>

namespace safety_crit::supervisor {

std::string failover_started_fields(std::uint32_t physical_worker) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "\"worker\":%u", physical_worker);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

std::string failover_recovered_fields(std::uint64_t latency_ms) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "\"latency_ms\":%llu",
                                static_cast<unsigned long long>(latency_ms));
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

std::string ring_degraded_fields(std::uint32_t ring) {
    char buf[96];
    const int n = std::snprintf(buf, sizeof(buf),
                                "\"ring\":%u,\"reason\":\"standby_exhausted\"", ring);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

std::string stall_recovered_fields(std::uint32_t physical_worker, std::uint64_t epoch) {
    char buf[96];
    const int n = std::snprintf(buf, sizeof(buf), "\"worker\":%u,\"epoch\":%llu",
                                physical_worker, static_cast<unsigned long long>(epoch));
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

std::string stall_escalated_fields(std::uint32_t physical_worker, std::uint64_t epoch) {
    return stall_recovered_fields(physical_worker, epoch);
}

std::string shutdown_summary_fields(int state, std::uint64_t a_records,
                                    std::uint64_t a_corruptions, bool a_first_post_failover,
                                    std::uint64_t b_records, std::uint64_t b_corruptions,
                                    bool b_first_post_failover, bool failover_timing_emitted) {
    char buf[512];
    const int n = std::snprintf(
        buf, sizeof(buf),
        "\"state\":%d,\"a_records\":%llu,\"a_corruptions\":%llu,\"a_first_post_failover\":%d,"
        "\"b_records\":%llu,\"b_corruptions\":%llu,\"b_first_post_failover\":%d,"
        "\"failover_timing_emitted\":%d",
        state, static_cast<unsigned long long>(a_records),
        static_cast<unsigned long long>(a_corruptions), a_first_post_failover ? 1 : 0,
        static_cast<unsigned long long>(b_records),
        static_cast<unsigned long long>(b_corruptions), b_first_post_failover ? 1 : 0,
        failover_timing_emitted ? 1 : 0);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
}

bool WitnessSink::open(const std::string& path, std::error_code& ec) {
    observability::EventLogWriterOptions options{};
    return log_.open(path, "supervisor", options, ec);
}

void WitnessSink::emit(observability::LogLevel level, std::string_view event,
                       std::string_view extra_fields) {
    std::string line;
    if (observability::format_stdout_event(observability::now_unix_ns(), level, "supervisor",
                                           event, extra_fields, line)) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    if (log_.is_open()) {
        std::error_code ec;
        (void)log_.append(level, event, extra_fields, ec);
    }
}

}  // namespace safety_crit::supervisor

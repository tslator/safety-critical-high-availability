// T-0038 tests for the observability daemon assembly (DEC-0014 §9):
// strict argument parsing, the --once snapshot path (success, missing
// region, corrupt region, event-log ingestion including the
// data_loss_observed witness scan), and the server path over loopback
// including the DEC-0014 §2 degradation window (region vanish/reappear).
#include "test_framework.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include "safety_crit/observability/daemon.hpp"
#include "safety_crit/observability/event_log.hpp"
#include "safety_crit/observability/http_server.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"

namespace {

using namespace safety_crit::observability;
using namespace std::chrono_literals;
namespace shm = safety_crit::shared_memory;

std::string unique_region(const char* tag) {
    static std::atomic<unsigned> counter{0};
    return std::string("/obsd_") + tag + "_" + std::to_string(::getpid()) + "_" +
           std::to_string(counter.fetch_add(1));
}

void unlink_region(const std::string& name) {
    std::remove(("/dev/shm" + name).c_str());
}

int connect_loopback(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

std::string http_get(std::uint16_t port, std::string_view path) {
    int fd = connect_loopback(port);
    if (fd < 0) {
        return {};
    }
    timeval rcv_timeout{};
    rcv_timeout.tv_sec = 2;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));
    const std::string request =
        std::string("GET ") + std::string(path) + " HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    if (::send(fd, request.data(), request.size(), MSG_NOSIGNAL) < 0) {
        ::close(fd);
        return {};
    }
    // Read the full response: headers and body may arrive in separate reads,
    // so keep reading until Content-Length bytes of body are present or the
    // peer closes / times out.
    std::string response;
    char buf[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        response.append(buf, static_cast<std::size_t>(n));
        const std::size_t header_end = response.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            continue;
        }
        std::size_t content_length = 0;
        const std::size_t cl_pos = response.find("Content-Length: ");
        if (cl_pos != std::string::npos && cl_pos < header_end) {
            content_length = std::strtoull(response.c_str() + cl_pos + 16, nullptr, 10);
        }
        if (response.size() >= header_end + 4 + content_length) {
            break;
        }
    }
    ::close(fd);
    const std::size_t body_pos = response.find("\r\n\r\n");
    return body_pos == std::string::npos ? std::string{} : response.substr(body_pos + 4);
}


// ---------------------------------------------------------------------------
// Argument parsing.

SAFETY_CRIT_TEST_CASE(Daemon, ParseDefaults) {
    DaemonConfig cfg{};
    std::string error;
    SAFETY_CRIT_ASSERT(parse_daemon_args(0, nullptr, cfg, error));
    SAFETY_CRIT_ASSERT(error.empty());
    SAFETY_CRIT_ASSERT(cfg.listen_address == kDefaultListenAddress);
    SAFETY_CRIT_ASSERT(cfg.region_name == kDefaultDaemonRegionName);
    SAFETY_CRIT_ASSERT(cfg.event_log_path.empty());
    SAFETY_CRIT_ASSERT(cfg.poll_interval == kDefaultDaemonPollInterval);
    SAFETY_CRIT_ASSERT(cfg.ticks == 0u);
    SAFETY_CRIT_ASSERT(!cfg.once);
}

SAFETY_CRIT_TEST_CASE(Daemon, ParseAllFlags) {
    const char* argv[] = {"--listen", "127.0.0.1:9999",
                          "--region", "/my_region",
                          "--event-log", "/tmp/daemon_test_ev.jsonl",
                          "--poll-interval-ms", "500",
                          "--ticks", "7",
                          "--once"};
    DaemonConfig cfg{};
    std::string error;
    SAFETY_CRIT_ASSERT(parse_daemon_args(11, argv, cfg, error));
    SAFETY_CRIT_ASSERT(error.empty());
    SAFETY_CRIT_ASSERT(cfg.listen_address == "127.0.0.1:9999");
    SAFETY_CRIT_ASSERT(cfg.region_name == "/my_region");
    SAFETY_CRIT_ASSERT(cfg.event_log_path == "/tmp/daemon_test_ev.jsonl");
    SAFETY_CRIT_ASSERT(cfg.poll_interval == std::chrono::milliseconds(500));
    SAFETY_CRIT_ASSERT(cfg.ticks == 7u);
    SAFETY_CRIT_ASSERT(cfg.once);
}

SAFETY_CRIT_TEST_CASE(Daemon, ParseRejectsBadInput) {
    DaemonConfig cfg{};
    std::string error;

    const char* unknown[] = {"--bogus"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(1, unknown, cfg, error));

    const char* no_port[] = {"--listen"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(1, no_port, cfg, error));

    const char* bad_port[] = {"observability", "--listen", "http://x:1"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(3, bad_port, cfg, error));

    const char* rel_region[] = {"observability", "--region", "relative"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(3, rel_region, cfg, error));

    const char* bare_region[] = {"observability", "--region", "/"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(3, bare_region, cfg, error));

    const char* no_event_log[] = {"--event-log"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(1, no_event_log, cfg, error));

    const char* zero_poll[] = {"observability", "--poll-interval-ms", "0"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(3, zero_poll, cfg, error));

    const char* huge_poll[] = {"observability", "--poll-interval-ms", "9999999999"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(3, huge_poll, cfg, error));

    const char* nan_poll[] = {"observability", "--poll-interval-ms", "abc"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(3, nan_poll, cfg, error));

    const char* zero_ticks[] = {"observability", "--ticks", "0"};
    SAFETY_CRIT_ASSERT(!parse_daemon_args(3, zero_ticks, cfg, error));
}

// ---------------------------------------------------------------------------
// --once snapshot path.

SAFETY_CRIT_TEST_CASE(Daemon, OnceMissingRegionFails) {
    DaemonConfig cfg{};
    cfg.region_name = unique_region("missing");
    cfg.once = true;
    std::ostringstream out;
    std::ostringstream err;
    SAFETY_CRIT_ASSERT(run_daemon(cfg, out, err) == 1);
    SAFETY_CRIT_ASSERT(out.str().empty());
}

SAFETY_CRIT_TEST_CASE(Daemon, OnceRendersHealthySnapshot) {
    const std::string region = unique_region("healthy");
    auto handle = shm::SharedRegionHandle::create_or_open(region.c_str());
    SAFETY_CRIT_ASSERT(handle.ok());

    DaemonConfig cfg{};
    cfg.region_name = region;
    cfg.once = true;
    std::ostringstream out;
    std::ostringstream err;
    SAFETY_CRIT_ASSERT(run_daemon(cfg, out, err) == 0);
    const std::string body = out.str();
    SAFETY_CRIT_ASSERT(body.find("\"identity_ok\":1") != std::string::npos);
    SAFETY_CRIT_ASSERT(body.find("\"rings\":[") != std::string::npos);
    SAFETY_CRIT_ASSERT(body.find("\"event_log\"") != std::string::npos);
    unlink_region(region);
}

SAFETY_CRIT_TEST_CASE(Daemon, OnceCorruptRegionFailsAttach) {
    const std::string region = unique_region("corrupt");
    auto handle = shm::SharedRegionHandle::create_or_open(region.c_str());
    SAFETY_CRIT_ASSERT(handle.ok());
    *reinterpret_cast<char*>(handle.get()) = 0;  // break the magic word

    DaemonConfig cfg{};
    cfg.region_name = region;
    cfg.once = true;
    std::ostringstream out;
    std::ostringstream err;
    SAFETY_CRIT_ASSERT(run_daemon(cfg, out, err) == 1);
    unlink_region(region);
}

SAFETY_CRIT_TEST_CASE(Daemon, OnceIngestsEventLogComponents) {
    const std::string region = unique_region("events");
    auto handle = shm::SharedRegionHandle::create_or_open(region.c_str());
    SAFETY_CRIT_ASSERT(handle.ok());

    const std::string log = "/tmp/daemon_test_events_" + std::to_string(::getpid()) + ".jsonl";
    std::remove(log.c_str());
    {
        EventLogWriter writer;
        std::error_code ec;
        SAFETY_CRIT_ASSERT(writer.open(log, "perturb", {}, ec));
        SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "perturbation_applied",
                                         "\"category\":\"crash\",\"target\":42", ec));
    }

    DaemonConfig cfg{};
    cfg.region_name = region;
    cfg.event_log_path = log;
    cfg.once = true;
    std::ostringstream out;
    std::ostringstream err;
    SAFETY_CRIT_ASSERT(run_daemon(cfg, out, err) == 0);
    const std::string body = out.str();
    SAFETY_CRIT_ASSERT(body.find("\"available\":1") != std::string::npos);
    SAFETY_CRIT_ASSERT(body.find("\"key\":\"perturb\"") != std::string::npos);
    unlink_region(region);
    std::remove(log.c_str());
}

SAFETY_CRIT_TEST_CASE(Daemon, ServerCountsDataLossWitnessRecords) {
    const std::string region = unique_region("lossw");
    auto handle = shm::SharedRegionHandle::create_or_open(region.c_str());
    SAFETY_CRIT_ASSERT(handle.ok());

    const std::string log = "/tmp/daemon_test_loss_" + std::to_string(::getpid()) + ".jsonl";
    std::remove(log.c_str());
    {
        EventLogWriter writer;
        std::error_code ec;
        SAFETY_CRIT_ASSERT(writer.open(log, "supervisor", {}, ec));
        SAFETY_CRIT_ASSERT(writer.append(LogLevel::kError, "data_loss_observed",
                                         "\"ring\":0,\"lost\":5", ec));
        SAFETY_CRIT_ASSERT(writer.append(LogLevel::kError, "data_loss_observed",
                                         "\"ring\":1,\"lost\":3", ec));
        SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "failover_recovered",
                                         "\"worker\":2", ec));
    }

    HttpServer::reset_stop();
    DaemonConfig cfg{};
    cfg.region_name = region;
    cfg.event_log_path = log;
    cfg.listen_address = "127.0.0.1:18100";
    cfg.poll_interval = std::chrono::milliseconds(50);
    std::ostringstream out;
    std::ostringstream err;
    std::thread runner([&] { run_daemon(cfg, out, err); });

    // The tick refresh ingests the witness records; /health exposes the
    // counter (lost is structurally independent of the record stream).
    std::string body;
    for (int i = 0; i < 200; ++i) {
        body = http_get(18100, "/health");
        if (body.find("\"data_loss_events_total\":8") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(50ms);
    }
    SAFETY_CRIT_ASSERT(body.find("\"data_loss_events_total\":8") != std::string::npos);
    SAFETY_CRIT_ASSERT(body.find("\"status\":\"degraded\"") != std::string::npos);

    const std::string metrics = http_get(18100, "/metrics");
    SAFETY_CRIT_ASSERT(metrics.find("data_loss_events_total 8") != std::string::npos);

    HttpServer::request_stop();
    runner.join();
    HttpServer::reset_stop();
    unlink_region(region);
    std::remove(log.c_str());
}

SAFETY_CRIT_TEST_CASE(Daemon, OnceMissingEventLogIsNonFatal) {
    const std::string region = unique_region("nolog");
    auto handle = shm::SharedRegionHandle::create_or_open(region.c_str());
    SAFETY_CRIT_ASSERT(handle.ok());

    DaemonConfig cfg{};
    cfg.region_name = region;
    cfg.event_log_path = "/tmp/daemon_test_absent_" + std::to_string(::getpid()) + ".jsonl";
    cfg.once = true;
    std::ostringstream out;
    std::ostringstream err;
    SAFETY_CRIT_ASSERT(run_daemon(cfg, out, err) == 0);
    SAFETY_CRIT_ASSERT(out.str().find("\"identity_ok\":1") != std::string::npos);
    SAFETY_CRIT_ASSERT(out.str().find("\"available\":0") != std::string::npos);
    unlink_region(region);
}

// ---------------------------------------------------------------------------
// Server path with degradation window (loopback client like health_report_test).

SAFETY_CRIT_TEST_CASE(Daemon, ServerServesAndTracksRegionLoss) {
    const std::string region = unique_region("serve");
    auto handle = shm::SharedRegionHandle::create_or_open(region.c_str());
    SAFETY_CRIT_ASSERT(handle.ok());

    HttpServer::reset_stop();
    DaemonConfig cfg{};
    cfg.region_name = region;
    cfg.listen_address = "127.0.0.1:18099";
    cfg.poll_interval = std::chrono::milliseconds(50);

    std::ostringstream out;
    std::ostringstream err;
    std::thread runner([&] { run_daemon(cfg, out, err); });

    // Wait for the *expected status*, not merely a response: the daemon
    // serves the previous state until its poll tick observes the change.
    auto wait_for = [&](const char* needle) {
        std::string b;
        for (int i = 0; i < 200; ++i) {
            b = http_get(18099, "/health");
            if (b.find(needle) != std::string::npos) {
                return b;
            }
            std::this_thread::sleep_for(50ms);
        }
        return b;
    };

    const std::string body = wait_for("\"status\":\"ok\"");
    SAFETY_CRIT_ASSERT(body.find("\"status\":\"ok\"") != std::string::npos);

    // DEC-0014 §2: region loss degrades health but never stops the daemon.
    unlink_region(region);
    const std::string degraded = wait_for("\"status\":\"degraded\"");
    SAFETY_CRIT_ASSERT(degraded.find("\"status\":\"degraded\"") != std::string::npos);

    // Reappearance re-attaches and health recovers.
    auto reattached = shm::SharedRegionHandle::create_or_open(region.c_str());
    SAFETY_CRIT_ASSERT(reattached.ok());
    const std::string recovered = wait_for("\"status\":\"ok\"");
    SAFETY_CRIT_ASSERT(recovered.find("\"status\":\"ok\"") != std::string::npos);

    const std::string metrics = http_get(18099, "/metrics");
    SAFETY_CRIT_ASSERT(metrics.find("worker_status") != std::string::npos);
    SAFETY_CRIT_ASSERT(metrics.find("data_loss_events_total") != std::string::npos);

    HttpServer::request_stop();
    runner.join();
    HttpServer::reset_stop();
    unlink_region(region);
}

}  // namespace

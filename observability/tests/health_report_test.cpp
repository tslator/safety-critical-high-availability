// T-0037 tests for the health report and status endpoints: pure render
// tests pin the documented JSON shapes (DEC-0014 §7), verdict flips
// (unowned ring, data loss, corrupted region identity), and loopback
// integration over the T-0035 server (raw POSIX socket client, no
// third-party HTTP lib). JSON validity is asserted with a hand-rolled
// bracket/string scanner and key extraction (dependency allowlist,
// DEC-0014 §6).
#include "test_framework.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "safety_crit/observability/event_metrics.hpp"
#include "safety_crit/observability/health_report.hpp"
#include "safety_crit/observability/region_metrics.hpp"
#include "safety_crit/shared_memory/atomic_flags.hpp"

namespace {

using namespace safety_crit::observability;
using namespace std::chrono_literals;
namespace shm = safety_crit::shared_memory;

// ---------------------------------------------------------------------------
// Hand-rolled JSON checks (no third-party lib).

bool json_balanced(const std::string& s) {
    std::vector<char> stack;
    bool in_string = false;
    bool escaped = false;
    for (const char c : s) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            stack.push_back(c);
        } else if (c == '}' || c == ']') {
            if (stack.empty()) {
                return false;
            }
            const char open = stack.back();
            stack.pop_back();
            if ((open == '{') != (c == '}')) {
                return false;
            }
        }
    }
    return stack.empty() && !in_string;
}

std::size_t count_occurs(const std::string& hay, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t pos = hay.find(needle); pos != std::string::npos;
         pos = hay.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

bool has_num(const std::string& body, std::string_view key, std::string_view value) {
    return body.find("\"" + std::string(key) + "\":" + std::string(value)) != std::string::npos;
}

// Extracts the top-level object keys in order (depth-1 strings followed by
// ':'). Returns nullopt-style empty vector on malformed input.
std::vector<std::string> top_level_keys(const std::string& s) {
    std::vector<std::string> keys;
    std::size_t depth = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') {
            const std::size_t end = s.find('"', i + 1);
            if (end == std::string::npos) {
                return {};
            }
            if (depth == 1) {
                // Look ahead past whitespace for ':' -> it is a key.
                std::size_t j = end + 1;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) {
                    ++j;
                }
                if (j < s.size() && s[j] == ':') {
                    keys.push_back(s.substr(i + 1, end - i - 1));
                }
            }
            i = end + 1;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
        }
        ++i;
    }
    return keys;
}

// ---------------------------------------------------------------------------
// Fixtures.

struct EndpointFixture {
    shm::SharedRegion region{};
    MetricsRegistry registry{};
    EndpointContext ctx{};

    void init() {
        SAFETY_CRIT_ASSERT(shm::initialize(region));
        SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
        ctx.region = &region;
        ctx.registry = &registry;
        ctx.now = [] { return 1234567890000000000ull; };
        ctx.start_ns = 1234567890000000000ull - 5000000ull;  // uptime 5 ms
    }
    std::error_code ec{};
};

std::string render_health(EndpointFixture& f) {
    std::string body;
    SAFETY_CRIT_ASSERT(render_health_json(f.ctx, body));
    return body;
}

std::string temp_log_path(const char* tag) {
    return "/tmp/safety_crit_ha_health_" + std::to_string(static_cast<long>(::getpid())) + "_" +
           tag + ".jsonl";
}

// ---------------------------------------------------------------------------
// Pure render tests.

SAFETY_CRIT_TEST_CASE(HealthReport, RenderHealthOkShape) {
    EndpointFixture f;
    f.init();
    const std::string body = render_health(f);
    SAFETY_CRIT_ASSERT(json_balanced(body));
    SAFETY_CRIT_ASSERT(has_num(body, "status", "\"ok\""));
    SAFETY_CRIT_ASSERT(has_num(body, "ts", "1234567890000000000"));
    SAFETY_CRIT_ASSERT(has_num(body, "uptime_ms", "5"));
    SAFETY_CRIT_ASSERT(has_num(body, "data_loss_events_total", "0"));
    // Region initialization owns ring i at epoch 2; all workers idle.
    SAFETY_CRIT_ASSERT(count_occurs(body, "\"epoch\":2") == shm::kMaxWorkers);
    SAFETY_CRIT_ASSERT(count_occurs(body, "\"state\":0") == shm::kMaxWorkers);
    SAFETY_CRIT_ASSERT(count_occurs(body, "\"last_sequence\":0") == shm::kMaxWorkers);
    for (std::size_t i = 0; i < shm::kMaxWorkers; ++i) {
        SAFETY_CRIT_ASSERT(
            body.find("\"physical_owner\":" + std::to_string(i)) != std::string::npos);
    }
    const std::vector<std::string> keys = top_level_keys(body);
    const std::vector<std::string> expected = {"ts",        "status",
                                               "uptime_ms", "workers",
                                               "ownership", "data_loss_events_total"};
    SAFETY_CRIT_ASSERT(keys == expected);
}

SAFETY_CRIT_TEST_CASE(HealthReport, UnownedRingDegrades) {
    EndpointFixture f;
    f.init();
    // Simulate an abandoned ring: epoch 0 reads as unassigned.
    f.region.ring_ownership[1].epoch.store(0u, std::memory_order_release);
    const std::string body = render_health(f);
    SAFETY_CRIT_ASSERT(json_balanced(body));
    SAFETY_CRIT_ASSERT(has_num(body, "status", "\"degraded\""));
    SAFETY_CRIT_ASSERT(count_occurs(body, "\"physical_owner\":4294967295") == 1);
    SAFETY_CRIT_ASSERT(count_occurs(body, "\"epoch\":0") == 1);
}

SAFETY_CRIT_TEST_CASE(HealthReport, DataLossDegrades) {
    EndpointFixture f;
    f.init();
    SAFETY_CRIT_ASSERT(f.registry.set("data_loss_events_total", "", 2.0, f.ec));
    const std::string body = render_health(f);
    SAFETY_CRIT_ASSERT(json_balanced(body));
    SAFETY_CRIT_ASSERT(has_num(body, "status", "\"degraded\""));
    SAFETY_CRIT_ASSERT(has_num(body, "data_loss_events_total", "2"));
    SAFETY_CRIT_ASSERT(render_health(f).find("degraded") != std::string::npos);
}

SAFETY_CRIT_TEST_CASE(HealthReport, CorruptedRegionIdentityDegrades) {
    EndpointFixture f;
    f.init();
    f.region.identity.magic = 0xDEADBEEFu;
    const std::string body = render_health(f);
    SAFETY_CRIT_ASSERT(json_balanced(body));
    SAFETY_CRIT_ASSERT(has_num(body, "status", "\"degraded\""));
}

SAFETY_CRIT_TEST_CASE(HealthReport, MissingDataLossFamilyAndNullContextFail) {
    MetricsRegistry registry{};
    shm::SharedRegion region{};
    SAFETY_CRIT_ASSERT(shm::initialize(region));
    EndpointContext ctx{};
    ctx.region = &region;
    ctx.registry = &registry;  // declare_event_metrics NOT called
    std::string body;
    SAFETY_CRIT_ASSERT(!render_health_json(ctx, body));

    EndpointContext empty{};
    SAFETY_CRIT_ASSERT(!render_health_json(empty, body));
    SAFETY_CRIT_ASSERT(!render_status_json(empty, body));

    HttpServer server;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(!register_daemon_routes(server, empty, ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::invalid_argument));
}

SAFETY_CRIT_TEST_CASE(HealthReport, StatusSnapshotFields) {
    EndpointFixture f;
    f.init();

    // Region-side activity: one push on ring 1, one corruption on ring 2.
    shm::set_status(f.region.worker_status[1].status,
                    shm::to_bits(shm::WorkerStatusFlag::kRunning));
    const std::uint64_t value = 0x0102030405060708ull;
    SAFETY_CRIT_ASSERT(shm::push(f.region, 1, value));
    SAFETY_CRIT_ASSERT(shm::push_with_bad_crc(f.region, 2, value));
    std::uint64_t drained = 0;
    SAFETY_CRIT_ASSERT(!f.region.rings[2].try_pop(drained));  // skip-and-count

    // Event log: three sequential records, then a hand-written seq jump
    // (4..6 skipped) to produce exactly one gap.
    const std::string path = temp_log_path("status");
    std::remove(path.c_str());
    EventLogWriter writer;
    std::error_code ec;
    EventLogWriterOptions options{};
    SAFETY_CRIT_ASSERT(writer.open(path, "monitor", options, ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "tick", "", ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "tick", "", ec));
    SAFETY_CRIT_ASSERT(writer.append(LogLevel::kInfo, "tick", "", ec));
    writer.close();
    {
        std::ofstream app(path, std::ios::app);
        app << "{\"schema\":1,\"ts\":123,\"level\":\"info\",\"component\":\"monitor\","
            << "\"seq\":7,\"event\":\"late\"}\n";
    }
    EventLogReader reader;
    SAFETY_CRIT_ASSERT(reader.open(path, ec));
    EventRecord record;
    std::string error;
    while (reader.read_next(record, error) == ReadStatus::kOk) {
    }
    SAFETY_CRIT_ASSERT(reader.gaps_total() == 1);

    f.ctx.event_reader = &reader;
    std::string body;
    SAFETY_CRIT_ASSERT(render_status_json(f.ctx, body));
    SAFETY_CRIT_ASSERT(json_balanced(body));
    const std::vector<std::string> keys = top_level_keys(body);
    const std::vector<std::string> expected = {"ts", "uptime_ms", "region", "rings", "event_log"};
    SAFETY_CRIT_ASSERT(keys == expected);
    SAFETY_CRIT_ASSERT(has_num(body, "version", std::to_string(shm::kRegionVersion)));
    SAFETY_CRIT_ASSERT(has_num(body, "identity_ok", "1"));
    SAFETY_CRIT_ASSERT(has_num(body, "integrity_ok", "1"));
    SAFETY_CRIT_ASSERT(count_occurs(body, "\"corruptions\":1") == 1);
    SAFETY_CRIT_ASSERT(count_occurs(body, "\"owned\":1") == shm::kMaxWorkers);
    SAFETY_CRIT_ASSERT(has_num(body, "available", "1"));
    SAFETY_CRIT_ASSERT(body.find("\"key\":\"monitor\"") != std::string::npos);
    SAFETY_CRIT_ASSERT(has_num(body, "gaps", "1"));
    SAFETY_CRIT_ASSERT(has_num(body, "missed", "3"));
    SAFETY_CRIT_ASSERT(has_num(body, "expected_seq", "8"));

    // No reader -> available:0, empty components.
    f.ctx.event_reader = nullptr;
    SAFETY_CRIT_ASSERT(render_status_json(f.ctx, body));
    SAFETY_CRIT_ASSERT(json_balanced(body));
    SAFETY_CRIT_ASSERT(has_num(body, "available", "0"));
    SAFETY_CRIT_ASSERT(body.find("\"components\":[]") != std::string::npos);
    std::remove(path.c_str());
}

SAFETY_CRIT_TEST_CASE(HealthReport, RegistryGetAccessorSemantics) {
    MetricsRegistry registry;
    std::error_code ec;
    SAFETY_CRIT_ASSERT(declare_event_metrics(registry, ec));
    double value = -1.0;
    SAFETY_CRIT_ASSERT(!registry.get("nope", "", value, ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::no_such_file_or_directory));
    SAFETY_CRIT_ASSERT(!registry.get("data_loss_events_total", "x", value, ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::invalid_argument));
    SAFETY_CRIT_ASSERT(!registry.get("perturbation_count_total", "", value, ec));
    SAFETY_CRIT_ASSERT(ec == std::make_error_code(std::errc::no_such_file_or_directory));
    SAFETY_CRIT_ASSERT(registry.set("data_loss_events_total", "", 7.0, ec));
    SAFETY_CRIT_ASSERT(registry.get("data_loss_events_total", "", value, ec));
    SAFETY_CRIT_ASSERT(value == 7.0);
    SAFETY_CRIT_ASSERT(registry.add("perturbation_count_total", "crash", 1.0, ec));
    SAFETY_CRIT_ASSERT(registry.get("perturbation_count_total", "crash", value, ec));
    SAFETY_CRIT_ASSERT(value == 1.0);
    SAFETY_CRIT_ASSERT(registry.get("perturbation_count_total", "stall", value, ec) == false);
}

// ---------------------------------------------------------------------------
// Loopback integration over the T-0035 server.

EndpointFixture* g_fixture = nullptr;

void register_routes(HttpServer& server) {
    std::error_code ec;
    SAFETY_CRIT_ASSERT(register_daemon_routes(server, g_fixture->ctx, ec));
}

class TestServer {
public:
    bool start() {
        HttpServer::reset_stop();
        std::error_code ec;
        HttpServerConfig config;
        config.listen_address = "127.0.0.1:0";
        config.request_timeout = 250ms;
        config.idle_timeout = 300ms;
        if (!server_.configure(config, ec) || !register_calls()) {
            return false;
        }
        if (!server_.open_listener(ec)) {
            return false;
        }
        thread_ = std::thread([this] { server_.run(stop_.get_token()); });
        return true;
    }

    void stop() {
        stop_.request_stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        server_.close();
    }

    std::uint16_t port() const { return server_.bound_port(); }
    HttpServer& server() { return server_; }

private:
    bool register_calls() {
        register_routes(server_);
        return true;
    }

    HttpServer server_{};
    std::stop_source stop_{};
    std::thread thread_{};
};

struct Response {
    int status = -1;
    std::string body;
    std::string head;
};

Response http_get(std::uint16_t port, std::string_view method, std::string_view path) {
    Response response;
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return response;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return response;
    }
    const std::string request =
        std::string(method) + " " + std::string(path) +
        " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (::send(fd, request.data(), request.size(), MSG_NOSIGNAL) !=
        static_cast<ssize_t>(request.size())) {
        ::close(fd);
        return response;
    }
    std::string raw;
    char buf[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            raw.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        break;
    }
    ::close(fd);
    if (raw.substr(0, 9) == "HTTP/1.1 ") {
        response.status = std::atoi(raw.substr(9, 3).c_str());
    }
    const std::size_t head_end = raw.find("\r\n\r\n");
    if (head_end != std::string::npos) {
        response.head = raw.substr(0, head_end);
        response.body = raw.substr(head_end + 4);
    }
    return response;
}

SAFETY_CRIT_TEST_CASE(HealthReport, RoutesServeOverLoopback) {
    EndpointFixture fixture;
    g_fixture = &fixture;
    fixture.init();
    SAFETY_CRIT_ASSERT(declare_region_metrics(fixture.registry, fixture.ec));
    SAFETY_CRIT_ASSERT(collect_region_metrics(fixture.region, fixture.registry, fixture.ec));
    SAFETY_CRIT_ASSERT(fixture.registry.set("data_loss_events_total", "", 0.0, fixture.ec));

    TestServer server;
    SAFETY_CRIT_ASSERT(server.start());

    const Response health = http_get(server.port(), "GET", "/health");
    SAFETY_CRIT_ASSERT(health.status == 200);
    SAFETY_CRIT_ASSERT(health.head.find("Content-Type: application/json") != std::string::npos);
    SAFETY_CRIT_ASSERT(json_balanced(health.body));
    SAFETY_CRIT_ASSERT(has_num(health.body, "status", "\"ok\""));

    const Response metrics = http_get(server.port(), "GET", "/metrics");
    SAFETY_CRIT_ASSERT(metrics.status == 200);
    SAFETY_CRIT_ASSERT(metrics.head.find("text/plain; version=0.0.4") != std::string::npos);
    std::string expected_metrics;
    fixture.registry.render_prometheus(expected_metrics);
    SAFETY_CRIT_ASSERT(metrics.body == expected_metrics);
    SAFETY_CRIT_ASSERT(metrics.body.find("data_loss_events_total 0") != std::string::npos);
    SAFETY_CRIT_ASSERT(metrics.body.find("worker_status{worker=\"0\"} 0") != std::string::npos);

    const Response status = http_get(server.port(), "GET", "/status");
    SAFETY_CRIT_ASSERT(status.status == 200);
    SAFETY_CRIT_ASSERT(json_balanced(status.body));
    SAFETY_CRIT_ASSERT(status.body.find("\"version\":" + std::to_string(shm::kRegionVersion)) !=
                       std::string::npos);

    const Response missing = http_get(server.port(), "GET", "/nope");
    SAFETY_CRIT_ASSERT(missing.status == 404);
    const Response posted = http_get(server.port(), "POST", "/health");
    SAFETY_CRIT_ASSERT(posted.status == 405);

    server.stop();
    g_fixture = nullptr;
}

SAFETY_CRIT_TEST_CASE(HealthReport, RegionLossMidRunFlipsDegraded) {
    EndpointFixture fixture;
    g_fixture = &fixture;
    fixture.init();

    TestServer server;
    SAFETY_CRIT_ASSERT(server.start());

    const Response before = http_get(server.port(), "GET", "/health");
    SAFETY_CRIT_ASSERT(before.status == 200);
    SAFETY_CRIT_ASSERT(has_num(before.body, "status", "\"ok\""));

    // Region corruption mid-run: the daemon must NOT exit (DEC-0014 §2);
    // health flips to degraded and the server keeps answering.
    fixture.region.identity.magic = 0u;
    const Response after = http_get(server.port(), "GET", "/health");
    SAFETY_CRIT_ASSERT(after.status == 200);
    SAFETY_CRIT_ASSERT(has_num(after.body, "status", "\"degraded\""));
    SAFETY_CRIT_ASSERT(json_balanced(after.body));

    const Response status = http_get(server.port(), "GET", "/status");
    SAFETY_CRIT_ASSERT(status.status == 200);
    SAFETY_CRIT_ASSERT(has_num(status.body, "identity_ok", "0"));

    server.stop();
    g_fixture = nullptr;
}

}  // namespace

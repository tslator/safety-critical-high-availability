// T-0035 loopback tests for the bounded HTTP/1.1 server core: a raw POSIX
// socket client (no third-party HTTP lib) exercises every documented
// boundary and status code against the server running its single-threaded
// loop on a worker thread (stop_source shutdown). The server itself stays
// single-threaded, so the loop is trivially TSan-clean; ASan covers socket
// lifetimes.
#include "test_framework.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "safety_crit/observability/http_server.hpp"

namespace {

using namespace safety_crit::observability;
using namespace std::chrono_literals;

bool handle_hello(void* /*context*/, HttpResponse& response) {
    response.body = "hello\n";
    return true;
}

bool handle_fails(void* /*context*/, HttpResponse& /*response*/) {
    return false;
}

const std::string& giant_body() {
    static const std::string body(70000, 'x');  // > kMaxResponseBodyBytes
    return body;
}

bool handle_giant(void* /*context*/, HttpResponse& response) {
    response.body = giant_body();
    return true;
}

int open_loopback(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void set_recv_timeout(int fd, std::chrono::milliseconds timeout) {
    timeval tv{static_cast<time_t>(timeout.count() / 1000),
               static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

bool send_request(int fd, std::string_view request) {
    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent,
                                 MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// Reads whatever arrives within `wait` (the recv timeout); n == 0 (peer
// closed) ends early. Returns the bytes plus whether the peer closed.
struct ReadResult {
    std::string data;
    bool peer_closed = false;
    bool timed_out = false;
};

ReadResult read_until_close(int fd, std::chrono::milliseconds wait) {
    set_recv_timeout(fd, wait);
    ReadResult result;
    char buf[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            result.data.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            result.peer_closed = true;
            return result;
        }
        if (errno == ECONNRESET) {
            // The server closes after answering overflow/malformed
            // requests; unread request bytes make the kernel send RST
            // instead of FIN. Either way the connection is closed.
            result.peer_closed = true;
            return result;
        }
        result.timed_out = true;
        return result;
    }
}

// Extracts the numeric status from a response head ("" if absent).
int status_of(std::string_view response) {
    const std::string_view prefix = "HTTP/1.1 ";
    if (response.substr(0, prefix.size()) != prefix) {
        return -1;
    }
    return std::atoi(std::string(response.substr(prefix.size(), 3)).c_str());
}

std::size_t open_fd_count() {
    std::size_t count = 0;
    if (DIR* dir = ::opendir("/proc/self/fd")) {
        while (const dirent* entry = ::readdir(dir)) {
            (void)entry;
            ++count;
        }
        ::closedir(dir);
    }
    return count;
}

// Runs the server on a worker thread for the duration of a test case.
class TestServer {
public:
    using RegisterFn = void (*)(HttpServer& server);

    TestServer(HttpServerConfig config, RegisterFn register_routes) {
        HttpServer::reset_stop();
        std::error_code ec;
        ok_ = server_.configure(config, ec);
        if (ok_ && register_routes != nullptr) {
            register_routes(server_);
        }
    }

    ~TestServer() {
        shutdown();
    }

    void shutdown() {
        stop_.request_stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        server_.close();
    }

    bool start() {
        std::error_code ec;
        if (!ok_ || !server_.open_listener(ec)) {
            return false;
        }
        thread_ = std::thread([this] { server_.run(stop_.get_token()); });
        return true;
    }

    std::uint16_t port() const { return server_.bound_port(); }
    HttpServer& server() { return server_; }

private:
    HttpServer server_{};
    std::stop_source stop_{};
    std::thread thread_{};
    bool ok_{false};
};

void register_hello(HttpServer& server) {
    std::error_code ec;
    server.register_route("/hello", &handle_hello, nullptr, ec);
}

void register_all(HttpServer& server) {
    std::error_code ec;
    server.register_route("/hello", &handle_hello, nullptr, ec);
    server.register_route("/fails", &handle_fails, nullptr, ec);
    server.register_route("/giant", &handle_giant, nullptr, ec);
}

HttpServerConfig test_config() {
    HttpServerConfig config;
    config.listen_address = "127.0.0.1:0";  // ephemeral
    config.request_timeout = 250ms;
    config.idle_timeout = 300ms;
    return config;
}

SAFETY_CRIT_TEST_CASE(HttpServer, ParseListenAddress) {
    std::string host;
    std::uint16_t port = 0;
    std::string error;
    SAFETY_CRIT_ASSERT(parse_listen_address("127.0.0.1:8080", host, port, error));
    SAFETY_CRIT_ASSERT((host == "127.0.0.1" && port == 8080));
    SAFETY_CRIT_ASSERT(parse_listen_address("0.0.0.0:0", host, port, error));
    SAFETY_CRIT_ASSERT(!parse_listen_address("8080", host, port, error));
    SAFETY_CRIT_ASSERT(!parse_listen_address("127.0.0.1:", host, port, error));
    SAFETY_CRIT_ASSERT(!parse_listen_address("127.0.0.1:70000", host, port, error));
    SAFETY_CRIT_ASSERT(!parse_listen_address("example.com:80", host, port, error));
    SAFETY_CRIT_ASSERT(!parse_listen_address("127.0.0.1:80x", host, port, error));

    HttpServerConfig config;
    config.listen_address = "not-an-address";
    std::error_code ec;
    HttpServer server;
    SAFETY_CRIT_ASSERT(!server.configure(config, ec));
    config.listen_address = "127.0.0.1:8080";
    config.idle_timeout = std::chrono::milliseconds::zero();
    SAFETY_CRIT_ASSERT(!server.configure(config, ec));
}

SAFETY_CRIT_TEST_CASE(HttpServer, GetRoundtrip200) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    SAFETY_CRIT_ASSERT(send_request(fd, "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n"));
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 200);
    SAFETY_CRIT_ASSERT(result.data.find("hello\n") != std::string::npos);
    SAFETY_CRIT_ASSERT(result.data.find("Content-Length: 6") != std::string::npos);
    SAFETY_CRIT_ASSERT(result.data.find("Connection: keep-alive") != std::string::npos);
}

SAFETY_CRIT_TEST_CASE(HttpServer, HeadReturnsHeadersWithoutBody) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    SAFETY_CRIT_ASSERT(send_request(fd, "HEAD /hello HTTP/1.1\r\nHost: x\r\n\r\n"));
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 200);
    SAFETY_CRIT_ASSERT(result.data.find("Content-Length: 6") != std::string::npos);
    SAFETY_CRIT_ASSERT(result.data.find("hello\n") == std::string::npos);
}

SAFETY_CRIT_TEST_CASE(HttpServer, UnknownPath404) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    SAFETY_CRIT_ASSERT(send_request(fd, "GET /missing HTTP/1.1\r\nHost: x\r\n\r\n"));
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 404);
}

SAFETY_CRIT_TEST_CASE(HttpServer, PostAndPut405) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    for (const char* method : {"POST", "PUT"}) {
        const int fd = open_loopback(server.port());
        SAFETY_CRIT_ASSERT(fd >= 0);
        const std::string request =
            std::string(method) + " /hello HTTP/1.1\r\nHost: x\r\n\r\n";
        SAFETY_CRIT_ASSERT(send_request(fd, request));
        const ReadResult result = read_until_close(fd, 1500ms);
        ::close(fd);
        SAFETY_CRIT_ASSERT(status_of(result.data) == 405);
    }
}

SAFETY_CRIT_TEST_CASE(HttpServer, OversizedRequestLine431AndClose) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    std::string request = "GET /" + std::string(600, 'a') + " HTTP/1.1\r\n\r\n";
    SAFETY_CRIT_ASSERT(send_request(fd, request));
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 431);
    SAFETY_CRIT_ASSERT(result.peer_closed);
}

SAFETY_CRIT_TEST_CASE(HttpServer, HeaderOverflow431AndClose) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    {  // too many header lines (33 > 32)
        const int fd = open_loopback(server.port());
        SAFETY_CRIT_ASSERT(fd >= 0);
        std::string request = "GET /hello HTTP/1.1\r\n";
        for (int i = 0; i < 33; ++i) {
            request += "X-H: " + std::to_string(i) + "\r\n";
        }
        request += "\r\n";
        SAFETY_CRIT_ASSERT(send_request(fd, request));
        const ReadResult result = read_until_close(fd, 1500ms);
        ::close(fd);
        SAFETY_CRIT_ASSERT(status_of(result.data) == 431);
        SAFETY_CRIT_ASSERT(result.peer_closed);
    }
    {  // too many header bytes (> 8 KiB in 30 lines)
        const int fd = open_loopback(server.port());
        SAFETY_CRIT_ASSERT(fd >= 0);
        std::string request = "GET /hello HTTP/1.1\r\n";
        for (int i = 0; i < 30; ++i) {
            request += "X-H: " + std::string(400, 'a') + "\r\n";
        }
        request += "\r\n";
        SAFETY_CRIT_ASSERT(send_request(fd, request));
        const ReadResult result = read_until_close(fd, 1500ms);
        ::close(fd);
        SAFETY_CRIT_ASSERT(status_of(result.data) == 431);
        SAFETY_CRIT_ASSERT(result.peer_closed);
    }
}

SAFETY_CRIT_TEST_CASE(HttpServer, MalformedRequests400) {
    TestServer server(test_config(), &register_all);
    SAFETY_CRIT_ASSERT(server.start());
    const char* requests[] = {
        "GARBAGE\r\n\r\n",
        "GET /hello HTTP/2.0\r\nHost: x\r\n\r\n",
        "GET /hello HTTP/1.1\r\nContent-Length: 5\r\n\r\n",
        "GET /hello HTTP/1.1\r\nnot-a-header-line\r\n\r\n",
    };
    for (const char* request : requests) {
        const int fd = open_loopback(server.port());
        SAFETY_CRIT_ASSERT(fd >= 0);
        SAFETY_CRIT_ASSERT(send_request(fd, request));
        const ReadResult result = read_until_close(fd, 1500ms);
        ::close(fd);
        SAFETY_CRIT_ASSERT(status_of(result.data) == 400);
        SAFETY_CRIT_ASSERT(result.peer_closed);
    }
}

SAFETY_CRIT_TEST_CASE(HttpServer, HandlerFailure500) {
    TestServer server(test_config(), &register_all);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    SAFETY_CRIT_ASSERT(send_request(fd, "GET /fails HTTP/1.1\r\nHost: x\r\n\r\n"));
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 500);
}

SAFETY_CRIT_TEST_CASE(HttpServer, ResponseBodyOverBound500) {
    TestServer server(test_config(), &register_all);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    SAFETY_CRIT_ASSERT(send_request(fd, "GET /giant HTTP/1.1\r\nHost: x\r\n\r\n"));
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 500);
    SAFETY_CRIT_ASSERT(result.data.size() < 1024);
}

SAFETY_CRIT_TEST_CASE(HttpServer, RequestTimeout408) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    SAFETY_CRIT_ASSERT(send_request(fd, "GET /he"));  // partial head, never completed
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 408);
    SAFETY_CRIT_ASSERT(result.peer_closed);
}

SAFETY_CRIT_TEST_CASE(HttpServer, IdleTimeoutClosesConnection) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    // Send nothing at all: the idle timeout (300 ms here) must close the
    // connection without a response.
    const ReadResult result = read_until_close(fd, 2000ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(result.peer_closed);
    SAFETY_CRIT_ASSERT(result.data.empty());
}

SAFETY_CRIT_TEST_CASE(HttpServer, KeepAliveServesSequentialRequests) {
    TestServer server(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    const int fd = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(fd >= 0);
    // Short read windows so the next request always beats the server's
    // 300 ms idle timeout in this config.
    for (int i = 0; i < 3; ++i) {
        SAFETY_CRIT_ASSERT(send_request(fd, "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n"));
        const ReadResult result = read_until_close(fd, 120ms);
        SAFETY_CRIT_ASSERT(status_of(result.data) == 200);
        SAFETY_CRIT_ASSERT(result.data.find("hello\n") != std::string::npos);
        SAFETY_CRIT_ASSERT(!result.peer_closed);
    }
    // Connection: close terminates.
    SAFETY_CRIT_ASSERT(send_request(fd, "GET /hello HTTP/1.1\r\nConnection: close\r\n\r\n"));
    const ReadResult result = read_until_close(fd, 1500ms);
    ::close(fd);
    SAFETY_CRIT_ASSERT(status_of(result.data) == 200);
    SAFETY_CRIT_ASSERT(result.peer_closed);
}

SAFETY_CRIT_TEST_CASE(HttpServer, ConnectionCapAcceptsAndClosesExcess) {
    HttpServerConfig config = test_config();
    config.idle_timeout = 60s;  // keep the 16 holders open
    TestServer server(config, &register_hello);
    SAFETY_CRIT_ASSERT(server.start());
    int held[kMaxConnections];
    std::size_t held_count = 0;
    while (held_count < kMaxConnections) {
        const int fd = open_loopback(server.port());
        if (fd < 0) {
            break;
        }
        held[held_count++] = fd;
    }
    SAFETY_CRIT_ASSERT(held_count == kMaxConnections);
    // Give the loop time to accept the holders, then verify the count.
    std::this_thread::sleep_for(300ms);
    SAFETY_CRIT_ASSERT(server.server().active_connections() == kMaxConnections);
    // The excess connection is accepted and closed immediately.
    const int excess = open_loopback(server.port());
    SAFETY_CRIT_ASSERT(excess >= 0);
    const ReadResult result = read_until_close(excess, 1500ms);
    ::close(excess);
    SAFETY_CRIT_ASSERT(result.peer_closed);
    SAFETY_CRIT_ASSERT(result.data.empty());
    for (std::size_t i = 0; i < held_count; ++i) {
        ::close(held[i]);
    }
}

SAFETY_CRIT_TEST_CASE(HttpServer, NoFdLeakAcrossCyclesAndCleanShutdown) {
    auto server = std::make_unique<TestServer>(test_config(), &register_hello);
    SAFETY_CRIT_ASSERT(server->start());
    // Let steady state settle, then snapshot.
    {
        const int fd = open_loopback(server->port());
        SAFETY_CRIT_ASSERT(fd >= 0);
        SAFETY_CRIT_ASSERT(send_request(fd, "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n"));
        (void)read_until_close(fd, 250ms);
        ::close(fd);
    }
    std::this_thread::sleep_for(300ms);
    const std::size_t before = open_fd_count();
    for (int i = 0; i < 20; ++i) {
        const int fd = open_loopback(server->port());
        SAFETY_CRIT_ASSERT(fd >= 0);
        SAFETY_CRIT_ASSERT(send_request(fd, "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n"));
        (void)read_until_close(fd, 250ms);
        ::close(fd);  // server observes EOF and closes its side
    }
    std::this_thread::sleep_for(300ms);
    SAFETY_CRIT_ASSERT(server->server().active_connections() == 0);
    SAFETY_CRIT_ASSERT(open_fd_count() == before);
    // Shutdown: run() returns on the stop token and close() releases the
    // listening socket; no fd survives.
    server->shutdown();
    SAFETY_CRIT_ASSERT(open_fd_count() <= before - 1);
    server.reset();
}

}  // namespace

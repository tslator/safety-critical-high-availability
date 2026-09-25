// Bounded HTTP/1.1 server core (T-0035, DEC-0014 §5).
//
// Hand-rolled, hard-bounded, single-threaded HTTP/1.1 server for the
// Phase 6 observability daemon. One `poll()` loop: accept + per-connection
// fixed-buffer state machine. Supported methods: GET, and HEAD (identical
// headers, body omitted); every other method is answered 405. Routes are
// registered as path -> handler producing a fixed-buffer response;
// unknown paths are 404.
//
// Bounds (DEC-0014 §5, all compile-time or validated config):
//   - request line            <= 512 bytes         (431 on overflow)
//   - headers                 <= 32 lines / 8 KiB  (431 on overflow)
//   - request timeout         2 s default          (408, connection closed)
//   - keep-alive idle timeout 5 s default          (connection closed)
//   - concurrent connections  <= 16                (excess: accept and
//                                                   close immediately)
//   - response body           <= 64 KiB            (500 on overflow)
// Requests carrying a body (Content-Length present) are rejected 400: this
// server accepts no request bodies. Malformed requests are 400 and close
// the connection. Responses are built in fixed buffers with the snprintf
// idiom; no allocation happens per request (all containers are bounded and
// created once at construction/open).
//
// Threading: the server itself is single-threaded (no threads are created;
// the loop is trivially TSan-clean). `run()` blocks until the passed
// std::stop_token is satisfied or the sig_atomic_t stop flag is set
// (SIGTERM/SIGINT via install_signal_handlers(), or request_stop()). A
// poll tick of 100 ms bounds stop latency. Callers that embed the server
// in a process run it on their main thread; tests may run it on a worker
// thread with a stop_source.
//
// Addresses: IPv4 only (demo scope, DEC-0014 §5). Default bind is
// 127.0.0.1:8080; port 0 requests an ephemeral port (bound_port() reports
// it). The endpoint is unauthenticated by design (documented demo scope).
//
// Error handling follows the standing idiom: bool fn(..., std::error_code&
// ec). Sockets are closed on every error path; fd lifetimes are observable
// in tests.
#pragma once

#include <stop_token>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace safety_crit::observability {

inline constexpr std::size_t kMaxConnections = 16u;
inline constexpr std::size_t kMaxRequestLineBytes = 512u;
inline constexpr std::size_t kMaxHeaderLines = 32u;
inline constexpr std::size_t kMaxHeaderBytes = 8192u;
inline constexpr std::size_t kMaxResponseBodyBytes = 65536u;
inline constexpr std::size_t kMaxRoutes = 16u;
inline constexpr std::size_t kMaxRoutePathBytes = 128u;
inline constexpr std::chrono::milliseconds kDefaultRequestTimeout{2000};
inline constexpr std::chrono::milliseconds kDefaultIdleTimeout{5000};
inline constexpr const char* kDefaultListenAddress = "127.0.0.1:8080";

// Response produced by a route handler (fixed-buffer semantics: `body` is
// a borrowed view of caller-owned storage; the server never grows it).
// HEAD requests send these exact headers with the body bytes omitted.
struct HttpResponse {
    int status = 200;
    std::string_view content_type = "text/plain; charset=utf-8";
    std::string_view body{};
};

// Route handler: fill `response`; return false to answer 500. Invoked on
// the server thread only.
using RouteHandler = bool (*)(void* context, HttpResponse& response);

struct HttpServerConfig {
    // "HOST:PORT" with numeric IPv4 HOST (validated by configure()).
    std::string listen_address{kDefaultListenAddress};
    // Deadline for a complete request head once bytes start arriving.
    std::chrono::milliseconds request_timeout = kDefaultRequestTimeout;
    // Deadline with no bytes at all on an idle keep-alive connection.
    std::chrono::milliseconds idle_timeout = kDefaultIdleTimeout;
};

// Validates and splits "HOST:PORT" (numeric IPv4 host, port 0..65535;
// port 0 means "ephemeral"). False with a description on any violation.
bool parse_listen_address(std::string_view text, std::string& host, std::uint16_t& port,
                          std::string& error);

// Single-threaded bounded server. Lifecycle: construct -> configure ->
// register_route* -> open_listener -> run (blocks until stopped) ->
// close (also implicit in the destructor).
class HttpServer {
public:
    HttpServer();
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Validates config (address, positive timeouts). EBUSY once the
    // listener is open.
    bool configure(const HttpServerConfig& config, std::error_code& ec);

    // Registers GET/HEAD route `path` (must start with '/', be unique,
    // <= kMaxRoutePathBytes, at most kMaxRoutes total).
    bool register_route(std::string_view path, RouteHandler handler, void* context,
                        std::error_code& ec);

    // socket + SO_REUSEADDR + bind + listen. EBUSY if already open.
    bool open_listener(std::error_code& ec);

    // Actual bound port (valid after open_listener; resolves port 0).
    std::uint16_t bound_port() const { return bound_port_; }

    // Blocks in the poll() loop until `stop` is requested or the
    // process-wide stop flag is set. Returns cleanly (closing nothing
    // beyond connections still open; call close() afterwards).
    void run(const std::stop_token& stop);

    // Closes the listening socket and all connections (idempotent).
    void close();

    // Live (accepted, not yet closed) connection count. Backed by an
    // atomic counter so it is safe to read from another thread (e.g. the
    // test harness); the server loop itself stays single-threaded.
    std::size_t active_connections() const;

    // sig_atomic_t stop pattern: handler sets the flag; run() observes it.
    static void install_signal_handlers();
    static void request_stop();
    static void reset_stop();

private:
    struct Connection;  // fixed-buffer per-connection state (defined in .cpp)
    struct Route {
        std::string path{};
        RouteHandler handler = nullptr;
        void* context = nullptr;
    };

    void close_connection(std::size_t index);
    void accept_pending();
    // Consumes complete requests from the connection buffer; returns false
    // when the connection must close.
    bool service_connection(std::size_t index);
    void apply_timeouts();
    bool send_response(Connection& conn, const HttpResponse& response, bool head,
                       bool keep_alive);
    bool send_simple(Connection& conn, int status, bool keep_alive);

    HttpServerConfig config_{};
    std::vector<Route> routes_{};  // bounded by kMaxRoutes at registration
    int listen_fd_{-1};
    std::uint16_t bound_port_{0};
    std::unique_ptr<Connection[]> connections_{};  // kMaxConnections entries
    std::atomic<std::size_t> live_connections_{0};
};

}  // namespace safety_crit::observability

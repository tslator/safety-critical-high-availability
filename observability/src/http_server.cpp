// Bounded HTTP/1.1 server core (T-0035, DEC-0014 §5). See header for the
// contract; every bound there is enforced here on pain of a 400/408/431/500
// response and (where specified) connection close.
#include "safety_crit/observability/http_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>

namespace safety_crit::observability {
namespace {

std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

constexpr int kPollTickMs = 100;
constexpr int kListenBacklog = 16;
// One request head at most per connection buffer: request line + headers
// (each at its bound) + the terminating CRLFCRLF.
constexpr std::size_t kHeadCapacity = kMaxRequestLineBytes + kMaxHeaderBytes + 4;

std::uint64_t monotonic_ns() {
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
}

const char* reason_phrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 431:
        return "Request Header Fields Too Large";
    default:
        return "Internal Server Error";
    }
}

// Position of the first occurrence of `needle` in [data, data+len), or npos.
std::size_t find_bytes(const char* data, std::size_t len, std::string_view needle) {
    if (needle.size() > len) {
        return std::string_view::npos;
    }
    for (std::size_t i = 0; i + needle.size() <= len; ++i) {
        if (std::memcmp(data + i, needle.data(), needle.size()) == 0) {
            return i;
        }
    }
    return std::string_view::npos;
}

bool token_chars(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char ch : text) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u) == 0 && std::strchr("!#$%&'*+-.^_`|~", ch) == nullptr) {
            return false;
        }
    }
    return true;
}

std::string_view lower_ascii(std::string_view text, char* scratch, std::size_t cap) {
    if (text.size() >= cap) {
        return {};
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        scratch[i] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(text[i])));
    }
    scratch[text.size()] = '\0';
    return std::string_view(scratch, text.size());
}

bool send_all(int fd, const char* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace

struct HttpServer::Connection {
    int fd = -1;
    std::array<char, kHeadCapacity> buf{};
    std::size_t len = 0;
    std::uint64_t last_activity_ns = 0;
    std::uint64_t first_byte_ns = 0;  // 0 while the buffer holds no request bytes
};

bool parse_listen_address(std::string_view text, std::string& host, std::uint16_t& port,
                          std::string& error) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string_view::npos) {
        error = "listen address must be HOST:PORT";
        return false;
    }
    host = std::string(text.substr(0, colon));
    const std::string_view port_text = text.substr(colon + 1);
    if (host.empty() || port_text.empty() || port_text.size() > 5) {
        error = "listen address must be HOST:PORT";
        return false;
    }
    unsigned int value = 0;
    for (const char ch : port_text) {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
            error = "listen port must be numeric";
            return false;
        }
        value = value * 10u + static_cast<unsigned>(ch - '0');
    }
    if (value > 65535u) {
        error = "listen port out of range";
        return false;
    }
    in_addr addr{};
    if (::inet_pton(AF_INET, host.c_str(), &addr) != 1) {
        error = "listen host must be a numeric IPv4 address";
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

HttpServer::HttpServer() : connections_(std::make_unique<Connection[]>(kMaxConnections)) {}

HttpServer::~HttpServer() {
    close();
}

bool HttpServer::configure(const HttpServerConfig& config, std::error_code& ec) {
    if (listen_fd_ >= 0) {
        ec = std::make_error_code(std::errc::device_or_resource_busy);
        return false;
    }
    std::string host;
    std::uint16_t port = 0;
    std::string error;
    if (!parse_listen_address(config.listen_address, host, port, error)) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (config.request_timeout <= std::chrono::milliseconds::zero() ||
        config.idle_timeout <= std::chrono::milliseconds::zero()) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    config_ = config;
    return true;
}

bool HttpServer::register_route(std::string_view path, RouteHandler handler, void* context,
                                std::error_code& ec) {
    if (handler == nullptr || path.empty() || path.front() != '/' ||
        path.size() > kMaxRoutePathBytes || routes_.size() >= kMaxRoutes) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    for (const Route& route : routes_) {
        if (route.path == path) {
            ec = std::make_error_code(std::errc::file_exists);
            return false;
        }
    }
    routes_.push_back(Route{std::string(path), handler, context});
    return true;
}

bool HttpServer::open_listener(std::error_code& ec) {
    if (listen_fd_ >= 0) {
        ec = std::make_error_code(std::errc::device_or_resource_busy);
        return false;
    }
    std::string host;
    std::uint16_t port = 0;
    std::string error;
    if (!parse_listen_address(config_.listen_address, host, port, error)) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
    int on = 1;
    (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ec = std::error_code(errno, std::generic_category());
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (::listen(listen_fd_, kListenBacklog) != 0) {
        ec = std::error_code(errno, std::generic_category());
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
        ec = std::error_code(errno, std::generic_category());
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    bound_port_ = ntohs(bound.sin_port);
    return true;
}

void HttpServer::close() {
    if (connections_) {
        for (std::size_t i = 0; i < kMaxConnections; ++i) {
            close_connection(i);
        }
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    bound_port_ = 0;
}

std::size_t HttpServer::active_connections() const {
    return live_connections_.load(std::memory_order_relaxed);
}

void HttpServer::install_signal_handlers() {
    (void)std::signal(SIGTERM, handle_signal);
    (void)std::signal(SIGINT, handle_signal);
}

void HttpServer::request_stop() {
    g_stop = 1;
}

void HttpServer::reset_stop() {
    g_stop = 0;
}

void HttpServer::close_connection(std::size_t index) {
    Connection& conn = connections_[index];
    if (conn.fd >= 0) {
        ::close(conn.fd);
        live_connections_.fetch_sub(1, std::memory_order_relaxed);
    }
    conn.fd = -1;
    conn.len = 0;
    conn.first_byte_ns = 0;
    conn.last_activity_ns = 0;
}

void HttpServer::accept_pending() {
    while (true) {
        const int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;  // EAGAIN/EWOULDBLOCK or a real error: nothing more now
        }
        if (live_connections_.load(std::memory_order_relaxed) >= kMaxConnections) {
            // Connection cap (DEC-0014 §5): accept and close immediately so
            // the client observes the refusal instead of a hung socket.
            ::close(fd);
            continue;
        }
        // Bound blocking sends (responses are <= 64 KiB + headers, but a
        // wedged client must never stall the single-threaded loop forever).
        const auto ms = config_.request_timeout.count();
        const timeval snd_timeout{static_cast<time_t>(ms / 1000),
                                  static_cast<suseconds_t>((ms % 1000) * 1000)};
        (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_timeout, sizeof(snd_timeout));
        for (std::size_t i = 0; i < kMaxConnections; ++i) {
            if (connections_[i].fd < 0) {
                connections_[i] = Connection{};
                connections_[i].fd = fd;
                connections_[i].last_activity_ns = monotonic_ns();
                live_connections_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
    }
}

bool HttpServer::send_response(Connection& conn, const HttpResponse& response, bool head,
                               bool keep_alive) {
    if (response.body.size() > kMaxResponseBodyBytes) {
        HttpResponse error_response{};
        error_response.status = 500;
        error_response.body = "response too large\n";
        return send_response(conn, error_response, head, keep_alive);
    }
    char header[512];
    const int written = std::snprintf(header, sizeof(header),
                                      "HTTP/1.1 %d %s\r\n"
                                      "Server: safety-critical-ha\r\n"
                                      "Content-Type: %.*s\r\n"
                                      "Content-Length: %zu\r\n"
                                      "Connection: %s\r\n\r\n",
                                      response.status, reason_phrase(response.status),
                                      static_cast<int>(response.content_type.size()),
                                      response.content_type.data(),
                                      response.body.size(),
                                      keep_alive ? "keep-alive" : "close");
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(header)) {
        return false;
    }
    if (!send_all(conn.fd, header, static_cast<std::size_t>(written))) {
        return false;
    }
    if (!head && !response.body.empty()) {
        if (!send_all(conn.fd, response.body.data(), response.body.size())) {
            return false;
        }
    }
    return true;
}

bool HttpServer::send_simple(Connection& conn, int status, bool keep_alive) {
    HttpResponse response{};
    response.status = status;
    switch (status) {
    case 400:
        response.body = "bad request\n";
        break;
    case 404:
        response.body = "not found\n";
        break;
    case 405:
        response.body = "method not allowed\n";
        break;
    case 408:
        response.body = "request timeout\n";
        break;
    case 431:
        response.body = "request header fields too large\n";
        break;
    default:
        response.body = "internal server error\n";
        break;
    }
    return send_response(conn, response, /*head=*/false, keep_alive);
}

bool HttpServer::service_connection(std::size_t index) {
    Connection& conn = connections_[index];
    while (true) {
        const std::size_t first_crlf = find_bytes(conn.buf.data(), conn.len, "\r\n");
        if (first_crlf == std::string_view::npos) {
            if (conn.len > kMaxRequestLineBytes) {
                (void)send_simple(conn, 431, /*keep_alive=*/false);
                return false;
            }
            return true;  // need more bytes
        }
        if (first_crlf > kMaxRequestLineBytes) {
            (void)send_simple(conn, 431, /*keep_alive=*/false);
            return false;
        }
        const std::size_t head_end = find_bytes(conn.buf.data(), conn.len, "\r\n\r\n");
        if (head_end == std::string_view::npos) {
            if (conn.len >= kHeadCapacity) {
                (void)send_simple(conn, 431, /*keep_alive=*/false);
                return false;
            }
            return true;  // need more bytes
        }
        // Header section bound: [first_crlf + 2, head_end). A request with
        // no headers has head_end == first_crlf (the terminating CRLFCRLF
        // directly follows the request line).
        const std::size_t headers_start = first_crlf + 2;
        const std::size_t headers_len =
            head_end >= headers_start ? head_end - headers_start : 0;
        if (headers_len > kMaxHeaderBytes) {
            (void)send_simple(conn, 431, /*keep_alive=*/false);
            return false;
        }
        // Lines in [headers_start, head_end): each CRLF-terminated line has
        // its LF inside the region except the last one (its CRLF is the
        // first half of the terminating CRLFCRLF), hence the +1.
        std::size_t header_lines = 0;
        for (std::size_t i = 0; i < headers_len; ++i) {
            if (conn.buf[headers_start + i] == '\n') {
                ++header_lines;
            }
        }
        if (header_lines + 1 > kMaxHeaderLines) {
            (void)send_simple(conn, 431, /*keep_alive=*/false);
            return false;
        }

        std::string_view request_line(conn.buf.data(), first_crlf);
        const std::size_t sp1 = request_line.find(' ');
        const std::size_t sp2 = request_line.rfind(' ');
        if (sp1 == std::string_view::npos || sp2 == sp1) {
            (void)send_simple(conn, 400, /*keep_alive=*/false);
            return false;
        }
        const std::string_view method = request_line.substr(0, sp1);
        const std::string_view target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
        const std::string_view version = request_line.substr(sp2 + 1);
        bool target_valid = !target.empty();
        for (const char ch : target) {
            const unsigned char u = static_cast<unsigned char>(ch);
            if (std::iscntrl(u) != 0 || ch == ' ' || ch == '\t') {
                target_valid = false;
                break;
            }
        }
        if (!token_chars(method) || !target_valid ||
            (version != "HTTP/1.0" && version != "HTTP/1.1")) {
            (void)send_simple(conn, 400, /*keep_alive=*/false);
            return false;
        }

        // Parse headers: syntax, Connection: close, body rejection.
        bool keep_alive = version == "HTTP/1.1";
        bool malformed = false;
        std::size_t pos = headers_start;
        while (pos < head_end && !malformed) {
            std::size_t line_end = find_bytes(conn.buf.data() + pos, head_end - pos, "\r\n");
            std::string_view line;
            if (line_end == std::string_view::npos) {
                // Last header line: its CRLF is the first half of the
                // terminating CRLFCRLF, so the region ends after the line.
                line = std::string_view(conn.buf.data() + pos, head_end - pos);
                pos = head_end;
            } else {
                line = std::string_view(conn.buf.data() + pos, line_end);
                pos += line_end + 2;
            }
            if (line.empty()) {
                malformed = true;
                break;
            }
            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0) {
                malformed = true;
                break;
            }
            std::string_view name = line.substr(0, colon);
            std::string_view value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') {
                value.remove_prefix(1);
            }
            char scratch[64];
            const std::string_view lowered = lower_ascii(name, scratch, sizeof(scratch));
            if (lowered == "content-length") {
                (void)send_simple(conn, 400, /*keep_alive=*/false);
                return false;  // request bodies are not accepted
            }
            if (lowered == "connection") {
                char value_scratch[32];
                const std::string_view lowered_value =
                    lower_ascii(value, value_scratch, sizeof(value_scratch));
                if (lowered_value == "close") {
                    keep_alive = false;
                }
            }
        }
        if (malformed) {
            (void)send_simple(conn, 400, /*keep_alive=*/false);
            return false;
        }

        // Route (query strings are ignored: only the path is matched).
        std::string_view path = target;
        const std::size_t question = path.find('?');
        if (question != std::string_view::npos) {
            path = path.substr(0, question);
        }
        HttpResponse response{};
        if (method != "GET" && method != "HEAD") {
            response.status = 405;
            response.body = "method not allowed\n";
        } else {
            const Route* match = nullptr;
            for (const Route& route : routes_) {
                if (path == route.path) {
                    match = &route;
                    break;
                }
            }
            if (match == nullptr) {
                response.status = 404;
                response.body = "not found\n";
            } else if (!match->handler(match->context, response)) {
                response = HttpResponse{};
                response.status = 500;
                response.body = "internal server error\n";
            }
        }
        if (!send_response(conn, response, method == "HEAD", keep_alive)) {
            return false;
        }

        // Consume the request head; keep any pipelined remainder for the
        // next iteration.
        const std::size_t consumed = head_end + 4;
        const std::size_t remainder = conn.len - consumed;
        if (remainder > 0) {
            std::memmove(conn.buf.data(), conn.buf.data() + consumed, remainder);
        }
        conn.len = remainder;
        conn.first_byte_ns = remainder > 0 ? monotonic_ns() : 0;
        conn.last_activity_ns = monotonic_ns();
        if (!keep_alive) {
            return false;
        }
        if (remainder == 0) {
            return true;
        }
    }
}

void HttpServer::apply_timeouts() {
    const std::uint64_t now = monotonic_ns();
    const std::uint64_t request_ns =
        static_cast<std::uint64_t>(config_.request_timeout.count()) * 1000000ull;
    const std::uint64_t idle_ns =
        static_cast<std::uint64_t>(config_.idle_timeout.count()) * 1000000ull;
    for (std::size_t i = 0; i < kMaxConnections; ++i) {
        Connection& conn = connections_[i];
        if (conn.fd < 0) {
            continue;
        }
        if (conn.len == 0) {
            if (now - conn.last_activity_ns >= idle_ns) {
                close_connection(i);  // idle: close without a response
            }
            continue;
        }
        if (now - conn.first_byte_ns >= request_ns) {
            (void)send_simple(conn, 408, /*keep_alive=*/false);
            close_connection(i);
        }
    }
}

void HttpServer::run(const std::stop_token& stop) {
    if (listen_fd_ < 0) {
        return;
    }
    while (!stop.stop_requested() && g_stop == 0) {
        pollfd pfds[kMaxConnections + 1];
        nfds_t nfds = 0;
        pfds[nfds].fd = listen_fd_;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        ++nfds;
        for (std::size_t i = 0; i < kMaxConnections; ++i) {
            if (connections_[i].fd < 0) {
                continue;
            }
            pfds[nfds].fd = connections_[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            ++nfds;
        }
        const int ready = ::poll(pfds, nfds, kPollTickMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;  // unrecoverable poll error: stop the loop
        }
        if (g_stop != 0 || stop.stop_requested()) {
            break;
        }
        apply_timeouts();
        if (pfds[0].revents & POLLIN) {
            accept_pending();
        }
        // Connection fds start at pfds[1]; map back by fd (close/accept may
        // have changed the table since it was built).
        for (nfds_t p = 1; p < nfds; ++p) {
            if (pfds[p].revents == 0) {
                continue;
            }
            std::size_t index = kMaxConnections;
            for (std::size_t i = 0; i < kMaxConnections; ++i) {
                if (connections_[i].fd == pfds[p].fd) {
                    index = i;
                    break;
                }
            }
            if (index >= kMaxConnections) {
                continue;  // closed since poll(); nothing to do
            }
            Connection& conn = connections_[index];
            const ssize_t n = ::recv(conn.fd, conn.buf.data() + conn.len,
                                     conn.buf.size() - conn.len, 0);
            if (n > 0) {
                if (conn.len == 0) {
                    conn.first_byte_ns = monotonic_ns();
                }
                conn.len += static_cast<std::size_t>(n);
                conn.last_activity_ns = monotonic_ns();
                if (!service_connection(index)) {
                    close_connection(index);
                }
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            close_connection(index);  // peer closed or read error
        }
    }
}

}  // namespace safety_crit::observability

// Observability daemon assembly (T-0038, DEC-0014 §9/#10). See daemon.hpp.
#include "safety_crit/observability/daemon.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#include "safety_crit/observability/event_log.hpp"
#include "safety_crit/observability/event_metrics.hpp"
#include "safety_crit/observability/health_report.hpp"
#include "safety_crit/observability/http_server.hpp"
#include "safety_crit/observability/region_metrics.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"
#include "safety_crit/shared_memory/shm_attach.hpp"

namespace safety_crit::observability {

namespace {

bool parse_u64(std::string_view text, std::uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        if (value > (UINT64_MAX - 9u) / 10u) {
            return false;
        }
        value = value * 10u + static_cast<std::uint64_t>(c - '0');
    }
    out = value;
    return true;
}

// shm_open name -> backing file path ("/name" -> "/dev/shm/name"). Used
// only for a non-blocking existence probe; the mapping itself always goes
// through SharedRegionHandle.
std::filesystem::path region_backing_path(const std::string& region_name) {
    return std::filesystem::path("/dev/shm" + region_name);
}

// Zeroed raw SharedRegion image (DEC-0014 §2 degraded sentinel). Built
// exactly like a fresh anonymous shm object (zero-filled bytes, never
// C++-constructed); verify_identity() on it fails by construction, so a
// context pointing here renders the "degraded" verdict while handlers
// never touch a possibly-dead live mapping.
class ZeroRegionImage {
public:
    using Region = safety_crit::shared_memory::SharedRegion;

    ZeroRegionImage() {
        void* p = nullptr;
        if (::posix_memalign(&p, alignof(Region), sizeof(Region)) != 0) {
            return;
        }
        std::memset(p, 0, sizeof(Region));
        mapping_ = p;
    }
    ~ZeroRegionImage() { std::free(mapping_); }
    ZeroRegionImage(const ZeroRegionImage&) = delete;
    ZeroRegionImage& operator=(const ZeroRegionImage&) = delete;

    [[nodiscard]] bool ok() const { return mapping_ != nullptr; }
    [[nodiscard]] Region* region() const { return static_cast<Region*>(mapping_); }

private:
    void* mapping_ = nullptr;
};

// Supervisor drain-witness data-loss event (supervisor/witness_events.hpp
// vocabulary; literal duplicated to keep the observability->supervisor
// dependency edge out of the allowlist).
constexpr const char* kDataLossEventName = "data_loss_observed";

class DaemonRuntime {
public:
    explicit DaemonRuntime(const DaemonConfig& cfg) : cfg_(cfg) {}

    // --once path: single attach, single collect, snapshot to `out`.
    int run_once(std::ostream& out, std::ostream& err) {
        err_ = &err;
        if (!zero_.ok()) {
            err << "observability: sentinel allocation failed\n";
            return 1;
        }
        ctx_.region = zero_.region();
        ctx_.start_ns = now_unix_ns();
        ctx_.registry = &registry_;
        if (!attach_now()) {
            return 1;
        }
        std::error_code ec;
        if (!declare_event_metrics(registry_, ec)) {
            err << "observability: metric declaration failed (" << ec.message() << ")\n";
            return 1;
        }
        if (!declare_region_metrics(registry_, ec)) {
            err << "observability: metric declaration failed (" << ec.message() << ")\n";
            return 1;
        }
        // Baseline the data-loss counter so /metrics always exposes the
        // series (a declared-but-unset family renders HELP/TYPE with no
        // sample line, which breaks naive `^data_loss_events_total` scrapes).
        if (!registry_.set("data_loss_events_total", "", 0.0, ec)) {
            err << "observability: data-loss baseline failed (" << ec.message() << ")\n";
            return 1;
        }
        open_event_log(err, true);
        ctx_.event_reader = reader_open_ ? &reader_ : nullptr;
        refresh_locked(err);
        std::string body;
        if (!render_status_json(ctx_, body)) {
            err << "observability: status render failed\n";
            return 1;
        }
        out << body << "\n";
        return 0;
    }

    // Server path: attach (bounded-wait for the region to appear), then
    // serve /health /metrics /status until stop flag or `ticks` budget.
    int run_server(std::ostream& out, std::ostream& err) {
        (void)out;
        err_ = &err;
        if (!zero_.ok()) {
            err << "observability: sentinel allocation failed\n";
            return 1;
        }
        ctx_.region = zero_.region();
        ctx_.start_ns = now_unix_ns();
        // DEC-0014 §9: SIGTERM/SIGINT set the HttpServer stop flag for a
        // clean exit(0) shutdown.
        HttpServer::install_signal_handlers();
        std::error_code ec;
        if (!declare_event_metrics(registry_, ec)) {
            err << "observability: metric declaration failed (" << ec.message() << ")\n";
            return 1;
        }
        if (!declare_region_metrics(registry_, ec)) {
            err << "observability: metric declaration failed (" << ec.message() << ")\n";
            return 1;
        }
        // Baseline the data-loss counter (see once path): /metrics must
        // always expose the sample line.
        if (!registry_.set("data_loss_events_total", "", 0.0, ec)) {
            err << "observability: data-loss baseline failed (" << ec.message() << ")\n";
            return 1;
        }
        // Startup wait: the supervisor creates the region before spawning
        // workers; a daemon started first retries instead of crash-looping.
        while (!region_ok_ && HttpServer::stop_requested() == false) {
            if (attach_now()) {
                break;
            }
            std::this_thread::sleep_for(cfg_.poll_interval);
        }
        if (!region_ok_ && HttpServer::stop_requested()) {
            return 0;  // signalled away during startup
        }
        open_event_log(err, true);
        ctx_.registry = &registry_;
        ctx_.event_reader = reader_open_ ? &reader_ : nullptr;

        HttpServerConfig server_config;
        server_config.listen_address = cfg_.listen_address;
        if (!server_.configure(server_config, ec)) {
            err << "observability: configure failed: " << ec.message() << "\n";
            return 1;
        }
        if (!register_daemon_routes(server_, ctx_, ec)) {
            err << "observability: route registration failed: " << ec.message() << "\n";
            return 1;
        }
        if (!server_.open_listener(ec)) {
            err << "observability: listen " << cfg_.listen_address
                << " failed: " << ec.message() << "\n";
            return 1;
        }
        server_.set_tick_hook(&DaemonRuntime::tick_trampoline, this);
        last_poll_ = std::chrono::steady_clock::now();

        const std::stop_source stop;
        server_.run(stop.get_token());
        server_.close();
        return 0;
    }

private:
    static void tick_trampoline(void* context) {
        static_cast<DaemonRuntime*>(context)->on_tick();
    }

    void on_tick() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_poll_ < cfg_.poll_interval) {
            return;
        }
        last_poll_ = now;
        refresh_locked(*err_);
        ++cycles_;
        if (cfg_.ticks != 0u && cycles_ >= cfg_.ticks) {
            HttpServer::request_stop();
        }
    }

    // Live-attach the region if the backing file is present and the object
    // verifies. Leaves ctx_.region on the live mapping on success.
    bool attach_now() {
        std::error_code fec;
        const bool present =
            std::filesystem::exists(region_backing_path(cfg_.region_name), fec);
        if (!present) {
            return false;
        }
        auto handle =
            shared_memory::SharedRegionHandle::create_or_open(cfg_.region_name.c_str());
        if (!handle.ok() || !shared_memory::verify_identity(*handle.get())) {
            if (handle.ok()) {
                handle.detach();
            }
            return false;
        }
        handle_ = std::move(handle);
        ctx_.region = handle_.get();
        region_ok_ = true;
        return true;
    }

    void open_event_log(std::ostream& err, bool verbose) {
        if (cfg_.event_log_path.empty() || reader_open_) {
            return;
        }
        std::error_code ec;
        if (!reader_.open(cfg_.event_log_path, ec)) {
            if (verbose && !reader_warned_) {
                err << "observability: event log '" << cfg_.event_log_path
                    << "' unavailable (" << ec.message() << "); event metrics stay empty\n";
                reader_warned_ = true;
            }
            return;
        }
        if (!loss_reader_.open(cfg_.event_log_path, ec)) {
            err << "observability: event log reopen failed (" << ec.message() << ")\n";
            return;
        }
        reader_open_ = true;
        loss_open_ = true;
    }

    // One poll cycle: region liveness probe, region metric sample, event
    // log drain, and the data-loss witness scan (separate reader; the
    // counter is NEVER derived from event-log content the collector
    // aggregates -- only observe_data_loss moves it, DEC-0014 §8).
    void refresh_locked(std::ostream& err) {
        std::error_code ec;
        probe_region(err);
        if (region_ok_) {
            (void)collect_region_metrics(*ctx_.region, registry_, ec);
        }
        open_event_log(err, false);
        scan_data_loss();
        if (reader_open_ && !collector_.poll(reader_, registry_, ec)) {
            reader_.close();
            loss_reader_.close();
            reader_open_ = false;
            loss_open_ = false;  // resume by reopening next cycle
        }
    }

    // DEC-0014 §2 mid-run region loss: vanished backing file or a failing
    // identity read flips the context to the zero sentinel (degraded);
    // reappearance re-attaches. Handlers never see a dead mapping.
    void probe_region(std::ostream& err) {
        std::error_code fec;
        const bool present =
            std::filesystem::exists(region_backing_path(cfg_.region_name), fec);
        if (!present) {
            if (region_ok_) {
                err << "observability: region vanished; health degraded\n";
                region_ok_ = false;
                handle_.detach();
                ctx_.region = zero_.region();
            }
            return;
        }
        if (!region_ok_) {
            if (attach_now()) {
                err << "observability: region attached\n";
            }
            return;
        }
        if (!shared_memory::verify_identity(*handle_.get())) {
            err << "observability: region identity failed; health degraded\n";
            region_ok_ = false;
            handle_.detach();
            ctx_.region = zero_.region();
        }
    }

    void scan_data_loss() {
        if (!loss_open_) {
            return;
        }
        while (true) {
            EventRecord record;
            std::string error;
            const ReadStatus status = loss_reader_.read_next(record, error);
            if (status == ReadStatus::kEnd || status == ReadStatus::kIncomplete) {
                return;
            }
            if (status == ReadStatus::kError) {
                loss_reader_.close();
                loss_open_ = false;  // resume by reopening next cycle
                return;
            }
            if (record.component != "supervisor" || record.event != kDataLossEventName) {
                continue;
            }
            double lost = 0.0;
            if (find_json_number_field(record.raw, "lost", lost) && lost >= 0.0) {
                std::error_code ec;
                (void)collector_.observe_data_loss(static_cast<std::uint64_t>(lost),
                                                   registry_, ec);
            }
        }
    }

    DaemonConfig cfg_{};
    std::ostream* err_ = nullptr;  // set by run_once/run_server before use
    HttpServer server_{};
    MetricsRegistry registry_{};
    EventMetricsCollector collector_{};
    EventLogReader reader_{};
    EventLogReader loss_reader_{};
    shared_memory::SharedRegionHandle handle_{};
    ZeroRegionImage zero_{};
    EndpointContext ctx_{};
    std::chrono::steady_clock::time_point last_poll_{};
    std::uint64_t cycles_{0};
    bool region_ok_{false};
    bool reader_open_{false};
    bool loss_open_{false};
    bool reader_warned_{false};
};

}  // namespace

bool parse_daemon_args(int argc, const char* const* argv, DaemonConfig& cfg,
                       std::string& error) {
    // argv holds the flags after the subcommand (main passes argv + 2);
    // every element is parsed, unlike the perturb parser which reserves
    // argv[0] for the action word.
    for (int i = 0; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--once") {
            cfg.once = true;
        } else if (arg == "--listen") {
            if (++i >= argc) {
                error = "observability: --listen requires HOST:PORT";
                return false;
            }
            std::string host;
            std::uint16_t port = 0;
            std::string address_error;
            if (!parse_listen_address(argv[i], host, port, address_error)) {
                error = "observability: --listen " + address_error;
                return false;
            }
            cfg.listen_address = argv[i];
        } else if (arg == "--region") {
            if (++i >= argc || argv[i][0] != '/' || argv[i][1] == '\0') {
                error = "observability: --region requires an absolute shm name";
                return false;
            }
            cfg.region_name = argv[i];
        } else if (arg == "--event-log") {
            if (++i >= argc || argv[i][0] == '\0') {
                error = "observability: --event-log requires a file path";
                return false;
            }
            cfg.event_log_path = argv[i];
        } else if (arg == "--poll-interval-ms") {
            std::uint64_t ms = 0;
            if (++i >= argc || !parse_u64(argv[i], ms) || ms < 1u ||
                ms > static_cast<std::uint64_t>(kMaxDaemonPollInterval.count())) {
                error = "observability: --poll-interval-ms requires an integer in "
                        "1..3600000";
                return false;
            }
            cfg.poll_interval = std::chrono::milliseconds(ms);
        } else if (arg == "--ticks") {
            std::uint64_t ticks = 0;
            if (++i >= argc || !parse_u64(argv[i], ticks) || ticks < 1u) {
                error = "observability: --ticks requires a positive integer";
                return false;
            }
            cfg.ticks = ticks;
        } else {
            error = "observability: unknown argument: " + std::string(arg);
            return false;
        }
    }
    return true;
}

int run_daemon(const DaemonConfig& cfg, std::ostream& out, std::ostream& err) {
    DaemonRuntime runtime(cfg);
    if (cfg.once) {
        return runtime.run_once(out, err);
    }
    return runtime.run_server(out, err);
}

}  // namespace safety_crit::observability

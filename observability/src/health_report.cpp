// Health report and status endpoint implementation (T-0037, DEC-0014 §7).

#include "safety_crit/observability/health_report.hpp"

#include <array>
#include <cstdio>
#include <string_view>

#include "safety_crit/observability/region_metrics.hpp"

namespace safety_crit::observability {

namespace shm = safety_crit::shared_memory;

namespace {

void append_text(std::string& out, std::string_view text) {
    out.append(text.data(), text.size());
}

// snprintf idiom (repo standard): fixed stack buffer, longest %llu fits.
void append_num(std::string& out, const char* fmt, unsigned long long value) {
    char buf[48];
    const int n = std::snprintf(buf, sizeof(buf), fmt, value);
    if (n > 0) {
        out.append(buf, static_cast<std::size_t>(n));
    }
}

// JSON string escaping for values that are not statically known (event-log
// component keys): double quote, backslash, the short control escapes, and
// any other control byte as \u00XX. Values from our own writers are plain
// identifiers; this keeps hostile input from producing invalid JSON.
void append_json_string(std::string& out, std::string_view value) {
    out.push_back('"');
    char esc[8];
    for (const char c : value) {
        switch (c) {
            case '"': append_text(out, "\\\""); break;
            case '\\': append_text(out, "\\\\"); break;
            case '\n': append_text(out, "\\n"); break;
            case '\r': append_text(out, "\\r"); break;
            case '\t': append_text(out, "\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    if (std::snprintf(esc, sizeof(esc), "\\u%04x",
                                      static_cast<unsigned>(
                                          static_cast<unsigned char>(c))) > 0) {
                        append_text(out, esc);
                    }
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

std::uint64_t uptime_ms(const EndpointContext& ctx, std::uint64_t now_ns) {
    return now_ns > ctx.start_ns ? (now_ns - ctx.start_ns) / 1000000u : 0u;
}

// One ownership read per ring, shared by the verdict and the rendering.
struct RingState {
    shm::OwnershipToken token{};
    bool owned = false;
};

std::array<RingState, shm::kMaxWorkers> read_ring_states(const EndpointContext& ctx) {
    std::array<RingState, shm::kMaxWorkers> states{};
    for (std::size_t i = 0; i < shm::kMaxWorkers; ++i) {
        states[i].owned = shm::read_ownership(*ctx.region, i, states[i].token);
    }
    return states;
}

bool handle_health(void* context, HttpResponse& response) {
    EndpointContext& ctx = *static_cast<EndpointContext*>(context);
    if (!render_health_json(ctx, ctx.health_body)) {
        return false;
    }
    response.status = 200;
    response.content_type = "application/json";
    response.body = ctx.health_body;
    return true;
}

bool handle_metrics(void* context, HttpResponse& response) {
    EndpointContext& ctx = *static_cast<EndpointContext*>(context);
    ctx.registry->render_prometheus(ctx.metrics_body);
    response.status = 200;
    response.content_type = "text/plain; version=0.0.4; charset=utf-8";
    response.body = ctx.metrics_body;
    return true;
}

bool handle_status(void* context, HttpResponse& response) {
    EndpointContext& ctx = *static_cast<EndpointContext*>(context);
    if (!render_status_json(ctx, ctx.status_body)) {
        return false;
    }
    response.status = 200;
    response.content_type = "application/json";
    response.body = ctx.status_body;
    return true;
}

}  // namespace

bool render_health_json(const EndpointContext& ctx, std::string& out) {
    if (ctx.region == nullptr || ctx.registry == nullptr) {
        return false;
    }
    // Daemon contract: declare_event_metrics first (T-0042). A declared
    // family without samples yet means "no loss observed" -> 0.
    double data_loss = 0.0;
    if (!ctx.registry->has_family("data_loss_events_total")) {
        return false;
    }
    std::error_code ec;
    if (!ctx.registry->get("data_loss_events_total", "", data_loss, ec) &&
        ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        return false;
    }
    const std::uint64_t now_ns = ctx.now();
    const bool identity_ok = shm::verify_identity(*ctx.region);
    const std::array<RingState, shm::kMaxWorkers> rings = read_ring_states(ctx);
    bool all_owned = identity_ok;
    for (const RingState& ring : rings) {
        all_owned = all_owned && ring.owned;
    }
    const bool ok = all_owned && data_loss == 0.0;

    out.clear();
    append_text(out, "{\"ts\":");
    append_num(out, "%llu", static_cast<unsigned long long>(now_ns));
    append_text(out, ",\"status\":");
    append_json_string(out, ok ? "ok" : "degraded");
    append_text(out, ",\"uptime_ms\":");
    append_num(out, "%llu", static_cast<unsigned long long>(uptime_ms(ctx, now_ns)));
    append_text(out, ",\"workers\":[");
    for (std::size_t i = 0; i < shm::kMaxWorkers; ++i) {
        if (i != 0) {
            append_text(out, ",");
        }
        const std::uint64_t status_word =
            ctx.region->worker_status[i].status.load(std::memory_order_acquire);
        append_text(out, "{\"id\":");
        append_num(out, "%u", static_cast<unsigned>(i));
        append_text(out, ",\"state\":");
        append_num(out, "%d",
                   static_cast<int>(worker_status_metric_value(status_word)));
        append_text(out, ",\"last_sequence\":");
        append_num(out, "%llu", static_cast<unsigned long long>(ctx.region->rings[i].pushed()));
        append_text(out, "}");
    }
    append_text(out, "],\"ownership\":[");
    for (std::size_t i = 0; i < shm::kMaxWorkers; ++i) {
        if (i != 0) {
            append_text(out, ",");
        }
        const RingState& ring = rings[i];
        append_text(out, "{\"ring\":");
        append_num(out, "%u", static_cast<unsigned>(i));
        // Unreadable ownership (unassigned / transfer in flight) renders the
        // sentinel owner and epoch 0.
        append_text(out, ",\"physical_owner\":");
        append_num(out, "%u", ring.owned ? ring.token.physical_owner
                                         : shm::kUnassignedPhysicalOwner);
        append_text(out, ",\"epoch\":");
        append_num(out, "%llu", ring.owned ? ring.token.epoch : 0ull);
        append_text(out, "}");
    }
    append_text(out, "],\"data_loss_events_total\":");
    append_num(out, "%llu", static_cast<unsigned long long>(data_loss));
    append_text(out, "}");
    return true;
}

bool render_status_json(const EndpointContext& ctx, std::string& out) {
    if (ctx.region == nullptr || ctx.registry == nullptr) {
        return false;
    }
    const std::uint64_t now_ns = ctx.now();
    const bool identity_ok = shm::verify_identity(*ctx.region);
    const std::uint32_t stored_word = ctx.region->integrity_word.load(std::memory_order_acquire);
    const bool integrity_ok = shm::compute_region_integrity(*ctx.region) == stored_word;
    const std::array<RingState, shm::kMaxWorkers> rings = read_ring_states(ctx);

    out.clear();
    append_text(out, "{\"ts\":");
    append_num(out, "%llu", static_cast<unsigned long long>(now_ns));
    append_text(out, ",\"uptime_ms\":");
    append_num(out, "%llu", static_cast<unsigned long long>(uptime_ms(ctx, now_ns)));
    append_text(out, ",\"region\":{\"version\":");
    append_num(out, "%u", ctx.region->identity.version);
    append_text(out, ",\"identity_ok\":");
    append_num(out, "%d", identity_ok ? 1 : 0);
    append_text(out, ",\"integrity_ok\":");
    append_num(out, "%d", integrity_ok ? 1 : 0);
    append_text(out, ",\"integrity_word\":");
    append_num(out, "%u", stored_word);
    append_text(out, ",\"global_seq\":");
    append_num(out, "%llu",
               static_cast<unsigned long long>(
                   ctx.region->global_seq.load(std::memory_order_acquire)));
    append_text(out, "},\"rings\":[");
    for (std::size_t i = 0; i < shm::kMaxWorkers; ++i) {
        if (i != 0) {
            append_text(out, ",");
        }
        const RingState& ring = rings[i];
        append_text(out, "{\"ring\":");
        append_num(out, "%u", static_cast<unsigned>(i));
        append_text(out, ",\"owned\":");
        append_num(out, "%d", ring.owned ? 1 : 0);
        append_text(out, ",\"physical_owner\":");
        append_num(out, "%u", ring.owned ? ring.token.physical_owner
                                         : shm::kUnassignedPhysicalOwner);
        append_text(out, ",\"epoch\":");
        append_num(out, "%llu", ring.owned ? ring.token.epoch : 0ull);
        append_text(out, ",\"sequence\":");
        append_num(out, "%llu", static_cast<unsigned long long>(ctx.region->rings[i].pushed()));
        append_text(out, ",\"corruptions\":");
        append_num(out, "%llu",
                   static_cast<unsigned long long>(ctx.region->rings[i].corruption_count()));
        append_text(out, "}");
    }
    append_text(out, "],\"event_log\":{\"available\":");
    if (ctx.event_reader == nullptr) {
        append_text(out, "0,\"watermark\":0,\"components\":[]}}");
        return true;
    }
    append_text(out, "1,\"watermark\":");
    append_num(out, "%llu", static_cast<unsigned long long>(ctx.event_reader->watermark()));
    append_text(out, ",\"components\":[");
    bool first = true;
    for (const auto& [key, state] : ctx.event_reader->states()) {
        if (!first) {
            append_text(out, ",");
        }
        first = false;
        append_text(out, "{\"key\":");
        append_json_string(out, key);
        append_text(out, ",\"expected_seq\":");
        append_num(out, "%llu", static_cast<unsigned long long>(state.expected_seq));
        append_text(out, ",\"gaps\":");
        append_num(out, "%llu", static_cast<unsigned long long>(state.gaps));
        append_text(out, ",\"missed\":");
        append_num(out, "%llu", static_cast<unsigned long long>(state.missed));
        append_text(out, "}");
    }
    append_text(out, "]}}");
    return true;
}

bool register_daemon_routes(HttpServer& server, EndpointContext& ctx, std::error_code& ec) {
    if (ctx.region == nullptr || ctx.registry == nullptr) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    if (!server.register_route("/health", &handle_health, &ctx, ec)) {
        return false;
    }
    if (!server.register_route("/metrics", &handle_metrics, &ctx, ec)) {
        return false;
    }
    if (!server.register_route("/status", &handle_status, &ctx, ec)) {
        return false;
    }
    ec.clear();
    return true;
}

}  // namespace safety_crit::observability

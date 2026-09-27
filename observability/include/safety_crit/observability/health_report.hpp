// Health report and status endpoints (T-0037, DEC-0014 §7).
//
// The three daemon endpoint surfaces built on the T-0035 server core, the
// T-0040 registry, and the read-only observers (T-0041 region collector,
// T-0042 event metrics):
//
//   GET /health   always answers 200 with a JSON body; the *body* carries
//                 the verdict (never the status code, so orchestrators can
//                 distinguish "daemon down" from "system degraded").
//   GET /metrics  the Prometheus text rendering of the registry
//                 (T-0040 `render_prometheus`), content type
//                 text/plain; version=0.0.4; charset=utf-8.
//   GET /status   deeper JSON diagnostic snapshot: region version and
//                 integrity, per-ring counters, event-log watermarks and
//                 per-component gap counts, daemon uptime.
//
// Response shapes (authoritative; pinned by the T-0037 tests; integers as
// JSON numbers, booleans as 0/1 — the supervisor witness idiom of T-0034):
//
//   /health   {"ts":<unix_ns>,"status":"ok"|"degraded","uptime_ms":N,
//              "workers":[{"id":N,"state":N,"last_sequence":N},...],
//              "ownership":[{"ring":N,"physical_owner":N,"epoch":N},...],
//              "data_loss_events_total":N}
//   /status   {"ts":<unix_ns>,"uptime_ms":N,
//              "region":{"version":N,"identity_ok":0|1,"integrity_ok":0|1,
//                        "integrity_word":N,"global_seq":N},
//              "rings":[{"ring":N,"owned":0|1,"physical_owner":N,"epoch":N,
//                        "sequence":N,"corruptions":N},...],
//              "event_log":{"available":0|1,"watermark":N,
//                           "components":[{"key":"...","expected_seq":N,
//                                          "gaps":N,"missed":N},...]}}
//
// Verdict rules (/health): "ok" only when verify_identity() succeeds,
// every logical ring is owned (read_ownership() succeeds — an unassigned
// ring or an ownership transfer in flight reads as unowned), and
// data_loss_events_total == 0. Any violation -> "degraded"; the daemon
// keeps serving (DEC-0014 §2: region loss flips health, never exits).
// `state` is the numeric worker status (0=IDLE,1=RUNNING,2=CRASHED,
// 3=RECOVERING,4=DEGRADED — the worker_status metric mapping). Unreadable
// ownership renders physical_owner = kUnassignedPhysicalOwner, epoch = 0.
// `integrity_ok` compares compute_region_integrity() against the stored
// integrity_word; under live push traffic a transient mismatch is possible
// (never masking corruption — shared-memory T1.3 contract), so /health
// does not fold it in; /status reports it raw for diagnosis.
//
// Threading: handlers run on the server thread only (T-0035 contract).
// All inputs are borrowed (EndpointContext pointers must outlive route
// registration); the per-route response bodies live in the context so the
// borrowed HttpResponse body views stay valid until the send completes.
// data_loss_events_total is read from the registry (single-writer,
// atomic samples), so no shared-memory write ever happens here.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <system_error>

#include "safety_crit/observability/event_log.hpp"
#include "safety_crit/observability/http_server.hpp"
#include "safety_crit/observability/metrics.hpp"
#include "safety_crit/shared_memory/shared_region.hpp"

namespace safety_crit::observability {

// Borrowed inputs shared by the three route handlers. `region` and
// `registry` are required (registration fails EINVAL otherwise);
// `event_reader` is optional — /status then renders
// "event_log":{"available":0,"watermark":0,"components":[]} and /health
// is unaffected. `registry` must already carry the data_loss_events_total
// family (declare_event_metrics, T-0042); rendering fails (500) otherwise.
// A declared family without samples yet reads as 0.
struct EndpointContext {
    const safety_crit::shared_memory::SharedRegion* region = nullptr;
    MetricsRegistry* registry = nullptr;
    const EventLogReader* event_reader = nullptr;
    std::uint64_t start_ns = 0;
    // Injectable nanosecond clock (default CLOCK_REALTIME via now_unix_ns).
    std::function<std::uint64_t()> now = &now_unix_ns;

    // Per-route response scratch, touched on the server thread only.
    std::string health_body{};
    std::string metrics_body{};
    std::string status_body{};
};

// Builds the /health JSON body (shape above). False if a required input is
// missing or the data_loss_events_total family is absent from the registry.
bool render_health_json(const EndpointContext& ctx, std::string& out);

// Builds the /status JSON body (shape above). False if a required input is
// missing.
bool render_status_json(const EndpointContext& ctx, std::string& out);

// Registers "/health", "/metrics" and "/status" on `server`, all bound to
// `ctx` (must outlive the server). False with EINVAL when region/registry
// are null, or the underlying register_route error otherwise.
bool register_daemon_routes(HttpServer& server, EndpointContext& ctx, std::error_code& ec);

}  // namespace safety_crit::observability

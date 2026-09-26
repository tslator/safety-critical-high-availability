# T-0040: Metrics Registry and Prometheus Text Encoder

- Status: In Progress
- Owner: AI agent (opencode)
- Priority: High
- Depends on: None (subtask 1/3 of T-0036)
- Phase: Phase 6
- Related decision: [DEC-0014](../decisions/0014-phase6-observability-logging.md)

## Scope

First slice of the T-0036 decomposition: the pure in-memory metrics layer
in `safety_crit::observability` (`observability/include/safety_crit/
observability/metrics.hpp` + `src/metrics.cpp`). No region access, no event
log access, no sockets — those are T-0041/T-0042 and the daemon.

- `MetricType`: gauge and counter (Prometheus text format v0.0.4 names).
- `MetricsRegistry`:
  - `declare_family(name, help, type, label_key)`: validated family name
    (`[a-zA-Z_:][a-zA-Z0-9_:]*`, unique), optional single label key
    (every metric in the DEC-0014 §8 table has 0 or 1 labels); invalid or
    duplicate declarations are errors.
  - `value(name, label)`: reference to the sample's `std::atomic<double>`,
    created at 0 on first touch. Sample creation happens at collector setup
    time (single-threaded); steady-state updates are atomic stores and the
    scrape-path reads are atomic loads — no locks anywhere.
  - `render_prometheus(out)`: deterministic output — families sorted by
    name, samples sorted by label value; `# HELP` and `# TYPE` lines per
    family; one sample line per existing sample; a declared family with no
    samples renders its comment lines only; an empty registry renders an
    empty string.
- Label-value escaping per the Prometheus text format: `\\` → `\\\\`,
  `"` → `\"`, newline → `\n` (order matters). Numbers rendered with the
  `snprintf` idiom (`%.10g`, integers without decimal point).

## Acceptance Criteria

- Registry/encoder unit tests in both frameworks: type/help rendering,
  label escaping, deterministic output ordering, empty-registry output,
  duplicate/invalid declaration rejection, atomic update visible on next
  render.
- ASan+UBSan and TSan legs green (scrape vs. update exercised under TSan).
- `git diff` shows zero `shared-memory/` changes.

## Evidence

Record implementation and validation in [Phase 6 evidence](../evidence/phase-6.md).

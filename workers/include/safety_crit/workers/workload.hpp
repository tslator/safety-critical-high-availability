#pragma once

#include <array>
#include <cstdint>
#include <ranges>

namespace safety_crit::workers {

// Deterministic workload simulation (T2.1, DEC-0009 #1). Phase 2 has no
// external input rings (layout v3 is frozen): each worker tick synthesizes a
// "sensor reading" from a splitmix64 stream seeded by
// (seed_base, worker_idx, tick), runs it through the processing pipeline,
// and commits the result. Same (seed_base, worker_idx, tick) always yields
// byte-identical output -- the plan's idempotency property, testable without
// any I/O.
//
// All facilities here are hot-path safe: fixed-size std::array storage, no
// allocation, no syscalls, no exceptions; only fixed-width integer
// arithmetic (wraparound defined; no signed overflow is possible because
// every intermediate is computed in std::int64_t and narrowed by static_cast,
// which is well-defined two's-complement truncation in C++20).

// splitmix64 (Steele et al. / Vigna's reference stream): small state,
// long period, high-quality 64-bit outputs -- adequate for a workload
// simulator (not for cryptographic use). Reference-implementation structure:
// the state advances by the golden gamma inside next(); construction seeds
// the state WITHOUT advancing it.
struct Splitmix64 {
    std::uint64_t state{0};

    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049C1C1232F3FULL;
        return z ^ (z >> 31);
    }
};

// Derive the workload stream seed for one tick. Mixing is xor/multiply over
// fixed-width unsigned values -- deterministic across compilers and
// platforms.
constexpr std::uint64_t workload_seed(std::uint64_t seed_base, std::uint32_t worker_idx,
                                      std::uint64_t tick) {
    std::uint64_t z = seed_base ^
                      (static_cast<std::uint64_t>(worker_idx) * 0x9E3779B97F4A7C15ULL) ^
                      tick;
    // One splitmix64 round so nearby seeds do not produce nearby streams.
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049C1C1232F3FULL;
    return z ^ (z >> 31);
}

// One raw sensor sample. signal_to_noise is a 0..255 pseudo-Q8 quality value.
struct RawSample {
    std::uint16_t channel{0};
    std::int16_t value{0};
    std::uint8_t signal_to_noise{0};
};

// One simulated sensor burst fed to the pipeline.
constexpr std::size_t kSamplesPerTick = 16;

struct RawTick {
    std::uint32_t worker_idx{0};
    std::uint64_t tick{0};
    std::array<RawSample, kSamplesPerTick> samples{};
};

// Pipeline output: calibrated values of the accepted samples, in input
// order. Fixed capacity; overflow samples are dropped by std::views::take.
constexpr std::size_t kMaxOutSamples = 12;

struct ProcessedData {
    std::uint32_t worker_idx{0};
    std::uint64_t tick{0};
    std::uint32_t count{0};
    std::array<std::int32_t, kMaxOutSamples> values{};
};

// Acceptance threshold and calibration gain, plain constants so the manual
// witness can use exactly the same policy.
inline constexpr std::uint8_t kSnThreshold = 96;
inline constexpr std::int32_t kCalibGain = 131;  // ~0.51 in Q8

// Synthesize one raw tick deterministically from (seed_base, worker_idx,
// tick). Uses exactly 4 next() draws per sample so the stream consumption
// pattern cannot depend on data values.
inline RawTick generate_raw_tick(std::uint64_t seed_base, std::uint32_t worker_idx,
                                 std::uint64_t tick) {
    Splitmix64 rng{workload_seed(seed_base, worker_idx, tick)};
    RawTick raw{};
    raw.worker_idx = worker_idx;
    raw.tick = tick;
    for (std::size_t i = 0; i < kSamplesPerTick; ++i) {
        const std::uint64_t a = rng.next();
        const std::uint64_t b = rng.next();
        const std::uint64_t c = rng.next();
        const std::uint64_t d = rng.next();  // consumed, policy reserved
        (void)d;
        RawSample s{};
        s.channel = static_cast<std::uint16_t>(a % 8u);
        s.value = static_cast<std::int16_t>(b % 65536u);
        s.signal_to_noise = static_cast<std::uint8_t>(c % 256u);
        raw.samples[i] = s;
    }
    return raw;
}

// Calibrate one sample value: fixed-point Q8 multiply in int64, narrowed.
constexpr std::int32_t calibrate(std::int16_t value) {
    const std::int64_t product = static_cast<std::int64_t>(value) * kCalibGain;
    return static_cast<std::int32_t>(product >> 8);
}

// Toolchain guard (T2.1 verification finding): clang 14.0.6 against libstdc++
// 12.2 cannot instantiate <ranges> views at all -- even
// views::iota fails with "no member named 'begin'" inside
// view_interface (witnessed in the pinned clang-verify image; upstream
// libstdc++ workarounds for this landed with GCC 13). The std::ranges
// pipeline below therefore builds only where it compiles (gcc-primary
// baseline and CI GCC 13); elsewhere the semantically identical manual
// composition is used. The equivalence test witnesses the two paths against
// each other wherever the ranges path is active, i.e. on the primary path.
#if defined(__clang__) && defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE <= 12
#define SAFETY_CRIT_WORKERS_NO_RANGES 1
#endif

// C++20 ranges pipeline (plan Phase 2): filter SN threshold -> calibrate ->
// take fixed capacity. Pure; no side effects. Takes `raw` by value because
// filter_view over a non-simple view requires a mutable view object; the
// copy is a fixed POD on the stack (hot-path safe).
inline ProcessedData process_sensor_data(RawTick raw) {
    ProcessedData out{};
    out.worker_idx = raw.worker_idx;
    out.tick = raw.tick;
#ifdef SAFETY_CRIT_WORKERS_NO_RANGES
    // Fallback identical to the manual witness below (see guard above).
    for (std::size_t i = 0; i < kSamplesPerTick && out.count < kMaxOutSamples; ++i) {
        const RawSample& s = raw.samples[i];
        if (s.signal_to_noise >= kSnThreshold) {
            out.values[out.count] = calibrate(s.value);
            ++out.count;
        }
    }
    return out;
#else
    // Not const: filter_view caches its position, so range-for over the
    // composition needs a mutable view object.
    auto calibrated = raw.samples | std::views::filter([](const RawSample& s) {
                          return s.signal_to_noise >= kSnThreshold;
                      }) |
                      std::views::transform(
                          [](const RawSample& s) { return calibrate(s.value); }) |
                      std::views::take(kMaxOutSamples);
    auto dst = out.values.begin();
    for (std::int32_t v : calibrated) {
        *dst++ = v;
        ++out.count;
    }
    return out;
#endif
}

// Deliberately naive manual loop computing the same policy -- the equivalence
// witness against the ranges pipeline (plan test #4).
inline ProcessedData process_sensor_data_manual(const RawTick& raw) {
    ProcessedData out{};
    out.worker_idx = raw.worker_idx;
    out.tick = raw.tick;
    out.count = 0;
    for (std::size_t i = 0; i < kSamplesPerTick && out.count < kMaxOutSamples; ++i) {
        const RawSample& s = raw.samples[i];
        if (s.signal_to_noise >= kSnThreshold) {
            out.values[out.count] = calibrate(s.value);
            ++out.count;
        }
    }
    return out;
}

// Byte-wise equality over the meaningful fields (payload arrays compared up
// to `count` so uninitialized tail bytes cannot mask a difference in either
// direction... they cannot: both producers leave the same tail bytes, but the
// witness compares only what is semantically defined).
inline bool processed_equal(const ProcessedData& a, const ProcessedData& b) {
    if (a.worker_idx != b.worker_idx || a.tick != b.tick || a.count != b.count) {
        return false;
    }
    for (std::uint32_t i = 0; i < a.count; ++i) {
        if (a.values[i] != b.values[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace safety_crit::workers

// Phase 2, T2.1: workers core (config validation, deterministic workload,
// ranges-pipeline equivalence). DEC-0009 #1/#2; plan Phase 2 tests #1 and #4.

#include "test_framework.hpp"

#include <array>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "safety_crit/workers/pidfile.hpp"
#include "safety_crit/workers/worker_config.hpp"
#include "safety_crit/workers/workload.hpp"

namespace {
using namespace safety_crit::workers;

constexpr std::uint64_t kTestSeed = 0x0123456789ABCDEFULL;

constexpr WorkerConfig kValidConfig{};

WorkerConfig make_valid() {
    WorkerConfig c{};
    c.worker_idx = 0;
    c.ticks = 10;
    return c;
}
}  // namespace

SAFETY_CRIT_TEST_CASE(WorkersCore, CorruptionHookOptInResolution) {
    // T-0027 (DEC-0012 #5): production default must stay off; only the CLI
    // flag or SAFETY_CRIT_CORRUPT_HOOK=1 opt in.
    SAFETY_CRIT_ASSERT(!corruption_hook_opt_in(false, nullptr));
    SAFETY_CRIT_ASSERT(!corruption_hook_opt_in(false, "0"));
    SAFETY_CRIT_ASSERT(!corruption_hook_opt_in(false, ""));
    SAFETY_CRIT_ASSERT(corruption_hook_opt_in(false, "1"));
    SAFETY_CRIT_ASSERT(corruption_hook_opt_in(true, nullptr));
    SAFETY_CRIT_ASSERT(corruption_hook_opt_in(true, "0"));
}

SAFETY_CRIT_TEST_CASE(WorkersCore, ConfigValidation) {
    SAFETY_CRIT_ASSERT(validate_config(make_valid()));

    WorkerConfig bad = make_valid();
    bad.worker_idx = safety_crit::shared_memory::kMaxWorkers;  // names no region slot
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    bad = make_valid();
    bad.ticks = 0;
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    bad = make_valid();
    bad.tick_interval = std::chrono::milliseconds::zero();
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    bad = make_valid();
    bad.cpu_budget = std::chrono::microseconds::zero();
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    // Defaults: a default-constructed config carries sane durations but
    // ticks == 0, so it must NOT validate (tick count is intentional).
    SAFETY_CRIT_ASSERT(!validate_config(kValidConfig));

    bad = make_valid();
    bad.logical_ring = safety_crit::shared_memory::kMaxWorkers;
    SAFETY_CRIT_ASSERT(!validate_config(bad));

    bad = make_valid();
    bad.process_generation = 0;
    SAFETY_CRIT_ASSERT(!validate_config(bad));
}

SAFETY_CRIT_TEST_CASE(WorkersCore, Splitmix64ReferenceVectors) {
    // Vectors computed independently from the reference splitmix64 stream
    // (state seeded without advancing; gamma/golden constants as documented).
    Splitmix64 a{0};
    SAFETY_CRIT_ASSERT(a.next() == 0xfbba6911f190ba46ULL);
    SAFETY_CRIT_ASSERT(a.next() == 0x024f8679a144d853ULL);

    Splitmix64 b{42};
    SAFETY_CRIT_ASSERT(b.next() == 0x7e8fc6dc6510d9d6ULL);
}

SAFETY_CRIT_TEST_CASE(WorkersCore, WorkloadDeterminismPerWorkerTick) {
    // Same (seed_base, worker_idx, tick) -> byte-identical processed output,
    // across repeated generation (plan test #1).
    for (std::uint32_t w = 0; w < safety_crit::shared_memory::kMaxWorkers; ++w) {
        for (std::uint64_t t = 0; t < 100; ++t) {
            const RawTick r1 = generate_raw_tick(kTestSeed, w, t);
            const RawTick r2 = generate_raw_tick(kTestSeed, w, t);
            SAFETY_CRIT_ASSERT(processed_equal(process_sensor_data(r1),
                                               process_sensor_data(r2)));
            // Raw ticks must match field-for-field too.
            SAFETY_CRIT_ASSERT(r1.worker_idx == r2.worker_idx);
            SAFETY_CRIT_ASSERT(r1.tick == r2.tick);
            for (std::size_t i = 0; i < kSamplesPerTick; ++i) {
                SAFETY_CRIT_ASSERT(r1.samples[i].channel == r2.samples[i].channel);
                SAFETY_CRIT_ASSERT(r1.samples[i].value == r2.samples[i].value);
                SAFETY_CRIT_ASSERT(r1.samples[i].signal_to_noise ==
                                   r2.samples[i].signal_to_noise);
            }
        }
    }
}

SAFETY_CRIT_TEST_CASE(WorkersCore, WorkloadSeedSensitivity) {
    // Different seed_base or different worker_idx must not replay the same
    // stream (over a 100-tick corpus, at least one processed tick differs).
    std::size_t seed_diffs = 0;
    std::size_t worker_diffs = 0;
    for (std::uint64_t t = 0; t < 100; ++t) {
        const auto base = process_sensor_data(generate_raw_tick(kTestSeed, 0, t));
        const auto other_seed =
            process_sensor_data(generate_raw_tick(kTestSeed ^ 0xA5A5A5A5ULL, 0, t));
        const auto other_worker =
            process_sensor_data(generate_raw_tick(kTestSeed, 1, t));
        if (!processed_equal(base, other_seed)) {
            ++seed_diffs;
        }
        if (!processed_equal(base, other_worker)) {
            ++worker_diffs;
        }
    }
    SAFETY_CRIT_ASSERT(seed_diffs > 50);
    SAFETY_CRIT_ASSERT(worker_diffs > 50);
}

SAFETY_CRIT_TEST_CASE(WorkersCore, PipelineMatchesManualLoop) {
    // The C++20 ranges pipeline must produce exactly what the naive manual
    // loop produces over a 1000-tick corpus (plan test #4).
    for (std::uint32_t w = 0; w < safety_crit::shared_memory::kMaxWorkers; ++w) {
        for (std::uint64_t t = 0; t < 1000; ++t) {
            const RawTick raw = generate_raw_tick(kTestSeed, w, t);
            SAFETY_CRIT_ASSERT(processed_equal(process_sensor_data(raw),
                                               process_sensor_data_manual(raw)));
        }
    }
}

SAFETY_CRIT_TEST_CASE(WorkersCore, PipelinePolicyExplicitWitness) {
    // Hand-built corpus with known SN values: the acceptance set, the
    // kMaxOutSamples cap, and the calibrate() arithmetic are all witnessed
    // from the constants directly (not through generation).
    RawTick raw{};
    raw.worker_idx = 2;
    raw.tick = 7;
    const std::uint8_t sn[kSamplesPerTick] = {255, 95,  96,  100, 0,   96,  255, 95,
                                              96,  100, 96,  100, 100, 96,  100, 100};
    for (std::size_t i = 0; i < kSamplesPerTick; ++i) {
        raw.samples[i].channel = static_cast<std::uint16_t>(i % 8u);
        raw.samples[i].value = static_cast<std::int16_t>(i * 1000 + 3);
        raw.samples[i].signal_to_noise = sn[i];
    }
    // Accepted positions: SN >= kSnThreshold(96): 0,2,3,5,6,8,9,10,11,12,13,14,15
    // -> 13 candidates, capped by kMaxOutSamples(8); 5 dropped.
    const ProcessedData out = process_sensor_data(raw);
    SAFETY_CRIT_ASSERT(out.worker_idx == 2u);
    SAFETY_CRIT_ASSERT(out.tick == 7u);
    SAFETY_CRIT_ASSERT(out.count == kMaxOutSamples);
    const std::uint32_t accepted[kMaxOutSamples] = {0, 2, 3, 5, 6, 8, 9, 10};
    for (std::uint32_t i = 0; i < kMaxOutSamples; ++i) {
        const std::int32_t expected =
            calibrate(raw.samples[accepted[i]].value);
        SAFETY_CRIT_ASSERT(out.values[i] == expected);
        // Cross-check the calibration policy: (value * gain) >> 8.
        const std::int64_t manual =
            (static_cast<std::int64_t>(raw.samples[accepted[i]].value) * kCalibGain) >> 8;
        SAFETY_CRIT_ASSERT(out.values[i] == static_cast<std::int32_t>(manual));
    }
    SAFETY_CRIT_ASSERT(processed_equal(process_sensor_data(raw),
                                       process_sensor_data_manual(raw)));
}

SAFETY_CRIT_TEST_CASE(WorkersPidfile, ContractPathWriteRemove) {
    // T3.2 (DEC-0010 #2) writer side: canonical path, atomic write of the
    // decimal pid, and removal. The alive/dead liveness verdict and the
    // end-to-end cross-process contract are pinned by the fork integration
    // tests (plain build only).
    SAFETY_CRIT_ASSERT(worker_pid_path("/x", 2) == "/x/safety_crit_worker_2.pid");

    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) /
                     ("scrit_pidfile_test_" + std::to_string(::getpid()));
    SAFETY_CRIT_ASSERT(!ec);

    SAFETY_CRIT_ASSERT(write_worker_pidfile(dir, 1));  // creates the directory too
    const auto path = worker_pid_path(dir, 1);
    SAFETY_CRIT_ASSERT(std::filesystem::is_regular_file(path, ec));

    std::ifstream in(path);
    std::string content;
    SAFETY_CRIT_ASSERT(static_cast<bool>(in));
    std::getline(in, content);
    SAFETY_CRIT_ASSERT(content == std::to_string(static_cast<long>(::getpid())));

    remove_worker_pidfile(dir, 1);
    SAFETY_CRIT_ASSERT(!std::filesystem::exists(path, ec));
    remove_worker_pidfile(dir, 1);  // removing again is a silent no-op
    std::filesystem::remove_all(dir, ec);
}

# Safety-Critical High-Availability Application — Development Plan

> **Goal:** Build a demonstrable safety-critical HA application inside a Docker container
> that exercises monitors, supervisors, shared memory, lock-free buffering, atomic
> synchronization, and perturbation injection to prove recoverability.

---

## 0. Safety-Critical HA — Common Approaches (Background)

Before diving into the build, here is a survey of industry-standard techniques:

| Technique | What It Does | Typical Use |
|---|---|---|
| **Watchdog timers** | Hardware/software timer resets the system if the application stalls | Automotive, avionics, industrial PLCs |
| **Heartbeat monitoring** | Periodic signals between processes; missing beats trigger failover | Distributed control, HA clusters |
| **Triple Modular Redundancy (TMR)** | Three replicas vote; majority wins | Safety PLCs, aerospace |
| **Watchdog + supervisor process** | A separate supervisor restarts crashed workers | Linux systemd services, Docker |
| **Lock-free / wait-free data structures** | Avoid deadlock and priority inversion under RTOS | Trading engines, real-time control |
| **Deterministic shared memory** | Zero-copy IPC via mmap'd regions with sequence counters | Low-latency trading, robotics |
| **Perturbation / fault injection** | Deliberately corrupt memory, kill processes, delay threads to prove recovery | Certification (DO-178C, IEC 61508) |
| **Deterministic replay** | Record all inputs/events; replay to reproduce bugs | Testing, forensics |
| **Resource isolation** | cgroups, namespaces, CPU pinning, hugepages | Prevent noisy-neighbor & OOM |
| **Bounded execution / deadline monitoring** | Per-task CPU budgets; overrun triggers mitigation | Hard RTOS, automotive |

---

## 1. System Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                      Docker Container                           │
│  ┌─────────────┐   ┌─────────────┐   ┌──────────────────────┐   │
│  │  Supervisor  │──▶│  Monitor    │   │  Perturbation        │   │
│  │  (PID 1)     │   │  Daemon     │   │  Injector            │   │
│  │              │   │             │   │  (SIGSEGV, SIGKILL,  │   │
│  │  - Detects   │   │  - Polls    │   │   mem corruption,    │   │
│  │    failures  │   │    health   │   │   latency spikes)    │   │
│  │  - Restarts  │   │    metrics  │   │                      │   │
│  │    workers   │   │  - Alerts   │   │                      │   │
│  └──────┬───────┘   └──────┬──────┘   └──────────┬───────────┘   │
│         │                  │                      │               │
│         ▼                  ▼                      ▼               │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │              Shared Memory Region (mmap, /dev/shm)        │    │
│  │  ┌────────────┐  ┌────────────┐  ┌───────────────────┐   │    │
│  │  │ Ring Buffer│  │ Ring Buffer│  │ Atomic Status     │   │    │
│  │  │ (Worker A) │  │ (Worker B) │  │ Flags / Counters  │   │    │
│  │  │ Lock-free  │  │ Lock-free  │  │                   │   │    │
│  │  └────────────┘  └────────────┘  └───────────────────┘   │    │
│  └──────────────────────────────────────────────────────────┘    │
│         │                  │                      │               │
│  ┌──────▼───────┐   ┌──────▼───────┐   ┌──────────▼───────────┐  │
│  │  Worker A    │   │  Worker B    │   │  Worker C (spare)    │  │
│  │  (hot)       │   │  (hot)       │   │  (warm standby)      │  │
│  └──────────────┘   └──────────────┘   └──────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

### Component Summary

| Component | Role |
|---|---|
| **Supervisor** | PID 1; monitors all workers, restarts on crash, manages lifecycle |
| **Monitor Daemon** | Collects metrics, writes health reports, exposes a query API |
| **Worker Processes** | Do the "real work" (simulated task processing); run in hot/warm roles |
| **Shared Memory** | `mmap`-ed region in `/dev/shm` with lock-free ring buffers + atomic flags |
| **Perturbation Injector** | Sends signals, corrupts memory via `process_vm_writev`, injects latency |

---

## 2. Technology Stack

| Layer | Choice | Rationale |
|---|---|---|
| Language | **C++20** (C++23 if toolchain supports it) | Lock-free atomics, coroutines, ranges, views, zero-cost abstractions |
| Container | **Docker** (Debian bookworm base) | Full environment control, reproducible builds |
| IPC | `/dev/shm` mmap + `madvise(MADV_HUGEPAGE)` | Zero-copy, deterministic latency |
| Scheduling | **cgroups v2** CPU pinning + `sched_setscheduler(SCHED_FIFO)` | Hard real-time priority |
| Testing | **GoogleTest** (primary) + **Catch2 v3** (secondary) | Flexible framework selection via CMake option |
| Observability | Prometheus metrics + structured JSON logs | Standard HA observability |
| Build | **CMake** (3.24+) + **docker-compose** | Industry-standard build system, multi-framework test support |

---

## 3. Development Phases

### Phase 0 — Container & Tooling Setup _(Week 1)_

**Deliverables:**
- `Dockerfile` with GCC 13 / Clang 17 toolchain (C++20 support), real-time scheduling capabilities, debuggers
- `CMakeLists.txt` with C++20 standard, test framework abstraction layer
- `docker-compose.yml` for multi-process orchestration
- CI build verification (compile + run unit tests inside container)

**Key decisions:**
- Use `--cap-add=SYS_NICE --cap-add=SYS_PTRACE` for RT scheduling & ptrace-based fault injection
- Enable `--memory` and `--cpus` limits for resource-isolation realism
- Pre-install `gdb`, `perf`, `sysstat`, `valgrind`, `AddressSanitizer` toolchain
- CMake option `SAFETY_CRIT_TEST_FRAMEWORK` to switch between GoogleTest and Catch2 at build time

---

### Phase 1 — Shared Memory & Lock-Free Ring Buffers _(Week 2-3)_

**Sub-components:**

#### 1a. Shared Memory Layout
```cpp
// Align to cache line (64 bytes) to prevent false sharing
alignas(64) struct alignas(64) SharedRegion {
    // Per-worker ring buffers
    RingBufferHeader ring_buffers[MAX_WORKERS];

    // Atomic status flags — no locks ever
    std::atomic<uint64_t> worker_status[MAX_WORKERS];
    // Bit flags: RUNNING(1<<0), IDLE(1<<1), CRASHED(1<<2), RECOVERING(1<<3)

    // Global sequence counter for ordering
    std::atomic<uint64_t> global_seq{0};

    // Checksum / CRC for corruption detection
    std::atomic<uint32_t> integrity_word{0};

    // Padding to ensure each worker's region doesn't share a cache line
    char padding[MAX_WORKERS * (CACHE_LINE_SIZE - sizeof(RingBufferHeader))];
};
```

#### 1b. Lock-Free Ring Buffer (C++20 — Multiple Producers, Single Consumer per worker)
```cpp
// Key design:
// - Fixed-size power-of-2 buffer (enforced at construction)
// - Each slot: { sequence: AtomicU64, data: [u8; SLOT_SIZE] }
// - Producer CAS on head; consumer CAS on tail
// - Sequence numbers enforce happens-before
// - No malloc / no syscalls in hot path
// - Verify lock-freedom at runtime: slot.sequence.is_lock_free() == true

template<std::size_t BufferSize>
    requires (BufferSize > 0) && ((BufferSize & (BufferSize - 1)) == 0)  // power of 2
struct alignas(64) LockFreeRingBuffer {
    alignas(64) struct Slot {
        std::atomic<uint64_t> sequence{0};
        alignas(64) std::byte data[SLOT_SIZE];
        std::atomic<uint32_t> crc{0};
    };

    Slot slots_[BufferSize];
    std::atomic<uint64_t> head_{0};   // producer index
    std::atomic<uint64_t> tail_{0};   // consumer index

    // Returns true if slot is available for writing
    bool try_push(const std::byte* payload, std::size_t len) {
        auto current_head = head_.load(std::memory_order_relaxed);
        auto& slot = slots_[current_head & (BufferSize - 1)];
        auto expected_seq = current_head;

        // CAS: only proceed if sequence hasn't changed (slot is free)
        if (!slot.sequence.compare_exchange_strong(
                expected_seq, current_head + 1,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return false;  // slot still in use by consumer
        }

        // Write data (safe: we own this slot now)
        std::ranges::copy_n(payload, len, slot.data);
        slot.crc.store(calculate_crc(payload, len), std::memory_order_release);

        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // Returns true if slot is available for reading
    bool try_pop(std::byte* out, std::size_t& out_len) {
        auto current_tail = tail_.load(std::memory_order_relaxed);
        auto& slot = slots_[current_tail & (BufferSize - 1)];
        auto expected_seq = current_tail + 1;

        // CAS: only proceed if sequence matches (slot is ready)
        if (!slot.sequence.compare_exchange_strong(
                expected_seq, current_tail + 1,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            return false;  // slot not yet written by producer
        }

        // Verify CRC — detect corruption
        auto stored_crc = slot.crc.load(std::memory_order_relaxed);
        auto computed_crc = calculate_crc(slot.data, SLOT_SIZE);
        if (stored_crc != computed_crc) {
            // Corruption detected — skip this slot
            tail_.store(current_tail + 1, std::memory_order_release);
            return false;
        }

        out_len = SLOT_SIZE;
        std::ranges::copy_n(slot.data, SLOT_SIZE, out);
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    constexpr bool is_lock_free() const {
        return head_.is_lock_free() && tail_.is_lock_free()
            && std::is_constant_evaluated() == false;
    }
};
```

**Tests:**
- Stress test: 1 producer × N consumers, verify no data loss at 1M ops/sec
- Corrupt a slot mid-read → consumer detects via CRC mismatch
- Validate cache-line alignment (`alignas(64)`) to prevent false sharing
- Runtime verification: `static_assert` + runtime check that atomics are lock-free
- Verify `BufferSize` is enforced as power-of-2 at compile time via `requires` clause

---

### Phase 2 — Worker Processes _(Week 4-5)_

**Worker responsibilities:**
1. Attach to shared memory region via `mmap`
2. Enter work loop: read from input ring, process, write to output ring
3. Periodically update `worker_status` flags
4. Handle signals gracefully (SIGTERM for shutdown, SIGUSR1 for forced crash for testing)

**Workload simulation:**
- Simulate a safety-critical task: e.g., sensor data processing with deadline enforcement
- Each "tick": read input → compute → write output → update status
- Enforce per-tick CPU budget (measure elapsed, yield if over budget)

**Roles:**
- **Hot workers** (A, B): actively processing
- **Warm standby** (C): monitoring, ready to take over on failover

**C++20 features in workers:**
```cpp
// Coroutine-based work loop — clean async style without callbacks
struct Worker {
    SharedRegion& shm_;
    std::stop_source stop_src_;

    // Coroutine that runs the main work loop
    // Uses std::stop_token for cooperative cancellation (no signal handlers in hot path)
    std::generator<WorkerEvent> work_loop() {
        while (!stop_src_.stop_requested()) {
            auto result = try_read_input();
            if (result.has_value()) {
                auto processed = process_sensor_data(result.value());
                write_output(processed);
                update_status(Status::RUNNING);

                // Deadline monitoring — yield if over budget
                auto elapsed = clock::now() - tick_start_;
                if (elapsed > cpu_budget_) {
                    std::this_thread::yield();
                    log_overrun();
                }

                co_yield WorkerEvent{.type = EventType::TICK_COMPLETED, .seq = global_seq_.fetch_add(1)};
            } else {
                // No data — brief spin with backoff
                std::this_thread::pause();  // C++26, or std::this_thread::yield() for C++20
            }
        }
    }

    // Ranges + views for processing pipeline
    auto process_sensor_data(const SensorReading& raw) -> ProcessedData {
        // Compose transformations with C++20 ranges — zero-copy, lazy evaluation
        auto filtered = raw.readings
            | std::views::filter([](auto r) { return r.signal_to_noise > THRESHOLD; })
            | std::views::transform([](auto r) { return calibrate(r); })
            | std::views::take(MAX_SAMPLES);

        ProcessedData output;
        std::ranges::copy(filtered, output.begin());
        return output;
    }

    // std::stop_token for clean cancellation (no signal handlers in hot path)
    void request_stop() { stop_src_.request_stop(); }
};
```

**Tests:**
- Deterministic workload: same input → same output (idempotency)
- Deadline monitoring: log overrun events
- Verify coroutine cancellation is clean (no resource leaks)
- Verify ranges pipeline produces identical output to manual loop (sanity check)

---

### Phase 3 — Monitor Daemon _(Week 6)_

**Responsibilities:**
1. Poll `worker_status` flags every 10ms
2. Check ring buffer health (stalled sequence counters = stuck worker)
3. Collect CPU/memory/latency metrics
4. Expose HTTP endpoint (`/health`, `/metrics`, `/status`)
5. Write structured JSON logs with timestamps

**Health algorithm (C++20):**
```cpp
for (int i = 0; i < MAX_WORKERS; ++i) {
    auto status = shm_.worker_status[i].load(std::memory_order_acquire);

    if (has_flag(status, WorkerStatus::CRASHED)) {
        co_yield Alert{.worker = i, .type = AlertType::CRASHED};
    } else if (has_flag(status, WorkerStatus::RUNNING)) {
        auto now = std::chrono::steady_clock::now();
        if ((now - last_advance_[i]) > stall_threshold_) {
            co_yield Alert{.worker = i, .type = AlertType::STALLED};
        }
    }
}
```

**Structured JSON logging with `std::format`:**
```cpp
auto ts = std::format("{}",
    std::chrono::system_clock::now()
        .time_since_epoch()
        .count());
log_file << std::format(
    "{{\"ts\": {}, \"level\": \"{}\", \"component\": \"{}\", \"details\": {}}}",
    ts, level, component, details_to_json(details)
);
```

---

### Phase 4 — Supervisor Process _(Week 7-8)_

**Responsibilities:**
1. Launch & manage worker processes
2. Receive alerts from monitor daemon
3. **Failover logic:**
   - Worker A crashes → promote Worker C (warm standby) to hot
   - Restart crashed Worker A as new warm standby
4. **Recovery verification:**
   - New worker attaches to shared memory
   - Reads last committed sequence from ring buffer header
   - Resumes from correct offset (no data loss)
5. Graceful shutdown: signal all workers → drain rings → exit

**Supervisor state machine:**
```
IDLE → LAUNCHING → RUNNING → FAILOVER_DETECTED → RECOVERING → RUNNING
                                        ↕
                                    MONITORING
```

**Tests:**
- Kill Worker A → verify Worker C takes over within < 100ms
- Verify no duplicate processing after failover
- Verify supervisor itself is robust (kill supervisor → exit cleanly)

---

### Phase 5 — Perturbation / Fault Injection Engine _(Week 9-10)_

**This is the crown jewel — proving recoverability.**

#### 5a. Fault Categories

| Category | Mechanism | What It Proves |
|---|---|---|
| **Process crash** | `kill -SIGSEGV <pid>` | Supervisor restart + failover |
| **Process hang** | Block worker thread for N seconds | Monitor stall detection |
| **Memory corruption** | `process_vm_writev` to corrupt ring buffer data | CRC/checksum detection |
| **Latency injection** | `setitimer(ITIMER_REAL)` to delay worker, or `nanosleep` spikes | Deadline monitoring + degradation |
| **Shared memory loss** | `munmap` + remap, or truncate `/dev/shm` | Re-attachment logic |
| **CPU starvation** | cgroup CPU limit throttling | Bounded execution monitoring |
| **Double fault** | Kill 2 workers simultaneously | Degraded mode operation |

#### 5b. Perturbation Harness API (C++20)
```cpp
class PerturbationEngine {
    std::vector<pid_t> target_pids_;
    std::filesystem::path shm_path_;
    std::ofstream replay_log_;  // For deterministic replay

public:
    // Inject a SIGSEGV into target worker
    std::expected<void, std::error_code> inject_crash(pid_t pid);

    // Corrupt N bytes at offset in shared memory via process_vm_writev
    std::expected<void, std::error_code> corrupt_memory(
        pid_t pid, std::size_t shm_offset, std::span<const std::byte> data);

    // Inject latency: use setitimer to delay worker, or nanosleep spikes
    std::expected<void, std::error_code> inject_latency(
        pid_t pid, std::chrono::milliseconds duration);

    // Starve CPU: set cgroup quota to 1% for N seconds
    std::expected<void, std::error_code> starve_cpu(
        pid_t pid, std::chrono::seconds duration);

    // Record a perturbation event for replay
    void record(const PerturbationEvent& event);

    // Replay recorded perturbations (deterministic testing)
    std::expected<void, std::error_code> replay(
        const std::filesystem::path& log_file);
};
```

**C++20 `std::expected` for error handling (C++23, with polyfill for C++20):**
- No exceptions in hot path — error codes are zero-cost, explicit
- `std::expected<void, std::error_code>` makes failure modes part of the type signature
- For C++20: use `std::expected` from [expected-lite](https://github.com/martinmoene/expected-lite)

#### 5c. Perturbation Scenarios (Test Suite)

| Scenario | Steps | Expected Result |
|---|---|---|
| **S1: Single crash** | Kill Worker A | Worker C promoted; A restarted; zero data loss |
| **S2: Stalled worker** | Block Worker B for 200ms | Monitor detects stall; supervisor restarts B |
| **S3: Memory corruption** | Corrupt ring buffer slot | CRC mismatch detected; corrupted slot skipped; alert logged |
| **S4: Latency spike** | Inject 500ms delay in Worker A | Deadline overrun logged; system degrades but continues |
| **S5: Double fault** | Kill A + B simultaneously | C takes over; both A & B restarted; degraded throughput |
| **S6: Supervisor crash** | Kill supervisor | Container exits (single point of trust); verify restart policy |
| **S7: Replay** | Replay recorded perturbations | Deterministic recovery — same outcome each run |

---

### Phase 6 — Observability & Certification-Grade Logging _(Week 11)_

**Deliverables:**
- Structured JSON logs: `{ "ts": "...", "level": "WARN", "component": "supervisor", "event": "failover", "details": {...} }`
- Prometheus metrics:
  - `worker_status{worker="A"}` (0=IDLE, 1=RUNNING, 2=CRASHED, 3=RECOVERING)
  - `ring_buffer_sequence{worker="A"}`
  - `perturbation_count_total{type="crash"}`
  - `failover_duration_seconds`
  - `data_loss_events_total` (should always be 0)
- Health report API: `GET /health` returns JSON with all worker states, last known good sequence, uptime

---

### Phase 7 — Integration Tests & Demo _(Week 12)_

**Deliverables:**
- `ctest --output-on-failure` — all unit + integration tests pass
- `./run_demo.sh` — scripted demonstration:
  1. Start container
  2. Show normal operation (metrics, logs)
  3. Inject fault (kill Worker A)
  4. Show failover in real-time (logs, metrics graph)
  5. Inject memory corruption
  6. Show detection & mitigation
  7. Replay recorded perturbations
  8. Show zero data loss guarantee
- README with architecture diagram, build instructions, demo walkthrough

---

## 4. Project Structure

```
safety-critical-ha/
├── Dockerfile
├── docker-compose.yml
├── CMakeLists.txt                       # Top-level CMake (C++20, test framework selection)
├── SAFETY_CRITICAL_HA_PLAN.md          ← this file
├── README.md
├── run_demo.sh
│
├── CMake/                               # CMake modules & test framework abstraction
│   ├── FindGoogleTest.cmake
│   ├── FindCatch2.cmake
│   ├── TestFramework.cmake             # Unified test wrapper (GTest or Catch2)
│   └── Sanitizers.cmake                # ASan, UBSan, TSan options
│
├── shared-memory/                       # Library: safety_crit::shared_memory
│   ├── CMakeLists.txt
│   ├── include/safety_crit/shared_memory/
│   │   ├── ring_buffer.hpp             # Lock-free MPMC ring buffer
│   │   ├── shared_region.hpp           # mmap layout + initialization
│   │   ├── atomic_flags.hpp            # Bit-packed atomic status flags
│   │   └── integrity.hpp               # CRC/checksum for corruption detection
│   ├── src/
│   │   └── ring_buffer.cpp             # CRC implementation (non-inline)
│   └── tests/
│       └── ring_buffer_stress.cpp
│
├── workers/                             # Library: safety_crit::workers
│   ├── CMakeLists.txt
│   ├── include/safety_crit/workers/
│   │   ├── worker.hpp                  # Worker process logic
│   │   ├── workload.hpp                # Simulated safety-critical workload
│   │   └── deadline.hpp                # CPU budget / deadline enforcement
│   ├── src/
│   │   └── worker.cpp
│   └── tests/
│       └── worker_integration.cpp
│
├── supervisor/                          # Library: safety_crit::supervisor
│   ├── CMakeLists.txt
│   ├── include/safety_crit/supervisor/
│   │   ├── supervisor.hpp              # Supervisor state machine
│   │   ├── failover.hpp                # Failover logic
│   │   └── health_report.hpp           # Health report generation
│   ├── src/
│   │   └── supervisor.cpp
│   └── tests/
│       └── supervisor_recovery.cpp
│
├── monitor/                             # Library: safety_crit::monitor
│   ├── CMakeLists.txt
│   ├── include/safety_crit/monitor/
│   │   ├── monitor.hpp                 # Health polling loop
│   │   ├── metrics.hpp                 # Prometheus metrics
│   │   └── http_server.hpp             # /health, /metrics, /status endpoints
│   ├── src/
│   │   └── monitor.cpp
│   └── tests/
│       └── monitor_detection.cpp
│
├── perturb/                             # Library: safety_crit::perturb
│   ├── CMakeLists.txt
│   ├── include/safety_crit/perturb/
│   │   ├── engine.hpp                  # PerturbationEngine
│   │   ├── faults.hpp                  # Fault category definitions
│   │   ├── injector.hpp                # Signal, memory, latency injectors
│   │   └── recorder.hpp                # Perturbation recording & replay
│   ├── src/
│   │   └── engine.cpp
│   └── tests/
│       └── perturbation_scenarios.cpp
│
├── app/                                 # Binary: safety-critical-ha
│   ├── CMakeLists.txt
│   └── src/
│       └── main.cpp                     # CLI entry point
│           ├── --mode=supervisor
│           ├── --mode=monitor
│           ├── --mode=worker --id=A
│           └── --mode=perturb
```

---

## 5. Test Framework Abstraction Layer

To support both GoogleTest and Catch2 with a unified test interface:

**`CMake/TestFramework.cmake`:**
```cmake
option(SAFECRIT_TEST_FRAMEWORK "Test framework: GoogleTest or Catch2" "GoogleTest")

if(SAFECRIT_TEST_FRAMEWORK STREQUAL "GoogleTest")
    find_package(GTest REQUIRED)
    add_library(TestFramework INTERFACE)
    target_link_libraries(TestFramework INTERFACE GTest::gtest GTest::gtest_main)
    # GoogleTest-specific macros exposed via alias
    add_library(TestAssertions INTERFACE)
    target_compile_definitions(TestAssertions INTERFACE \
        TEST_ASSERT_EQ(a,b) = EXPECT_EQ(a,b) \
        TEST_ASSERT_NE(a,b) = EXPECT_NE(a,b) \
        TEST_ASSERT_TRUE(x) = EXPECT_TRUE(x) \
        TEST_ASSERT_FALSE(x) = EXPECT_FALSE(x) \
        TEST_ASSERT_NEAR(a,b,e) = EXPECT_NEAR(a,b,e)
endif()

if(SAFECRIT_TEST_FRAMEWORK STREQUAL "Catch2")
    find_package(Catch2 REQUIRED)
    add_library(TestFramework INTERFACE)
    target_link_libraries(TestFramework INTERFACE Catch2::Catch2WithMain)
    # Catch2-specific macros exposed via alias
    add_library(TestAssertions INTERFACE)
    target_compile_definitions(TestAssertions INTERFACE \
        TEST_ASSERT_EQ(a,b) = REQUIRE(a == b) \
        TEST_ASSERT_NE(a,b) = REQUIRE(a != b) \
        TEST_ASSERT_TRUE(x) = REQUIRE(x) \
        TEST_ASSERT_FALSE(x) = REQUIRE_NOTHROW(x == false) \
        TEST_ASSERT_NEAR(a,b,e) = REQUIRE(a == Approx(b).epsilon(e))
endif()
```

**Test code (framework-agnostic):**
```cpp
// This test works with BOTH GoogleTest and Catch2
#include "safety_crit/shared_memory/ring_buffer.hpp"
#include "TestFramework.cmake"  // pulls in TestAssertions via CMake

TEST(RingBufferLockFree, NoDataLoss) {
    LockFreeRingBuffer<256> rb;
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};

    // Producer thread
    std::jthread producer([&] {
        for (uint64_t i = 0; i < 1'000'000; ++i) {
            std::byte payload[16];
            std::ranges::fill(payload, static_cast<std::byte>(i & 0xFF));
            while (!rb.try_push(payload, sizeof(payload))) {
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Consumer thread
    std::jthread consumer([&] {
        std::byte out[16];
        std::size_t len;
        while (consumed.load() < 1'000'000) {
            if (rb.try_pop(out, len)) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_ASSERT_EQ(produced.load(), consumed.load());
    TEST_ASSERT_EQ(produced.load(), 1'000'000ULL);
}
```

**Switching frameworks:**
```bash
# GoogleTest (default)
cmake -B build -DSAFCRIT_TEST_FRAMEWORK=GoogleTest

# Catch2
cmake -B build -DSAFCRIT_TEST_FRAMEWORK=Catch2

# Run tests (works the same either way)
cd build && ctest --output-on-failure
```

---

## 6. Dockerfile (Draft)

```dockerfile
FROM debian:bookworm AS builder

# C++20 toolchain + build tools
RUN apt-get update && apt-get install -y \
    g++-13 gcc-13 cmake make \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DSAFCRIT_TEST_FRAMEWORK=GoogleTest \
        -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG" && \
    make -j$(nproc) && \
    ctest --output-on-failure

FROM debian:bookworm-slim

# Safety-critical container capabilities
RUN apt-get update && apt-get install -y \
    gdb perf sysstat procps curl strace \
    && rm -rf /var/lib/apt/lists/*

# Allow real-time scheduling (normally restricted)
RUN echo "* soft rtprio 99" >> /etc/security/limits.conf \
    && echo "* hard rtprio 99" >> /etc/security/limits.conf

COPY --from=builder /app/build/app /usr/local/bin/safety-critical-ha
COPY run_demo.sh /usr/local/bin/run_demo.sh

VOLUME ["/dev/shm"]

ENTRYPOINT ["safety-critical-ha"]
```

**docker-compose.yml** would define:
- `supervisor` service
- `monitor` service
- 3x `worker` services (A, B, C)
- `perturb` service (on-demand fault injection)
- All sharing `/dev/shm` volume

---

## 7. Key Safety Properties to Prove

| Property | How We Prove It |
|---|---|
| **No data loss** | CRC per ring buffer slot; verify sequence continuity after every fault |
| **No duplicate processing** | Monotonic sequence numbers; each slot consumed exactly once (CAS semantics) |
| **Bounded recovery time** | Measure time from fault injection to full recovery; must be < SLA threshold |
| **No deadlock** | Lock-free data structures; no mutexes in hot path |
| **No priority inversion** | SCHED_FIFO with explicit priority; monitor < supervisor < workers |
| **Graceful degradation** | With N/2 workers down, system continues at reduced throughput (no total failure) |
| **Deterministic replay** | Recorded perturbations produce identical outcomes on replay |

---

## 8. Risk Mitigations

| Risk | Mitigation |
|---|---|
| C++ UB in lock-free code | Minimal `reinterpret_cast`/pointer arithmetic; ASan + UBSan in CI; Valgrind memcheck on tests; reference `moodycamel::ConcurrentQueue` (C++) / `boost::lockfree` for proven patterns |
| Shared memory corruption goes undetected | CRC-32C per slot + global integrity word + periodic health checksum |
| Supervisor is a single point of failure | Acceptable for this demo; in production, use Paxos/Raft between supervisors |
| Docker not truly isolated | Use `--privileged` for ptrace; document this is a demo, not production deployment |
| Real-time guarantees not achievable in Docker | Acknowledge Docker adds jitter; use `--cpuset-cpus` and `SCHED_FIFO` to minimize |
| Undefined behavior from data races | `std::atomic` with explicit `memory_order`; TSan in CI (though TSan has false positives with lock-free code — acceptable limitation) |
| `std::format` / `std::expected` availability | `std::format` available in GCC 13+ / Clang 17+; `std::expected` from `expected-lite` polyfill for C++20 |

---

## 9. Success Criteria

- [ ] All unit tests pass (`ctest --output-on-failure`)
- [ ] All 7 perturbation scenarios execute and report correct recovery
- [ ] Zero data loss across all scenarios (verified by CRC chain)
- [ ] Failover time < 100ms (measured)
- [ ] Demo script runs end-to-end without manual intervention
- [ ] Architecture diagram and plan document complete
- [ ] ASan + UBSan clean (no sanitizer violations)
- [ ] Test framework switch works (`GoogleTest` → `Catch2` via CMake option, all tests pass)

---

## 10. Timeline Summary

| Week | Phase |
|---|---|
| 1 | Container & tooling |
| 2–3 | Shared memory & lock-free ring buffers |
| 4–5 | Worker processes |
| 6 | Monitor daemon |
| 7–8 | Supervisor process |
| 9–10 | Perturbation engine & fault scenarios |
| 11 | Observability & logging |
| 12 | Integration tests & demo |

---

## 11. Next Steps

1. **Create the Dockerfile, CMakeLists.txt, and docker-compose.yml** — set up the build environment
2. **Implement `shared-memory/` library** — lock-free ring buffer + shared region layout
3. **Implement `workers/` library** — basic worker loop with shared memory attachment
4. **Set up test framework abstraction** — `CMake/TestFramework.cmake` to switch between GoogleTest and Catch2
5. **Iterate through remaining phases** in order

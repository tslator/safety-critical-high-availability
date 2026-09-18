#pragma once

#include <cstddef>
#include <cstdint>

namespace safety_crit::shared_memory {

// CRC-32C (Castagnoli), reflected form: polynomial 0x1EDC6F41, reflected table
// constant 0x82F63B78, initial state and final xorout both 0xFFFFFFFF. The
// standard iSCSI/RoCE variant; the published canonical check value is
// crc32c("123456789") == 0xE3069283 (pinned by known-vector tests).
//
// T1.3 uses this for two things: the per-slot payload CRC stored in each ring
// Cell (see ring_buffer.hpp) and the whole-region integrity word (see
// shared_region.hpp). A mismatch always means "corrupt or unverified" -- it is
// never an all-clear for bytes outside the covered range.
//
// Implementation (plan T1.3 step 1: hardware intrinsics where available, table
// fallback): when this translation unit is compiled with SSE4.2
// (__SSE4_2__), crc32c() and its running form chain _mm_crc32_u8; otherwise a
// 256-entry table generated at compile time (constexpr -- no runtime
// allocation, no syscalls: hot-path safe) is used. The two paths are
// bit-identical by construction (same reflected polynomial, init, xorout).
// crc32c_table() exposes the table path unconditionally so tests can run
// differential checks whenever a TU does enable SSE4.2. Availability is purely
// a compile-feature fact -- no CMake option; every current gated build
// (x86-64 baseline) runs the table path.

std::uint32_t crc32c_init();
std::uint32_t crc32c_update(std::uint32_t state, const void* data, std::size_t len);
std::uint32_t crc32c_finalize(std::uint32_t state);

// One-shot convenience over the running form.
std::uint32_t crc32c(const void* data, std::size_t len);

// Explicit table-based one-shot: always available regardless of compile
// features (reference fallback and differential-test target).
std::uint32_t crc32c_table(const void* data, std::size_t len);

}  // namespace safety_crit::shared_memory

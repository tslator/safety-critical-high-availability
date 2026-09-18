#include "safety_crit/shared_memory/integrity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

namespace safety_crit::shared_memory {
namespace {

constexpr std::uint32_t kInitialState = 0xFFFFFFFFu;
constexpr std::uint32_t kXorOut = 0xFFFFFFFFu;
// Bit-reflected Castagnoli polynomial (bit reversal of 0x1EDC6F41).
constexpr std::uint32_t kReflectedPoly = 0x82F63B78u;

constexpr std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        std::uint32_t c = static_cast<std::uint32_t>(i);
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1u) != 0u ? (kReflectedPoly ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

// Compile-time generated: zero runtime allocation (hot-path safe).
inline constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

std::uint32_t table_step(std::uint32_t state, const unsigned char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        state = kCrcTable[(state ^ static_cast<std::uint32_t>(p[i])) & 0xFFu] ^ (state >> 8);
    }
    return state;
}

#if defined(__SSE4_2__)
std::uint32_t hw_step(std::uint32_t state, const unsigned char* p, std::size_t n) {
    // Per-byte chain: at the sizes this project hashes (52-byte slot payloads,
    // a 200-byte region block) the simple loop is sufficient. A word-aligned
    // fast path can be added later with perf evidence if it ever matters.
    for (std::size_t i = 0; i < n; ++i) {
        state = _mm_crc32_u8(state, p[i]);
    }
    return state;
}
#endif

}  // namespace

std::uint32_t crc32c_init() {
    return kInitialState;
}

std::uint32_t crc32c_finalize(std::uint32_t state) {
    return state ^ kXorOut;
}

std::uint32_t crc32c_update(std::uint32_t state, const void* data, std::size_t len) {
    if (len == 0) {
        return state;
    }
    const auto* p = static_cast<const unsigned char*>(data);
#if defined(__SSE4_2__)
    return hw_step(state, p, len);
#else
    return table_step(state, p, len);
#endif
}

std::uint32_t crc32c(const void* data, std::size_t len) {
    return crc32c_finalize(crc32c_update(kInitialState, data, len));
}

std::uint32_t crc32c_table(const void* data, std::size_t len) {
    if (len == 0) {
        return 0u;  // finalize(initial) for empty input
    }
    const auto* p = static_cast<const unsigned char*>(data);
    return crc32c_finalize(table_step(kInitialState, p, len));
}

}  // namespace safety_crit::shared_memory

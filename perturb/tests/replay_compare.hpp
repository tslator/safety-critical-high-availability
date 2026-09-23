// T-0030 replay comparison helpers (DEC-0012 #8): semantic equivalence of
// two scenario runs. Wall-clock timestamps and process pids are excluded by
// construction; event categories compare in order, ownership per logical
// ring and committed record counts compare exactly (with an explicit
// tolerance variant for wall-paced record counts).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace safety_crit::perturb::test {

struct OwnershipSnapshot {
    std::uint32_t physical_owner{0};
    std::uint64_t epoch{0};
};

struct Witness {
    std::vector<std::string> event_categories;      // in emission order
    std::vector<OwnershipSnapshot> ownership;       // per logical ring
    std::vector<std::uint64_t> records_per_ring;    // supervisor shutdown
};

// Extracts the monitor event-category names from raw supervisor stdout in
// order (timestamps and pids dropped).
inline std::vector<std::string> event_categories_from_log(const std::string& text) {
    std::vector<std::string> categories;
    constexpr std::string_view marker = "\"event\":\"";
    std::size_t at = 0;
    while ((at = text.find(marker, at)) != std::string::npos) {
        const std::size_t start = at + marker.size();
        const std::size_t end = text.find('"', start);
        if (end == std::string::npos) {
            break;
        }
        categories.push_back(text.substr(start, end - start));
        at = end;
    }
    return categories;
}

// Exact semantic comparison (DEC-0012 #8 determinism contract).
inline bool witnesses_equal(const Witness& lhs, const Witness& rhs) {
    if (lhs.event_categories != rhs.event_categories) {
        return false;
    }
    if (lhs.ownership.size() != rhs.ownership.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.ownership.size(); ++i) {
        if (lhs.ownership[i].physical_owner != rhs.ownership[i].physical_owner ||
            lhs.ownership[i].epoch != rhs.ownership[i].epoch) {
            return false;
        }
    }
    return lhs.records_per_ring == rhs.records_per_ring;
}

// Same contract with a per-ring absolute tolerance on committed record
// counts (wall-paced ticks; used by the compose S1 replay scenario).
inline bool witnesses_equal_with_tolerance(const Witness& lhs, const Witness& rhs,
                                           std::uint64_t records_tolerance) {
    if (lhs.event_categories != rhs.event_categories) {
        return false;
    }
    if (lhs.ownership.size() != rhs.ownership.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.ownership.size(); ++i) {
        if (lhs.ownership[i].physical_owner != rhs.ownership[i].physical_owner ||
            lhs.ownership[i].epoch != rhs.ownership[i].epoch) {
            return false;
        }
    }
    if (lhs.records_per_ring.size() != rhs.records_per_ring.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.records_per_ring.size(); ++i) {
        const std::uint64_t a = lhs.records_per_ring[i];
        const std::uint64_t b = rhs.records_per_ring[i];
        const std::uint64_t delta = a > b ? a - b : b - a;
        if (delta > records_tolerance) {
            return false;
        }
    }
    return true;
}

}  // namespace safety_crit::perturb::test

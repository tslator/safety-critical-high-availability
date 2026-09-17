#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace safety_crit::shared_memory {

// Compile-time ring configuration predicate. SlotCount must be a power of two
// (the index arithmetic below uses `& (n - 1)`); the class additionally
// requires at least two slots (a one-slot buffer has zero in-flight capacity,
// and with it the ready/commit marker classes below would alias -- the commit
// offset is 1, which must be strictly less than SlotCount); SlotBytes bounds
// the size of one slot's payload.
template <std::size_t N>
inline constexpr bool is_power_of_two_v = (N > 0) && ((N & (N - 1)) == 0);

// LockFreeRingBuffer
// ------------------
// A multi-producer / multi-consumer bounded ring buffer that lives in shared
// memory and uses no locks, no allocation, and no syscalls on the hot path.
//
// Protocol: bounded MPMC ring with per-slot sequence counters, in the
// lineage of Dmitry Vyukov's bounded MPMC queue (cf. moodycamel::
// ConcurrentQueue and boost.lockfree). This replaces the plan sketch's
// try_push/try_pop CAS logic, which did not define correct multi-producer
// slot ownership (see docs/phases/PHASE_1_SHARED_MEMORY.md, deviations #1).
//
// State (n = SlotCount >= 2; let P_k(i) = i + k*n be the k-th position
// served by slot i):
//   - `tail_`: position of the next slot a producer may claim.
//   - `head_`: position of the next item a consumer may take.
//   - cells_[i].sequence: per-slot counter, always in exactly one of two
//     value classes:
//       ready(P_k)  := P_k      -- P_k is not yet committed (slot free)
//       commit(P_k) := P_k + 1  -- P_k is published; awaiting release
//     The classes never alias: ready(P_a) == commit(P_b) requires
//     (a - b)*n == 1, impossible for n >= 2. A slot's value therefore
//     determines its state uniquely.
//
// Phase transitions for the position p a slot currently serves:
//   producer claims position p (p % n == i) by CAS-ing tail_ from p to p+1,
//   writes the payload under exclusive ownership, then stores
//     cells_[i].sequence = commit(p)    (release)  -- "committed"
//   consumer claims position p by CAS-ing head_ from p to p+1, reads the
//   payload, then stores
//     cells_[i].sequence = ready(p+n)   (release)  -- "released"
// The released value p + n is exactly the slot's next-lap ready marker, so a
// re-lapping producer observes its claim condition only after release.
// Claim soundness: the CAS on tail_/head_ gives each position a unique owner,
// and no value produced by one position can satisfy another position's claim
// test -- commit(q) == p would require q = p - 1 with q ≡ p (mod n), i.e. n | 1
// (same argument for the consumer side, ready(q) == commit(p) with q = p + 1).
// Hence a producer never overwrites an unconsumed item and a consumer never
// reads an uncommitted slot. Full/empty is detected from signed differences:
//   producer at p: seq - p < 0        -> slot holds commit(p-n): the previous
//                                        lap's item awaits release -> full
//   consumer at p: seq - (p + 1) < 0  -> position p not yet committed -> empty
//
// 64-bit counter lifetime assumption: the signed-difference math is valid
// while positions stay below 2^63. At a sustained 10 million operations per
// second that is roughly 2.9e8 years; the project's lifetime bound is many
// orders of magnitude smaller, and verify_consistent() (below) is available
// as a cross-check on re-attach in any case.
//
// Memory-ordering argument (documented per plan T1.2 step 3):
//   - `tail_.load(relaxed)` / `head_.load(relaxed)`: the value only selects a
//     candidate position and index; correctness of the claim comes from the
//     sequence check + CAS below, so no ordering is required here.
//   - `cells_[i].sequence.load(acquire)`: synchronizes-with the release store
//     that published this slot's payload (producer commit, or a previous
//     release making the slot ready for a new lap). This is what makes the
//     payload bytes visible to the claimer.
//   - The tail_/head_ CAS itself is relaxed: the subsequent sequence store
//     (release) is what publishes the payload; the counter only orders which
//     *position* was claimed, and positions are unique per successful CAS.
//   - Payload copies use plain memory operations under exclusive ownership:
//     a producer owns a slot between its successful CAS and its commit store;
//     a consumer owns it between claim and release. No other thread may read
//     or write those bytes in that window (that invariant is the protocol's
//     core guarantee).
template <std::size_t SlotCount, std::size_t SlotBytes>
    requires is_power_of_two_v<SlotCount> && (SlotCount >= 2) && (SlotBytes > 0)
class LockFreeRingBuffer {
public:
    static constexpr std::size_t kSlotCount = SlotCount;
    static constexpr std::size_t kSlotBytes = SlotBytes;
    static constexpr std::uint64_t kMask = static_cast<std::uint64_t>(SlotCount - 1);

    // One slot occupies whole cache lines only: `alignas(64)` makes each array
    // element start on a fresh line, and the trailing padding is part of the
    // struct, so no two slots share a line (no false sharing between slots).
    struct alignas(64) Cell {
        std::atomic<std::uint64_t> sequence{0};
        char payload[SlotBytes];
    };

    // Derived from slot counters only; see derive_state().
    struct DerivedState {
        std::uint64_t head{0};  // first not-yet-consumed position
        std::uint64_t tail{0};  // first not-yet-committed position
    };

    // Precondition: no other thread uses this buffer yet (fresh mapping or
    // test storage). Returns false if 64-bit atomics are not lock-free on
    // this platform; the design's accepted-platform assumption (x86_64/ARM64
    // have lock-free 64-bit CAS) is then violated and the region must be
    // rejected.
    bool initialize() {
        // Runtime lock-free verification (plan T1.2 step 5): the protocol
        // assumes lock-free 64-bit CAS (x86_64/ARM64 satisfy this). Query a
        // temporary object via the stable per-object is_lock_free(); if the
        // platform cannot honor that assumption the region must be rejected.
        std::atomic<std::uint64_t> lock_free_probe{0};
        if (!lock_free_probe.is_lock_free()) {
            return false;
        }
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        // NOTE: the initial sequence of cell i is the index i (not zero):
        // a slot at position p becomes producible when its sequence equals p.
        for (std::size_t i = 0; i < kSlotCount; ++i) {
            cells_[i].sequence.store(static_cast<std::uint64_t>(i), std::memory_order_relaxed);
        }
        return true;
    }

    // Attempts to publish one value. Returns false without side effects when
    // the buffer is full (or after any failed claim). sizeof(T) must fit in a
    // slot and T must be trivially copyable: the payload travels as raw bytes
    // (memcpy semantics), which matches shared-memory transport requirements.
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    bool try_push(const T& value) {
        static_assert(sizeof(T) <= kSlotBytes, "value does not fit in one slot");
        // Producer side; see protocol notes at the top of this header.
        std::uint64_t pos = tail_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = cells_[pos & kMask];
            const std::uint64_t seq = cell.sequence.load(std::memory_order_acquire);
            const std::int64_t diff = static_cast<std::int64_t>(seq) -
                                      static_cast<std::int64_t>(pos);
            if (diff == 0) {
                // Slot ready for this position; claim it. On failure (or a
                // spurious weak failure) `pos` is updated to the current tail,
                // so the loop re-derives everything from a fresh view.
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    // Claimed: exclusive ownership from here until the commit
                    // store below publishes the payload to consumers.
                    std::memcpy(cell.payload, &value, sizeof(T));
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;  // committed: slot holds commit(p) = p + 1
                }
            } else if (diff < 0) {
                // Slot holds commit(p-n): the previous lap's item has not been
                // released yet. Buffer full -- a sound "no" for this attempt.
                return false;
            } else {
                // Stale view of `pos` (slot already progressed); re-read tail.
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    // Attempts to take the oldest value. Returns false without side effects
    // when the buffer is empty.
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    bool try_pop(T& out) {
        static_assert(sizeof(T) <= kSlotBytes, "value does not fit in one slot");
        // Consumer side; mirror image of try_push().
        const std::uint64_t n = static_cast<std::uint64_t>(kSlotCount);
        std::uint64_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = cells_[pos & kMask];
            const std::uint64_t seq = cell.sequence.load(std::memory_order_acquire);
            const std::int64_t diff = static_cast<std::int64_t>(seq) -
                                      static_cast<std::int64_t>(pos + 1);
            if (diff == 0) {
                // Slot holds commit(pos) = pos + 1: position committed; the
                // acquire load above synchronizes-with the producer's commit,
                // so the payload is visible.
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    std::memcpy(&out, cell.payload, sizeof(T));
                    cell.sequence.store(pos + n, std::memory_order_release);
                    return true;  // released: stored value is ready(p+n), the
                                  // slot's next-lap claim marker
                }
            } else if (diff < 0) {
                // Position p not committed yet: buffer empty for this attempt.
                return false;
            } else {
                // Stale view of `pos`; re-read head.
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }

    // Total positions ever claimed by producers / consumers (monotonic).
    // Observability and tests only -- the protocol never branches on these.
    std::uint64_t pushed() const { return tail_.load(std::memory_order_relaxed); }
    std::uint64_t consumed() const { return head_.load(std::memory_order_relaxed); }

    [[nodiscard]] bool empty() const {
        // Invariant: tail_ - head_ stays within [0, SlotCount], so equality
        // exactly means "no committed-but-unconsumed positions".
        return consumed() == pushed();
    }

    [[nodiscard]] bool full() const {
        // May be true transiently while a producer has claimed the last slot
        // but not yet committed; consumers are unaffected (the uncommitted
        // slot reads as empty to them).
        return pushed() - consumed() >= kSlotCount;
    }

    // Reconstructs head/tail purely from the per-slot sequence counters, in
    // O(SlotCount), trusting no stored counter. This is the re-attach
    // recovery check required by plan section 3 (Phase 4): after a process
    // remaps the region it can verify that the stored head_/tail_ agree with
    // what the slot counters actually record, instead of taking either word
    // on faith.
    //
    // Decoding: for cell i with sequence s, let r = (s - i) mod n. Legal
    // values are r == 0 (ready marker P_k, k = (s-i)/n -- P_k uncommitted)
    // and r == 1 (commit marker P_k + 1, k = (s-i-1)/n -- committed, awaiting
    // release); any other residue is corruption. In either state the slot's
    // first not-yet-consumed position is P_k (earlier positions are released,
    // and an uncommitted position is trivially unconsumed); its first
    // not-yet-committed position is P_k when ready and P_{k+1} when committed.
    // The global positions are strictly increasing, so each global value is
    // the minimum over all slots. Returns false (leaving `out` untouched) if
    // any counter is malformed -- s < i or a residue other than 0/1 -- which
    // marks corrupt or not-yet-initialized shared memory.
    //
    // Limitation: this is a *reconstruction* aid, not a verification. The
    // minima are blind to a well-formed counter that corruption has advanced
    // into a future lap on a non-minimal slot (every minimum stays put). For
    // attach-time validation use verify_consistent(), which checks every slot
    // against the stored window.
    bool derive_state(DerivedState& out) const {
        std::uint64_t head = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t tail = std::numeric_limits<std::uint64_t>::max();
        const std::uint64_t n = static_cast<std::uint64_t>(kSlotCount);
        for (std::size_t i = 0; i < kSlotCount; ++i) {
            const std::uint64_t s = cells_[i].sequence.load(std::memory_order_relaxed);
            if (s < static_cast<std::uint64_t>(i)) {
                return false;  // below this slot's first legal value
            }
            const std::uint64_t r = (s - static_cast<std::uint64_t>(i)) % n;
            if (r > 1u) {
                return false;  // neither a ready nor a commit marker: corrupt
            }
            const std::uint64_t k = (s - static_cast<std::uint64_t>(i) - r) / n;
            const std::uint64_t first_unconsumed = static_cast<std::uint64_t>(i) + k * n;
            const std::uint64_t first_uncommitted =
                (r == 0u) ? first_unconsumed : static_cast<std::uint64_t>(i) + (k + 1) * n;
            if (first_uncommitted < tail) {
                tail = first_uncommitted;
            }
            if (first_unconsumed < head) {
                head = first_unconsumed;
            }
        }
        out.head = head;
        out.tail = tail;
        return true;
    }

    // True when every slot counter decodes to a state consistent with the
    // stored head_/tail_ window -- i.e., a state reachable by valid
    // try_push/try_pop operations. Precondition: the ring is quiescent (no
    // in-flight operation on it; an in-flight claim legitimately leaves its
    // slot one step behind the counter it CAS'd). Intended for attach-time
    // verification, not the hot path (it reads all SlotCount slots).
    //
    // Failure direction when the precondition is violated: transient states
    // (an uncommitted claim, an unclaimed release, a torn head_/tail_ read)
    // can only make this return false spuriously. A corrupted slot's stored
    // value is fixed at inspection time and no other thread's transient
    // state can move it into coincidence with the window, so a returned true
    // always means every slot matched the window at the instant of
    // inspection. Callers must therefore treat a single false as "unverified"
    // and re-check under established quiescence before acting -- never as
    // proof of corruption by itself.
    //
    // Stronger than comparing derived minima (derive_state): a corrupted
    // counter advanced to a future lap on a non-minimal slot leaves every
    // minimum untouched and would pass such a check. Here each slot i must
    // decode exactly to the window's expectation: for h = head_, t = tail_,
    // its first not-yet-consumed position must be min{p ≡ i (mod n) : p >= h}
    // and its first not-yet-committed position min{p ≡ i (mod n) : p >= t},
    // which additionally requires 0 <= h <= t <= h + n (at most one lap of
    // committed-but-unreleased items). Any other value -- malformed residue,
    // backward slot, or future-lap slot -- is rejected.
    bool verify_consistent() const {
        const std::uint64_t h = head_.load(std::memory_order_relaxed);
        const std::uint64_t t = tail_.load(std::memory_order_relaxed);
        if (h > t) {
            return false;  // inverted window: corrupt counters
        }
        if (t - h > kSlotCount) {
            return false;  // more than one lap outstanding: impossible
        }
        const std::uint64_t n = static_cast<std::uint64_t>(kSlotCount);
        for (std::size_t i = 0; i < kSlotCount; ++i) {
            const std::uint64_t idx = static_cast<std::uint64_t>(i);
            const std::uint64_t s = cells_[i].sequence.load(std::memory_order_relaxed);
            if (s < idx) {
                return false;  // below this slot's first legal value
            }
            const std::uint64_t r = (s - idx) & kMask;  // (s - i) mod n
            if (r > 1u) {
                return false;  // neither a ready nor a commit marker
            }
            const std::uint64_t k = (s - idx - r) / n;
            const std::uint64_t first_unconsumed = idx + k * n;
            const std::uint64_t first_uncommitted =
                (r == 0u) ? first_unconsumed : idx + (k + 1) * n;
            // Expected window positions for this slot's residue class. The
            // unsigned wrap in (idx - h) computes the modular difference, and
            // masking is mod n because n is a power of two.
            const std::uint64_t expected_unconsumed = h + ((idx - h) & kMask);
            const std::uint64_t expected_uncommitted = t + ((idx - t) & kMask);
            if (first_unconsumed != expected_unconsumed ||
                first_uncommitted != expected_uncommitted) {
                return false;  // slot outside the [h, t] window: corruption
            }
        }
        return true;
    }

    // Slot storage. Exposed (not private) so that supervisors and the T1.3
    // integrity layer can inspect individual slot counters/payloads in mapped
    // memory; nothing here should be written except through try_push/try_pop
    // (and verification/corruption tests).
    Cell* slots() { return cells_; }
    const Cell* slots() const { return cells_; }

    // State words, readable by any process that maps this buffer (the
    // supervisor inspects them directly; verify_consistent() cross-checks
    // them against the slot counters at attach time).
    // They sit on separate cache lines: producers write tail_ (plus the
    // sequences of the slots they claim) and consumers write head_, so the
    // two sides never false-share a line.
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    alignas(64) std::atomic<std::uint64_t> head_{0};

private:
    alignas(64) Cell cells_[kSlotCount];
};

}  // namespace safety_crit::shared_memory

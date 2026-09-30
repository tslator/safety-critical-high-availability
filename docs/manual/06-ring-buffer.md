# Chapter 06 — The Lock-Free Ring Buffer

## Motivation

The data plane crosses a process boundary thousands of times a second. A
mutex there buys you three things you cannot afford: a priority-inversion
vector (a low-priority holder blocks your real-time worker), a deadlock
surface (crash while holding the lock = poison forever), and unbounded
latency. The standard answer is a lock-free bounded ring buffer — but the
naive "CAS the index and hope" version is subtly broken for *multiple*
producers, and this project learned that lesson on its own sketch
(`shared-memory/include/safety_crit/shared_memory/ring_buffer.hpp:31`).

This chapter teaches the protocol actually shipped: the Vyukov-style bounded
MPMC queue with per-slot sequence counters.

## The shape of the problem

A ring of `n` slots shared by producers and consumers. The one hard rule:
every slot must have exactly one owner at a time, and nobody may read a slot
between "someone claimed it" and "someone finished writing it." A single
shared head/tail pair cannot express that — you need per-slot state.

## The protocol, from the header comment

State (`n = SlotCount >= 2`; position `P_k(i) = i + k*n` is the k-th "lap"
through slot `i`) (`ring_buffer.hpp:36`):

- `tail_` — position of the next slot a producer may claim.
- `head_` — position of the next item a consumer may take.
- `cells_[i].sequence` — always in exactly one of two value classes:

```text
ready(P_k)  := P_k        -- slot is free for position P_k
commit(P_k) := P_k + 1    -- P_k is published, awaiting consumption
```

The elegance is that these classes **never alias**: `ready(P_a) ==
commit(P_b)` requires `(a - b) * n == 1`, impossible for `n >= 2`
(`ring_buffer.hpp:42`). So a single 64-bit counter determines a slot's state
uniquely — no separate flag word, no padding tricks.

Transitions for position `p` at slot `i = p % n` (`ring_buffer.hpp:47`):

1. **Producer claims**: CAS `tail_` from `p` to `p+1`. Winner has *exclusive*
   ownership of slot `i` for this lap.
2. **Producer commits**: write payload, then `sequence.store(p + 1)` with
   **release** ordering — this single store publishes the payload bytes.
3. **Consumer claims**: CAS `head_` from `p` to `p+1`.
4. **Consumer reads**: `sequence.load(**acquire**)` and wait until it equals
   `p + 1` (the commit class); the acquire synchronizes-with the producer's
   release, making payload bytes visible.
5. **Consumer releases**: `sequence.store(p + n)` with release — which is
   exactly the *next lap's* ready marker. Recycling falls out of the
   arithmetic.

Full and empty are signed differences (`ring_buffer.hpp:69`):

```text
producer at p:  seq - p < 0        -> slot still holds last lap's item: FULL
consumer at p:  seq - (p + 1) < 0  -> position not committed yet:    EMPTY
```

Claim soundness (why two producers can never collide, why a consumer can
never read an uncommitted slot) is a one-line modular-arithmetic argument
proved in the comment (`ring_buffer.hpp:64`). Read it once; it is the whole
correctness kernel.

## Memory ordering, spelled out

The header documents each operation's order and its justification
(`ring_buffer.hpp:93`) — this is what "lock-free code reviewed to
safety-critical standards" looks like in practice:

- `tail_.load` / `head_.load`: **relaxed** — the value only selects a
  candidate; correctness comes from the sequence check + CAS.
- The claim CAS itself: **relaxed** — it only orders which position was
  claimed; positions are unique per successful CAS.
- `cells_[i].sequence` loads: **acquire** — synchronizes-with the commit
  release; this edge *is* the payload publication.
- Payload copies: plain memory ops under exclusive ownership between claim
  and commit/release. No other thread may touch those bytes in that window —
  that invariant is the protocol's core guarantee.

## Crash in the middle: abandoned claims

A producer can die between its `tail_` CAS and its commit store, stranding
the slot at `ready(p)` — claimed but never committed. The rule
(`ring_buffer.hpp:53`):

> An epoch bump implies every in-flight claim from the prior epoch is
> abandoned. `transfer_ownership()` rolls `tail_` back to the last committed
> sequence on takeover so the new owner resumes from a quiescent ring; never
> force-commits an in-flight slot.

So a crashed mid-write is handled by the *same* ownership protocol that
handles clean failover (chapter [10](10-supervisor-failover.md)) — there is
no second recovery mechanism to get wrong. Consumers never pop a slot from a
prior epoch.

## Integrity rides along

Each cell carries a plain `uint32_t crc` of its payload, written by the
producer under exclusive ownership just before the commit store — its
visibility rides the same release/acquire edge as the payload, so it costs
nothing extra (`ring_buffer.hpp:73`). On pop, the CRC is recomputed; on
mismatch the slot is released normally, `corruption_count_` is bumped
(relaxed — observability only, never gates the protocol), and the record is
**skipped, not delivered**: detection and accounting, not recovery
(`ring_buffer.hpp:78`). Chapter [07](07-data-integrity.md) is the full
integrity story.

## Where the ring meets the region

The typed API is `try_push<T>()` / `try_pop<T>()`
(`ring_buffer.hpp:174,195`), but Phase 2 workers commit through the
region-level `push()` which additionally bumps `global_seq` and refreshes the
region `integrity_word` in the same step
(`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:184`).
There is also a test-only `try_push_with_bad_crc()`
(`ring_buffer.hpp:187`) — the deliberate poison path the S3 scenario uses
(over the payload XOR, so CRC-vs-payload disagreement is guaranteed).

Backpressure policy lives one level up: the worker's push callback retries
with `yield()` while the ring is full, abortable by a stop request
(`workers/include/safety_crit/workers/work_loop.hpp:66`).

## Try it

The protocol is exercised by the shared-memory test suites (chapter 13):

```console
$ ./build/gtest/tests/shared-memory-tests \
    --gtest_filter='*Ring*:*Corrupt*:*Ownership*'
```

You will see tests for: multi-producer claim races, full/empty boundaries,
CRC-corruption skip-and-count, abandoned-claim rollback via
`transfer_ownership`, and the power-of-two / `SlotCount >= 2` static
constraints (`ring_buffer.hpp:16` — a one-slot buffer would alias the
ready/commit classes; the compiler enforces it).

## What to look for

- **The sequence counter does four jobs**: state, claim, publication edge,
  and recycling — each for free.
- **Every memory order is written down with a reason.** Copy that habit.
- **Crash handling is protocol-level, not exception-level.** The ring never
  "recovers"; ownership takes over around it.

## Further reading

- `docs/phases/PHASE_1_SHARED_MEMORY.md` deviation #1 — why the original plan
  sketch's CAS logic was replaced.
- Dmitry Vyukov, "Bounded MPMC queue" — the lineage this sits in
  (`ring_buffer.hpp:29`).
- Next: [Chapter 07 — Data Integrity](07-data-integrity.md).

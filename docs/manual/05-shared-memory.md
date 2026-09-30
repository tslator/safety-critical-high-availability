# Chapter 05 — The Shared Memory Region

## Motivation

Shared memory is the fastest IPC on a single machine — and the most
dangerous: any attached process can scribble anywhere, a stale mapping can
outlive its owner, and a peer crash mid-write leaves you with bytes nobody
owns. Safety-critical shared memory therefore needs three things plain
`shm_open` does not give you: an **identity** you can verify before trusting,
a **layout** fixed tightly enough to reason about, and an **attach discipline**
that refuses anything suspicious instead of coping with it later.

## Layout: SharedRegion v4

The whole system shares exactly one named `/dev/shm` object, typed
`SharedRegion`, currently version 4
(`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:21`):

```cpp
inline constexpr std::uint32_t kRegionMagic   = 0x53484D41u;  // "SHMA"
inline constexpr std::uint32_t kRegionVersion = 4u;
inline constexpr std::size_t   kMaxWorkers        = 3;    // A, B, C
inline constexpr std::size_t   kDefaultSlotCount  = 1024; // power of two
inline constexpr std::uint32_t kDefaultSlotBytes  = 52;
using RingBuffer = LockFreeRingBuffer<kDefaultSlotCount, kDefaultSlotBytes>;
```

The 52-byte payload size is deliberate: payload + 8-byte sequence counter +
4-byte CRC = exactly one 64-byte cache line per slot
(`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:28`).
No two hot fields share a cache line (false sharing), and the whole region is
small enough that integrity checks are cheap.

Contents at a glance:

| Field | Purpose |
|-------|---------|
| `magic`, `version` | Identity — verified on every attach (`:83`) |
| `rings[kMaxWorkers]` | The three lock-free ring buffers (chapter 06) |
| `ownership[kMaxWorkers]` | Ownership tokens per logical ring (chapter 10) |
| `worker_status[kMaxWorkers]` | Atomic status words, one per physical worker |
| `global_seq` | Region-wide commit counter, bumped on every publish (`:95`) |
| `integrity_word` | CRC-32C over the region headers + `global_seq` (`:96`) |

`global_seq` and `integrity_word` share a cell and are refreshed together on
every region-level commit, so an external observer (monitor, daemon) can
detect a *torn* header view with one CRC recompute
(`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:169`).

## Verification is scoped — on purpose

There are three verify entry points and using the wrong one is a bug:

- `verify_identity()` — cheap magic/version check; safe on a live region.
- `verify_worker_ring(idx)` — full check of one ring; valid only while that
  ring is quiescent (no in-flight claims).
- `verify()` — whole-region check; valid only when **all** rings are
  quiescent.

Why scope it? Because "walk every slot and validate" on a live ring races
with concurrent publishers: a mid-scan CRC mismatch would be a false alarm.
The rule keeps the observer honest without freezing the data plane. (The
failover-smoke script uses exactly this discipline when it confirms the
region "survived the failover".)

## The attach path: `shm_attach`

`SharedRegionHandle::create_or_open(name)`
(`shared-memory/include/safety_crit/shared_memory/shm_attach.hpp:58`) is the
**only** way to obtain a mapping:

- Fresh object (size 0 after `O_CREAT`): `ftruncate` + `mmap` + `madvise` +
  in-place `initialize()`.
- Existing object of exactly `sizeof(SharedRegion)`: `mmap` +
  `verify_identity()` — **never re-initialized**. A region that fails
  identity is rejected (`kStaleIdentity`); any other nonzero size is rejected
  (`kSizeMismatch`).
- The attach path **never throws**; failures travel back as
  `AttachError` enumerators + `errnum()` (`shm_attach.hpp:19`).
- The handle is move-only; its destructor unmaps and closes the fd but
  **never `shm_unlink`s** — peers may still be attached (`shm_attach.hpp:34`).

The POSIX syscalls live only in this header's translation unit; the core
layout header includes no `<sys/mman.h>` at all, so consumers compile without
inheriting the syscall surface (DEC-0007 #1, `shm_attach.hpp:11`).

Two more details worth noticing:

1. **No block-wait for a concurrent creator.** Attach assumes the supervisor
   created the region before spawning workers — an ordering guarantee, not a
   race to be won (`shm_attach.hpp:55`).
2. **`destroy()` is TEST-ONLY.** The one function that removes the named
   object is explicitly fenced by comment and used only by re-attachment
   tests and scenario S6 (`shm_attach.hpp:63`).

Try the stale-identity path yourself — create a region with one name, then
truncate garbage over the header from a second process... or simply note that
every demo in this manual has printed `cannot attach` / rejection messages
rather than crashing. That is the attach path doing its job.

## Ownership: the header the failover lives in

Nested in the region (and covered in depth in chapter 10):

```cpp
struct OwnershipToken {
    std::uint32_t physical_owner;
    std::uint64_t epoch;
    ...
};
bool read_ownership(const SharedRegion&, std::size_t logical_ring, OwnershipToken&);
bool transfer_ownership(SharedRegion&, std::size_t logical_ring, ...);
```

(`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:66,137`.)

Every publish path checks the token; every handoff bumps the epoch; a writer
holding a stale token is fenced out even if it is alive. That single design
converts "crashed worker" and "frozen worker that might wake up" into the
same safe case.

## The worker status word

`worker_status[i]` is a single atomic word whose bits are the
`WorkerStatusFlag` vocabulary (chapter 09):

```text
kRunning=1<<0  kIdle=1<<1  kCrashed=1<<2  kRecovering=1<<3  kOverrun=1<<4  kDegraded=1<<5
```

Set/clear operations are single-word read-modify-write
(`shared-memory/include/safety_crit/shared_memory/atomic_flags.hpp:42`) —
hot-path legal. The monitor reads it; the supervisor may write recovery and
degraded bits; a dead process's RUNNING bit is *interpreted*, never cleared
by the observer (`monitors/include/safety_crit/monitors/health.hpp:48`).

## Try it

```console
$ ./build/gtest/app/safety-critical-ha worker --id a --role hot --ticks 3 \
    --region /manual_ch05 --pid-dir /tmp/pids
worker: scheduling fallback (errno 1)
worker 0 (hot): 3 ticks, 0 overruns

$ ls -l /dev/shm/ | grep manual_ch05
-rw-r--r-- 1 tim ... 66560 Sep 29 ... manual_ch05

$ ./build/gtest/app/safety-critical-ha ownership --region /manual_ch05
ownership 0 physical=0 epoch=2
ownership 1 physical=1 epoch=2
ownership 2 physical=2 epoch=2
```

The 66,560-byte object is the whole system: three 1024-slot rings plus
headers. One `ls` and you have "inspected shared memory" on your CV (résumé);
read it properly with `od -A d -t x8 /dev/shm/manual_ch05 | head` and you will
find `41 4d 48 53` (little-endian `0x53484D41`) at offset 0.

## What to look for

- The region is **fixed size, fixed layout** — version bumps are decisions,
  not edits.
- Every consumer verifies something before trusting anything.
- The destructive path (`destroy`) is fenced and never production-reachable.

## Further reading

- `shared-memory/include/safety_crit/shared_memory/shared_region.hpp` — the annotated layout (well commented).
- DEC-0007 — the attach/verify discipline; DEC-0011 — the v4 ownership layout.
- Next: [Chapter 06 — The Lock-Free Ring Buffer](06-ring-buffer.md).

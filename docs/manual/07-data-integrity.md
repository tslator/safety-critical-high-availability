# Chapter 07 — Data Integrity

## Motivation

Shared memory has no hypervisor, no TCP checksum, and no filesystem between
writer and reader. A wild pointer, a torn view, or a bit flip lands *as data*
in the consumer's hands. The safety-critical stance: never trust bytes you
can validate; detect corruption loudly; account for it; and keep the sequence
story intact so detection never masquerades as — or hides — loss.

Three integrity layers stack in this system: per-slot CRC-32C, the region
integrity word, and sequence continuity.

## Layer 1: CRC-32C per ring slot

Each ring cell carries `crc32c(payload)` in a plain `uint32_t`, written by
the producer under exclusive ownership immediately before the commit store —
visibility rides the slot's release/acquire edge, so integrity costs one CRC
per record and nothing else
(`shared-memory/include/safety_crit/shared_memory/ring_buffer.hpp:73`).

Why CRC-32C (Castagnoli polynomial, the SSE4.2 instruction) rather than a
fast hash?

- **Provable detection.** CRC-32C detects all single- and double-bit errors,
  all odd-count errors, and burst errors up to 32 bits. That is a theorem,
  which is the kind of claim chapter [02](02-safety-properties.md) is built
  on; "it hashes differently, usually" is not.
- **Hardware support.** Compiled with SSE4.2 (`__SSE4_2__`), `crc32c()` runs
  the `_mm_crc32_u8` instruction sequence
  (`shared-memory/include/safety_crit/shared_memory/integrity.hpp`).
- **Hot-path-legal fallback.** Without SSE4.2, the same header uses a
  **constexpr-generated 256-entry table** — no runtime init, no allocation,
  no syscalls. Both paths are bit-identical by construction, and
  `crc32c_table()` is exposed unconditionally so tests can prove the two
  implementations agree.

The honest limit is documented too: corruption that flips payload and CRC
*consistently* (correlated) is undetectable by any single-slot checksum —
roughly 2^-32 per random event (`ring_buffer.hpp:82`). Safety engineering
writes down what the design does **not** cover.

On mismatch (`ring_buffer.hpp:78`): the consumer releases the slot with the
normal ready marker, increments `corruption_count_` (relaxed atomic — pure
observability, never gates the protocol), and returns "no item." The corrupt
record is **skipped, not delivered**. Detection and accounting, not recovery
— the stream stays live.

## Layer 2: the region integrity word

Per-slot CRCs protect payloads; the region **headers** (magic, version, ring
positions, ownership words) are protected by `integrity_word` — a CRC over
the header words plus the observed `global_seq`, seeded by `initialize()`
and refreshed on every region-level commit
(`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:96`). An
observer recomputes it to detect a torn or scribbled header view; the refresh
CAS-converges (`shared-memory/include/safety_crit/shared_memory/shared_region.hpp:176`).

Identity (magic/version) is deliberately *outside* the CRC's job: identity
answers "is this my region at all"; integrity answers "are these bytes
mutually consistent."

## Layer 3: sequence continuity — the no-data-loss witness

CRC answers "is this record intact?" Only sequence answers "did I get *all*
of them?" The consumer's position in the ring is the authoritative witness
(the monotonic positions of chapter 06), and the supervisor's drain codifies
the difference (`supervisor/include/safety_crit/supervisor/supervisor.hpp:28`):

> The transport position is the authoritative sequence witness; corruption
> skips are counted and tolerated (sequence continuity carries across the
> skip), while ownership read failures and real sequence gaps are rejected.

| Event | Meaning | System response |
|-------|---------|-----------------|
| Corruption skip | A slot failed CRC; we know which and how many | Count it; keep going |
| Sequence gap | A committed position will never be seen | `data_loss_observed` witness record |

The `OutputWitness` struct tracks both accounts separately:
`records`/`corruptions` per ring, `first_post_failover` for handoff proof,
and `gap_lost` (T-0038) carrying committed records skipped by a rejected
drain, feeding `data_loss_observed`
(`supervisor/include/safety_crit/supervisor/supervisor.hpp:29`). That witness
becomes `data_loss_events_total` on the metrics endpoint — sourced
**exclusively** from supervisor witness records; log content can never move
it any other way (chapter [12](12-observability.md)).

## Try it

The S3 scenario poisons one live slot end-to-end and asserts the system
tolerates exactly one counted corruption
(`containers/compose/scenarios/s3_corrupt.sh`):

```text
==> S3: poisoning next slot of hot worker A (pid 8)
==> S3: stopping supervisor gracefully to read the witness summary
==> S3 PASS: poisoned slot tolerated (a_corruptions=1), supervisor survived, replay /tmp/s3_replay.0kMpgm.jsonl
```

And the replayed version of the same poison reproduces the corruption
witness — you can verify the CRC bookkeeping yourself:

```console
$ containers/compose/scenarios/s3_replay.sh   # if present on your tree
```

Natively, the integrity layer is covered by:

```console
$ ./build/gtest/tests/shared-memory-tests --gtest_filter='*ntegrity*:*CRC*:*crc*'
```

## What to look for

- **Three layers, three different questions**: payload intact? (slot CRC),
  metadata consistent? (integrity word), everything delivered? (sequence).
- **Corruption is a counted event, not an error path.** No exceptions, no
  aborts — a corrupt slot is released normally so one bad record cannot
  stall the stream.
- **`a_corruptions=1` is a *passing* outcome.** In this project, a
  correctly-counted corruption proves the property; silently swallowed ones
  would fail the test.

## Further reading

- `shared-memory/include/safety_crit/shared_memory/integrity.hpp` — CRC-32C with the differential-testing design.
- DEC-0012 — T-0024/T-0027 (abandoned claims, fault-injection push).
- Next: [Chapter 08 — Workers](08-workers.md).

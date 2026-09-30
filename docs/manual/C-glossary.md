# Appendix C — Glossary

Project-specific vocabulary. Generic C++/Linux terms are included only where
this project uses them in a load-bearing way.

## Acronyms

| Acronym | Expansion |
|---------|-----------|
| API | Application Programming Interface |
| CAS | Compare-And-Swap (see Terms) |
| CI | Continuous Integration — the `.github/workflows/ci.yml` pipeline (chapter 13) |
| CLI | Command-Line Interface |
| CPU | Central Processing Unit |
| CRC / CRC-32C | Cyclic Redundancy Check / Castagnoli variant (see Terms) |
| CV | Curriculum Vitae (résumé) |
| HTTP | HyperText Transfer Protocol |
| IPC | Inter-Process Communication |
| JSON | JavaScript Object Notation |
| MPMC | Multi-Producer / Multi-Consumer (see Terms) |
| PID | Process ID |
| POSIX | Portable Operating System Interface (the Unix API standard) |
| RT | Real-Time (hard-real-time scheduling context) |
| SLA | Service-Level Agreement — a stated, agreed performance bound the system commits to; here, the 100 ms recovery budget |
| STL | Standard Template Library (the C++ standard library) |
| TCP | Transmission Control Protocol |
| UB | Undefined Behavior |
| UBSan | Undefined Behavior Sanitizer |
| TSan | Thread Sanitizer |

## Terms

**abandoned claim** — A ring slot claimed by a producer that died before its
commit store. An epoch bump implies all prior-epoch in-flight claims are
abandoned; `transfer_ownership` rolls `tail_` back to the last committed
sequence (chapter 06).

**alert vocabulary** — The six contract-graded monitor alert names:
`worker_crashed`, `worker_stalled`, `worker_recovered`, `worker_overrun`,
`worker_idle`, `worker_running`. Renames/removals require a decision record
(chapter 09).

**attach (or-create)** — Open a named region, creating it only if absent; an
existing region is identity-verified and never silently re-initialized
(`shm_attach.hpp`, chapter 05).

**cache line (64B)** — The unit of memory-system traffic; the region layout
fits one slot (payload + sequence + CRC) into exactly one line, avoiding
false sharing (chapter 05).

**CAS (compare-and-swap)** — The atomic primitive underpinning ring claims and
ownership transfers; each successful CAS gives exactly one winner
(chapters 06, 10).

**commit store / commit(p)** — The release-ordered store of `sequence = p+1`
that publishes a ring slot's payload (chapter 06).

**CRC-32C** — Castagnoli CRC-32 (reflected poly 0x82F63B78, init/xorout
0xFFFFFFFF), hardware via SSE4.2 with a constexpr table fallback; per-slot
payload integrity (chapter 07).

**data_loss_events_total** — Prometheus counter of supervisor witness-
recorded sequence gaps; sourced exclusively from `data_loss_observed` witness
records (chapter 12).

**degraded (state)** — A ring whose owner crashed with no promotable standby;
terminal and reportable for the supervisor's lifetime, never silently cleared
(chapter 10). Distinct from the daemon's `/health: degraded` (region lost,
chapter 12).

**determinism (workload)** — Output is a pure function of
`(seed_base, worker_idx, tick)` via splitmix64; makes replay equality checks
meaningful (chapters 08, 11).

**dual framework** — The requirement that tests build and pass under both
GoogleTest and Catch2 via `SAFETY_CRIT_TEST_FRAMEWORK` (chapter 13).

**epoch** — Monotonically bumped per-ownership-token counter; fencing writers
whose token predates a handoff (chapter 10).

**fencing** — Making a stale token holder unable to publish, forever, even if
alive (chapter 10).

**handoff grace** — Window (`handoff_grace_ms`, default 750) in which stall
alerts describe an in-flight promotion and must not arm stall escalation
(chapter 10).

**hot worker / hot role** — A worker actively ticking and publishing
(`WorkerRole::kHot`) (chapter 08).

**logical ring vs physical worker** — The three queues are logical (0, 1, 2);
processes are physical (A/B/C = 0/1/2). Ownership maps ring → physical owner;
after failover they diverge (chapter 03).

**MPMC** — Multi-producer/multi-consumer; the ring's Vyukov-lineage protocol
class (chapter 06).

**near-miss** — A timing-sensitive check (e.g. S1 recovery) that lands right
at its budget edge on a loaded host — host noise, not a regression. A lone
near-miss is a rerun; a consistent trend is a bug (chapters 02, 11;
Appendix B).

**ownership token / `OwnershipToken`** — `{physical_owner, epoch}` per
logical ring, CAS-transferred (chapter 10).

**pidfile liveness** — `kill(pid, 0)` check of the recorded pid; stale files
(survivors of crashes) are meaningful signals (chapter 09).

**promotion** — Standby acquires a crashed/failed hot worker's ring via
ownership transfer and starts publishing (chapter 10).

**ready(P) / release** — Slot state marker `sequence = P` (free for position
P); release is the consumer storing the next lap's ready marker (chapter 06).

**region integrity word** — CRC over region header words + observed
`global_seq`; detects torn/scribbled metadata views (chapter 07).

**region (SharedRegion v4)** — The single named `/dev/shm` object: magic
`0x53484D41`, version 4, 3 rings x 1024 slots x 52B, ownership, status words
(chapter 05).

**replay log** — JSON-lines record of every harness action
(`{schema, ts_ns, category, target_pid, params}`); lets a recorded incident
be re-executed with PID remapping (chapter 11).

**sequence continuity** — The consumer-position invariant that witnesses
"all committed records were seen"; the no-data-loss property's operational
form (chapter 07).

**sequence key** — `component` + optional `instance`; per-key monotonic `seq`
in the event log (chapter 12).

**stall** — RUNNING status word + ring tail unchanged beyond threshold +
process alive (chapter 09).

**stall ladder / stall grace** — One bounded SIGCONT, grace window
(`stall_grace_ms`, default 200), then SIGKILL into the crash-failover path
(chapter 10).

**standby (warm) / role `kStandby`** — Idle process watching a sibling's
status word, ready to be promoted; never touches a ring until it owns one
(chapter 08).

**tick** — One unit of worker work: a deterministic workload invocation
within `cpu_budget`, pace to `tick_interval` (chapter 08).

**witness (witness record / `OutputWitness`)** — Supervisor-observed drain
evidence: records, corruptions, `first_post_failover`, `gap_lost`; the only
source for `data_loss_events_total` (chapters 07, 10, 12).

**workload** — The deterministic fake-sensor pipeline (synthesize → filter →
transform) producing `ProcessedData` records (chapter 08).

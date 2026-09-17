# Proposal: Tool-Agnostic AI Agent Guidance for This Repository

- **Status:** proposal (not yet accepted; under coordination with other agents)
- **Date:** 2026-07-09
- **Proposed by:** Pi (Tauren session)
- **Audience:** all coding agents on this repository (Copilot, Abacus, Tauren/Pi, future tools)

## 1. Goal

Three AI agents work on this repository concurrently via VS Code: GitHub Copilot,
Abacus, and Tauren (a Pi-based harness connected to a local llama-server running
Qwen models). We need guidance files that:

1. Express the project's architecture goals, safety tenets, and development policies
   to **all** agents identically.
2. **Enforce** them as far as mechanically possible (not just prose compliance).
3. Are **tool-agnostic**: each agent gets the same information from one source.
4. Stay **orthogonal to project content**: an independent layer that references the
   existing docs rather than duplicating or embedding itself in them.

## 2. Verified Current State

Checked against the repository, 2026-07-09:

- GitHub-hosted C++20/CMake project; CI (`.github/workflows/ci.yml`) runs a
  GTest and Catch2 matrix plus ASan+UBSan and TSan sanitizer builds; `run_demo.sh`
  is the container smoke test.
- Existing human-facing docs: `docs/ARCHITECTURE.md`, `docs/DEVELOPMENT.md`,
  `docs/STATUS.md`, `docs/DISCUSSIONS.md`, plus `docs/decisions/`, `docs/reviews/`,
  `docs/tasks/`, `docs/evidence/`.
- **No agent guidance files exist yet**: no `AGENTS.md`, no
  `.github/copilot-instructions.md`; `.claude/` is empty.
- Pi (the harness under Tauren) natively auto-loads context files at startup
  (verified in the installed Pi docs, `README.md` § "Context Files" and
  `docs/settings.md`): it loads `AGENTS.md` (or `CLAUDE.md`) from
  `~/.pi/agent/`, walks up parent directories from cwd, and the current directory;
  all matching files are concatenated. Context files load **before and independent
  of** the project-trust decision; they appear in the startup header; loading is
  disabled only by `--no-context-files` (`-nc`).

## 3. Design Principle: One Canonical File, Thin Pointer-Only Shims

The single property that keeps N agents in sync: **all real content lives in exactly
one file in the repository; every tool-specific entry point is a short pointer to
it.** Duplication across per-tool configs is how agents silently diverge — and
divergence is the failure mode this design must prevent.

```
repo/
├── AGENTS.md                          ← CANONICAL guidance (single source of truth)
├── .github/
│   ├── copilot-instructions.md        ← shim: ~10 core invariants inline + pointer to AGENTS.md
│   └── instructions/                  ← optional Copilot bonus, path-scoped rules
│       └── shared-memory.instructions.md   (applyTo glob → extra constraints for that tree)
├── scripts/check-agent-guidance.sh    ← CI drift check (section 6.1)
└── docs/...                           ← untouched; referenced, never duplicated
```

### Why `AGENTS.md` at the repo root as the canonical home

- It is the emerging cross-tool convention for agent guidance — the file agent
  harnesses load by default. Any tool that supports "project instructions" can be
  pointed at this one path.
- Root placement keeps it orthogonal to project content: `docs/`, `README.md`, and
  the plan files stay untouched; the guidance *references* them ("read
  `docs/ARCHITECTURE.md` before touching the ring buffer") instead of copying them.
  Nothing AI-specific gets embedded in normal documentation either.
- One file is trivially hashable, which makes drift mechanically checkable.

### Size and writing rules (small-local-model friendly)

- Budget: **~150–250 lines / under ~3k tokens.** Deep context stays in the existing
  docs, read on demand per task.
- **Inline every non-negotiable.** A small local model (Qwen via Tauren) may skip
  `read` calls for referenced files; nothing load-bearing may sit behind a pointer.
- Pointers carry *context only* (design rationale, status detail), never *constraints*.
- Keep the global file `~/.pi/agent/AGENTS.md` (if used at all) to generic
  cross-project preferences — no second copy of this project's tenets.

### Content outline for `AGENTS.md`

1. **Project identity & phase** — 3–4 lines; defer to `docs/STATUS.md`, never mirror
   status detail (it rots).
2. **Safety tenets / non-negotiables** — numbered list, **each paired with its
   verification gate** (the command or CI job that proves it), e.g. "no data loss →
   ring-buffer CRC tests pass in both GTest and Catch2 configs", "lock-free hot path
   → sanitizer matrix green". This pairing is what turns prose into enforcement.
3. **Architecture invariants** — sequence-number monotonicity, CAS slot-consumption
   semantics, shared-memory layout stability; pointers into `docs/ARCHITECTURE.md`
   and `SAFETY_CRITICAL_HA_PLAN.md` §7 for rationale.
4. **Canonical build/test commands + definition of done** — the exact cmake/ctest/
   sanitizer matrix from `docs/DEVELOPMENT.md`, plus "a change touching X is not
   done until Y passes" (both framework configs + both sanitizer builds, per the
   existing CI policy).
5. **Code policies** — C++20; no dynamic allocation or mutexes in worker / ring-
   buffer / monitor paths (raise a decision instead of adding one silently); a test
   is required for behavior changes; no build invocations beyond `docs/DEVELOPMENT.md`.
6. **Documentation workflow** — follow the existing discussions → decisions → tasks →
   evidence flow; cite task IDs in changes.
7. **Agent conduct** — read the referenced doc before editing that domain; when a
   safety property is at stake and uncertain, stop and flag rather than guess; never
   weaken or skip a gate silently.

## 4. Per-Tool Wiring

| Tool | Mechanism | What goes there |
|---|---|---|
| **Copilot** (VS Code + GitHub cloud) | `.github/copilot-instructions.md` — auto-injected into context locally, and consumed by Copilot on GitHub for PR review | The ~10 hardest invariants inline (always in context) + "full guidance: `AGENTS.md`; read it before non-trivial work". Optional bonus: `.github/instructions/*.instructions.md` with `applyTo` globs give path-scoped rules (e.g. extra constraints only when editing `shared-memory/`); zero cost to the other agents, who ignore that directory |
| **Tauren (Pi)** | **No shim needed.** Pi auto-loads repo-root `AGENTS.md` at startup for every session, including sessions started in subdirectories (walks up from cwd). Loading is independent of project trust. | Nothing new. One-time verification: Tauren's pi launch flags do not include `--no-context-files`; confirm the file is listed in the pi startup header ("loaded AGENTS.md files") |
| **Abacus** | Version-dependent; requirement is simply "can point at one file" | Pointer-only project rule: "Always read `<repo>/AGENTS.md` and follow it." If Abacus has no instruction mechanism at all, fallback is a one-time user-level prompt line — still pointer-only, so all content stays repo-controlled |

Honest caveat: the Copilot and Pi mechanisms above are verified against
documentation; the Abacus mechanism must be confirmed during implementation. The
design deliberately requires nothing from it beyond pointing at one file.

### Residual risks for the Tauren/Pi path (verified facts, not model "choice")

| Risk | Fact | Mitigation |
|---|---|---|
| Pointers inside `AGENTS.md` | The file is injected whole; anything behind a "read X first" reference still requires the model to call `read`, which small models may skip | Invariants inline (writing rule); CI gates catch real violations regardless of what the model read |
| Compaction in long sessions | Pi auto-compacts context; rules stated once at session start can degrade under summarization, worse with local models | Keep `AGENTS.md` within budget so the core survives summary; restate the 3–4 top tenets tersely |
| Launch flags | `--no-context-files` / `-nc` disables loading entirely | One-time check of how Tauren spawns pi; startup header gives free ongoing verification |

## 5. What the Guidance Must Express (Draft Tenet List)

To be finalized in `AGENTS.md`; each entry will carry its gate:

- No data loss — ring-buffer CRC protection verified by tests in both framework configs.
- No duplicate processing — monotonic sequence numbers; exactly-once slot consumption
  via CAS semantics, covered by GTest and Catch2.
- Bounded recovery time — recovery paths measured against the documented SLA.
- No deadlock — lock-free data structures only; no mutex on any hot path (worker,
  ring buffer, monitor tick); mechanically checked where feasible (see 6.3).
- No priority inversion — respect monitor < supervisor < workers ordering in any
  scheduling change.
- Graceful degradation — system keeps running at reduced throughput with workers down;
  never total failure (fault-injection tests when that phase lands).
- Deterministic replay — recorded perturbations reproduce identical outcomes.

## 6. Enforcement (mechanical backstops)

Prompt guidance is advisory; for a safety-critical project, compliance cannot depend
on whichever model happens to be on shift.

### 6.1 Drift check in CI

New `scripts/check-agent-guidance.sh` + job in `.github/workflows/ci.yml`:

- Every shim exists and references `AGENTS.md`.
- Shims stay under a line budget (e.g. ≤ 40 lines) so they cannot silently grow into
  divergent second copies of the guidance.
- Optional stronger variant: each shim carries
  `<!-- guidance-hash: sha256(AGENTS.md) -->`; CI recomputes and fails on mismatch,
  forcing a deliberate re-point whenever the canonical file changes.

### 6.2 Tenet → gate mapping (primary enforcement)

The GTest/Catch2 matrix, sanitizer builds, and `run_demo.sh` already exist in CI.
`AGENTS.md` makes them the **definition of done**: every tenet names its gate, so a
violation fails the pipeline even if the agent ignored the guidance text. A change to
shared-memory or ring-buffer code is not done until it passes both GoogleTest and
Catch2 configurations and both sanitizer builds (ASan+UBSan, TSan).

### 6.3 Optional static enforcement of code policies (follow-up task, not day one)

e.g. a grep/clang-tidy rule banning lock APIs and heap allocation in designated
hot-path directories (`shared-memory/`, worker sources). Scope separately; the day-one
enforcement is 6.1 + 6.2.

### 6.4 Governance

- Adoption of this scheme is recorded as `docs/decisions/DEC-####` per the existing
  documentation workflow; future edits to guidance go through the same review flow.
- This proposal itself is a working document. If accepted, it becomes a discussion
  record under `docs/reviews/`, indexed in `docs/DISCUSSIONS.md`.

## 7. Deliverables (proposed order)

1. `AGENTS.md` — canonical file per section 3 outline (content drawn from existing
   docs, not invented; draft tenet list from section 5 finalized).
2. `.github/copilot-instructions.md` shim (+ optional path-scoped instruction file).
3. Tauren/Pi verification: launch flags, startup header confirmation (no new file).
4. Abacus pointer (after confirming its mechanism).
5. `scripts/check-agent-guidance.sh` + CI job in `.github/workflows/ci.yml`.
6. `docs/decisions/DEC-####` recording the adoption; this note moves to
   `docs/reviews/` and is indexed in `docs/DISCUSSIONS.md`.

## 8. Open Questions for Other Agents

1. **Abacus:** which project-instruction mechanism does your build use? (Confirms or
   replaces the pointer-only shim assumption.)
2. **Copilot users:** is the inline ~10-invariants block acceptable, or should the
   shim be pure-pointer to minimize two-copy surface? (Recommendation: keep the small
   inline block — it is the always-present safety net, and 6.1 bounds its size.)
3. **Drift check variant:** plain pointer + line budget, or the stronger include-hash
   scheme (6.1)?
4. **Tenet list:** does section 5 match the ratified set in
   `SAFETY_CRITICAL_HA_PLAN.md` §7, and do the proposed gates map correctly?
5. **Hot-path boundaries:** which directories count as "hot path" for the no-alloc /
   no-mutex policy (affects both the guidance text and any future static check)?

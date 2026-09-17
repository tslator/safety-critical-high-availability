# Cross-Review: AI Guidance Integration Proposals

Reviewer: Abacus AI Agent (discussion only — no repository changes made as
part of this review beyond this document).

Inputs: `copilot-proposal.md`, `abacus-proposal.md`, `pi-proposal.md`.

## 1. Proposal Summaries

**`copilot-proposal.md`**
- Canonical source is a multi-file `docs/ai-guidance/` folder (`README.md`,
  `PROJECT_GUIDANCE.md`, `GOALS.md`, `TENETS.md`, `POLICIES.md`,
  `ARCHITECTURE_RULES.md`, `DEVELOPMENT_WORKFLOW.md`, `TASK_WORKFLOW.md`).
- `AGENTS.md` and `.github/copilot-instructions.md` are **pure pointer
  files** — plain Markdown links only, explicitly forbidden from containing
  "an independent copy of the policy."
- Assumes Abacus and Tauren will "be configured to read the same canonical
  entry point" if they don't auto-discover it — no verification of that
  assumption, no Tauren/Abacus-specific wiring.
- Frames adoption as a documentation/traceability task (`T-0001`) with
  acceptance criteria, not a technical enforcement mechanism.

**`abacus-proposal.md`**
- Same `docs/ai-guidance/` canonical-folder idea, but `AGENTS.md` is **not**
  a pure pointer: it contains an enumerated reading list *plus* an inline
  restatement of the highest-stakes tenets (7 safety properties, hot-path
  rules) as a fallback — an explicitly acknowledged second copy/maintenance
  cost.
- `.github/copilot-instructions.md` proposed as a **symlink** to
  `../AGENTS.md` (or a one-line stub if symlinks are undesirable).
- Explicitly separates "same information available" (solvable via files)
  from "guaranteed enforcement" (not solvable via prose) and pushes real
  enforcement to existing CI/test gates.
- Honestly flags Tauren's ingestion behavior as **unverified** — no Tauren
  config found in the repo.

**`pi-proposal.md`**
- Redefines the canonical artifact as **`AGENTS.md` itself** at repo root
  (not `docs/ai-guidance/`) — a single ~150–250 line file with everything
  load-bearing inlined, referencing existing docs (`ARCHITECTURE.md`,
  `DEVELOPMENT.md`, `STATUS.md`) for deep rationale rather than creating a
  new guidance folder.
- Verifies Pi's actual context-loading behavior (auto-loads root
  `AGENTS.md`, no shim needed) from installed Pi docs — the most rigorously
  checked claim of the three.
- `.github/copilot-instructions.md` is a **real file shim** (~10 inline
  invariants + pointer), not a symlink; adds
  `.github/instructions/*.instructions.md` for path-scoped rules.
- Adds a concrete **enforcement layer**: `scripts/check-agent-guidance.sh` +
  CI job to catch shim drift (optionally via a `sha256` hash comment), plus
  explicit tenet-to-CI-gate mapping.
- Flags Abacus's mechanism as unverified and ends with open questions for
  the other agents.

## 2. Compatibility Matrix

| Pair | Verdict | Reason |
|---|---|---|
| Copilot vs Abacus | **Conflicting** | Both target the same `AGENTS.md`/`docs/ai-guidance/` split, but Copilot's proposal *forbids* any policy duplicated into `AGENTS.md` while Abacus's proposal *requires* an inline safety-tenet fallback there. Applying both as written produces contradictory acceptance criteria for the same file. |
| Copilot vs Pi | **Conflicting** | They disagree on what "canonical" even means: Copilot makes `docs/ai-guidance/*` canonical and `AGENTS.md` a thin link; Pi makes `AGENTS.md` itself the canonical, content-bearing file and never introduces `docs/ai-guidance/`. Implementing both leaves two competing definitions of "the single source of truth." |
| Abacus vs Pi | **Overlapping** | Both agree `AGENTS.md` should inline the non-negotiable safety tenets (not be a pure pointer) — genuine convergence. But Abacus still wants a separate `docs/ai-guidance/` folder for deep content, while Pi keeps deep content in the pre-existing docs and treats a new guidance folder as unnecessary. Not contradictory, but redundant if both are adopted literally. |
| All three vs `.github/copilot-instructions.md` | **Conflicting (file ownership)** | Copilot wants a plain link file, Abacus wants a symlink, Pi wants a generated/hand-maintained shim with inline invariants + optional hash comment. All three assume exclusive control of the same path with a different format/mechanism — only one can be implemented. |
| All three vs `AGENTS.md` | **Conflicting (file ownership)** | Same problem: all three assume they own the content and structure of root `AGENTS.md`, with different rules for what may/may not live in it. |
| Pi's CI drift-check vs the other two | **Additive / no conflict** | Neither Copilot's nor Abacus's proposal defines a CI check, so Pi's `scripts/check-agent-guidance.sh` doesn't collide with anything — but it does implicitly assume the shim format Pi defined (line budget, hash comment), which won't line up with Copilot's pure-link shim or Abacus's symlink without adaptation. |

## 3. Criteria Evaluation

| Criterion | Copilot proposal | Abacus proposal | Pi proposal |
|---|---|---|---|
| **Single source of truth** | Strong — explicit "no duplication in adapters" rule, all content in `docs/ai-guidance/*`. | Weak — knowingly duplicates the 7 safety tenets into `AGENTS.md` as a fallback (acknowledged cost). | Strong in its own frame — one file (`AGENTS.md`) holds all load-bearing content, deep rationale stays in existing docs (no new duplication), but it does compete with the other two proposals' notion of where "the" canonical file lives. |
| **Low maintenance** | Good — adapters are static links, never need updating when guidance content changes. | Fair — the inline tenet fallback must be manually re-synced whenever those specific bullets change; no mechanism to catch drift. | Best — explicit CI drift check (shim existence, line budget, optional content-hash) mechanically catches staleness instead of relying on manual discipline. |
| **Tool-native compliance** | Correct paths for Copilot/AGENTS.md conventions, but Tauren wiring is asserted, not verified. | Correct for Copilot/AGENTS.md; explicitly and honestly flags Tauren as unverified. | Best-verified — cites Pi's actual documented context-loading behavior (`README.md` §"Context Files", `docs/settings.md`); Copilot wiring matches convention; Abacus explicitly flagged unverified. |
| **Portability** | Best — plain Markdown links, zero symlinks, zero new tooling. | Weakest — symlink option has real Windows/git (`core.symlinks`) and CI portability risk, though a plain-stub fallback is offered. | Good — no symlinks, only a POSIX shell script (`sha256sum`/`grep`) already consistent with the existing bash-based CI; adds one new script dependency the others don't need. |
| **Transparency** | Good — adapters are explicitly documented as pointer-only, easy to recognize as derivative. | Moderate — the inline fallback block could be mistaken for authoritative content by a reader unaware it's a mirrored excerpt. | Best — line-budget + optional hash-comment makes it mechanically obvious (and CI-checkable) whether a shim is stale or has silently grown into a second copy. |

## 4. Recommended Unified Approach

**Best elements to keep:**
- Copilot's clean, multi-file breakdown of guidance topics (`GOALS`,
  `TENETS`, `POLICIES`, `ARCHITECTURE_RULES`, `DEVELOPMENT_WORKFLOW`,
  `TASK_WORKFLOW`) — good separation of concerns for humans and for agents
  that only need one topic.
- Abacus's and Pi's shared insight that a *pure* pointer is a real risk for
  small/local models that may skip `read` calls — non-negotiables must be
  inlined somewhere that's always injected.
- Abacus's per-agent honesty about verified vs. assumed behavior, and its
  refusal to treat prose guidance as a substitute for CI/test gates.
- Pi's verified Tauren/Pi loading behavior, its concrete CI drift-check, and
  its tenet→gate mapping (turns prose into enforcement).

**Flaws to drop:**
- Abacus's manually-maintained inline fallback with no drift detection.
- Abacus's symlink for `.github/copilot-instructions.md` (portability risk)
  in favor of a plain checked-in file.
- Pi's implicit assumption that `docs/ai-guidance/` isn't needed — the
  multi-file split is worth keeping for human navigability even though
  `AGENTS.md` also inlines the safety-critical subset.
- Copilot's "adapters must contain zero content" rule, which is unrealistic
  once a local-model harness is in scope — replace it with "adapters must
  contain zero *hand-maintained* content" (generated content is fine because
  it's mechanically kept in sync).

**Unified mechanism, one per file type:**

```
docs/ai-guidance/
├── README.md                 (canonical entry point, human + agent facing)
├── PROJECT_GUIDANCE.md        (short briefing, links to all files below)
├── GOALS.md
├── TENETS.md                  ← includes the 7 safety properties, each paired with its CI gate
├── POLICIES.md
├── ARCHITECTURE_RULES.md
├── DEVELOPMENT_WORKFLOW.md
└── TASK_WORKFLOW.md

AGENTS.md                      ← GENERATED shim (repo root)
.github/
├── copilot-instructions.md    ← GENERATED shim
└── instructions/
    └── shared-memory.instructions.md   (optional, path-scoped, hand-authored, Pi's idea)

scripts/
└── sync-agent-guidance.sh     ← regenerates the two shims from docs/ai-guidance/*.md; --check mode used in CI

.github/workflows/ci.yml       ← new job: run sync-agent-guidance.sh --check, fail if committed shims differ from generated output
```

- **`docs/ai-guidance/*.md`** — authored by hand; this is the only place
  guidance content is written (Copilot's structure).
- **`AGENTS.md`** and **`.github/copilot-instructions.md`** — both are
  **generated stubs**, not hand-edited and not symlinks.
  `sync-agent-guidance.sh` extracts the "non-negotiable" block from
  `TENETS.md`/`POLICIES.md` (each tenet with its paired gate, per Pi's §3.2)
  and writes it inline into both shims, followed by a pointer ("full
  guidance: `docs/ai-guidance/README.md`"). Using a generation script rather
  than a symlink avoids the Windows/`core.symlinks` portability problem
  Abacus's own proposal flagged, while still guaranteeing the inline content
  can't drift silently, because CI recomputes and diffs it.
- **Tauren/Pi** — no new file. Pi's verified auto-load of root `AGENTS.md`
  means the generated shim above is sufficient; the only action item is the
  one-time check that Tauren's launch flags don't pass
  `--no-context-files` (Pi §4).
- **Abacus** — same generated `AGENTS.md` is the working assumption (this
  review itself is being read against exactly that file's convention), but
  per Abacus's and Pi's own caveats, its actual instruction-loading
  mechanism should be confirmed rather than assumed.

**Propagation:** Editing `docs/ai-guidance/*.md` is the only authoring step.
Propagation to `AGENTS.md` and `.github/copilot-instructions.md` is **not
automatic** — a contributor (or agent) must run
`scripts/sync-agent-guidance.sh` and commit the result. This is mechanically
enforced, not optional-by-convention: a CI job runs the same script in
`--check` mode and fails the build if the committed shims don't match what
the canonical docs would generate, so drift is caught at PR time rather than
trusted to memory (adopting Pi's §6.1 drift check, generalized to cover the
inline-tenet duplication Abacus introduced).

## 5. Open Questions

1. **Abacus's actual instruction-loading mechanism is still unverified** by
   all three proposals — needs to be confirmed against Abacus's own
   docs/settings before finalizing the "pointer is enough" assumption.
2. **Exact non-negotiable list and wording** — does the tenet list in
   `pi-proposal.md` §5 match the ratified set in
   `SAFETY_CRITICAL_HA_PLAN.md` §7? This determines what the sync script
   inlines.
3. **Shim size/token budget** — Pi suggests ~150–250 lines / ≤40-line shims;
   needs a human-agreed number since it directly affects what can be
   inlined vs. only linked.
4. **Drift-check strictness** — plain regenerate-and-diff (proposed above)
   vs. Pi's lighter-weight hash-comment variant; a tradeoff between stronger
   guarantees and script complexity.
5. **Hot-path directory boundaries** for the no-alloc/no-mutex policy
   (affects `ARCHITECTURE_RULES.md`/`TENETS.md` content and any future
   static-analysis enforcement) — flagged by Pi, unresolved by any
   proposal.
6. **Governance record** — all three treat this as needing a
   `docs/decisions/DEC-####` entry; a human should confirm this unified
   approach before that decision is recorded, since it deliberately
   overrides pieces of all three original proposals.

# Cross-Review: AI Guidance Integration Proposals (Unified Recommendation)

- **Status:** cross-review + unified proposal (not yet accepted)
- **Date:** 2026-09-17
- **Proposed by:** Pi (Tauren session)
- **Audience:** all coding agents on this repository (Copilot, Abacus, Tauren/Pi, future tools)
- **Input documents:** [copilot-proposal.md](copilot-proposal.md), [abacus-proposal.md](abacus-proposal.md), [pi-proposal.md](pi-proposal.md)

Scope: how the agent-specific configuration files (`.github/copilot-instructions.md`,
`AGENTS.md`, Pi's config) link to a single canonical source of truth at
`docs/ai-guidance/`. Claims below were checked against the repository on
2026-09-17: no `AGENTS.md`, no `.github/copilot-instructions.md`, and no
`docs/ai-guidance/` exist yet; `.claude/` is empty; CI
(`.github/workflows/ci.yml`) runs a GTest + Catch2 matrix plus an ASan+UBSan /
TSan sanitizer matrix and container smokes; `~/.pi/agent/` contains no global
`AGENTS.md`; `.vscode/settings.json` holds no Tauren-specific config.

## 1. Proposal Summaries

**Copilot proposal (`copilot-proposal.md`)**

- **Mechanism:** canonical, tool-agnostic folder `docs/ai-guidance/` (8 files:
  `README.md`, `PROJECT_GUIDANCE.md`, `GOALS.md`, `TENETS.md`, `POLICIES.md`,
  `ARCHITECTURE_RULES.md`, `DEVELOPMENT_WORKFLOW.md`, `TASK_WORKFLOW.md`). Both
  `AGENTS.md` and `.github/copilot-instructions.md` are **pure pointer stubs**
  ("thin adapters") containing only a directive to read the canonical entry
  point — explicitly *no* policy content.
- **Assumptions about other agents:** Abacus and Tauren "should be configured to
  read the same canonical entry point at session start if they do not
  automatically discover repository guidance files" — i.e., it assumes
  link-following works for all of them and treats any agent that won't follow
  pointers as a configuration limitation, not a reason to duplicate.
- Also specifies governance: an implementation task (drafted as `T-0001`, which
  **already exists** in `docs/tasks/` — an ID collision), acceptance criteria,
  and maintenance rules; no enforcement mechanism beyond the pointers
  themselves.

**Abacus proposal (`abacus-proposal.md`)**

- **Mechanism:** canonical folder `docs/ai-guidance/`; `AGENTS.md` is a thin
  entry point containing (a) an **enumerated reading list** of specific file
  names (not a bare folder link), and (b) an **inline restatement of the 7
  highest-stakes safety tenets + hot-path rules** as a fallback for agents that
  never open the folder — explicitly acknowledged as intentional duplication
  with "a real, maintenance cost." `.github/copilot-instructions.md` is a
  **symlink to `../AGENTS.md`** (or one-line stub if symlinks are undesirable).
- **Assumptions about other agents:** most honest of the three — includes a
  per-agent "consumption reality" section: Abacus auto-reads `AGENTS.md`
  (verified); Copilot injects `copilot-instructions.md` verbatim but link
  following is not guaranteed; Tauren/Pi is declared **unverified** (no config
  found) and must be checked before assuming parity.
- Key framing: distinguishes "same information available" (structurally
  solvable) from "strict enforcement" (not achievable by markdown — CI/test
  gates remain the backstop).

**Pi proposal (`pi-proposal.md`, dated 2026-07-09)**

- **Mechanism:** one canonical **file** — repo-root `AGENTS.md` (~150–250 lines,
  <3k tokens) — with tool-specific **pointer-only shims**:
  `.github/copilot-instructions.md` carries ~10 core invariants inline *plus* a
  pointer (deliberate, size-bounded duplication), optional path-scoped
  `.github/instructions/*.instructions.md` with `applyTo` globs, and a **CI
  drift check** (`scripts/check-agent-guidance.sh`: shim existence, line budget,
  optional `<!-- guidance-hash: sha256(AGENTS.md) -->` recompute-and-fail).
- **Assumptions about other agents:** Pi/Tauren needs **no shim** (verified
  native auto-load of root `AGENTS.md`, walks up from cwd, independent of
  project trust; flags `--no-context-files` risk and startup-header
  verification). Copilot: auto-injection verified against docs. Abacus:
  mechanism unconfirmed — requirement is only that it "can point at one file,"
  with a pointer-only rule as the fallback.
- Distinctive content design: every tenet paired with its verification gate
  (existing CI jobs), turning prose into definition-of-done; small-local-model
  writing rules (inline all non-negotiables; pointers carry context, never
  constraints).

## 2. Compatibility Matrix

| Pair | Verdict | One-line reason |
|---|---|---|
| Copilot ↔ Abacus | **Conflicting** | Both claim ownership of `AGENTS.md` and `copilot-instructions.md`; copilot forbids *any* policy in adapters while abacus deliberately mirrors the 7 tenets inline in `AGENTS.md`, and abacus's symlink replaces copilot's self-contained pointer file. |
| Copilot ↔ Pi | **Conflicting** | Different canonical location (copilot: `docs/ai-guidance/` folder; pi: root `AGENTS.md` file) — implementing either makes the other's "canonical" artifact a stale copy; pi also inlines ~10 invariants in the Copilot shim that copilot's maintenance rules forbid. |
| Abacus ↔ Pi | **Overlapping / conflicting on mechanism** | Same two files, same *intent* (thin `AGENTS.md`-centered pointer + small inline safety net), but they disagree on where full content lives (`docs/ai-guidance/` vs root `AGENTS.md`) and how the Copilot file is realized (symlink vs committed shim); no exclusive folder control claimed by either beyond those two files. |
| Any pair ↔ repo state | Note | All three assume different starting points: copilot's draft reuses task ID `T-0001` (already taken), pi predates the other two by ~2 months (its "verified current state" is otherwise still accurate), and none has been implemented — no merge conflict exists yet, so all conflicts are design-level, resolvable before any file is written. |

**Exclusive-control callouts:** all three proposals touch `AGENTS.md` *and*
`.github/copilot-instructions.md`; each prescribes their full content, so none
can be adopted unmodified alongside another. Only the canonical-home question
(folder vs root file) is a true fork; the adapter files are convergent in
spirit (thin pointer + optional safety net) and divergent only in duplication
policy.

## 3. Criteria Evaluation

| Criterion | Copilot proposal | Abacus proposal | Pi proposal |
|---|---|---|---|
| **Single source of truth** | ✅ Best — zero content outside `docs/ai-guidance/`; adapters are pure pointers. | ⚠️ Partial — folder is canonical but the 7-tenet inline mirror in `AGENTS.md` is a second copy site (acknowledged, undetected by any check). | ❌ Weakest on location (content lives in root `AGENTS.md`, not `docs/ai-guidance/`) and ~10 invariants duplicated in the Copilot shim; mitigated by mechanical drift detection rather than elimination. |
| **Low maintenance** | ✅ Nothing to re-sync ever — but "automatic" only in the weak sense: if an agent doesn't follow the link, content silently never arrives, with no signal. | ⚠️ Symlink + pointer = zero sync for those paths; the inline tenet mirror requires manual re-edit whenever a core tenet changes, and nothing catches staleness. | ⚠️ Shim must be touched after canonical changes (hash variant turns this into an explicit CI failure until updated) — one extra step, but *visible* rather than silent. |
| **Tool-native compliance** | ✅ Respects each tool's expected path/format; relies on link-following all three actually perform, which is the weakest assumption (Copilot cloud injection and small local models can skip reads). | ✅ Best per-agent behavior analysis of the three; correctly identifies that `copilot-instructions.md` is injected verbatim (so what lives there is *guaranteed* context) and declines to assume for Tauren. ⚠️ Its chosen guarantee (inline tenets in `AGENTS.md`) lands on Copilot's injection path only via the symlink — which may not resolve at all. | ✅ Strongest native wiring: Pi needs no new file (verified auto-load + flag/header checks); uses a real Copilot feature others miss (`applyTo` path-scoped instructions); Abacus path honestly left as "point at one file." |
| **Portability** | ✅ Best — plain Markdown everywhere, no symlinks, works on Windows, adds no tools/scripts. | ❌ Weakest — git symlink breaks on Windows without `core.symlinks=true` (checks out as a text file containing `../AGENTS.md`, which Copilot would then inject literally) and GitHub-hosted Copilot's symlink handling is unverified; relative links inside the symlinked file resolve ambiguously (`.github/docs/…` vs repo root). | ✅ No symlinks, plain files; adds one CI script (`sha256sum` + shell — already-ubiquitous tooling) and a CI job to the existing workflow. |
| **Transparency** | ✅ Adapters self-describe as pointers ("should contain only a pointer"). | ✅ Symlink makes derivativeness obvious; proposal documents the duplication cost explicitly. | ✅ Shims labeled shims, line-budgeted so they can't silently grow into second copies, hash comment exposes staleness; CI encodes the rule. |

## 4. Recommended Unified Approach

**Design in one sentence:** adopt the Copilot proposal's canonical *folder* and
pointer discipline, inject the Abacus proposal's safety-net principle via the
Pi proposal's mechanism (generated, drift-checked stubs + tenet→gate pairing),
so `docs/ai-guidance/` is the only place content is authored and the two
adapter files are machine-regenerated derivatives.

### File tree

```text
repo/
├── docs/
│   └── ai-guidance/                 ← SINGLE SOURCE OF TRUTH (all authoring happens here)
│       ├── README.md                ← canonical entry point: what this dir is, reading order,
│       │                              "mandatory" marking convention, precedence rules
│       ├── PROJECT_GUIDANCE.md      ← short briefing: status pointer, safety posture, links to all focused files
│       ├── CORE.md                  ← the always-injected "core card": ≤7 non-negotiables, each
│       │                              paired with its verification gate (GTest/Catch2 job, sanitizer
│       │                              matrix, run_demo.sh). Extracted verbatim into both adapters.
│       ├── GOALS.md
│       ├── TENETS.md                ← full tenet list + rationale; CORE.md is its distilled subset
│       ├── POLICIES.md              ← mandatory rules (marked) + advisory rules
│       ├── ARCHITECTURE_RULES.md    ← ownership/boundaries, hot-path directories, layout versioning
│       ├── DEVELOPMENT_WORKFLOW.md  ← agent loop; links docs/DEVELOPMENT.md, STATUS.md, CI
│       └── TASK_WORKFLOW.md         ← discussion→decision→task→evidence chain
├── AGENTS.md                        ← GENERATED STUB (do not edit) — read natively by Tauren/Pi
│                                      and Abacus. Same generated body as copilot-instructions.md:
│                                      1. header: "Generated derivative of docs/ai-guidance/CORE.md;
│                                         authoritative content lives there; to change guidance,
│                                         edit docs/ai-guidance/ then run scripts/sync-agent-guidance.sh"
│                                      2. the verbatim CORE.md card (tenets + gates)
│                                      3. directive: before planning/editing, read
│                                         docs/ai-guidance/README.md and PROJECT_GUIDANCE.md,
│                                         with an ENUMERATED list of focused filenames (abacus insight)
├── .github/
│   ├── copilot-instructions.md      ← GENERATED STUB (do not edit) — identical generated body to
│   │                                      AGENTS.md; verbatim-injected by Copilot locally and on GitHub
│   ├── instructions/                ← OPTIONAL, phase 2 (pi bonus): shared-memory.instructions.md
│   │                                      with applyTo glob for path-scoped rules; its rule
│   │                                      text sourced from ARCHITECTURE_RULES.md, generated by the
│   │                                      same script (or pure pointer if kept manual)
│   └── workflows/ci.yml             ← +1 job: "agent-guidance drift" (see propagation below)
├── scripts/
│   └── sync-agent-guidance.sh       ← regenerates both stubs from CORE.md; idempotent, plain bash+sha256sum
└── (Tauren/Pi: NO new file — root AGENTS.md is auto-loaded; one-time verification that launch
    flags lack --no-context-files and that the startup header lists "AGENTS.md"; any
    ~/.pi/agent/AGENTS.md holds only generic cross-project preferences)
```

### One mechanism per file type, justified

| File | Mechanism | Why this one |
|---|---|---|
| `docs/ai-guidance/*.md` | Authored Markdown | Canonical home per the task premise and copilot proposal; folder (not one root file) keeps it navigable for humans and orthogonal to project docs, which pi-proposal's root-file design sacrificed. |
| `AGENTS.md` | **Generated stub** (real committed file) | It is natively auto-loaded by *two* of the three agents — it must be a real, thin, always-correct file. Symlink rejected: nothing links *to* it that needs dedup (Copilot's file is generated with identical body instead), and symlinks add no value here. Inline full content rejected: that's pi-proposal's drift-prone second home. |
| `.github/copilot-instructions.md` | **Generated stub**, same generator as `AGENTS.md` | Copilot injects this file *verbatim into every request* (abacus's verified insight) — it is the one adapter whose full content has a delivery guarantee, so it must carry the CORE card, not just a link. A symlink to `../AGENTS.md` (abacus) rejected on two portability grounds: Windows checkouts without `core.symlinks=true` produce a text file whose literal content gets injected, and GitHub-hosted Copilot's symlink handling is unverified — with no benefit, since the generator makes duplication zero-effort and CI-verified. A pure pointer (copilot proposal) rejected because it wastes the only guaranteed-injection slot on text that may never trigger a follow-up read by a small local model or cloud agent. |
| Tauren/Pi config | **No file** — native auto-load + one-time flag/header verification | Pi loads root `AGENTS.md` natively (verified in pi docs per pi-proposal; consistent with its observed behavior). Inventing a second entry point would create the exact duplication disease all three proposals set out to prevent. |
| Abacus project rules | **No repo file** — one line of user-level rule if needed: "always read `<repo>/AGENTS.md` and follow it" | Abacus already auto-reads `AGENTS.md` (self-reported, needs one-time confirmation); a pointer-only external rule keeps all content repo-controlled. |

### Propagation when `docs/ai-guidance/` changes

Two tiers:

1. **Non-core guidance** (everything except `CORE.md`): propagates
   **automatically and instantly** — adapters point by path and never copy. No
   step, no sync, no staleness possible. This is the copilot proposal's best
   property.
2. **Core card changes** (`CORE.md`): requires **one scripted step** —
   `scripts/sync-agent-guidance.sh` rewrites both stubs (and updates embedded
   hashes). It is *enforced, not habitual*: a new CI job runs the same check on
   every PR and fails with "stubs out of sync; run scripts/sync-agent-guidance.sh"
   if the committed stubs don't match a regeneration from `CORE.md`, or exceed
   the line budget (≤ ~40 lines). Agents may also run it pre-commit via an
   optional git hook. Net effect: guidance updates are normally zero-step; core
   changes produce at most one deterministic command, and silent divergence is
   mechanically impossible — combining pi-proposal's drift check with the
   folder-based SSOT that check was originally written for (its hash pointed at
   root `AGENTS.md`; here it points at `docs/ai-guidance/CORE.md`).

### What each agent's file literally contains (shape)

Both generated stubs, identically:

```markdown
<!-- GENERATED from docs/ai-guidance/CORE.md by scripts/sync-agent-guidance.sh — DO NOT EDIT.
     Authoritative guidance: docs/ai-guidance/. Edit there, then re-run the script. -->
# Core safety card (verbatim from docs/ai-guidance/CORE.md)
1. No data loss — ring-buffer CRC tests pass in GTest AND Catch2 configs.
2. No duplicate processing — monotonic seq + CAS slot consumption; both frameworks green.
… (≤7 tenets, each paired with its CI gate)
# Before planning or editing this repository
Read docs/ai-guidance/README.md and PROJECT_GUIDANCE.md, then the files you need:
GOALS.md, TENETS.md, POLICIES.md, ARCHITECTURE_RULES.md, DEVELOPMENT_WORKFLOW.md, TASK_WORKFLOW.md.
Requirements marked mandatory are binding; project guidance overrides generic agent habits.
```

### Best elements kept / dropped (per task item 4)

- **Copilot proposal — keep:** the `docs/ai-guidance/` IA and README-entry-point
  design; pure-pointer discipline as the *default* tier; "treat unsupported
  discovery as a config limitation, not a reason to duplicate"; governance via
  the existing decision/task/evidence flow. **Drop:** reliance on link-following
  as the only delivery mechanism (no enforcement layer at all); the `T-0001`
  task ID (already used — use the next free number).
- **Abacus proposal — keep:** per-agent consumption-reality analysis (should
  become a section of the decision record); inline safety-net *principle*;
  enumerated filename reading list; the "information availability vs strict
  enforcement" framing with CI gates as backstop. **Drop:** the unmanaged manual
  tenet mirror (replaced by generated `CORE.md` excerpt + hash check — same
  guarantee, zero silent staleness); the symlink mechanism (Windows/cloud-Copilot
  portability risks for no benefit once generation exists).
- **Pi proposal — keep:** tenet→gate pairing (this is the enforcement heart); CI
  drift check concept (retargeted at `CORE.md`); size budget + small-local-model
  writing rules; Pi needs-no-shim verification plan incl. `--no-context-files`
  and startup-header checks; `applyTo` path-scoped instructions as optional
  phase 2; governance trail (DEC record, proposal→reviews/). **Drop:** root
  `AGENTS.md` as canonical home (conflicts with the agreed premise and scatters
  guidance outside `docs/`); a hand-maintained two-copy surface where a
  generator removes it.

## 5. Open Questions

1. **Drift-check enforcement point:** pre-commit git hook, CI-only fail-and-tell,
   or both? (CI-only is the minimum; a hook means every contributor — including
   all three agents' sessions — runs the script locally. Needs human buy-in.)
2. **Core card scope:** which ≤7 tenets make the always-injected card, and does
   pi-proposal's §5 list match the ratified set in
   `SAFETY_CRITICAL_HA_PLAN.md` §7? (Carried over from pi's open question 4; this
   is content authoring, not mechanism.)
3. **Hot-path boundaries:** which directories count as "hot path" for the
   no-alloc/no-mutex policy — needed both for `ARCHITECTURE_RULES.md` wording
   and any future static check (pi's open question 5).
4. **Abacus confirmation:** one-time check that Abacus in *this* setup actually
   auto-reads root `AGENTS.md` (self-reported, not independently verified here);
   if its real mechanism is a separate instructions file, the generator just
   gains a third output slot — no design change.
5. **Symlink purists:** if anyone still prefers `copilot-instructions.md` as a
   symlink to `../AGENTS.md`, the decision should be explicit and evidence-backed
   (verify GitHub-hosted Copilot follows symlinks; mandate `core.symlinks=true`
   on all Windows dev machines). Recommendation stands: generated stub.
6. **Copilot double-reading:** recent VS Code Copilot versions also recognize
   `AGENTS.md`; if that applies here, the same card could be injected twice into
   Copilot's context (harmless but wasteful) — worth a one-time check; worst
   case, drop the card from the stub and keep only the pointer for Copilot.
7. **Phase-2 option:** adopt `.github/instructions/shared-memory.instructions.md`
   now or defer? (Pure add-on; zero cost to other agents.)
8. **Governance record:** per `docs/decisions/` convention, this cross-review
   itself should be filed under `docs/reviews/`, indexed in
   `DISCUSSIONS.md`, and adoption recorded as the next `DEC-####` with an
   implementation task (next free `T-00xx`) — confirm owner/priority.

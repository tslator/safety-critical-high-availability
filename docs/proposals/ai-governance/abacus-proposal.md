# Abacus Proposal: Agent-Agnostic AI Guidance

Author: Abacus AI Agent (discussion only — no repository changes made as
part of this proposal).

## Goal

One canonical set of AI guidance documents that all three tools in use on
this project — GitHub Copilot, Abacus, and Tauren (local llama-server /
Qwen) — read identically, kept orthogonal to project/architecture content so
it does not drift out of sync and is not duplicated per tool.

## Proposed Structure

- **`docs/ai-guidance/`** — holds the actual guidance documents: project
  tenets/safety properties, development policies, the documentation workflow
  (discussion → decision → task → evidence), and agent-conduct rules. This
  folder is the single source of truth; content is authored once here.
- **`AGENTS.md`** (repo root) — thin entry point. Contains:
  - An explicit directive instructing agents to read the files under
    `docs/ai-guidance/` before answering questions about architecture,
    safety properties, or development policy — as an enumerated list of
    filenames, not a bare folder link, since an enumerated checklist is more
    likely to be acted on than a directory reference requiring an extra
    listing step.
  - A restated summary of the highest-stakes, non-negotiable tenets (the 7
    safety properties, hot-path rules) inline, as a fallback in case an
    agent never opens `ai-guidance/`. This mirrors specific load-bearing
    bullets from the source documents rather than re-authoring them, but it
    does create two places to update if those particular bullets change —
    a real, acknowledged maintenance cost.
  - Links to `docs/ARCHITECTURE.md`, `docs/STATUS.md`, and the project plan
    for anything not safety-critical enough to warrant inline restatement.
- **`.github/copilot-instructions.md`** — a symlink to `../AGENTS.md` (or a
  one-line stub pointing to it if symlinks are undesirable for this repo/CI),
  so Copilot's conventional instructions path resolves to the same content
  with zero duplication.

## Consumption Reality, Per Agent

This structure improves discoverability and removes duplication, but it does
not make ingestion uniform or guaranteed. Verified/known behavior:

- **Abacus (me):** `AGENTS.md` at the workspace root is read automatically.
  I will follow links to `docs/ai-guidance/*.md` and open them when a task
  seems relevant to their content, more reliably if they're named explicitly
  rather than left as a folder reference. I do not reflexively re-read the
  full folder on every unrelated turn (e.g., a one-line typo fix).
- **GitHub Copilot:** `.github/copilot-instructions.md` is injected into
  every request verbatim, so the directive text itself is always seen.
  Whether Copilot's agent mode then actually opens and reads the linked
  `ai-guidance/` files depends on that mode's own behavior — it is not
  something the instruction text can force. A bare folder link with no
  enumerated files is weaker here than an explicit reading list.
- **Tauren (local Qwen via llama-server):** unverified. No Tauren
  configuration was found in this repository (checked `.vscode/settings.json`,
  `.github/`, and root dotfiles), so whether it auto-loads `AGENTS.md` or
  `copilot-instructions.md` at all — let alone follows markdown links out of
  them — is unknown. This must be checked against Tauren's own config
  (system-prompt include path, instructions-file setting, or similar) before
  assuming parity with the other two agents.

**Conclusion on the scheme:** it is sound for the stated goal (single
source, no duplication, discoverable by all three) but should be understood
as improving the *odds* of consistent context, not guaranteeing it. Splitting
guidance out of the auto-injected file into a linked folder trades some
guaranteed-injection strength for better organization; keeping the
non-negotiable tenets mirrored inline in `AGENTS.md` is the mitigation for
that trade-off.

## Is Strict, Identical Enforcement Across Agents Critical?

Two distinct concerns, not to be conflated:

1. **Same information available to all three agents** — yes, worth
   guaranteeing structurally via a single canonical folder with no per-tool
   copies. Achievable with the structure above.
2. **Strict enforcement that every agent follows the guidance every time** —
   not achievable through markdown files alone, for any of the three tools.
   Guidance placed in a prompt/context file is advisory to an LLM, not a
   hard constraint; a model can still deviate regardless of where the file
   lives or how it's worded.

For anything actually safety-critical in this project (no mutex/dynamic
allocation in hot paths, sanitizer-clean builds, both test frameworks
passing), the real enforcement mechanism already exists and should remain
CI/test gates, not prose guidance. `AGENTS.md` and `docs/ai-guidance/` should
be treated as steering context that improves compliance odds before code is
written, not as a substitute for the gates that catch violations after.

## Recommendation

Adopt the `docs/ai-guidance/` folder plus thin `AGENTS.md` entry point (with
inline safety-tenet summary and enumerated reading list) and the Copilot
symlink, understanding that:

- It solves single-sourcing and discoverability.
- It does not solve, and should not be relied on for, guaranteed or strict
  cross-agent enforcement — CI/test gates remain the backstop for anything
  safety-critical.
- Tauren's ingestion path is an open item that needs its own config
  investigation before assuming it sees the same guidance as Copilot and
  Abacus.

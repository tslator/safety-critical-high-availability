# Proposal: Unified Task Registry (Content-Neutral Record Process)

- Status: Accepted
- Date: 2026-09-18
- Related discussion: [D-2026-09-18-001](../../reviews/2026-09-18-record-process-scope.md)
- Related decision: [DEC-0006](../../decisions/0006-unified-task-registry.md)
- Related task: [T-0003](../../tasks/T-0003-unify-record-process.md)

## Problem

The discussion → decision → task → evidence chain is documented, but past
chat sessions were unsure whether it applied to development or only to
documentation. The ambiguity has concrete sources:

1. **Labeling bias.** `docs/tasks/README.md` was titled "Documentation Tasks"
   and `docs/DISCUSSIONS.md` "Documentation Discussions" — the word
   "Documentation" read as scope, contradicting `TASK_WORKFLOW.md`, which says
   the chain "applies to AI-guidance changes as much as to code changes."
2. **Two competing task systems.** `docs/tasks/` held only doc/guidance tasks
   (T-0001, T-0002), while the actual development tasks (Phase 0 `0.1`–`0.6`,
   Phase 1 `T1.1`–`T1.5`) lived inside phase plans with their own gates,
   recorded deviations, and evidence pointers. Nothing said where a code task's
   registry record went, so `DEC-0005` punted a code task ("Phase 1 task T1.3")
   into a phase plan.
3. **Self-referential seed corpus.** The only complete `docs/tasks/` chains
   were about the guidance system itself, so every tidy example looked
   documentation-only.

## Verdict

The process is **not** documentation-only. It is content-neutral by design and
already applied to development: [DEC-0005](../../decisions/0005-t1.3-crc-integrity.md)
and
[D-2026-09-17-003](../../reviews/2026-09-17-t1.3-crc-integrity.md)
are a code design decision running the full chain, and `POLICIES.md` mandates
the records for shared-memory/ring-buffer changes. The fix is naming plus a
documented boundary — not a new process.

## Decision

Unify on a single task registry at `docs/tasks/` covering all work:

- **Single registry.** `docs/tasks/` is the one task registry for code, docs,
  and infra work. Phase plans keep the technical task narrative (scope, steps,
  gates) but link to the canonical registry record instead of being the task
  source of truth.
- **Canonical IDs.** Sequential `T-####` is the canonical namespace. Phase
  plans keep phase-scoped human labels (`T1.3`, `Task 0.2`) as aliases and link
  to the canonical record. Existing T-0001/T-0002 are unchanged.
- **Record depth.** Full per-task file (the existing schema) for any task that
  has an authorizing decision or is not yet complete; a lighter roll-up index
  row linking to the phase plan for completed phase tasks with no decision of
  their own.
- **Status vocabulary.** One vocabulary everywhere: `Planned | Ready | In
  Progress | In Review | Complete | Blocked`.
- **Evidence.** Keep `NOTES.md` as the raw gate log and `docs/evidence/` as the
  roll-up; each task record links onward rather than duplicating.

## Plan

### Stage 1 — Naming and scope (no data moves)

- `docs/tasks/README.md`: title `# Tasks`; content-neutral intro; add a **Phase**
  column to the index.
- `docs/DISCUSSIONS.md`: title `# Discussions`; drop the doc-only qualifier.
- `docs/ai-guidance/TASK_WORKFLOW.md`: declare `docs/tasks/` the single
  registry; state that phase plans link to registry records; cite `DEC-0005` /
  `T-0004` as the development exemplar (closes the current gap).
- `docs/phases/README.md`: implementation tasks are recorded in the registry;
  the phase plan holds scope/gates narrative plus links.

### Stage 2 — Populate the registry

- Create `T-0003` (this unification), `T-0004` (Phase 1 `T1.3`, the completed
  development exemplar), `T-0005` (Phase 1 `T1.4`), `T-0006` (Phase 1 `T1.5`).
- Index everything in `docs/tasks/README.md` with the Phase column so code and
  docs tasks sit side by side.

### Stage 3 — Phase plans link, not own

- Add a `Record: [T-####]` line to the Phase 1 plan `T1.3`–`T1.5` task
  headings.
- Re-point `DEC-0005` and `D-2026-09-17-003` `Authorizes:` to `[T-0004]`, which
  links onward to the phase gate. Minimal link-target correction only; decision
  bodies are not rewritten (per `DEC-0003` maintenance rules), justified in
  `DEC-0006`.

## Acceptance Criteria

1. Every task (code + docs) is resolvable from `docs/tasks/README.md`; `T-0004`
   walks `DEC-0005` → `T-0004` → phase gate → evidence.
2. No index title or scope label reads "Documentation Tasks"/"Documentation
   Discussions" (historical references explaining the rename may quote them).
3. `TASK_WORKFLOW.md` unambiguously states where code task records live.
4. One status vocabulary across `README` and `STATUS.md`.

## Verification

- Repo-relative Markdown link check across `docs/`.
- `git diff --check`.
- `./scripts/sync-agent-guidance.sh --check` exits 0 (CORE unchanged, so
  adapters stay byte-identical and `agent-guidance-drift` stays green).
- Grep confirms zero doc-only qualifier strings.

## Out of Scope

Code, CI, `CORE.md` Tier 1 content, phase-plan technical content, historical
`docs/proposals/`, raw chat, and `NOTES.md` gate logs.

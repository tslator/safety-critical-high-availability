# D-2026-09-18-001: Record Process Scope and Registry Unification

- Date: 2026-09-18
- Status: Accepted
- Related decision: [DEC-0006](../decisions/0006-unified-task-registry.md)
- Resulting task: [T-0003](../tasks/T-0003-unify-record-process.md)

## Question

Is the discussion → decision → task → evidence chain strictly for
documentation, or does it also govern development work? Past sessions were
unsure which system to use and where a code task's record belongs.

## Findings

- The chain is content-neutral by design and already applied to development:
  [DEC-0005](../decisions/0005-t1.3-crc-integrity.md) and
  [D-2026-09-17-003](2026-09-17-t1.3-crc-integrity.md)
  are a code design decision running the full chain, and `POLICIES.md` mandates
  the records for shared-memory/ring-buffer changes.
- The confusion has concrete sources, not user error:
  1. `docs/tasks/README.md` was titled "Documentation Tasks" and
     `docs/DISCUSSIONS.md` "Documentation Discussions" — the word
     "Documentation" read as scope, contradicting `TASK_WORKFLOW.md`.
  2. Two competing task systems: `docs/tasks/` held only doc/guidance tasks
     (T-0001, T-0002) while the real development tasks (`0.1`–`0.6`,
     `T1.1`–`T1.5`) lived in phase plans; nothing said where a code task record
     goes, so `DEC-0005` punted "Phase 1 task T1.3" into a phase plan.
  3. The only complete `docs/tasks/` chains were about the guidance system
     itself, so every tidy example looked documentation-only.

## Alternatives Considered

- **Formalize the split** (phase plans own code tasks, `docs/tasks/` owns meta
  work): rejected — keeps two task schemas and the same ambiguity about where a
  record goes.
- **Minimal clarification only** (add a note, keep structure): rejected — leaves
  the doc-only task registry that triggered the confusion.
- **Rewrite accepted decisions** to repoint links: rejected — violates the
  `DEC-0003` maintenance rule that decisions are superseded, not rewritten.

## Result

Unify on a single task registry at `docs/tasks/` covering all work with
sequential `T-####` canonical IDs (phase-scoped labels as aliases), drop the
"Documentation" qualifiers, state the boundary in `TASK_WORKFLOW.md`, populate
the active-phase and decision-bearing tasks, and have phase plans link to
registry records. Rationale and plan captured in
[the proposal](../proposals/record-process/unified-task-registry.md).

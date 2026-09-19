# T-0003: Unify Record Process on docs/tasks

- Status: Complete
- Owner: Unassigned
- Priority: High
- Depends on: [DEC-0006](../decisions/0006-unified-task-registry.md)
- Phase: Meta
- Related decision: [DEC-0006](../decisions/0006-unified-task-registry.md)
- Related discussion: [D-2026-09-18-001](../reviews/2026-09-18-record-process-scope.md)

## Scope

Make the discussion → decision → task → evidence chain unambiguously
content-neutral by unifying all work into a single task registry at
`docs/tasks/`, per [DEC-0006](../decisions/0006-unified-task-registry.md).

## Deliverables

- Re-title and re-scope `docs/tasks/README.md` and `docs/DISCUSSIONS.md`
  (drop the "Documentation" qualifier); add a Phase column to the task index.
- Update `docs/ai-guidance/TASK_WORKFLOW.md` to declare `docs/tasks/` the single
  registry and cite `DEC-0005` / `T-0004` as the development exemplar.
- Align `docs/phases/README.md` task-placement wording to the registry.
- Register the active-phase and decision-bearing tasks (`T-0004`–`T-0006`); add
  roll-up rows for completed phase tasks with no decision of their own.
- Add `Record: [T-####]` links to Phase 1 plan tasks `T1.3`–`T1.5`.
- Re-point `DEC-0005` and `D-2026-09-17-003` `Authorizes:` lines to `T-0004`.

## Acceptance Criteria

1. Every task (code + docs) is resolvable from `docs/tasks/README.md`; `T-0004`
   walks `DEC-0005` → `T-0004` → phase gate → evidence.
2. No index title or scope label reads "Documentation Tasks"/"Documentation
   Discussions" (historical references explaining the rename may quote them).
3. `TASK_WORKFLOW.md` unambiguously states where code task records live.
4. One status vocabulary across `README` and `STATUS.md`.

## Validation

- Repo-relative Markdown link check across `docs/` passes.
- `git diff --check` passes.
- `./scripts/sync-agent-guidance.sh --check` exits 0 (`CORE.md` unchanged).
- `grep` confirms zero doc-only qualifier strings.

## Completion Notes

Complete. Single registry at `docs/tasks/` now covers code + docs + infra with a
Phase column; `T-0004` is the development exemplar (DEC-0005 → T-0004 → gate
G1.3 → evidence). Index titles dropped the "Documentation" qualifier.
`CORE.md` untouched, so adapters stay byte-identical (`sync --check` exit 0).
Verified: repo-relative link check (0 broken in scope), `git diff --check` clean.

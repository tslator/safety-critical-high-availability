# T-0002: Implement AI Guidance Architecture

- Status: Complete
- Owner: Unassigned
- Priority: High
- Depends on: None
- Related decision: [DEC-0004](../decisions/0004-ai-guidance-architecture.md)

## Scope

Implement the consensus AI guidance architecture: a canonical
`docs/ai-guidance/` directory, a generator script producing `AGENTS.md` and
`.github/copilot-instructions.md`, and CI enforcement of adapter drift.

## Deliverables

- Add `docs/ai-guidance/{README,PROJECT_GUIDANCE,CORE,GOALS,TENETS,POLICIES,
  ARCHITECTURE_RULES,DEVELOPMENT_WORKFLOW,TASK_WORKFLOW}.md`.
- Add `scripts/sync-agent-guidance.sh` with default (regenerate) and
  `--check` (drift detection, non-mutating) modes.
- Generate `AGENTS.md` and `.github/copilot-instructions.md` from
  `docs/ai-guidance/CORE.md` using the script.
- Add an `agent-guidance-drift` CI job running
  `scripts/sync-agent-guidance.sh --check`.
- Add an optional POSIX pre-commit hook (`scripts/hooks/pre-commit`).
- Record this decision and discussion under `docs/decisions/` and
  `docs/reviews/ai-guidance/`, and index them in `docs/DISCUSSIONS.md` and
  `docs/tasks/README.md`.

## Acceptance Criteria

- All authored AI guidance resides under `docs/ai-guidance/`.
- `CORE.md` is the single source of embedded Tier 1 adapter content.
- `AGENTS.md` and `.github/copilot-instructions.md` are generated, carry a
  visible generated-file header, and are byte-identical to what
  `scripts/sync-agent-guidance.sh` produces.
- `scripts/sync-agent-guidance.sh --check` passes against the committed
  adapters and fails if `CORE.md` changes without regeneration.
- CI runs the drift check and fails a pull request that changes `CORE.md`
  without committing regenerated adapters.

## Validation

`./scripts/sync-agent-guidance.sh` followed by
`./scripts/sync-agent-guidance.sh --check` exits 0 against the committed
`AGENTS.md` and `.github/copilot-instructions.md`.

## Completion Notes

The `docs/ai-guidance/` directory, generator script, generated adapters, CI
drift-check job, and optional pre-commit hook are in place. Abacus and
Tauren/Pi loading-behavior verification (open items in the consensus
summary) remain outstanding and are not blocking for this task.

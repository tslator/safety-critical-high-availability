# DEC-0004: AI Guidance Architecture

- Status: Accepted
- Date: 2026-09-17
- Related discussion: [D-2026-09-17-002](../reviews/ai-guidance/2026-09-17-ai-guidance-architecture.md)
- Related task: [T-0002](../tasks/T-0002-ai-guidance-implementation.md)

## Context

Three independent proposals for structuring AI agent guidance (for Copilot,
Abacus, and Tauren/Pi) were cross-reviewed and synthesized into
[`docs/proposals/ai-governance/ai-guidance-consensus-summary.md`](../proposals/ai-governance/ai-guidance-consensus-summary.md).
See the linked discussion record for the full findings and alternatives
considered.

## Decision

Adopt `docs/ai-guidance/` as the single authored source of AI agent
guidance, with `docs/ai-guidance/CORE.md` as the sole authored source of
always-injected Tier 1 content. Root `AGENTS.md` and
`.github/copilot-instructions.md` are generated derivatives produced by
`scripts/sync-agent-guidance.sh`; they must not be hand-edited, and CI
rejects any pull request where the committed adapters drift from a fresh
regeneration.

## Consequences

- Guidance content is edited in exactly one place; adapters cannot silently
  duplicate or diverge from it.
- Both Copilot entry points (`AGENTS.md` and
  `.github/copilot-instructions.md`) carry the current Tier 1 core card
  without manual synchronization.
- Tauren/Pi and Abacus can rely on native `AGENTS.md` discovery without a
  separate authored copy.
- Adding or changing a non-negotiable safety tenet requires updating
  `CORE.md` and regenerating the adapters as one atomic change, which the
  `agent-guidance-drift` CI job enforces.
- An optional local pre-commit hook (`scripts/hooks/pre-commit`) can catch
  drift before commit; CI remains the mandatory enforcement point.

## Maintenance Rules

- Edit only `docs/ai-guidance/*.md`; never edit `AGENTS.md` or
  `.github/copilot-instructions.md` directly.
- Run `scripts/sync-agent-guidance.sh` after any `CORE.md` change and commit
  the regenerated adapters in the same change.
- Keep `CORE.md` limited to non-negotiable, testable requirements paired
  with their verification gate.

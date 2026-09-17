# D-2026-09-17-002: AI Guidance Architecture

- Date: 2026-09-17
- Status: Accepted
- Related decision: [DEC-0004](../../decisions/0004-ai-guidance-architecture.md)
- Resulting task: [T-0002](../../tasks/T-0002-ai-guidance-implementation.md)

## Question

How should AI agent guidance for Copilot, Abacus, and Tauren/Pi be
structured so all three agents receive equivalent, non-duplicated guidance
from a single authored source, without relying on unverified per-tool
link-following behavior?

## Source Material

Three independent proposals and cross-reviews were produced, then
synthesized into a final consensus:

- [`copilot-proposal.md`](../../proposals/ai-governance/copilot-proposal.md),
  [`abacus-proposal.md`](../../proposals/ai-governance/abacus-proposal.md),
  [`pi-proposal.md`](../../proposals/ai-governance/pi-proposal.md) — initial
  independent proposals.
- [`copilot-unified.md`](../../proposals/ai-governance/copilot-unified.md),
  [`abacus-unified.md`](../../proposals/ai-governance/abacus-unified.md),
  [`pi-unified.md`](../../proposals/ai-governance/pi-unified.md) — cross-review
  and unification passes over the three proposals.
- [`ai-guidance-consensus-summary.md`](../../proposals/ai-governance/ai-guidance-consensus-summary.md)
  — the final, approved design summary this decision implements.

## Findings

All three initial proposals agreed that `docs/ai-guidance/` should hold the
authored content, and all three agreed that a hand-maintained second copy of
policy in `AGENTS.md` or `.github/copilot-instructions.md` is a maintenance
and drift risk. They disagreed on whether the adapters should be pure
pointers (Copilot), inline-with-manual-duplication (Abacus), or the
canonical content itself (Pi). The consensus summary resolves this by making
the adapters generated, not hand-maintained: they embed a small, always-fresh
Tier 1 core card and enumerate Tier 2 documents, with CI enforcing that the
committed adapters match a fresh regeneration.

## Alternatives Considered

- Pure pointer-only adapters with no inline content: rejected because
  link-following by all target agents was unverified, and non-negotiable
  safety tenets could silently never reach a session's context.
- Hand-maintained inline duplication of tenets in the adapters: rejected
  because it has no drift detection and requires manual re-sync on every
  tenet change.
- Root `AGENTS.md` as the canonical, content-bearing file: rejected because
  it conflicts with keeping `docs/ai-guidance/` as the single authored
  source and scatters guidance outside `docs/`.
- Symlinking `.github/copilot-instructions.md` to `AGENTS.md`: rejected for
  Windows/checkout portability risk.

## Result

Adopt the architecture in `ai-guidance-consensus-summary.md`: `docs/ai-guidance/`
as the sole authored source, `CORE.md` as the sole Tier 1 source, and
generated, CI-drift-checked adapters at `AGENTS.md` and
`.github/copilot-instructions.md`.

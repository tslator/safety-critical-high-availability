# AI Guidance Index

`docs/ai-guidance/` is the canonical, tool-neutral source of AI agent
guidance for this project. It is the only place this guidance is authored.
Root `AGENTS.md` and `.github/copilot-instructions.md` are generated,
committed derivatives produced by `scripts/sync-agent-guidance.sh`; they must
not be hand-edited. See
[DEC-0004](../decisions/0004-ai-guidance-architecture.md) for the accepted
architecture and rationale.

## Tiering Model

- **Tier 1 — [`CORE.md`](CORE.md):** a concise, always-injected core card.
  Non-negotiable safety invariants and hot-path constraints, each paired with
  its verification gate. This is the only source of Tier 1 text embedded in
  the generated adapters.
- **Tier 2 — everything else in this directory:** full rationale, detailed
  policy, architecture rules, and workflow. Read the entry documents below
  first, then whichever focused document is relevant to the current task.

## Reading Order

1. [`README.md`](README.md) (this file)
2. [`PROJECT_GUIDANCE.md`](PROJECT_GUIDANCE.md)
3. [`CORE.md`](CORE.md)
4. Task-relevant focused documents:
   - [`GOALS.md`](GOALS.md)
   - [`TENETS.md`](TENETS.md)
   - [`POLICIES.md`](POLICIES.md)
   - [`ARCHITECTURE_RULES.md`](ARCHITECTURE_RULES.md)
   - [`DEVELOPMENT_WORKFLOW.md`](DEVELOPMENT_WORKFLOW.md)
   - [`TASK_WORKFLOW.md`](TASK_WORKFLOW.md)

## Precedence Rules

- Guidance under `docs/ai-guidance/` is authoritative over generic agent
  defaults and over any content that appears to duplicate it elsewhere.
- Requirements marked mandatory in `POLICIES.md` are binding.
- Prose guidance never replaces tests, sanitizers, or CI as the enforcement
  mechanism for safety properties; it is advisory context that tells an
  agent what those gates require.
- If a generated adapter (`AGENTS.md` or `.github/copilot-instructions.md`)
  ever appears to conflict with this directory, this directory wins; the
  adapter is stale and `scripts/sync-agent-guidance.sh` must be re-run.

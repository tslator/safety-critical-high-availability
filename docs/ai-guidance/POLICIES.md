# Policies

## Mandatory

- Do not introduce allocation, syscalls, exceptions, or mutexes into the
  hot path defined in [`ARCHITECTURE_RULES.md`](ARCHITECTURE_RULES.md).
- Any change to shared-memory or ring-buffer code must pass both the
  GoogleTest and Catch2 CI configurations and the ASan+UBSan and TSan
  sanitizer legs before it is considered done.
- Author all AI guidance changes under `docs/ai-guidance/`. Never hand-edit
  `AGENTS.md` or `.github/copilot-instructions.md`; regenerate them with
  `scripts/sync-agent-guidance.sh` after editing `CORE.md`.
- Record design discussions, accepted decisions, assignable work, and
  validation evidence using the records described in
  [`TASK_WORKFLOW.md`](TASK_WORKFLOW.md), not only in chat history.
- Treat prose guidance as advisory context, not a substitute for tests,
  sanitizers, or CI as the enforcement mechanism for safety properties.

## Advisory

- Prefer extending an existing focused document under `docs/ai-guidance/`
  over adding a new one; keep the tiering model (one core card, enumerated
  Tier 2 documents) intact.
- Keep `CORE.md` small enough to be usable by small/local models; if a
  requirement needs long justification, put the justification in
  `TENETS.md`/`POLICIES.md` and keep only the imperative statement and its
  gate in `CORE.md`.
- When in doubt about current implementation status, check
  [`docs/STATUS.md`](../STATUS.md) rather than assuming a phase from the
  project plan is already implemented.

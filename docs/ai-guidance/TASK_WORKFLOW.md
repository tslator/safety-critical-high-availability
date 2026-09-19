# Task Workflow

This project turns discussions into durable, assignable, auditable records
rather than leaving them in chat history. See
[DEC-0003](../decisions/0003-documentation-discussion-records.md) for the
accepted rationale.

## Chain

1. **Discussion/review** (`docs/reviews/`) — distilled findings from a
   design discussion; not a raw transcript.
2. **Decision** (`docs/decisions/`) — an accepted design choice, linked to
   the discussion that produced it and the task(s) it authorizes.
3. **Task** (`docs/tasks/`) — the single task registry for all work (code,
   docs, infra). Assignable work with an owner, priority, dependencies, scope,
   and acceptance criteria, linked to its decision. Canonical IDs are sequential
   `T-####`; phase-scoped labels such as `T1.3` are aliases that link to the
   canonical record.
4. **Evidence** (`docs/evidence/`) — validation results proving a task's
   acceptance criteria were met.

## Rules

- Accepted decisions are superseded by new decisions rather than rewritten.
- Tasks require acceptance criteria before assignment; see
  [`docs/tasks/README.md`](../tasks/README.md) for the current task rules
  and index.
- Closed tasks link to implementation and validation evidence.
- Index new discussions in [`docs/DISCUSSIONS.md`](../DISCUSSIONS.md).
- Phase plans keep the technical task narrative (scope, steps, gates) and link
  to the canonical registry record; `docs/tasks/` is the source of truth for
  task status and traceability. See [DEC-0006](../decisions/0006-unified-task-registry.md).

The chain is content-neutral: it applies to development work as much as to
guidance and documentation changes.
[DEC-0005](../decisions/0005-t1.3-crc-integrity.md) /
[T-0004](../tasks/T-0004-t1.3-crc-integrity.md) is the reference for a
development task run through the chain, and a proposal to change
`docs/ai-guidance/` content goes through the same chain before the adapters are
regenerated and committed.

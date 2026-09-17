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
3. **Task** (`docs/tasks/`) — assignable work with an owner, priority,
   dependencies, scope, and acceptance criteria, linked to its decision.
4. **Evidence** (`docs/evidence/`) — validation results proving a task's
   acceptance criteria were met.

## Rules

- Accepted decisions are superseded by new decisions rather than rewritten.
- Tasks require acceptance criteria before assignment; see
  [`docs/tasks/README.md`](../tasks/README.md) for the current task rules
  and index.
- Closed tasks link to implementation and validation evidence.
- Index new discussions in [`docs/DISCUSSIONS.md`](../DISCUSSIONS.md).

This applies to AI-guidance changes as much as to code changes: a proposal
to change `docs/ai-guidance/` content should go through this same chain
before the adapters are regenerated and committed.

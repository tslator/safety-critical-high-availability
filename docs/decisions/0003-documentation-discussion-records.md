# DEC-0003: Documentation Discussion Records

- Status: Accepted
- Date: 2026-09-17
- Related discussion: [D-2026-09-17-001](../reviews/2026-09-17-documentation-workflow.md)
- Related task: [T-0001](../tasks/T-0001-documentation-index.md)

## Context

Design discussions and reviews contain useful reasoning, rejected alternatives,
and follow-up work, but raw conversations are difficult to assign and audit.

## Decision

Use concise Markdown records for discussions/reviews, decisions, tasks, and
evidence. Link them through stable IDs and maintain simple indexes rather than
introducing a database or tracker integration.

## Consequences

- Design rationale remains searchable and reviewable.
- Agreed work can be distributed with explicit acceptance criteria.
- Validation evidence can be traced back to the originating discussion.
- Raw chat transcripts remain working material, not repository artifacts.

## Maintenance Rules

- Accepted decisions are superseded by new decisions rather than rewritten.
- Tasks require acceptance criteria before assignment.
- Closed tasks link to implementation and validation evidence.

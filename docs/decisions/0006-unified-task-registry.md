# DEC-0006: Unified Task Registry

- Status: Accepted
- Date: 2026-09-18
- Related discussion: [D-2026-09-18-001](../reviews/2026-09-18-record-process-scope.md)
- Related task: [T-0003](../tasks/T-0003-unify-record-process.md)
- Extends: [DEC-0003](0003-documentation-discussion-records.md)

## Context

The discussion → decision → task → evidence chain is content-neutral and
already covers development work ([DEC-0005](0005-t1.3-crc-integrity.md)), but
past sessions were unsure whether it applied to development or only to
documentation. "Documentation Tasks"/"Documentation Discussions" labels read as
scope, and the real development tasks lived only in phase plans rather than in
the task registry. See
[the proposal](../proposals/record-process/unified-task-registry.md) for the
full findings and alternatives.

## Decision

1. **Single registry.** `docs/tasks/` is the one task registry for all work —
   code, docs, and infra. Phase plans keep the technical task narrative (scope,
   steps, gates) but link to the canonical registry record; the registry is the
   source of truth for task status and traceability.
2. **Canonical IDs.** Sequential `T-####` is the canonical task namespace.
   Phase plans keep phase-scoped labels (`T1.3`, `Task 0.2`) as human-readable
   aliases that link to the canonical record. Existing T-0001/T-0002 keep their
   IDs and files unchanged.
3. **Record depth.** A full per-task file (Status/Owner/Priority/Depends on/
   Related decision + Scope/Deliverables/Acceptance Criteria/Validation/
   Completion Notes) is required for any task that has an authorizing decision
   or is not yet complete. Completed phase tasks with no decision of their own
   may be a roll-up index row that links to the phase plan.
4. **Status vocabulary.** One vocabulary everywhere: `Planned | Ready | In
   Progress | In Review | Complete | Blocked`.
5. **Evidence.** `NOTES.md` stays the raw gate log and `docs/evidence/` the
   roll-up; task records link onward rather than duplicating results.
6. **Labels.** Drop the "Documentation" qualifier from the tasks and
   discussions index titles and intros.

This extends DEC-0003: the record types and linking it defines are unchanged,
but task placement is clarified so development tasks live in the registry, not
only in phase plans.

## Consequences

- Development and documentation tasks are discoverable from one index with a
  Phase column, making the content-neutral scope explicit.
- `DEC-0005` and `D-2026-09-17-003` `Authorizes:` lines are re-pointed from
  "Phase 1 task T1.3 in `docs/phases/...`" to `[T-0004]`, which links onward to
  the phase gate. This is a minimal link-target correction, not a body rewrite;
  the decision content stands as accepted.
- Phase plans gain a `Record:` line per task; their technical narrative and gates
  are otherwise unchanged.
- `CORE.md` is untouched, so the generated adapters stay byte-identical and the
  `agent-guidance-drift` CI job stays green; no regeneration is required.

## Maintenance Rules

- Create new tasks in `docs/tasks/` with the next sequential `T-####`; never
  reuse an ID.
- A phase-plan task heading links to its canonical record; status is maintained
  in the registry, and `docs/STATUS.md` remains a human roll-up briefing.
- Correct wrong links in accepted decisions as a minimal maintenance edit and
  note the justification in the decision that authorizes the correction.

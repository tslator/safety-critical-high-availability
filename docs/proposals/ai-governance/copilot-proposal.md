# Shared AI Guidance Proposal

## Purpose

Provide Copilot, Abacus, Tauren, and future development agents with the same
project goals, architectural boundaries, engineering tenets, development
policies, and task-traceability workflow.

The guidance must be visible to human contributors, independent of any one AI
vendor, and separate from implementation source. Agent-specific files should
only provide discovery adapters; they must not duplicate the policy.

## Recommended Layout

```text
docs/
└── ai-guidance/
    ├── README.md
    ├── PROJECT_GUIDANCE.md
    ├── GOALS.md
    ├── TENETS.md
    ├── POLICIES.md
    ├── ARCHITECTURE_RULES.md
    ├── DEVELOPMENT_WORKFLOW.md
    └── TASK_WORKFLOW.md

AGENTS.md
.github/
└── copilot-instructions.md
```

`docs/ai-guidance/` is the canonical, tool-agnostic source. `AGENTS.md` and
`.github/copilot-instructions.md` are thin adapters for tools that recognize
those conventions. Abacus and Tauren should be configured to read the same
canonical entry point at session start if they do not automatically discover
repository guidance files.

## Canonical Entry Point

`docs/ai-guidance/README.md` should be prominent in the documentation map and
should state:

- this directory is the canonical source for AI-assisted development guidance;
- agents must read `PROJECT_GUIDANCE.md` before planning or editing;
- mandatory requirements are explicitly marked;
- focused guidance files contain the detailed goals, rules, and workflows; and
- project-specific guidance takes precedence over generic agent habits.

`PROJECT_GUIDANCE.md` should be a short briefing that links to every focused
file. It should cover the current project status, safety-critical posture,
required validation behavior, and documentation traceability without duplicating
the full phase plans.

## Guidance Content

### `GOALS.md`

Describe the intended outcomes:

- demonstrable high availability and controlled failover;
- bounded, observable, and deterministic behavior;
- reproducible builds and tests;
- evidence-based confidence; and
- no unsupported claims of hard real-time behavior or certification.

### `TENETS.md`

Record the principles used for engineering judgment:

- prefer correctness and explicit failure modes over convenience;
- preserve bounded behavior in hot paths;
- treat shared-memory layout as an interface contract;
- make concurrency assumptions explicit;
- prefer conservative failure over silent integrity loss; and
- keep changes small, traceable, and testable.

### `POLICIES.md`

Mark mandatory rules clearly. At minimum:

- safety-relevant behavior changes require focused tests;
- validation must not be weakened merely to reduce false failures;
- unrelated user changes must not be reverted;
- focused validation follows each substantive edit;
- residual risks and rejected alternatives are recorded;
- generated build artifacts are not committed; and
- new dependencies require a documented justification.

### `ARCHITECTURE_RULES.md`

Capture current ownership and boundaries:

- `shared-memory/` owns the shared-memory substrate;
- live worker takeover uses scoped ring verification;
- whole-region verification is limited to pre-boot or maintenance windows;
- shared-region layout changes require an explicit versioning decision; and
- worker, supervisor, monitor, and perturbation components remain roadmap work
  until implemented.

### `DEVELOPMENT_WORKFLOW.md`

Define the expected agent loop:

```text
inspect -> form a local hypothesis -> make a minimal change
       -> run focused validation -> inspect the diff -> report evidence
```

Link to the existing [development guide](DEVELOPMENT.md), current
[status](STATUS.md), and CI workflow. Add guidance-specific detail here only
when it is not already authoritative in those documents.

### `TASK_WORKFLOW.md`

Define the traceability chain:

```text
discussion/review -> decision, when needed -> task -> evidence -> status
```

Discussion and review records belong under `docs/reviews/`, decisions under
`docs/decisions/`, assignable work under `docs/tasks/`, and validation records
under `docs/evidence/`.

## Thin Adapters

The root `AGENTS.md` should contain only a pointer such as:

```markdown
# Shared Project Guidance

Before planning or modifying this repository, read
[docs/ai-guidance/README.md](docs/ai-guidance/README.md) and
[docs/ai-guidance/PROJECT_GUIDANCE.md](docs/ai-guidance/PROJECT_GUIDANCE.md).

These are the canonical, tool-agnostic project goals, architecture rules,
engineering policies, development workflow, and task-traceability guidance.
Follow requirements marked mandatory.
```

`.github/copilot-instructions.md` should provide the same pointer using the
path appropriate to that file. Neither adapter should contain an independent
copy of the policy.

## Initial Implementation Task

Create task `T-0001: Establish shared AI guidance` with these acceptance
criteria:

- `docs/ai-guidance/README.md` is the canonical entry point;
- `PROJECT_GUIDANCE.md` links to all focused guidance files;
- `AGENTS.md` and `.github/copilot-instructions.md` point to the same source;
- the root [README](../README.md) and [status page](STATUS.md) link to the
  guidance entry point;
- no policy is duplicated in the adapters; and
- all repository-relative Markdown links resolve.

The first implementation should not move or rewrite the phase plans. It should
add the guidance layer, update navigation, and record the work through the
existing discussion, decision, task, and evidence system.

## Maintenance Rules

- Keep the canonical guidance concise and link to authoritative project docs.
- Update guidance when goals, architecture boundaries, or mandatory policies
  change.
- Record accepted design choices as decisions rather than silently rewriting
  history.
- Close tasks only when implementation and validation evidence are linked.
- Review the guidance when the project enters a new phase.
- Treat unsupported agent discovery as a configuration limitation, not as a
  reason to duplicate policy across multiple adapter formats.

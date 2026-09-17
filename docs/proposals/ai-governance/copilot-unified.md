# Cross-Review: AI Guidance Integration Proposals

## 1. Proposal Summaries

### `copilot-proposal.md`

- Makes `docs/ai-guidance/` the canonical, tool-agnostic source.
- Uses thin Markdown adapters in root `AGENTS.md` and `.github/copilot-instructions.md`, each linking to `README.md` and `PROJECT_GUIDANCE.md`.
- Assumes Abacus and Tauren can be configured to read the canonical entry point.
- Avoids duplicating policy content in agent-specific files.

### `abacus-proposal.md`

- Makes `docs/ai-guidance/` the conceptual source of truth.
- Uses `AGENTS.md` as an enumerated reading list, but also duplicates a summary of safety-critical tenets inline.
- Proposes `.github/copilot-instructions.md` as a symlink to `AGENTS.md`, with a stub fallback where symlinks are undesirable.
- Assumes Abacus discovers `AGENTS.md`; Tauren's configuration remains unverified.

### `pi-proposal.md`

- Makes root `AGENTS.md` the canonical guidance file rather than `docs/ai-guidance/`.
- Places the full guidance, safety tenets, verification gates, and workflow in `AGENTS.md`.
- Uses `.github/copilot-instructions.md` as a shim containing selected invariants plus a pointer to `AGENTS.md`.
- Relies on Pi's native discovery of `AGENTS.md`; proposes CI drift checks and optional static enforcement.

## 2. Compatibility Matrix

| Proposal A | Proposal B | Result | Reason |
|---|---|---|---|
| Copilot | Abacus | Conflicting / overlapping | Copilot makes `docs/ai-guidance/` authoritative; Abacus makes `AGENTS.md` partly authoritative and duplicates policy. Both claim control over `AGENTS.md` and `.github/copilot-instructions.md`. |
| Copilot | Pi | Conflicting | Copilot requires `AGENTS.md` to remain a thin adapter. Pi requires it to contain the canonical full guidance. |
| Abacus | Pi | Overlapping with compatible mechanics | Both favor `AGENTS.md` as the main loaded file, inline safety guidance, and a Copilot shim. Pi adds CI checks and verification gates, but its canonical-file location still conflicts with the requested `docs/ai-guidance/` ownership. |

File ownership conflicts are the central issue. The Copilot proposal owns both adapter files as pointers into `docs/ai-guidance/`; Abacus and Pi instead make `.github/copilot-instructions.md` derive from `AGENTS.md`. Pi also claims additional ownership of `scripts/check-agent-guidance.sh`, CI changes, and optional path-scoped Copilot instructions.

## 3. Criteria Evaluation

| Proposal | Single source of truth | Low maintenance | Tool-native compliance | Portability | Transparency |
|---|---|---|---|---|---|
| Copilot | Strong. Policy remains in `docs/ai-guidance/`; adapters contain pointers only. | Strong. Guidance changes are visible immediately when agents follow the links. | Good, assuming agents follow Markdown links. It respects `AGENTS.md` and `.github/copilot-instructions.md` locations. | Strong. Plain Markdown avoids symlink and platform issues. | Strong. The adapters clearly identify the canonical files. |
| Abacus | Weak to moderate. Inline safety-tenet duplication creates a second policy location. | Moderate. Duplicated bullets require manual synchronization. | Good for Copilot and Abacus. Tauren support is explicitly uncertain. | Weak to moderate with symlinks; better with the stub fallback. | Moderate. The pointer is clear, but mirrored content makes authority ambiguous. |
| Pi | Moderate. It has one canonical file, but that file is outside `docs/ai-guidance/`. | Moderate to strong. A single `AGENTS.md` reduces drift, and CI checks help, but the desired canonical directory is not authoritative. | Strong for Pi based on its documented `AGENTS.md` discovery. Good for Copilot with a shim. Abacus remains unspecified. | Strong for plain files; symlink recommendations reduce portability. | Strong. The canonical-versus-shim roles are explicit, especially with drift checks. |

The strongest combination is the Copilot proposal's directory ownership with Pi's discovery verification and CI checks. The main risk to remove is duplicated inline policy.

## 4. Recommended Unified Approach

Keep `docs/ai-guidance/` as the only authored source. Use plain Markdown pointer stubs rather than symlinks or custom include syntax. Markdown include/import directives are not reliably supported by Copilot, Abacus, or Pi, and symlinks are less portable on Windows and some checkout or CI configurations.

### Concrete file tree

```text
repo/
├── docs/
│   └── ai-guidance/
│       ├── README.md
│       ├── PROJECT_GUIDANCE.md
│       ├── GOALS.md
│       ├── TENETS.md
│       ├── POLICIES.md
│       ├── ARCHITECTURE_RULES.md
│       ├── DEVELOPMENT_WORKFLOW.md
│       └── TASK_WORKFLOW.md
├── AGENTS.md
├── .github/
│   └── copilot-instructions.md
└── scripts/
    └── check-agent-guidance.sh
```

### `AGENTS.md`

Use a short, committed Markdown stub:

```markdown
# Shared Project Guidance

Before planning, answering architecture questions, or modifying this repository,
read:

- [AI guidance index](docs/ai-guidance/README.md)
- [Project guidance](docs/ai-guidance/PROJECT_GUIDANCE.md)

Those files are the canonical source for project goals, safety policies,
architecture rules, development workflow, and task traceability. Do not
duplicate or replace their guidance in this file.
```

This preserves Pi's native `AGENTS.md` discovery without moving canonical content out of `docs/ai-guidance/`.

### `.github/copilot-instructions.md`

Use the same pointer-only mechanism with paths relative to `.github/`:

```markdown
# Shared Project Guidance

Read the canonical project guidance before planning or editing:

- [AI guidance index](../docs/ai-guidance/README.md)
- [Project guidance](../docs/ai-guidance/PROJECT_GUIDANCE.md)

The files under `docs/ai-guidance/` are authoritative. This file is only a
discovery adapter and must not contain a second copy of the policy.
```

### Pi configuration

Do not add a second repository-local Pi guidance file. Pi's existing context-file discovery loads root `AGENTS.md`, which points to the canonical guidance.

If the Tauren launcher has an explicit context or system-prompt setting, configure it to load either the repository's `AGENTS.md` or, where direct file loading is supported, `docs/ai-guidance/README.md`. This is a minimal launcher configuration change, not a new authored guidance copy.

### Abacus configuration

Configure Abacus's repository-instruction mechanism to load root `AGENTS.md`. If Abacus has no such mechanism, provide a one-time user-level instruction to read `AGENTS.md` before repository work. No Abacus-specific copy of the guidance should be created.

### Propagation and validation

- Edit guidance only under `docs/ai-guidance/`.
- No adapter regeneration is required because the adapters contain stable paths.
- Agents receive current guidance when they follow the pointers.
- Add `scripts/check-agent-guidance.sh` to CI to verify that both adapter files exist, reference the canonical entry points, remain below a small line limit, and contain resolving Markdown links.
- Do not hash or synchronize duplicated guidance because the unified design has no duplicated guidance to synchronize.
- Keep the existing tests, sanitizer jobs, and other CI gates as the actual enforcement mechanism for safety properties; agent guidance is advisory context.

### Best elements to retain

- From the Copilot proposal: canonical ownership in `docs/ai-guidance/`, pointer-only adapters, and concise focused documents.
- From the Abacus proposal: explicit reading lists, clear discovery instructions, and the warning that linked documents may not be opened automatically.
- From the Pi proposal: Pi-specific discovery verification, CI validation of adapter drift, and the distinction between advisory prose and enforceable build/test gates.

### Elements to drop

- Drop inline safety-tenet duplication from `AGENTS.md` and Copilot instructions.
- Drop symlink dependence because of Windows and checkout portability.
- Drop custom include/import syntax because tool support is not established.
- Drop the proposal that makes `AGENTS.md` the canonical content file; it conflicts with the required `docs/ai-guidance/` source of truth.
- Drop assumptions that a Markdown link guarantees model ingestion. Make required reads explicit, but keep CI and tests as the enforcement boundary.

## 5. Open Questions

- What exact configuration mechanism does the Abacus integration use for repository instructions?
- Does the Tauren launcher always enable Pi context-file loading, or can it start with `--no-context-files`?
- Should Pi be configured to load `README.md` directly in addition to `AGENTS.md`, or is the `AGENTS.md` pointer sufficient?
- Which existing CI job should run `scripts/check-agent-guidance.sh`?
- Should agents read all focused guidance files every session, or only `PROJECT_GUIDANCE.md` plus task-relevant files?

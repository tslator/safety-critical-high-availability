# T-0007: Pin the Primary Build Compiler (Baseline Evidence Prerequisite)

- Status: Ready
- Owner: Unassigned
- Priority: High
- Depends on: None
- Phase: Phase 0 (tooling)
- Phase label: Task 0.1/0.3 (compiler strategy)
- Related decision: [DEC-0008](../decisions/0008-clang-supplementary-verification.md)

## Scope

Make the primary production compiler selection explicit and record it, so that
native/container/CI results are attributable baseline evidence. Today the
Dockerfile installs `build-essential` (GNU 12.2.0) while the intent is GCC 13.

## Deliverables

- Explicit primary compiler in the Dockerfile builder stage (e.g. pinned
  `g++-13`/`gcc-13`, or the Debian-supported default with a note).
- Recorded exact compiler package name, version, and source in the Dockerfile
  comment / README and `--version` output.
- Host, container, and CI report the same intended compiler, or the divergence is
  explicitly documented.

## Acceptance Criteria

- `docker build` and `--version` show an explicit, recorded primary compiler.
- Existing CI (native-gtest/catch2, sanitizers, docker-build/compose) stays green.

## Validation

Record output in [`NOTES.md`](../../NOTES.md); referenced by T-0008's evidence.

## Completion Notes

Prerequisite for treating Clang-vs-primary comparisons as baseline evidence
(DEC-0008 #7).

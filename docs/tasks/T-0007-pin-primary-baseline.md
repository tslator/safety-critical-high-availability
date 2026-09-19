# T-0007: Pin the Primary Build Compiler (Baseline Evidence Prerequisite)

- Status: Complete
- Owner: AI agent (opencode)
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

Recorded in [`NOTES.md`](../../NOTES.md), section "T-0007"; referenced by
T-0008's evidence.

## Completion Notes

Prerequisite for treating Clang-vs-primary comparisons as baseline evidence
(DEC-0008 #7).

Resolved 2026-09-19 via the "Debian-supported default with a note" option:
Debian bookworm and `bookworm-backports` ship no `gcc-13`/`g++-13` (verified
on the digest-pinned base), and the Phase 0 toolchain policy declines
third-party toolchain repositories, so the Dockerfile now installs the
versioned `g++-12` package explicitly (12.2.0-14+deb12u1, bookworm/main),
selects it with `-DCMAKE_CXX_COMPILER=g++-12`, and logs `g++-12 --version` on
every image build; `--version` reports `Compiler: GNU 12.2.0`. Host
(Ubuntu 13.3.0) and CI (ubuntu-latest, 13.x) diverge intentionally; the
divergence is documented in `docs/DEVELOPMENT.md` ("Compiler baseline") and
CI native/sanitizer jobs record `g++ --version` per run. Local matrix and
container/compose smokes are green. Hosted CI run 35450545373 on commit
db73b6a (2026-09-19): all nine jobs succeeded; the new report steps recorded
CI compiler `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0` (native/sanitizer
jobs) and `Compiler: GNU 12.2.0` (container/compose jobs), matching the
documented baseline.

# Contributing to PNG Analyzer

PNG Analyzer is developed through small, independently verifiable Work Packages. Contributions should expand one observable capability without mixing unrelated cleanup or architecture changes.

## Before Starting

1. Read `AGENTS.md`, `REPOSITORY_LAYOUT.md` and the ADRs relevant to the change.
2. Use an approved Work Package issue, or create one with the repository template before implementation.
3. Confirm dependencies are complete and record allowed paths, forbidden changes, tests, acceptance criteria and stop conditions.
4. Inspect the working tree and preserve changes outside the task.

Architecture changes require a dedicated ADR before feature implementation. This includes new production modules or dependencies, moving ownership between modules, changing public backend contracts, allowing Qt under `libs/`, or changing the package-management policy.

## Code Policy

- Production code uses C++20 and follows existing module naming and formatting.
- Public types and functions use explicit ownership and error semantics. Input-derived arithmetic must be checked.
- Core libraries remain independent of Qt. Applications compose capabilities; they do not contain codec algorithms.
- Do not access libpng or zlib private structures.
- Do not add unpinned dependencies, unattributed fixtures, generated output or local machine paths.
- Keep comments focused on non-obvious invariants and decisions.

Use these naming conventions unless a later accepted style specification says otherwise:

- Namespace names, functions, variables and file names use lowercase `snake_case`; directory names use lowercase `kebab-case` as required by the layout contract.
- Types and concepts use `PascalCase`. CMake targets use lowercase `snake_case` with the `pnga_` prefix, and public aliases use `pnga::<name>`.
- Public headers use descriptive names under `include/pnga/<module>/`; do not create ambiguous `common.h` or `utils.h` headers.
- Source is formatted by the repository formatter once WP-001 introduces it. Avoid formatting unrelated lines before that tool is available.

Expected failures from files, format validation, resource limits and cancellation are returned as the project's structured `Result`/error types once those types exist. Exceptions must not represent normal malformed-input control flow and must not cross C callbacks, thread entry points or executable boundaries. Translate libpng longjmp/error behavior and platform errors at their owning module boundary. Never silently convert corruption or an unsupported capability into success.

The exact formatter, warning policy and unified build commands will be introduced by WP-001. Until then, a documentation-only contribution must at least verify links, unique ADR identifiers, Markdown structure and changed-path scope.

## Tests And Evidence

Each behavior change needs a test capable of exposing an incorrect implementation. Parser and decoder changes require normal, boundary and malformed cases; decoder changes also require golden or independent differential evidence where applicable.

A pull request must report:

- Work Package identifier and scope.
- Changed files and why they changed.
- Tests added or updated.
- Exact verification commands and concise results.
- Acceptance criteria status.
- Known limitations and follow-up work.

Do not disable, weaken or delete a failing test to make a pull request pass. If acceptance cannot be met within the approved scope, report `BLOCKED` or `FAIL` with evidence.

For the current mainline, the minimum verification set is:

```text
cmake --build --preset dev
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
cmake --build --preset asan
QT_QPA_PLATFORM=offscreen ctest --preset asan --output-on-failure
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
```

Work Packages that touch the related gates should also run the fixed fuzz,
performance-threshold and package-smoke commands documented in `README.md`.

## Third-Party Material

Third-party dependencies and source are governed by ADR-0001 and the project bootstrap specification. Any copied or derived source requires its exact upstream URL, commit, license, checksums and modification record. Test images require equivalent corpus manifest entries. Material with unknown provenance or redistribution terms is not accepted.

## Review Expectations

Reviewers prioritize correctness, bounds safety, ownership, deterministic output, cancellation, dependency direction and test quality. A passing build does not override an architecture boundary or missing provenance.

## Bug reports

Use the **Bug report** issue form for reproducible non-security defects. Include
the Work Package/revision, preset and platform, exact command or GUI steps,
expected versus observed behavior, the smallest safe input and relevant JSON
or sanitizer output. Remove secrets, user data and absolute local paths. Use
the private vulnerability channel in `SECURITY.md` instead of a public issue
for suspected security impact.

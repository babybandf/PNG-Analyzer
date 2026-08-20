# PNG Analyzer Agent Contract

This file defines mandatory rules for coding agents and contributors. Read it together with `REPOSITORY_LAYOUT.md` before changing repository structure or production code.

## Authority

1. Accepted ADRs define architecture decisions.
2. `REPOSITORY_LAYOUT.md` defines canonical paths, module ownership and dependency direction.
3. An approved Work Package defines the narrower scope of an individual task.
4. When these sources conflict, stop and report `BLOCKED`; do not invent a local exception.

## Architecture Boundaries

- Production code uses C++20, CMake and Qt 6 for the desktop UI.
- Everything under `libs/` is Qt-free. Qt code belongs under `ui/qt/`, the GUI application, GUI tests or packaging helpers.
- GUI code must not parse PNG data, implement Inflate, or access zlib/libpng private structures. It consumes immutable analysis models through the application layer.
- libpng is the Reference Backend. It is not the Trace Backend and may only be linked by `libs/backend-libpng` and explicit oracle tests.
- Decoder code must use public zlib/libpng APIs. Third-party source may be modified only when the active Work Package explicitly authorizes it.
- Multiple IDAT payloads form a virtual logical stream. Never concatenate the complete payload into a new large buffer.
- Static PNG is the first delivery target, but persistent domain models must remain compatible with APNG frames.
- Fast indexing and on-demand Deep Trace are separate capabilities. Do not generate or retain a complete token trace in the default path.

## Safety And Correctness

- Use checked arithmetic for every file offset, length, span end, dimension product, row byte count and allocation size derived from input.
- Treat PNG files and analysis caches as untrusted input. Reject unsafe allocation or work requests without discarding already validated structure.
- Define ownership and lifetime for every borrowed byte view. A view must not outlive its backing mapping or source.
- Do not perform file reads or decoding on the UI thread.
- Cancelable work must check document generation before publishing results so stale jobs cannot overwrite a newer document or selection.
- Preserve deterministic test, JSON and golden output independent of locale, clock and test order.
- New parser or decoder behavior requires focused boundary/error tests and, where applicable, golden or libpng differential tests.
- Never remove, skip, weaken or rewrite a failing test solely to make verification pass.

## Work Package Discipline

Before editing:

1. Read this file, the relevant ADRs, `REPOSITORY_LAYOUT.md` and the active Work Package.
2. Inspect the working tree and preserve changes not owned by the task.
3. Confirm all dependencies are complete and identify allowed and forbidden paths.
4. State the goal, non-goals, input/output invariants and the cheapest discriminating test.

During implementation:

- Make the smallest change that satisfies the current Work Package.
- Do not refactor adjacent modules or change public APIs opportunistically.
- Do not add dependencies, top-level directories or reverse dependency edges without an approved architecture task.
- Do not modify generated files or third-party sources unless explicitly authorized.
- Public API changes require an ADR or an update to the owning interface documentation as specified by the Work Package.

Before reporting completion:

1. Run the Work Package verification commands and relevant sanitizer/differential levels.
2. Review changed paths, arithmetic boundaries, ownership/copies, cancellation and deterministic output.
3. Report exactly one status: `PASS`, `BLOCKED` or `FAIL`, with commands and concise evidence.

## Repository And Dependency Policy

- Follow the module paths and naming rules in `REPOSITORY_LAYOUT.md`; do not create catch-all `common`, `utils`, `misc`, `new`, `old` or versioned source directories.
- Dependencies must be pinned and auditable. Qt comes from the official installer; libpng, zlib and Catch2 come from the approved vcpkg manifest workflow.
- Do not use floating branches, system libpng/zlib fallback, a second package manager or unrecorded downloaded binaries.
- Every vendored or derived source requires exact upstream URL, commit, hashes, license and modification record under `third_party/`.
- Every external test fixture requires source, license, SHA-256, expected classification and linked test metadata in the corpus manifest.
- Do not copy substantial specification text into the repository; store concise summaries and stable section links.

## Stop Conditions

Return `BLOCKED` when the task requires any of the following without prior approval:

- Changing an accepted ADR or the repository layout contract.
- Writing outside the Work Package's allowed paths.
- Adding a dependency, package manager, top-level directory or forbidden dependency edge.
- Using Qt under `libs/`, libpng private structures, or libpng outside its approved boundary.
- Copying third-party code or test assets without exact provenance and license.
- Claiming support for a backend capability that cannot produce the required stage or evidence.
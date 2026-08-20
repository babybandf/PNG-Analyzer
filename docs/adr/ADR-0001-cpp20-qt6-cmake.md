# ADR-0001: Use C++20, Qt 6 And CMake

- Status: Accepted
- Date: 2026-08-20

## Context

The analyzer needs native performance, bounded memory access, cross-platform desktop UI, docking/model-view support and one reproducible build description for applications, libraries, tests and CI.

## Decision

Use C++20 for production code, Qt 6 Widgets for the desktop UI and CMake with presets for builds. Qt is installed through the official Qt distribution. Approved C/C++ dependencies are resolved through a pinned vcpkg manifest. The initial compatibility floor is Qt 6.8 and CMake 3.28; CI may pin a newer tested Qt release.

## Consequences

- Codec and analysis libraries can use modern C++ ownership and type-safety features.
- Qt provides the desktop shell but is isolated from reusable libraries by ADR-0003.
- Build and dependency versions must be pinned and reproducible across supported platforms.
- Adding another GUI framework, build system or package manager requires a superseding ADR.

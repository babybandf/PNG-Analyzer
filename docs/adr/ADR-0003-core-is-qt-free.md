# ADR-0003: Keep Core Libraries Independent Of Qt

- Status: Accepted
- Date: 2026-08-20

## Context

PNG parsing, reconstruction, validation and tracing must be reusable by the GUI, CLI, tests and possible future bindings. Qt types in domain interfaces would couple ownership, threading and deployment to one frontend.

## Decision

All targets under `libs/` are Qt-free. They use the C++ standard library and explicitly approved codec or platform dependencies. Qt code is restricted to `ui/qt/`, the GUI application, GUI tests and packaging helpers. GUI components consume backend-neutral immutable models and do not parse or decode PNG data.

## Consequences

- CLI and automated tests reuse the same production analysis implementation.
- Public library APIs cannot expose Qt containers, strings, object ownership or signals.
- Qt adapters translate between domain types and UI models at the UI boundary.
- A request to link Qt under `libs/` is an architecture change and must not be solved inside a feature Work Package.

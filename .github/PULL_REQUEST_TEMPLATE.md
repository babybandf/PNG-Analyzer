# Work Package

- ID:
- Status: PASS | BLOCKED | FAIL
- Dependencies confirmed:
- Goal:
- Non-goals:

## Scope

- Allowed paths touched:
- Architecture or public API changes: none / linked ADR
- Unrelated working-tree changes preserved:

## Changes

- File or area: reason

## Tests Added

- Test: behavior proved

## Verification

| Command | Exit code | Result |
|---|---:|---|
|  |  |  |

## Acceptance Criteria

- [ ] Each Work Package criterion is linked to evidence above.
- [ ] Required normal, boundary and malformed cases pass.
- [ ] Required differential, sanitizer, fuzz, performance or GUI levels pass, or are explicitly out of scope.

## Self-review

- [ ] Input-derived arithmetic is checked.
- [ ] Borrowed views and large object ownership/copy behavior are explicit.
- [ ] No full IDAT concatenation or unintended whole-file copy was introduced.
- [ ] No file access or decoding was added to the UI thread.
- [ ] Cancellation and document generation prevent stale publication where applicable.
- [ ] Output and golden data are deterministic.
- [ ] No third-party source or corpus file lacks provenance, license and hashes.
- [ ] No test was disabled, weakened or deleted to hide a failure.

## Limitations And Follow-up

- Known limitations:
- Recommended next Work Package:
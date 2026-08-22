# Security Policy

## Supported Versions

PNG Analyzer is pre-release software. Security fixes are applied to the default branch; no released version currently receives separate maintenance.

## Reporting A Vulnerability

Do not open a public issue for a suspected vulnerability. Use the repository's private vulnerability reporting feature under **Security > Advisories > Report a vulnerability**. If private reporting is unavailable, contact a repository maintainer privately through the contact method listed on the maintainer's GitHub profile.

Include, when possible:

- Affected revision and platform.
- Minimal reproduction steps or a minimized PNG file.
- Expected and observed behavior.
- Crash report or sanitizer output with secrets and local paths removed.
- Potential impact and whether the issue is already public.

Do not include confidential user images or credentials. A synthetic or minimized reproducer is preferred.

Maintainers will acknowledge a complete report, investigate impact, coordinate a fix and agree on disclosure timing with the reporter. Exact response deadlines are not promised while the project remains pre-release.

## Security Baseline

PNG files, caches, plugins and imported reports are untrusted input. Project code must enforce checked arithmetic, bounded allocation and decode budgets, stable ownership for mapped views, cancellation and stale-result suppression. Parser and decoder changes require malformed-input tests. High-risk paths are expected to pass the fixed WP-603A/WP-603B fuzz smoke and the WP-603C ASan/UBSan replay gate; coverage-guided fuzzing remains a later release gate.

Dependencies must be pinned and auditable. Security upgrades are isolated from feature changes and must update version locks, notices and relevant differential tests.

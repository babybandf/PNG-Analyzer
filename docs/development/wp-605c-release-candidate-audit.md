# WP-605C — Release Candidate Audit

Status: **implemented as a reproducible audit runner** (2026-08-23)

Run the complete audit with:

```text
python3 scripts/run_release_candidate_audit.py
```

The runner checks, in a fixed order:

1. repository layout and pinned dependency audits;
2. required license, metadata and user/developer/Trace documentation plus
   local Markdown links;
3. dev and ASan/UBSan builds and full CTest suites;
4. the WP-604B performance threshold gate;
5. the WP-605A portable package smoke; and
6. release CLI version consistency with `vcpkg.json`.

It writes `build/release/rc-audit-v1.json` with no timestamp or locale data.
The report contains the host, fixed check ids, PASS/FAIL status and the
recommended tag `v0.1.0-rc1`. The runner does not create, sign or push a Git
tag; those actions require an explicit release decision after review.

This audit is evidence for the current macOS runner and portable archive
boundary. It does not claim coverage-guided fuzz completeness, native DMG/
MSIX/AppImage/Flatpak installers, Qt framework deployment, Windows/Linux
native GUI certification or an external conformance corpus.

# Third-Party Notices

PNG Analyzer is distributed under its own license (see `LICENSE`). This file
lists third-party components that appear in supported builds, their licenses
and how they are consumed. Licensing classification follows the project
bootstrap spec §11 and does not substitute for legal advice.

## Runtime / build dependencies

| Component | Version (pinned) | License | Obtained via | In product |
|---|---|---|---|---|
| Qt 6 | CI: 6.11.2, floor: 6.8 | LGPL-3.0 / GPL-2.0 / commercial | Qt Online Installer | yes, dynamic links |
| libpng | 1.6.58 | libpng license | vcpkg manifest | yes |
| zlib | 1.3.2 | zlib license | vcpkg manifest | yes |
| Catch2 | 3.11.0 | BSL-1.0 | vcpkg manifest (dev/CI only) | no |

- The vcpkg registry is pinned by `vcpkg.json` → `builtin-baseline` and
  cross-checked by `cmake/dependencies.lock.json`. Upgrades follow the
  dependency-upgrade workflow in the bootstrap spec §10.
- Reference backend may link libpng only under the boundary in
  `docs/architecture/REPOSITORY_LAYOUT.md` §8.2.

## Evaluation-only references (not yet consumed)

| Component | License | Status |
|---|---|---|
| zlib `contrib/puff` | zlib license | evaluation-only until an authorized WP adds it to `third_party/` |
| zlib `examples/zran` | zlib license | evaluation-only until an authorized WP adds it to `third_party/` |

Each of these, when authorized, requires a `third_party/sources.lock.yaml`
provenance record and a license notice retained with any derived source.
# third_party/ policy

Everything under this directory is **explicitly approved vendored or reference
source**. It is NOT a place for installed packages.

- libpng, zlib and Catch2 arrive through vcpkg manifest mode and are never
  copied here. vcpkg owns its download/build under the git-ignored `.deps/`
  and `vcpkg_installed/` trees.
- A derived implementation may live in `libs/`, but its pristine upstream
  files, license, exact commit and hashes must live under `third_party/<source>/`
  and be recorded in `third_party/sources.lock.yaml` (see
  `docs/architecture/REPOSITORY_LAYOUT.md` §8.5 and §14).
- zlib `contrib/puff` and `examples/zran` are evaluation-only references until
  a Work Package explicitly authorizes reuse; when authorized they are added
  here with a `sources.lock.yaml` entry.
- `scripts/verify_dependencies.py` fails if a file or directory here has no
  `sources.lock.yaml` entry.
# tests/corpus/

Contains small, committed test images and their audit metadata. External
fixtures and generated files never live here without a manifest record.

- Big or third-party fixtures (PngSuite, libpng testpngs) are downloaded by
  `scripts/fetch_corpus.py` (a WP-00A follow-up) into the build cache, keyed by
  SHA-256; they are not committed.
- Every committed or downloaded external fixture needs an entry in
  `tests/corpus/manifest.yaml` with source, license, SHA-256, expected
  classification and linked tests, per the bootstrap spec §5 and
  `docs/architecture/REPOSITORY_LAYOUT.md` §11.
- WP-00A ships the manifest schema only; the first fixtures arrive with WP-100.
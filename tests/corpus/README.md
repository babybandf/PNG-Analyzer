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

## WP-607C: generated and external kinds

Manifest records are kind-aware. Every record carries `kind: generated` or
`kind: external`.

- `kind: external` keeps the full provenance contract above (source URL,
  upstream version/commit, SHA-256, reviewed license).
- `kind: generated` describes files produced by `pnga_generate_wp607c_corpus`
  into the build tree only (`valid/` or `malformed/` under
  `${CMAKE_BINARY_DIR}/tests/corpus/wp-607c/`). Generated records never point
  at source-tree files and must not carry `source_url`, `license` or upstream
  fields.

`scripts/verify_wp607c_manifest.py` is the deep validator: schema, generator
mapping, catalog equality, 64-hex hashes, linked CTest names, path traversal
and the aggregate corpus revision (pinned to `manifest.yaml`,
`controlled_fixture.h`, `controlled_fixture.cpp` and
`generate_controlled_corpus.cpp`). No generated PNG is ever committed here.
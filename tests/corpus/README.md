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

## WP-607C operator guidance

- Generate the corpus (CTest does this automatically through the
  `wp607c-generated-corpus` fixture):

  ```sh
  cmake --build --preset dev --target pnga_generate_wp607c_corpus
  ./build/dev/tests/corpus/pnga_generate_wp607c_corpus \
    --output build/dev/tests/corpus/wp-607c
  ```

  Output lives only under the build tree. The generator refuses destinations
  resolving inside this source directory and replaces existing output
  atomically.

- Validate the manifest contract against a generated catalog:

  ```sh
  python3 scripts/verify_wp607c_manifest.py \
    --manifest tests/corpus/manifest.yaml \
    --catalog build/dev/tests/corpus/wp-607c/index.json \
    --build-dir build/dev
  ```

- `expected_sha256` values are blessed only through the proven
  double-generation flow (`--comparison-catalog` is mandatory, the two
  catalogs must be equal, and only the hash scalars are rewritten):

  ```sh
  python3 scripts/verify_wp607c_manifest.py \
    --manifest tests/corpus/manifest.yaml \
    --catalog build/wp607c-double-generation/run-a/index.json \
    --comparison-catalog build/wp607c-double-generation/run-b/index.json \
    --build-dir build/dev --refresh-generated-hashes
  ```

  Reconfigure afterwards: the manifest bytes are one of the four hashed
  inputs of the aggregate corpus revision.

- One-shot operator gate (build, double generation, validation, labeled
  CTest, evidence record under `build/evidence/`):

  ```sh
  python3 scripts/run_wp607c_corpus_gate.py --preset dev --jobs 4
  ```

  `--dry-run` prints the pinned command sequence; `--self-test` checks the
  planner and the deterministic evidence writer.
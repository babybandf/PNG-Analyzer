# SDD ledger — defect: compression tables column resize + truncation

Worktree: .worktrees/fix-compression-column-resize (branch fix-compression-column-resize, base ef33fff)
Authorized by product owner 2026-09-05 ("按建议修复").

## Diagnosis (controller, 2026-09-05)
- Root cause of no-resize: WP-5U12C/D/E set QHeaderView::ResizeToContents on the three sub-page tables (block_inspector.cpp:123-139 data cols, huffman_inspector.cpp:135 all, decode_trace_inspector.cpp:139 all + Event Stretch) — Qt disables user resizing in that mode. Chunk list unaffected (default Interactive).
- Frozen tests only assert Event column = Stretch (product gate :728, responsive :396); other columns' mode is not frozen → Interactive switch is contract-compatible.
- Constraint: 22 locked baselines (WP-5U12F) captured with content-derived widths → fix must preserve identical initial widths (refit via resizeColumnsToContents() on publish = same Qt computation) — baseline compare is a mandatory gate.
- Second symptom "显示不全": mechanism TBD — candidates: Blocks stretchLastSection(true) squeezes last column at narrow widths (frozen-normative, baselines); ResizeToContents virtual-model hint incompleteness. Implementer must reproduce concretely and either fix in-scope or record precisely.

## Dispatch: single implementer, TDD
- Failing tests: data columns assert Interactive (RED) on all three tables.
- Fix: Interactive + resizeColumnsToContents() refit per publish; re-apply fixed marker widths after refit; Event stays Stretch; investigate truncation mechanism, fix if in-scope (no baseline breakage).
- Gates: 57/57 offscreen; responsive + product gate; **22/22 baseline compare** mandatory.

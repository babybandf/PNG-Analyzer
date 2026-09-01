# WP-5U12 A–F 执行计划审计报告

- 审计者：GLM 5.3 Flash（opencode agent）
- 审计日期：2026-09-01
- 审计基线：commit `bf62d1e`（fix(theme): render numeric base button reliably on hover），工作区含 7 个未提交计划文件
- 被审计文件：
  - `docs/superpowers/plans/2026-09-01-wp-5u12-compression-inspector-master.md`
  - `docs/superpowers/plans/2026-09-01-wp-5u12a-offset-fast-index.md`
  - `docs/superpowers/plans/2026-09-01-wp-5u12b-selection-navigation.md`
  - `docs/superpowers/plans/2026-09-01-wp-5u12c-blocks-page.md`
  - `docs/superpowers/plans/2026-09-01-wp-5u12d-huffman-page.md`
  - `docs/superpowers/plans/2026-09-01-wp-5u12e-decode-trace-page.md`
  - `docs/superpowers/plans/2026-09-01-wp-5u12f-product-gate.md`
- 交叉核对规格：`docs/development/wp-5u12-compression-inspector-completion.md`、`wp-5u12-compression-inspector-flow-ui.md`（§5–20）
- 交叉核对约束：AGENTS.md、REPOSITORY_LAYOUT.md、已接受的 ADR、WP-5U15 完成后的门面现状

## 一、总体结论

**计划质量高，可执行**。master 的串行 handoff（A→B→C→D→E→F）、每子计划的 TDD 红灯先行、Mandatory Task Exit Gate、`git show --name-status` 边界审计、`BLOCKED` 升级路径齐备；未发现架构越界（无 libs 反向依赖、无 Qt 进 libs、无 IDAT 拼接、无未预算 trace）。发现 **2 个跨计划契约矛盾（执行前必须裁决）**、4 个轻微问题、1 个前置依赖提醒。

## 二、已核实成立的关键前提（逐项对代码验证）

| # | 计划假设 | 核实结果 |
|---|---|---|
| 1 | A：`Offset` 存在隐式 `operator std::uint64_t()`，待移除（TDD 红灯成立） | ✓ `libs/trace-model/include/pnga/trace-model/offset_range.h:33` |
| 2 | A：`kIndexCacheSchemaVersion = 1` 待 bump 到 2、v1 缓存拒绝 | ✓ `libs/deflate-index/include/pnga/deflate-index/index_cache.h:18` |
| 3 | A：`VirtualIDATStream::segment_count()/segment(i)`、`IByteSource::read(offset,…)` 存在 | ✓ `libs/png-format/include/pnga/png-format/virtual_idat_stream.h:41-43`、`libs/io/include/pnga/io/byte_source.h:52` |
| 4 | A/C：`FastCompressionBlockRow` 存在且待扩展（stored_length/event_count/scanline optionals） | ✓ `libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h:88`，现字段仅 block_index/type/last/input_range/output_range/physical_spans |
| 5 | C：BlockInspector 现用 QTableWidget/QTableWidgetItem，待换 QAbstractTableModel | ✓ `ui/qt/src/block_inspector.cpp:13-14` |
| 6 | C：BlockInspector 有 legacy `showInHexRequested(quint64 file_offset, quint64 length)` 可移除 | ✓ `ui/qt/include/pnga/ui/qt/block_inspector.h:44`（另有 spans 版 :45） |
| 7 | D：`libs/deflate-trace` 的 `token_decoder.h`/`TokenEvent` 存在 | ✓ `token_decoder.h:73,95` |
| 8 | D：`HuffmanTableMode` 存在 | ✓ `libs/analysis-engine/include/pnga/analysis-engine/huffman_inspector.h:27` |
| 9 | E：`DecodeTracePath`、analysis-engine 版 `decode_trace_inspector.h` 存在 | ✓ `decode_trace_inspector.h:26` |
| 10 | B/E：`FileByteRange/ZlibByteRange/ZlibBitRange/DeflateBitRange/InflatedByteRange` 等 typed domains 存在 | ✓ trace-model |
| 11 | B：`tests/unit/trace-model/CMakeLists.txt` 存在 | ✓（含 offset_range_test、selection_test） |
| 12 | B：目标 `pnga_gui_selection_navigation_controller_tests` 存在（WP-5U15 产物） | ✓ |
| 13 | E：`tests/unit/analysis-engine/trace_query_test.cpp`、`trace_orchestrator_test.cpp` 存在 | ✓ |
| 14 | F：`pnga_performance_runner`、`tests/performance/thresholds-v1.json`、`README.md` 存在 | ✓ `tests/performance/CMakeLists.txt:5` |
| 15 | F：`run_gui_gate.py` 支持 `--preset/--jobs/--output` | ✓ `scripts/run_gui_gate.py:36-38` |
| 16 | F：22 个 baseline 清单计数正确（9 light 尺寸 + 6 dark + 7 状态/细节） | ✓ 逐名清点 |
| 17 | master：WP-5U15 后门面状态（widgets_/workspace_/session_/selection_/trace_）可注入共享 store | ✓ 与 `main_window.h:66-73` 一致 |
| 18 | master：asan preset、`run_sanitizer_fuzz_gate.py`、`run_performance_corpus.py --enforce-thresholds` 存在 | ✓ |

## 三、发现的问题

### P1【契约矛盾·执行前必须裁决】`deflate_data_begin` 的单位

- A 计划 Required Interfaces：`FastCompressionStreamSummary.deflate_data_begin` 类型为 `pnga::trace_model::ZlibBitOffset`（**位**）。
- E 计划 Task 1 Step 3："convert each token's DeflateBitRange to zlib bits by checked addition of `deflate_data_begin * 8`"（**×8 暗示字节**）。
- 现状：`libs/analysis-engine/include/pnga/analysis-engine/trace_query.h:111` 为纯 `std::uint64_t deflate_data_begin`，且 `apps/png-analyzer-gui/src/trace_controller.cpp` 的 `byte_range_for_bits(bit_begin, bit_end, trace_deflate_data_begin_)` 将其作为**字节** origin（先 bit/8 再相加）。
- 影响：A handoff 一旦固化类型，E 执行时二者必撞一个。两份计划文本只能对一个。
- 建议裁决：A 采用 `ZlibByteOffset`（语义=DEFLATE 数据在 zlib 流中的字节起点，与现状一致），E 的 ×8 公式成立；或 A 保持 `ZlibBitOffset{16}` 位语义并修订 E 为"直接相加、禁止 ×8"。**推荐前者（改动面最小、与现有 trace_controller 语义一致）。**

### P2【契约矛盾·执行前必须裁决】`FastCompressionBlockRow::physical_spans` 类型

- C 计划 Required Interfaces：`std::vector<pnga::trace_model::FileByteRange> physical_spans`。
- 现状：`libs/analysis-engine/include/pnga/analysis-engine/block_inspector.h:95` 为 `std::vector<pnga::trace_model::ProvenanceSpan>`（`trace-model/provenance.h:23`，携带 UI 语义字段）。
- 影响：C Task 1 改类型会牵动 ui/qt 现有 `HexHighlightSpan` 构造与 binding 代码；不改则 C 的接口段与实现不符。
- 建议裁决：C 明确"改型并迁移调用方"（推荐，消除 UI 语义侵入 analysis-engine）或"保留 ProvenanceSpan、修正计划文本"。

### P3【前置依赖】F 将立即 BLOCKED——WP-607C 必须先行

- `tests/corpus/manifest.yaml` 存在，但 grep 0 命中 stored/fixed/dynamic/overlap/cross-idat/truncated/reserved/invalid-distance/adler 类别（与 `verify_repository_layout` 的 "no binary fixtures tracked yet" 一致）。
- F Task 1 Step 1 已预设该 BLOCKED 路径（"If manifest.yaml is absent/incomplete, record BLOCKED"），master Task 6 Step 1 亦然——机制正确，但**排期上必须把 WP-607C 排在 F 之前**，否则 F 空转。

### P4【轻微】`rg`（ripgrep）依赖

- master Task 6 Step 1、A Task 1 Step 3 使用 `rg`。本机无 ripgrep（实测 `command not found`）。
- 建议：计划文本改 `grep -rnE` 或在贡献者环境要求中加 ripgrep。

### P5【轻微】E Task 1 的"确认失败"措辞

- `DecodeTraceStep.huffman_symbol` 已存在（`decode_trace_inspector.h:38`）。E 真正新增的是 `TraceTokenSummary.physical_input_spans`（trace_query.h）与 token 级 symbol 投影/序列化。Step 2 预期的编译失败仅对 `TraceTokenSummary` 扩展成立，对 `DecodeTraceStep` 不成立——措辞应明确，避免执行者误判。

### P6【轻微·执行前事项】计划文件本身未纳入版本管理

- 7 个计划文件均为 untracked。master 的 Exit Gate 要求 "after commit, git status --short is empty, except pre-existing user-owned files recorded before task start"——应先以一个 docs commit 收入计划（可附本审计报告），否则 A Task 1 Step 1 的 clean-worktree 检查即报脏。

## 四、未发现的问题（专项排查结论）

- 无 Qt 进 `libs/**`：B 的 store 放 `ui/qt` ✓；B 的值类型放 `trace-model` 且 Qt-free ✓。
- 无 IDAT 拼接：A/C/E 全部经 `VirtualIDATStream` 分段 API ✓。
- 无未预算 trace：C/D/E 的"零 incidental replay"断言 + `PNGA_TRACE_CONTROLLER_TESTING` 计数器（WP-5U15 已有）复用 ✓。
- 预算/策略不变：A Task 4 Step 3、B Task 4 Step 2、E Task 4 均含 4096 tokens/8 MiB/单 worker 的显式审计 ✓。
- legacy 信号清理有明确迁移句（C：BlockInspector 两个 showInHex 信号；E：DecodeTraceInspector `showInHexRequested`）✓。
- WP-5U15 特征化测试（`replacingDocumentCannotPublishTheFirstTraceBundle` 等）依赖的 `BlockInspector::view()` 在 C 重构后保留于 Required Interfaces 之外的 view 语义——C Task 3 已把 `trace_pipeline_integration_test.cpp` 列入修改文件，迁移路径闭环 ✓（执行时 handoff review 需盯该特征化断言不弱化）。

## 五、执行前建议清单

1. 修订 A/E 计划文本，统一 `deflate_data_begin` 单位（推荐 ZlibByteOffset + E 保留 ×8）。
2. 修订 C 计划文本，裁决 `physical_spans` 类型并写明调用方迁移。
3. 修正 P4（rg→grep）与 P5（红灯预期措辞）。
4. 以 docs commit 提交 7 个计划文件 + 本审计报告。
5. 排期确认 WP-607C 先于 F。
6. 每阶段 handoff 审查时，用本报告第二节清单复核"前提仍成立"（代码可能已被前序阶段改变）。

## 六、审计方法说明

- 逐行通读 7 份计划（master 350 行、A 328、B 251、C 227、D 234、E 250、F 270）。
- 对 18 项可机械验证的代码库假设逐一以 grep/ls/cat 核对（第三节#4 的 manifest 类别为否定性验证）。
- 与 WP-5U15 完成后的实际门面/测试目标清单交叉核对（本审计者此前执行过 WP-5U15 全部 8 个 task，commit 6e42970…0320d39）。
- 本审计为静态审查，未执行任何计划步骤、未修改任何生产代码。

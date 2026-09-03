# WP-5U12 Stage A–E 执行完成度审计

- 审计日期：2026-09-03
- 审计对象：`docs/development/wp-5u12-stages-a-e-execution-report.md`（commit `bc1a1e1`）所声称的完成情况，对照分支 `wp-5u12-compression-inspector` 实际仓库状态（base `27d7012`）
- 审计方法（双轨）：
  1. 控制器机械重放：退出门禁命令重放（verify 脚本、构建、CTest、`git diff --check`）、forbidden-path 全量清单比对、关键契约 grep；
  2. 独立审计 agent：对报告 12 组事实声明逐条对照代码、git 历史、测试配置核查（只读，不重跑全量套件，47/47 由控制器同日重放提供）。
- 交叉约束：AGENTS.md（单一状态 PASS/BLOCKED/FAIL）、master 计划 Handoff Rule、五个子计划 Files 清单。

## 一、总体结论

**PASS** —— 12 组声明全部 VERIFIED，零重大差异；3 项报告措辞级勘误（不构成正确性问题），其中 2 项已当场修正回报告。F 阶段的 BLOCKED 停止系 master 计划 Handoff Rule（master.md:373）明文规定。

## 二、审计判定表

| # | 声明组 | 判定 | 关键证据 |
|---|---|---|---|
| 1 | Commit 清单（A–E + fix + 清理波 + housekeeping） | VERIFIED | `git log --oneline 27d7012..HEAD` = 27 commits，扣除报告 commit `bc1a1e1` 即报告所称 26 个；各阶段 SHA 逐一命中；Task 0 `fbd8ac7`（main）恰为 8 份计划文档 |
| 2 | 变更路径纪律（无 third_party/packaging/Compare/Statistics/APNG/解析重建路径） | VERIFIED | 65 个变更文件全部落在 libs/{trace-model,deflate-index,deflate-trace,analysis-engine}、ui/qt、apps/png-analyzer-gui/src、tests、docs、.gitignore；禁词 grep 0 命中；每个路径可归属子计划 Files 清单或裁决授权 |
| 3 | A：typed offset（无隐式转换/`raw_value()`/默认 ctor）、4 条 static_asserts、cache v2 + v1 拒绝、`ZlibByteOffset deflate_data_begin`、legacy 字段已移除、checked make_range + 字节对齐错误路径 | VERIFIED | offset_range.h:28,30；offset_range_test.cpp:20-23（另含 3 条 is_same 超集断言）；index_cache.h:21、index_cache.cpp:444；block_inspector.h:70-75,94；block_inspector.cpp:239-263 |
| 4 | B：`CompressionNavigationTarget`/`CompressionSelectionState`、serial/once 语义文档化、双层回环抑制 | VERIFIED | compression_navigation.h:40,63；compression_selection_store.h:8,36,41,45,47；store `last_applied_serial_` + controller 循环守卫（selection_navigation_controller.cpp:186） |
| 5 | C：QAbstractTableModel、blocks 表无 QTableWidget 实用、legacy `showInHexRequested(quint64,quint64)` 移除、`physical_spans` 维持 ProvenanceSpan | VERIFIED | block_inspector_model.h:34；QTableWidget 仅存于注释（block_inspector.cpp:141）；block_inspector.h:37,111 |
| 6 | D：`count_occurrences` kind 门控、Qt 无位反转逻辑、`huffman_symbol` 双层落位 | VERIFIED | huffman_inspector.cpp:351-353；ui/qt 仅注释性声明；token_decoder.h:73,96、trace_query.h:78 |
| 7 | E：`checked_mul(…,8)` 字节原点、tiling 验证 `covered == logical_length`、`physical_input_spans`、view 恰为 `{scope, steps}`、仅 typed `navigationRequested` 信号、untyped 信号族零代码引用 | VERIFIED | trace_query.cpp:149,186,322；trace_query.h:83；decode_trace_inspector.h:60-63,75-80；apps/ 与 ui/ 下 `showInHexRequested\|showInDeflateRequested\|byte_range_for_bits` 0 命中（仅文档/计划命中） |
| 8 | 预算/策略不变（4096 tokens / 8 MiB / 单 worker） | VERIFIED | trace_controller.cpp:28,31,136 常量原值；diff 中仅出现常量复用行，无常量变更 |
| 9 | 清理波声明（死词汇/死标志/死调用/恒假检查/过期注释） | VERIFIED | `git show 7b3fd4a` 对应 hunk；repo-wide 死词汇 0 命中；`applying_state_` 仅存于 live 的 huffman_inspector；其余 `setDetailsInstruction` 调用点存活 |
| 10 | 过程声明（15 条裁决、阶段报告/handoff、9 个审查包） | VERIFIED | ledger 124 行含全部 15 条裁决（行号逐一核对）；task-A..E + preflight 报告与审查包均在盘 |
| 11 | WP-607C BLOCKED（manifest 空、包未批准、Handoff Rule 强制停止） | VERIFIED | manifest.yaml 末尾字面 `[]`；wp-607 文档 :3 状态行；master.md:373 Handoff Rule 原文 |
| 12 | 报告准确性 meta（commit 数、行数、resize 回退、2049/5000/10000 等数字） | VERIFIED | `ctest -N` = 47；trace_controller_test.cpp:199 已回退 `(980,640)`；`2049 = kMaxVisibleRows(2048)+1`；perf 行数 5000/10000 落位 |

## 三、重大差异

无。全部事实声明（commit、路径、代码工件、门禁证据、阻塞理由）与仓库一致。

## 四、报告勘误（3 项）

1. **"15,270 行 diff"**：系审查包文件行数（含上下文/头），变更行实际 9,963+/1,555−（11,518）。→ **已修正**为双数字表述。
2. **"全分支最终审查"**：审查范围为 `27d7012..81f322f`（25 commits），不含其后清理波 `7b3fd4a`（该波经独立 scoped 复审逐项 ADDRESSED）。→ **已修正**为显式范围 + 清理波复审说明。
3. **`make_deflate_range` "直接形式"**：替换后的第二子句 `end > UINT64_MAX` 对 uint64_t 仍恒假，实质守卫为 `end < begin`。→ **已加注**；该残留列为 F 后清理（与 scoped 复审 out-of-scope 观察一致）。

## 五、控制器本地机械验证记录（2026-09-03 重放）

```
git log --format= --name-only 27d7012..HEAD | sort -u | grep -icE 'third_party|compare|statistic|apng|packaging'  → 0
python3 scripts/verify_repository_layout.py → 0 failure(s), 0 warning(s)
python3 scripts/verify_dependencies.py      → 0 failure(s), 0 warning(s)
cmake --build --preset dev --parallel 4     → exit 0
QT_QPA_PLATFORM=offscreen ctest --preset dev → 100% tests passed out of 47
git diff --check                             → 无输出
git status --short                           → 空
ctest -N（dev）                              → Total Tests: 47
```

## 六、审计状态

**PASS（范围限定：报告一致性）** —— 依 AGENTS.md 单一状态惯例。本审计验证的是"报告声明与仓库一致"，**不构成行为级复审，也不表示实现质量问题已全部关闭**。WP-5U12 完成状态仍为 **未完成/BLOCKED**（Task 6 等待 WP-607C；且存在第七节登记的未决行为类发现）。

（审计后修正：执行报告的两处措辞勘误随本审计同 commit 入库。）

## 七、补记（同日）：审计范围限定与未决行为类发现

外部核对反馈（针对 `e84f80d`）：

1. **确认属实**：`e84f80d` 仅含两份文档（审计文件 + 报告勘误），无任何实现或测试变更；代码 blob 与父提交 `bc1a1e1` 一致。
2. **范围限定（采纳）**：第六节 PASS 仅覆盖"报告一致性"。`e84f80d` 应理解为"文档勘误完成"，**不得**解读为"实现质量问题已处理"。WP-5U12 维持未完成/阻塞状态。
3. **登记三项未决行为类发现**（外部核对指出，本执行过程无记录）：
   - Decode Trace 切页行为；
   - 边界 EOB 行为；
   - Match source provenance 行为。

   这三项在 A–E 五轮阶段审查、全分支最终审查及 ledger 中均无缺陷记录（grep 证据：相关词条仅出现于实施者报告的特性描述与测试断言，非审查发现）。控制器抽查显示相关守卫存在（如 EOB 空 output_range 时 `inflatedTargetFor` 返回 `nullopt` 不发射、`match_source_ranges` 全程携带并序列化、overlap 用 checked 半开区间交集），但抽查不能推翻未见过的复现场景。

   **处置要求**：按 master 纪律，任何修复必须"先有失败回归，后修"。这三项在获得具体复现描述/触发序列并落成失败回归测试之前，保持 OPEN 状态登记于本节；随后进入 fix 循环（实现修复 + 回归证据），全部关闭前 WP-5U12 不得进入 F 的完成判定。

### 三项 OPEN 发现的具体复现序列

（外部核对补充，commit `76bd0d8`；以下步骤均针对 `7b3fd4a` 实现终点撰写，作为各修复的需求溯源，原文保留。）

#### 7.1 Blocks/Huffman drill-down 不切换到 Decode Trace

已有 PNG/GUI 初始化可复用 `tests/gui/trace_pipeline_integration_test.cpp:509-559` 的 `openDecodeTracePublishesBoundedBundle()`：打开固定 1×1 PNG，等待 Compression context 为 ready，取得 `compressionBlocksTable` 并选择第 0 行。

在点击 `Open Decode Trace` 前取得：

```cpp
auto* pages = window.findChild<QTabWidget*>(
    QStringLiteral("compressionInspectorPages"));
QVERIFY(pages != nullptr);
pages->setCurrentIndex(0);
```

点击按钮并等待 bounded trace ready 后增加：

```cpp
QTRY_COMPARE_WITH_TIMEOUT(pages->currentIndex(), 2, 10000);
```

期望：Blocks=0、Huffman=1、Decode Trace=2，点击后切到 Decode Trace。当前触发路径为 `ui/qt/src/block_inspector.cpp:387-397` 发出 `decodeTraceRequested`，`apps/png-analyzer-gui/src/trace_controller.cpp:44-109` 只提交 trace，没有改变 `compressionInspectorPages`。

Huffman occurrence 可复用 `tests/gui/trace_pipeline_integration_test.cpp:740-842`：该测试在第 764 行手动切到 Huffman=1、选择有 occurrence 的 symbol、点击 `huffmanOpenOccurrence`。点击后增加：

```cpp
QCOMPARE(compression->currentIndex(), 2);
```

当前实现会留在 Huffman=1；`HuffmanInspector::openOccurrence()`（`ui/qt/src/huffman_inspector.cpp:568-599`）只向 selection store 发出 typed target，selection controller 只处理 Hex 导航（`apps/png-analyzer-gui/src/selection_navigation_controller.cpp:179-231`）。

#### 7.2 查询边界的最终 EOB 被过滤

复用 `tests/unit/analysis-engine/trace_query_test.cpp:563-610` 的 `fixed_abc_two_idat_png()`。该 fixture 的 `inputs.trace.tokens` 有 4 项，最后一项为 EOB，且其 output range 为 `[3,3)`。

对现有 compose 调用（第 581-583 行）保留查询范围 `[0, inputs.trace.output_bytes)`，将当前断言：

```cpp
REQUIRE(result.tokens.size() == 3);
```

替换/扩展为：

```cpp
REQUIRE(result.tokens.size() == 4);
REQUIRE(result.tokens.back().kind ==
        pnga::deflate_trace::TokenKind::kEndOfBlock);
REQUIRE(result.tokens.back().output_begin == 3);
REQUIRE(result.tokens.back().output_end == 3);
```

当前预期失败：`libs/analysis-engine/src/trace_query.cpp:93-97,336-340` 使用半开区间 overlap；零宽 EOB `[3,3)` 不与查询 `[0,3)` 相交，结果只有 3 个 Literal。规范示例要求最终 Match 后保留 EOB 行（`docs/development/wp-5u12-compression-inspector-flow-ui.md:643-645`）。

#### 7.3 Match Current byte 缺少 source logical offset

复用 `tests/unit/analysis-engine/decode_trace_inspector_test.cpp:139-181` 的 Match 构造方式，构造一个物理一致的正向范围：target `[100,118)`、distance `7`、Current output `104`、root source range `[82,100)`。

最小模型断言为：

```cpp
trace.tokens.push_back(
    match_token(0, 18, 7, 0, 100,
                {TokenOutputRange{82, 100, 0}}));
const auto view =
    build_decode_trace_inspector(trace, std::nullopt, 104);
REQUIRE(view.steps.front().selected_byte_offset_in_event ==
        std::optional<std::uint64_t>{4});
```

随后在 `tests/gui/decode_trace_inspector_test.cpp` 选中该 Match 行，要求详情同时包含：

```text
Current byte … target offset +4 … source logical offset 97
```

其中 `97 = 104 - distance(7)`。当前模型只有 `selected_byte_offset_in_event`（`libs/analysis-engine/include/pnga/analysis-engine/decode_trace_inspector.h:63-70`），UI 只显示 `match offset +4`（`ui/qt/src/decode_trace_inspector.cpp:552-558`）。`match_source_ranges` 的 root token range（如 `[82,100) token 0`）不能替代当前字节按 DEFLATE overlap 逐字节复制得到的 source logical offset。

### 7.1–7.3 处置结果（2026-09-03，CLOSED）

三项发现已按"失败回归先行"纪律修复并关闭：

| 项 | 失败回归（RED 证据） | 修复 commit | 审查结论 |
|---|---|---|---|
| 7.2 边界 EOB 被过滤 | `tests/unit/analysis-engine/trace_query_test.cpp`（4-token 断言，RED 因 `[3,3)` 与 `[0,3)` 半开不相交） | `92239c4`（闭包规则 `query_begin <= pos <= query_end`，仅零宽 EOB 生效） | Approved，无过度包含，兄弟测试被强化 |
| 7.3 Match source logical offset | 模型编译失败（缺字段）+ GUI 第二子串失败 | `59ebffc`（`DecodeTraceStep` 新增当前字节源偏移，checked 减法 + Match/contains-current 门控；UI 追加 "source logical offset %1"） | Approved，逐字落地审计 fixture（+4 / 97） |
| 7.1 drill-down 切页 | GUI 集成断言失败（点击后 currentIndex 停留原页） | `1dc8385`（Blocks 路径在既有 `decodeTraceRequested` 连接内切页；Huffman 路径经 store `navigationRequested` 按 `origin == kHuffman` 过滤切页；均不产生额外 trace 提交） | Approved，零回放计数器语义不变 |

- 退出门禁重放：scripts 0 failure 0 warning；构建 exit 0；CTest 47/47；`git diff --check` 干净；树干净（HEAD 见 git log）。
- 审查裁决：`goBack()`/`goForward()` 导航至 Huffman-origin 目标同样切页——裁定为 drill-down 意图的一致扩展（Back/Forward 恢复完整导航上下文）。
- 缓办新 Minor：边界 EOB 行 `block_index = -1`（归因循环对空区间不命中，后续可按闭端包含归因）；序列化器未输出新字段（保持 `decode-trace-v2` 稳定，视为正确取舍）。
- **三项发现状态：CLOSED。WP-5U12 完成状态不变：未完成，Task 6（F）仍待 WP-607C。**

# WP-5U12 — Compression Inspector 开发计划

> 对应任务包：`wp-5u12-compression-inspector-flow-ui.md`
> Milestone：M5 UI Refinement
> Status：revised after live implementation audit (2026-08-24)
> 实施目标：在不建立第二套 Inflate/DEFLATE 解码路径的前提下，实现 `DEFLATE Blocks`、
> `Huffman`、`Decode Trace` 三个自然联动、结构稳定、可验证的 Compression 页面。

本计划规定实施顺序、阶段门禁和交付节奏。业务语义、页面字段、错误状态、性能和完成
定义以 WP-5U12 任务包为准；视觉实现还必须满足任务包第 20 节的规范性 UI 契约。

## 0. 当前基线与本轮目标

### 0.1 已验证的当前基线

用当前 GUI 打开 `basn2c08.png` 并锁定 `(0, 0)` 后，三个子页均能显示同一 generation
的真实 bounded result：

- Blocks：1 个 Dynamic final Block，范围 `zlib-stream bits [16, 539)`、Inflated
  `[0, 3104)`；
- Huffman：Dynamic Literal/Length 表，当前 UI 显示 292 项；
- Decode Trace：当前 output window 返回 7 个 Literal/Match token；
- 共同上下文能显示 ready、scanline、output byte 和 Block；
- 页面切换不触发 replay，stale generation 防护已存在。

因此本轮不是“从空白页面接线”，而是对已接通的工程视图做数据域纠正、信息架构补全和
可验收的 UI 收口。

### 0.2 必须保留的架构

- Fast Block Index 与 on-demand bounded Deep Trace 分离；
- 不默认生成或保留 whole-file token trace；
- 不在 GUI 中重新解析 DEFLATE、反转 Huffman code 或计算 LZ77；
- 不拼接 IDAT payload，继续通过 `VirtualIDATStream` 映射；
- generation、cancel、budget 和 worker → GUI queued publication 规则不变；
- 页面切换、行选择、resize、DEC/HEX 不提交 trace request。

### 0.3 本轮必须修复的实证差距

| ID | 差距 | 证据位置 | 目标 |
|---|---|---|---|
| G-01 | 无 Lock 时三页只有重复指令，Blocks 不可浏览 | `trace_inspector_binding.cpp` 把 instruction 同时写入 status/mapping；bounded bundle 只含相交块 | Stream summary 常驻；完整 Blocks 来自 Fast Index；指令只出现一次 |
| G-02 | ready 主文案突出 generation/内部计数，无 zlib/IDAT/Adler 摘要 | `TraceInspectorBinding::updateContext` | 第一行 `zlib N B → M B · IDAT ×K · Adler-32 status`；generation 移到诊断 |
| G-03 | Block 与 Token 的 `Input bits` 原点不同 | `main_window.cpp` 注释和 `TokenEvent` 契约 | 类型化 offset domain；UI 明确 `zlib-stream bits` 或 `DEFLATE bits` |
| G-04 | Block physical mapping 只导航 `spans.front()` | `BlockInspector::showSelectedInHex` | 全部 spans 可选择/高亮，或明确分段导航，绝不伪装为连续 file range |
| G-05 | Decode 的 `Show in Hex` 实际跳 Inflated，`Show in DEFLATE` 跳 IDAT | `main_window.cpp` navigation wiring + integration test | 动作固定为 `Show in Hex`（压缩输入）与 `Show inflated output`（输出） |
| G-06 | Huffman 主表是 Build/十进制 Canonical/Definition bits，含大量 0-bit 行 | `huffman_inspector.cpp` 和实机页面 | Symbol/Meaning/Bits/Canonical bit string/Read order/Uses in result；默认过滤 unused |
| G-07 | Dynamic table 归属可能比较相对 provenance 与绝对 Block bit range | `huffman_inspector.cpp` ownership overlap | 先加多 Dynamic Block offset-domain test，再统一为同一类型/原点 |
| G-08 | Decode 主表只有 path，实际 Literal/Match 含义藏在详情 | `decode_trace_inspector.cpp` | Event 列直接显示 Literal value 或 Match len/dist；标题声明 bounded scope |
| G-09 | code/range/offset 没有等宽字体，关键表头在当前 Dock 首屏裁切 | 三个 widget 无 FixedFont；共同 table 仅 stretch last section | 数值 role 使用应用 FixedFont token；按 360/480/600 px 设 resize/hide 优先级 |
| G-10 | 现有测试能证明“不撑宽”，不能证明“首屏可读/语义正确” | responsive/component/integration tests | 增加视觉层级、列可见性、动作语义、多 span、offset-domain 和状态矩阵门禁 |

### 0.4 已完成与未完成的边界

可直接复用：bounded orchestration、bundle publication、Current marker、master/detail
splitter、Literal/Match/EOB projection、Match base/extra 和 root source ranges。

不能继续作为“完成证据”：项目文档中的 `implemented` 状态、39/39 回归、单纯 row-count
断言和 `minimumWidth <= width`。这些只能证明旧合同没有回退，不证明与本任务包的设计
目标一致。

### 0.5 预计修改路径与责任

| 路径 | 责任 | 约束 |
|---|---|---|
| `libs/analysis-engine/**/trace_query.*` | 保持 bounded result；引入/复用 typed range，修正 Block/Token/Huffman offset domain | 不扩大 token replay |
| `libs/analysis-engine/**/block_inspector.*` 或新 Fast Index projection | 发布 stream summary、完整 Blocks、Current overlay | 数据来自现有 Block Index/Virtual IDAT facts |
| `libs/analysis-engine/**/huffman_inspector.*` | meaning、bit string/read-order、bounded use/occurrence、正确 Block ownership | Qt-free；不在 UI 反转或扫描 |
| `libs/analysis-engine/**/decode_trace_inspector.*` | Event summary、target/overlap/current match provenance | 只基于 bounded token facts |
| `ui/qt/src/compression_context.cpp`、`trace_inspector_binding.cpp` | 两行 context、去重 instruction、合并 fast/bounded state | generation 一致，旧 document 原子清理 |
| `ui/qt/src/compression_inspector_page.cpp` | 固定字体 token、splitter 持久性、响应式基础 | 不提高 Inspector minimum width |
| `ui/qt/src/block_inspector.cpp` | 完整 Blocks、详情层级、segmented provenance、动作文案 | 不再 `spans.front()` 代表全部 |
| `ui/qt/src/huffman_inspector.cpp` | 原型列结构、unused filter、码表选择器、详情动作 | 不触发 replay |
| `ui/qt/src/decode_trace_inspector.cpp` | 可读 Event 列、bounded 标题、Match 详情、两个明确动作 | 不生成 header/full-file timeline |
| `apps/png-analyzer-gui/src/main_window.cpp` | 组合 Fast Index + bounded window；修正 Hex source/range wiring | 保留 WP-5U13 request owner 和 stale 防护 |
| `tests/unit/analysis-engine/**`、`tests/gui/**` | G-01～G-10 的失败先行测试和验收门禁 | 不以旧 row-count 代替产品断言 |

预计不修改 `libs/deflate-trace/**`、`libs/deflate-index/**`、`third_party/**`。若 typed range
或必要事实确实只能由这些 owner 提供，应拆出最小 core 工作包并先评审，不在 UI 提交中
顺手扩大范围。

## 1. 实施原则

1. **先审计，后编码**：不得在不知道实际 Inflate、trace、selection 和 Hex 架构时直接
   重写页面。
2. **先静态骨架，后接真实数据**：先以固定 fixture 对齐布局和状态，再绑定结构化模型。
3. **Core 产生事实，UI 只展示**：GUI 不解析 bitstream、不从字符串提取字段、不推算
   Huffman 或 LZ77。
4. **两层结果、同一 generation**：打开文件时产生不可变 Fast Compression Index；只有
   明确的 output/block provenance 操作才产生 bounded Deep Trace Window。切页、选行、
   resize 和 DEC/HEX 不 replay。
5. **Current 与 Selection 分离**：当前像素的来源标记不能覆盖用户手动浏览的行。
6. **错误保留部分结果**：后期校验或 decode 错误不清空此前已确认的 Block/event。
7. **不以截图代替测试**：截图用于视觉验收，offset、code、event 和 mapping 必须由
   golden test 验证。
8. **不得先做装饰性增强**：树图、热力图、统计仪表和动画均不得进入 P0。

## 2. 前置依赖与合并顺序

### 2.1 必需依赖

- WP-5U2：共享文件/选择/异步 generation 基础。
- WP-5U7：稳定三栏 Inspector 容器。
- WP-5U10：Inspector 一级结构为 `Reconstruction | Compression`。

### 2.2 协调依赖

- WP-5U9：提供或复用 output byte 到 pass/scanline/pixel 的映射。
- WP-5U11：决定 `Show in Hex`、Hex source 与底部动作最终承载位置。

### 2.3 推荐合并顺序

```text
WP-5U10 ───────┐
               ├─→ WP-5U12 UI skeleton ─→ core binding ─→ navigation
WP-5U7  ───────┘                         ↑
WP-5U9 mapping ──────────────────────────┤
WP-5U11 Hex navigation ──────────────────┘
```

如果 WP-5U9 或 WP-5U11 尚未合并，WP-5U12 应通过窄接口和测试替身推进，不得复制它们
的数据模型或控件；最终合并前必须替换测试替身并完成联合回归。

## 3. 阶段总览

| 阶段 | 目标 | 主要产出 | 进入下一阶段的门禁 |
|---|---|---|---|
| D0 | 基线与能力审计（已完成） | 第 0 节和任务包第 0/5 节 | 以 G-01～G-10 建立失败测试 |
| D1 | 固定分层领域契约 | typed ranges、Fast Index projection、bounded window | 多 Block/多 IDAT golden fixture 证明域一致 |
| D2 | 当前 UI 契约收口 | 在现有三页/上下文/master-detail 上建立目标 fixture | 360/480/600 px 关键列和层级对齐 |
| D3 | Trace 数据实现 | 真实 stored/fixed/dynamic/partial result | core golden tests 通过 |
| D4 | Model/View 绑定 | 虚拟化表格、详情、数值格式 | 不使用 debug string，无主线程阻塞 |
| D5 | Selection 与导航 | X/Y、Block、Symbol、Event、Hex 联动 | 导航矩阵和循环防护测试通过 |
| D6 | 错误/性能/可访问性 | 完整状态、大文件、键盘、复制 | 性能和状态门禁通过 |
| D7 | 回归与交付 | 全量测试、截图、说明 | WP-5U12 完成定义全部满足 |

## 4. D0 — 基线与能力审计

### 4.1 已完成的只读工作项

- 确认 Qt 版本、Widgets/Quick 架构、目标平台和主题入口。
- 定位 Compression 三页的页面类、table model、占位文本和 refresh path。
- 定位 IDAT 拼接、zlib wrapper、Inflate 和现有 deflate trace 的真实调用关系。
- 定位当前文件 generation、取消/替换和 stale-result 防护。
- 定位 X/Y、Lock、scanline、Hex source 和 selection bus 的 owner。
- 检查 `libs/deflate-trace/**` 是否为正式结构化能力或仅 Debug trace。
- 已填写任务包第 5.1 节能力矩阵。
- 已用有效 PNG 复现无 Lock 时重复 instruction/空 bounded table，并验证 Lock 后 ready
  三页均有真实数据。

### 4.2 产出

- 一页组件/数据流图。
- 完整能力矩阵。
- 现有与目标 offset 语义差异表。
- 预计修改路径与明确不修改路径。
- 风险列表：第三方 Inflate 限制、数据规模、跨 IDAT、Adam7 mapping、workspace 冲突。

### 4.3 门禁

- 已证明不会在 GUI 中创建第二套 decoder。
- 已确定如何得到真实 block/codebook/event；若无法得到，已提交最小插桩方案。
- 已确定 Hex 导航所需 file range API。
- 已确定 WP-5U9 mapping 可复用接口或临时窄适配接口。

D0 已完成。实现 Agent 开始改代码前只需复核工作树、当前分支和相关文件是否自本审计后
发生变化；若变化影响 G-01～G-10，先更新差距表，不重复做无目的 UI 探索。

## 5. D1 — 分层领域契约与黄金样例

### 5.1 工作项

- 固定半开 range 类型和四个 offset 域：File、zlib、DEFLATE payload、Inflated。
- 固定 `FastCompressionIndexView`（stream summary + complete blocks）和
  `BoundedDeepTraceView`（query scope + codebooks + tokens）的只读语义。
- 固定 stable Block ID 和 Event ID，不使用可见 row index 作为业务 ID。
- 定义 `Complete / Partial / Error / Unavailable` result 状态。
- 为每个 range 使用显式 domain/type；禁止用裸 `uint64_t begin/end` 在 zlib-stream bits、
  DEFLATE-payload bits、File bytes 和 Inflated bytes 之间传递。
- 定义 output range → block、bounded event、bounded symbol occurrence 和 zlib → segmented
  file ranges 查询。
- 准备 stored、fixed、dynamic、overlap match、跨 IDAT、truncated、Adler mismatch fixture。

### 5.2 实施顺序

```text
range/domain types
    ↓
Fast stream + complete block summary
    ↓
bounded codebook + token event window
    ↓
offset/output/occurrence indexes
    ↓
golden assertions
```

### 5.3 门禁

- 每个 fixture 的 input bit range 和 output byte range 有精确断言。
- canonical code 与 read-order bits 分别断言。
- overlap Match 按逐字节复制语义通过测试。
- 跨 IDAT 映射只选择 data 字段，不包含 length/type/CRC。
- Partial/Error 可保留错误点之前的有效事实。
- 多 Dynamic Block fixture 证明 Huffman provenance 与 Block range 在比较前已归一到同一
  offset domain。
- Whole-file Fast Index 不含 token/event vector；bounded window 明确记录 request range、
  max tokens、truncated/completion status。

## 6. D2 — 当前 UI 契约收口与视觉基线

此阶段在现有 `CompressionContext`、`CompressionInspectorPage` 和三个 page widget 上
使用固定 projection fixture 对齐目标；不另建一套替代页面。目的在于先锁住可验收布局，
再接 Fast Index/新增字段。

### 6.1 共同骨架

保留并修订：

- Compression 二级标签栏；
- 两行共同上下文；
- 现有 `QTabWidget` 页面容器；
- 三个子页的虚拟化 table/view 外壳；
- 独立详情区；
- 底部导航动作；
- Loading、Empty、Error 的自然状态；
- 两行共同上下文：Stream summary + Current mapping；
- 数值/bit/offset 的应用等宽字体 token；
- 页面级动作 `Show in Hex` + `Show inflated output`。

### 6.2 三页静态状态

必须用同一 fixture 渲染：

- Blocks：三行 Stored/Fixed/Dynamic，其中 Dynamic 同时是 Current 和 Selection；无 Lock
  时仍显示三行且只隐藏 Current overlay。
- Huffman：Dynamic Literal/Length 表，明确 canonical/read-order 两列和 bounded Uses。
- Trace：Literal、Match、EOB，标题明确 query scope，Match 展开
  length/distance/source/target/overlap。

### 6.3 视觉检查宽度

每页至少检查：

| Inspector 正文宽度 | 目的 |
|---:|---|
| 320 px | 最窄降级；标签、上下文和底部动作仍可用 |
| 360 px | 产品最低常用宽度 |
| 480 px | 规范参考宽度 |
| 600 px | 宽栏，主要字段全部可见 |

浅色和深色主题均检查 360、480 px。参考环境之外允许平台原生像素差异，但不得改变
任务包第 20 节规定的层级、列优先级、间距范围和响应式行为。

### 6.4 门禁

- 三页不出现横向撑宽 Inspector 的 minimumSizeHint。
- Current 与 Selection 同时出现时仍可辨认。
- 表格和详情区各自滚动，不形成难用的多层整页滚动。
- 360 px 下无文字遮挡、按钮重叠或不可访问动作。
- 480 px 截图经产品/reference review 接受后，才允许进入真实数据绑定。

## 7. D3 — Fast Index projection 与 bounded Deep Trace projection

### 7.1 工作项

- 从现有 zlib wrapper、Inflate/checksum 和 Virtual IDAT facts 建立 generation 级 stream
  summary；不重复 decode。
- 从现有 Fast Block Index 建立完整 Stored/Fixed/Dynamic Block projection，允许无 Lock 浏览。
- 保留当前 bounded trace 的 Dynamic code-length、literal/length、distance codebook 和
  Literal/Match/EOB；所有计数与 occurrence 都携带 `in current trace` scope。
- 为 Match projection 补 target、overlap、current match offset/source logical offset；这些
  由 Qt-free layer 基于已有精确 facts 产生。
- 建立 zlib byte 到一个或多个 PNG file range 的映射。
- 提供 Fast Index 与 bounded window 各自的 Ready/Partial/Error/Unavailable 状态；页面不再
  依赖 `Block trace: no trace`。

### 7.2 技术约束

- 不修改第三方源码。
- 插桩不得改变最终 Inflate 输出。
- 不为每个输出 byte 创建一条事件；Stored data 允许连续聚合。
- 不为每个事件预生成所有展示字符串。
- Release 构建必须具备产品所需结构化 trace。
- 不把完整 Block 列表复制进每次 bounded result；bundle 组合时按 generation 引用/投影。
- 不为匹配原型而新增 wrapper/header/codebook-build 的 whole-file event timeline。

### 7.3 门禁

- Core golden tests 全部通过。
- Deep Trace 开启/关闭对最终解码字节逐字节一致。
- 错误 fixture 不崩溃、不越界且 stop location 准确。
- 事件和 codebook 不依赖 GUI 或 Qt。

## 8. D4 — Qt Model/View 数据绑定

### 8.1 工作项

- Block、Huffman、Trace 分别使用 `QAbstractTableModel` 或项目等价虚拟化模式。
- 采用 stable ID 将 table index 映射回领域对象。
- DEC/HEX 只触发格式刷新，不替换分析结果。
- raw bits 和长详情按当前 selection 延迟格式化。
- 详情区绑定结构化字段，不解析 row display text。
- 为大 trace 实现分页/fetchMore 或等价可证明的虚拟化。

### 8.2 门禁

- 坐标变化不线性扫描完整 event list。
- 切页不重新解析、不重新 Inflate。
- 选择大范围 output 时可得到相交事件而不冻结 UI。
- 表格没有逐行 QWidget。
- 更换文件时旧 generation model 不会回写新页面。

## 9. D5 — Selection 与跨区域导航

### 9.1 实施顺序

1. Blocks 内部选择和详情。
2. Blocks → Huffman / `Open Decode Trace`（后者可显式提交 bounded window）。
3. Huffman symbol → bounded occurrence → Trace。
4. Block/Trace `Show in Hex` → compressed input（logical IDAT + segmented File provenance）。
5. Block/Trace `Show inflated output` → Inflated output。
6. X/Y/output range → Current Block/events。
7. Central stage → Compression origin。

### 9.2 导航规则

- Current context 与 manual selection 使用不同字段和视觉语义。
- 明确的图像点击或坐标提交可以在 Lock 规则下定位 current event。
- 普通刷新不得抢走用户正在浏览的 manual event。
- 选择非 IDAT Chunk 不清空 Compression。
- 所有跨组件请求携带 navigation origin 或等价 reentrancy guard。
- 不再提供含义模糊的 `Show in DEFLATE`；不能把 `Show in Hex` 用作 Inflated 导航。
- 如果压缩输入跨多个 IDAT physical spans，Hex 高亮必须分段，或 UI 明确提供 span chooser；
  不允许只取第一段却把动作描述为完整范围。

### 9.3 门禁

- 任务包第 10.2 节导航矩阵逐项有自动化或聚焦集成测试。
- Hex 与 Compression 不产生循环选择。
- 跨 IDAT event 能定位多个真实文件范围。
- packed/Adam7 无精确映射时显示降级，不猜测 channel/pixel。

## 10. D6 — 状态、性能、主题与可访问性

### 10.1 状态覆盖

- 未加载文件；
- 分析中且只有 stream/block 摘要；
- 无 IDAT；
- 无锁定像素；
- Stored Block 的 Huffman 页面；
- symbol 已定义但 Uses=0；
- Partial trace；
- truncated/BTYPE=11/invalid distance；
- Adler mismatch；
- 当前构建没有 structured trace 能力。

### 10.2 性能基线

记录小 fixture 和大 trace：

- stream/block 首屏时间；
- bounded Deep Trace Window 时间；
- request output range、token budget、返回 token 数与 truncated 状态；
- 峰值内存；
- 切换 X/Y 的查询时间；
- Trace 快速滚动时主线程最长阻塞。

具体阈值应结合现有项目基线在 D0 固定；不得在完成后才临时选择一个容易通过的阈值。

### 10.3 可访问性

- 表格、标签、按钮均有可访问名称。
- 键盘可完成选 Block、切页、打开 occurrence、跳 Hex。
- Current/Selected/Error 不只靠颜色。
- Focus ring 清晰且不被 Current 标记覆盖。
- 所有展示值和详情可复制。

### 10.4 门禁

- 状态矩阵全部有截图或 GUI 测试。
- 360/480 px 浅色和深色主题无布局回退。
- 性能数据满足 D0 固定阈值。
- Release 构建和 Debug 构建行为一致。

## 11. D7 — 完整回归与交付

### 11.1 自动化

- Core golden tests。
- Output/pass/scanline mapping tests。
- Qt table model tests。
- Selection/navigation integration tests。
- Workspace restore 和 stale generation tests。
- 关键视觉状态的 screenshot baseline tests。

至少新增/改写以下 discriminating cases：

- `compression_context_no_lock_keeps_stream_summary_without_duplicate_instruction`；
- `blocks_use_complete_fast_index_and_overlay_current_bounded_block`；
- `huffman_table_ownership_normalizes_zlib_and_deflate_bit_domains`（至少两个 Dynamic Block）；
- `block_hex_navigation_preserves_all_physical_spans`；
- `decode_actions_map_input_to_idat_and_output_to_inflated`；
- `huffman_filters_unused_symbols_and_labels_bounded_uses`；
- `compression_numeric_cells_use_fixed_font`；
- `compression_360px_keeps_required_headers_and_complete_action_labels`；
- `partial_trace_keeps_fast_blocks_and_verified_tokens`。

旧测试中期待 `Show in DEFLATE`、Decode `Show in Hex → Inflated` 或只取
`physical_spans.front()` 的断言必须改写；它们是旧行为锁定，不是新合同的回归保障。

### 11.2 手工回归

- 打开、关闭、重载和快速切换 PNG。
- 图像点击、X/Y、Lock、DEC/HEX。
- 左侧 Chunk 选择，包括多个 IDAT 和非 IDAT。
- Reconstruction 与 Compression 来回切换。
- Blocks/Huffman/Trace 全钻取链。
- File、IDAT Stream、Inflated、Defiltered Hex source。
- 320、360、480、600 px Inspector。
- 浅色、深色、macOS 高分屏。
- 大 trace 快速滚动、选择和关闭文件。

### 11.3 交付门禁

- WP-5U12 第 19 节完成定义逐项勾选并附证据。
- 所有偏差列入交付说明；未获批准的规范性 UI 偏差视为未完成。
- 提供 D0 能力矩阵、数据契约、性能报告、截图和手工回归记录。

## 12. Agent 执行约束

### 12.1 允许自主决定

- 符合现有项目风格的类名、文件拆分和私有辅助函数。
- `QSplitter` 与现有等价组件之间的选择，只要满足规范性 geometry 和状态保持。
- 查询索引的具体数据结构，只要满足正确性和性能要求。
- 测试 fixture 的存放位置，只要来源、manifest 和测试边界符合项目规则。

### 12.2 必须暂停并说明

- 需要修改 `third_party/**`。
- 现有 decoder 无法在不重写解码路径的情况下提供 trace。
- WP-5U9 无法提供 output mapping，且替代方案会建立第二套坐标模型。
- WP-5U11 的 Hex 导航契约与任务包冲突。
- 需要改变任务包定义的 offset、range、Current/Selection 语义。
- 指定宽度下无法同时满足必要字段和 Dock 最小宽度。

### 12.3 禁止的“近似完成”

- 用示例数据或硬编码行填充页面。
- 继续显示笼统 `no trace`。
- 只实现 Dynamic Block，忽略 Stored/Fixed/Error。
- 让 Huffman 页面只显示频次，不显示 canonical/read-order 区别。
- Match 只显示 length/distance，不显示 source/target/overlap。
- 用最终像素颜色反推 compressed provenance。
- 为了截图效果给 Inspector 设置超大 minimum width。
- 只在 Debug 构建提供 trace。
- 通过扩大 screenshot diff tolerance 掩盖文字裁切或布局变化。

## 13. 推荐提交拆分

每个提交保持可构建、测试边界清晰：

1. `tests: expose compression offset-domain and navigation gaps`
2. `compression: add fast stream and complete block projection`
3. `compression: type zlib and deflate bit ranges`
4. `ui: render shared stream summary and remove duplicate instruction`
5. `ui: align blocks master detail and segmented provenance actions`
6. `compression: project huffman meaning read order and bounded uses`
7. `ui: align huffman table hierarchy and typography`
8. `compression: project decode event summary and current match provenance`
9. `ui: align decode trace and input output navigation labels`
10. `tests: gate responsive visual states performance and accessibility`

不要把 core trace、三个页面、导航和全部测试压进一个不可审查的大提交。

## 14. 最终检查清单

- [ ] D0 能力矩阵完成并与实际代码一致。
- [ ] 四个 offset 域和半开 range 契约固定。
- [ ] Fast Index 与 bounded Deep Trace 的数据/生命周期边界固定，默认路径无 whole-file
      token trace。
- [ ] 无 Lock 时可查看完整 Blocks 和 stream summary，且只显示一次操作提示。
- [ ] Stored、Fixed、Dynamic、Partial/Error 均有 golden test。
- [ ] 三页静态 UI 在接数据前通过参考评审。
- [ ] Blocks、Huffman、Trace 分别回答唯一问题且无重复堆砌。
- [ ] canonical code 与 read-order bits 分列，Uses/occurrence 明确 bounded scope。
- [ ] Match provenance 和 overlap 语义正确。
- [ ] Current 与 manual Selection 可同时存在。
- [ ] X/Y、stage、Compression、Hex 双向导航无循环。
- [ ] 跨 IDAT file ranges 准确。
- [ ] `Show in Hex` 始终表示压缩输入，`Show inflated output` 始终表示输出；项目内不再以
      `Show in DEFLATE` 混用页面钻取和数据域跳转。
- [ ] 大 trace 虚拟化且无坐标变更全表扫描。
- [ ] 320/360/480/600 px 无变形、无遮挡、无 Dock 撑宽。
- [ ] 浅色/深色、键盘、复制、可访问名称通过。
- [ ] Debug/Release、workspace restore、stale generation 通过。
- [ ] 性能数据、截图和回归记录齐全。
- [ ] WP-5U12 第 19 节完成定义全部满足。

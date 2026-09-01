# WP-5U12 — Compression Inspector 的 DEFLATE 分析流程与三子页实现

> Work Package：`WP-5U12`
> Milestone：M5 UI Refinement
> Status：normative audited UI contract; execution resumed by
> [WP-5U12 Completion](wp-5u12-compression-inspector-completion.md) (2026-09-01)
> Depends on：WP-5U2、WP-5U7、WP-5U10
> Coordinates with：WP-5U9、WP-5U11
> Product reference：用户提供的 2026-08-23 PNG Analyzer 截图
> Development plan：`wp-5u12-compression-inspector-development-plan.md`
> Normative UI contract：本文件第 20 节；与前文建议性描述冲突时以第 20 节为准
> 主要允许路径：`libs/deflate-trace/**`、与只读分析结果直接相关的 `libs/analysis-engine/**`、`ui/qt/**`、聚焦测试、本工作包文档
> 条件允许路径：只有在现有 Inflate 观察接口无法产生真实 trace 时，才可对当前自有 Inflate/DEFLATE 适配层作最小、无行为变化的插桩
> 禁止路径：`third_party/**`、无关 PNG parser/filter/reconstruction 实现、打包与品牌资源、无关工作包文档

## 0. 2026-08-24 当前实现审计与修订结论

### 0.1 审计方法与边界

本次审计同时使用了两类证据：

- 通过 Computer Use 打开当前构建的 PNG Analyzer，加载
  `/Users/lijiangbo/project/PNG-Handbook/pngsuite/png/basn2c08.png`，在 Lock 开启后逐页读取
  `DEFLATE Blocks`、`Huffman`、`Decode Trace` 的真实内容；
- 只读检查 `/Users/lijiangbo/project/PNG-Analyzer` 中对应的 Qt widget、binding、Qt-free
  projection、bounded trace contract、Fast Block Index 和现有 GUI/integration tests。

Computer Use 对这个 Qt 开发包的窗口定位曾出现 `noWindowsAvailable`；这是自动化工具面对
无有效 Bundle ID/Bundle Name 开发包时的定位限制，不是 PNG Analyzer 崩溃，也不列为产品
缺陷。审计结论只采用成功读取到的页面状态和源码/测试证据。

审计 fixture 是 32 × 32、Truecolor、单个 Dynamic final Block 的 PNG。它足以验证正常
ready 状态和三页基本联动，但不能替代 Stored、Fixed、多 Block、跨 IDAT、Partial/Error、
窄宽和深色主题的验收矩阵。

### 0.2 总体判断

当前实现不是“没有接线”：bounded Deep Trace、generation/stale 防护、三页共同 bundle、
当前关联行高亮、master/detail 外壳和基本导航均已工作。但它仍是“工程投影视图”，尚未达到
本任务包和交互原型定义的产品级 Compression Inspector。项目内现有 WP-5U12 文档虽标记
`implemented`，该结论只能说明既有投影字段和回归测试已接通，不能作为本次 UI/功能目标
的完成证明。

差距优先级如下：

| 优先级 | 结论 | 影响 |
|---|---|---|
| P0 | Blocks 只显示当前 bounded output 相交块，未显示 Fast Index 的完整 Block 列表 | 无 Lock 时页面空白，不能从宏观结构开始浏览 |
| P0 | 两个页面动作的名称和数据域不清晰；Decode 的 `Show in Hex` 实际跳 Inflated，`Show in DEFLATE` 实际跳压缩输入 | 用户无法从标签预测跳转结果，容易混淆 File/IDAT/Inflated |
| P0 | Block 输入 bit 是含 zlib header 的逻辑流绝对 bit，Token 输入 bit 是 DEFLATE payload 相对 bit，却都显示为 `Input bits` | 同名列存在不同 offset 原点，属于正确性风险 |
| P0 | Block `Show in Hex` 只使用 `physical_spans.front()` | 跨 IDAT 时只显示第一段，不能代表完整输入范围 |
| P0 | 共享上下文无 zlib/IDAT/Inflated/Adler 摘要，且无 Lock 时同一句指令显示两遍 | 页面入口缺少流级结论且信息重复 |
| P0 | Huffman 的码表归属比较混用了相对 DEFLATE provenance 与绝对 zlib Block bit range | 多 Dynamic Block 时可能把码表关联到错误 Block，必须先用测试确认并统一域 |
| P1 | Huffman 显示 `Build`、十进制 `Canonical`、全部 0-bit 未使用 symbol；缺 Meaning、read-order、bounded occurrence | 码表很难读，且与设计原型的任务语言不一致 |
| P1 | Decode Trace 主表只有 `literal/match/eob`，Literal/Match 摘要被埋在详情；缺当前范围标题和清晰 scope | 用户要逐行选择后才知道事件实际做了什么 |
| P1 | 表头/数值均使用普通 UI 字体，列宽依赖内部横向滚动；关键列在当前 Inspector 宽度会裁切 | 代码、bit range 和偏移不易扫描，首屏阅读效率低 |
| P1 | Current 与手动 Selection 虽有不同视觉机制，但没有明确的状态说明和跨页 selection model | 用户浏览其他行时容易误认当前像素来源 |
| P2 | 现有测试主要断言 row count、文本和值域跳转，不断言首屏可读性、动作语义、完整多 span、高级状态和视觉层级 | 文档可标记完成而实际 UI 仍明显偏离设计 |

### 0.3 实机逐页差距

#### 共同上下文

当前 ready 文案为：

```text
Trace ready · generation 1 · 1 associated blocks · 7 tokens in result
Current · scanline 0 · output byte 0 · Block #0
```

`generation` 是诊断信息，不应占据主要产品状态；`1 associated blocks` 和 `7 tokens in
result` 必须明确是 bounded result，而不是整条流。目标改为：第一行长期显示 stream
summary，第二行显示 current mapping；generation 进入可复制诊断详情。无 Lock 时只显示
一次操作提示，Fast Index 的 Blocks 仍可浏览。

#### DEFLATE Blocks

当前表为 `# | Type | Final | Input bits | Output bytes`，fixture 对当前关联行使用浅黄色背景，显示一个
`dynamic / yes / 16..539 / 0..3104` Block。详情能显示 BFINAL、输入/输出半开范围、当前
output、file span 和 scanline，这是可复用基础。

目标差距：

- 表数据源从 bounded `TraceQueryResult.blocks` 改为 document generation 的完整 Fast
  Block Index projection；bounded result 只负责当前关联行信息和 trace 可用状态；
- 详情补 BTYPE 原始值、Block 输出字节数、Stored/Dynamic metadata（仅显示已有 core
  事实）和多段 IDAT provenance；
- `Show in Hex` 必须能表达完整逻辑输入及全部 physical spans；
- 第二动作改为 `Show inflated output`；钻取用详情标题中的 `Open Decode Trace`，不要用
  `Show in DEFLATE` 同时承担“跳输入”和“切页面”两种含义。

#### Huffman

当前 Dynamic Literal/Length 表显示 292 行，列为
`Build | Symbol | Bits | Canonical | Definition bits`。当前关联 entry 使用浅黄色背景。`Canonical` 当前是类似
`42 (7 bits)` 的十进制整数，0-bit 未使用 symbol 也全部展示。

目标差距：

- 标题明确 `Block #n · Dynamic Huffman`，`Build` 从主列移到诊断/详情；
- 默认只显示 `bit_length > 0` 的有效 symbol，提供可选 `Show unused symbols` 而不是用
  0-bit 行淹没有效码字；
- 主表使用 `Symbol | Meaning | Bits | Canonical | Read order | Uses in result`；
- Canonical 和 read-order 显示固定位数 bit string，数值/bit 使用等宽字体；
- `Meaning` 对 literal、EOB、length range、distance range 给出自然语义；
- occurrence 只能声明 bounded scope。没有 whole-block occurrence index 时显示
  `Uses in current trace`，不得把 bounded count 伪装成整块 Uses；
- 当前 projection 没有 read-order、meaning、use count/occurrence，因此这些属于 Qt-free
  projection 的最小扩展，GUI 不得自行反转位或扫描 trace 猜测。

#### Decode Trace

当前 fixture 返回 7 个与选定 scanline output 相交的 token，列为
`Token | Path | Input bits | Output bytes`；当前关联 token 使用浅黄色背景，详情已经具备 Literal value，以及 Match
的 base/extra length、distance 和 root source ranges。这些是正确基础。

目标差距：

- 标题改为 `Current trace · output [begin, end) · N tokens` 或 `Block #n trace window`，始终
  显示 bounded scope，不宣称 whole-block/full-file trace；
- 主表改为 `Current | Step | Input bits | Event | Output`，Event 直接显示
  `Literal 0x41`、`Match · length 18 / distance 7`、`End of block`；
- Match 详情增加 target、overlap yes/no、当前 byte 在 match 内的 offset 和可证明的源逻辑
  offset；
- 详情补 Block、pass/scanline/row byte 映射（仅在现有 mapping 精确时）；
- `Show in Hex` 跳压缩输入，`Show inflated output` 跳输出，两个动作不再靠隐含 Hex source
  切换来解释；
- P0 仍是 bounded token trace。Block header、dynamic build phase、wrapper 和 Adler timeline
  不属于本轮匹配原型的必需行，不得借 UI 改造扩大成默认 whole-file event trace。

### 0.4 架构修订：Fast Index 与 bounded Deep Trace 分层

原任务包中“打开文件时生成一次完整不可变 Compression analysis result”的措辞与项目
ADR-0006/仓库契约冲突，现修订为：

```text
Document generation
├─ FastCompressionIndex（整流、轻量、可长期保留）
│  ├─ zlib wrapper / IDAT / Adler summary
│  └─ complete DEFLATE Block list + input/output ranges
└─ DeepTraceWindow（显式选择触发、bounded、可取消、带 generation）
   ├─ selected output interval
   ├─ associated tokens
   ├─ selected-block codebooks needed by this window
   └─ Partial/Error/budget scope
```

因此：

- Blocks 可在无 Lock 时完整浏览，来源是 Fast Index，不触发 token replay；
- X/Y/Lock 只触发当前 output interval 的 bounded Deep Trace；
- 切换子页、选择 table row、调整 splitter/宽度不得触发 replay；
- `Open Decode Trace` 若要从非当前 Block 进入，只能发起明确、预算化的 trace window，不能
  生成或保留 whole-file token trace；
- Huffman 的 Uses/occurrence 必须带 scope，或由独立的 bounded occurrence index 支持；
- 三页共享同一 generation，但不要求共用一个无界、全量 event 容器。

### 0.5 当前实现可保留部分

- `TraceOrchestrator` 的 bounded request、预算、cancel 和 stale-generation 防护；
- `TraceInspectorBinding` 同 generation 发布三页 bundle 的边界；
- `CompressionInspectorPage` 的 master/detail splitter 结构；
- Literal/Match/EOB、length/distance arithmetic、root source ranges 的 Qt-free projection；
- 当前关联行浅黄色背景与 Qt 原生 row selection 的双重视觉基础；
- `VirtualIDATStream` 和 Fast Block Index，不新增 IDAT 拼接或第二套 GUI decoder。


本工作包把 `Compression` 下现有的三个子页建设成一条连续、可钻取、可回溯的
DEFLATE 分析链路：

```text
合并 IDAT 得到 zlib stream
        ↓
DEFLATE Blocks：流被分成哪些块，当前输出来自哪个块
        ↓ 选择 Block
Huffman：该块使用哪些码表，bit pattern 如何映射为 symbol
        ↓ 选择 Symbol / occurrence
Decode Trace：解码器如何消费输入位并产生 literal 或 LZ77 match
        ↓
Inflated output byte → pass / scanline / filtered byte → 当前像素
```

三个页面不是三个互相重复的“DEFLATE 信息表”：

- `DEFLATE Blocks` 负责宏观结构和入口选择；
- `Huffman` 负责当前块的编码规则；
- `Decode Trace` 负责按时间顺序解释真实解码事件。

## 1. 背景与问题

截图中 `Compression` 已包含：

```text
DEFLATE Blocks | Huffman | Decode Trace
```

但正文只显示：

```text
Block trace: no trace
scanline: — | current output: —
```

并保留一个没有数据、没有错误原因、没有下一步动作的大面积空表。当前界面存在以下
问题：

1. 用户无法判断 `no trace` 是没有 IDAT、尚未分析、功能未接线、格式不支持，还是文件损坏。
2. 三个子页的职责和钻取顺序不明确。
3. 当前 X/Y、扫描行、Inflate 输出、DEFLATE token、块和压缩输入位之间没有可见映射。
4. 选择非 IDAT Chunk 时，Compression 不应失去全局 IDAT 上下文。
5. Huffman canonical code、实际读取位序和 symbol 语义容易被混为一谈。
6. LZ77 match 只显示长度/距离仍不足以解释当前输出字节来自哪里。
7. 大文件 trace 若直接创建逐事件控件，会造成高内存、长时间 UI 阻塞和滚动性能问题。
8. Hex 导航必须跨越 PNG 文件偏移、IDAT 拼接流偏移和 DEFLATE bit offset，不能用近似偏移跳转。

## 2. 用户可见目标

完成后，用户应能自然地完成以下操作：

1. 打开任意有效 PNG，进入 `Compression / DEFLATE Blocks`，立即看到 zlib stream
   摘要和完整 Block 列表。
2. 点击图像或修改 X/Y 后，界面自动定位当前像素所对应的 Inflate 输出范围，并高亮
   产生该范围的 Block 和 decode event。
3. 选中一个 Dynamic 或 Fixed Block 后，切到 `Huffman` 可查看该 Block 的真实码表。
4. 从某个 Huffman symbol 跳到 `Decode Trace`，查看该 symbol 在当前 Block 中的实际出现。
5. 在 `Decode Trace` 选中 Literal、Match 或 Block 事件后，可跳到准确的压缩输入 Hex；
   对产生输出的事件还可跳到准确的 `Inflated` 输出范围。
6. 对 LZ77 Match，可同时查看 length、distance、复制源范围、目标范围及重叠复制说明。
7. 对 Stored Block、无当前像素、损坏流、部分 trace 等状态，页面显示明确说明且保留
   已成功分析的内容。

## 3. 范围与非目标

### 3.1 本工作包包含

- 三个 Compression 子页的最终信息架构、页面布局和交互。
- zlib wrapper 摘要、DEFLATE Block 摘要、Huffman codebook 和 decode event 的只读展示模型。
- PNG 文件偏移、拼接 IDAT/zlib 偏移、DEFLATE bit offset、Inflate 输出偏移之间的准确映射。
- 当前像素/扫描行到 Inflate 输出范围，再到 token 和 Block 的反向定位。
- 三页之间以及 Compression 与 Hex、中央 stage 页面之间的跳转。
- 无文件、加载中、无 IDAT、Stored Block、部分 trace、损坏流和校验失败等状态。
- 大 trace 的虚拟化、查询索引、异步生成和 stale-generation 防护。
- 相关 core、model 和 GUI 测试。

### 3.2 本工作包不包含

- 编写第二套 Inflate/DEFLATE 解码器。
- 改变 PNG 最终解码结果、Filter 或 Reconstruction 算法。
- 在 GUI 中重新解析 bitstream、重建 Huffman tree 或计算 LZ77 输出。
- 把逐 bit 记录作为默认主视图；主 trace 粒度是语义事件。
- 绘制装饰性的 Huffman 树动画、压缩率仪表盘或全图压缩热力图。
- 修改 `Pixels`、`Filtered`、`Defiltered` 的阶段正文；它们属于 WP-5U9。
- 改造 Inspector 一级导航、Hex source 竖向标签或主窗口三栏；分别由 WP-5U10、
  WP-5U11、WP-5U7 负责。
- 修改第三方 zlib/miniz/libdeflate 等源码。

若前置检查发现当前项目只使用不提供观察接口的第三方 Inflate，开发 Agent 必须先提交
“能力缺口与最小接入方案”，不得通过复制第三方实现或让 UI 重新解码来绕过。

## 4. 必须先统一的术语与偏移语义

所有 core、UI、测试和日志必须使用同一套语义。

### 4.1 四个数据域

| 名称 | Offset 0 的含义 | 是否包含 zlib wrapper |
|---|---|---|
| File | PNG signature 的第一个字节 | 是，作为 PNG 文件内容的一部分 |
| IDAT Stream / zlib stream | 所有 IDAT data 字段按文件顺序拼接后的第一个字节 | 是，含 CMF/FLG 和 Adler-32 |
| DEFLATE payload | CMF/FLG 后第一个 DEFLATE bit | 否 |
| Inflated output | Inflate 产生的第一个字节 | 不适用；包含每个 pass/scanline 的 Filter byte |

禁止把 `zlib byte 0`、`deflate bit 0` 和第一个 IDAT 的 PNG 文件 data offset 混称为
“input offset”。UI 标签必须写明域，例如：

```text
DEFLATE bits 802–936
zlib bytes 102–119
file bytes 0x0043–0x0054, 0x0061–0x0064
Inflated bytes 1536–1586
```

### 4.2 IDAT 拼接

- Compression 始终分析所有 IDAT data 字段拼接后的一个逻辑 zlib stream。
- DEFLATE Block 和 token 可以跨 IDAT Chunk 边界。
- Chunk 边界只发生在字节之间，但一个事件的输入范围可覆盖多个 IDAT 文件区间。
- `Show in Hex` 必须通过 offset map 映射到一个或多个真实文件范围。
- 左侧选择 IHDR、gAMA、IEND 或其他非 IDAT Chunk，不得清空 Compression 页面。
- 选择 IDAT Chunk 时可以附加高亮该 Chunk 对应的 zlib byte 范围，但不能把它当作
  独立可 Inflate 的流。

### 4.3 数值与位序

- 顶部 DEC/HEX 控制普通数值、offset、length、distance 和 symbol 编号的显示。
- 原始 bit sequence、BFINAL/BTYPE 及 Huffman code 始终用二进制表示。
- Huffman 页面必须分别标注：
  - `Canonical code`：canonical Huffman 码值；
  - `Read-order bits`：解码器从压缩流实际读取的顺序。
- 不允许只显示一列含义不明的 `Code`。
- 所有输入 bit range 采用半开区间 `[start, end)`，所有输出 byte range 也采用半开区间。
  UI 可显示成用户友好的 `1536–1585`，但 model/API/测试必须保留半开区间语义。

### 4.4 当前输出不是单个整数

当前 X/Y 可能映射到：

- RGB/RGBA 的多个源字节；
- 16-bit channel 的高低字节；
- packed sample 所在的共享字节；
- Adam7 当前 pass 中的若干字节；
- 暂时只能定位到 scanline，而不能精确定位到 logical pixel 的降级范围。

因此共享 selection 必须表达 `InflatedOutputRange` 或多个 range，不能只保留一个
`currentOutputByte`。如果多个 range 跨越多个 token 或 Block，全部高亮，并选择最早
相交事件作为主事件；上下文条明确显示 `3 bytes / 2 events` 等信息。

## 5. 开发前置审计

开发 Agent 必须先完成以下只读检查，并在交付说明中给出结果。

### 5.1 能力矩阵

确认现有实现是否已经提供：

| 能力 | Existing | Reusable | Gap / action |
|---|---|---|---|
| zlib CMF/FLG 与 Adler-32 结果 | `ZlibWrapperTrace`、Fast Index/Inflate checksum 状态 | 是 | 尚未投影到 Compression 共同上下文；补只读 stream summary |
| Block header、BFINAL、BTYPE | Fast Block Index 有 type/BFINAL，UI 已显示 type/final | 部分 | 详情补 BTYPE 原始值；不在 UI 重读 bitstream |
| Block compressed bit range | Fast Index 有完整整流范围 | 是 | 当前只投影 bounded 相交 Block；新增 generation 级完整 Blocks projection |
| Block output byte range | Fast Index 已有 | 是 | 同上；显示半开范围和 byte count |
| Dynamic code-length sequence | token decoder 能构建并保留 entry provenance | 部分 | 当前无 HLIT/HDIST/HCLEN 和 repeat 语义 projection；本轮只补原型必需 summary |
| Literal/length codebook | bounded trace 已有 | 是 | 统一 Block 归属 offset 域，补 meaning/read-order/bounded Uses |
| Distance codebook | bounded trace 已有 | 是 | 同上；Fixed 仅在 core 提供时展示完整条目 |
| Literal、Match、EOB events | bounded trace 已有，实机正常显示 | 是 | 主表补可读 Event 摘要，保持 bounded scope |
| Match source/target range | output/length/root source ranges 已有 | 部分 | projection 补 overlap、current match offset/source logical offset |
| zlib byte → PNG file ranges | `VirtualIDATStream` 已有；Block 详情已有 spans | 是 | navigation 不能只取 `front()`；支持全部 spans 或明确分段选择 |
| output byte → pass/scanline position | MainWindow 能提供单 scanline | 部分 | 复用 reconstruction mapping 扩展为 range/pass/row byte；不能精确时明确降级 |
| output range → event/block query | bounded compose 已有相交过滤；Fast Index 有 `block_for_output` | 是 | 分离完整 Blocks index 与 bounded tokens；避免线性 UI 扫描 |
| partial/error trace | state machine 和 projection 已有 | 部分 | 文案保留 verified rows，补用户可理解的 budget/location/scope |

### 5.2 实际组件与状态所有权

定位：

- 三个 Compression 页面和当前占位 table 的创建位置；
- 当前 Block/token selection 的 owner；
- 文件 generation、异步分析和 stale-result 丢弃机制；
- Hex source/selection bus 的真实接口；
- WP-5U9 stage offset resolver 的真实接口；
- `libs/deflate-trace/**` 与实际 Inflate 路径的关系；
- 大表格使用的 model/view 模式以及现有复制、排序、键盘导航能力。

不得在完成审计前假设页面类名、信号名或 decoder 类型。任务包中的数据结构名称是语义
要求，不强制照搬命名。

## 6. Compression 共同页面骨架

三个子页共享同一个只读 `CompressionSelection` 和一条紧凑上下文区。切页不创建第二份
分析模型，不重新 Inflate，也不丢失选择。

### 6.1 正常状态线框

```text
┌──────────────────────────────────────────────────────────────┐
│ Reconstruction | Compression                                │
│        DEFLATE Blocks | Huffman | Decode Trace               │
├──────────────────────────────────────────────────────────────┤
│ zlib 72 B → 3,104 B   IDAT ×1   Adler-32 ✓                  │
│ Current  output 1,573–1,575 · row 16 · Block #2 · Event #35 │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│                  当前子页主内容                              │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│ Show in Hex                         Show inflated output      │
└──────────────────────────────────────────────────────────────┘
```

### 6.2 共同上下文第一行：Stream summary

默认显示：

```text
zlib <compressed bytes> → <inflated bytes>   IDAT ×<count>   Adler-32 ✓/✕/—
```

规则：

- 使用自然文本和轻量分隔符，不堆叠大量徽章或卡片。
- `compressed bytes` 指完整 zlib stream；Block 表的 Input 指 DEFLATE payload bits。
- Adler-32 尚未计算显示 `—`，匹配显示 `✓`，不匹配显示 `✕` 并提供可复制详情。
- CMF/FLG 异常、FDICT、truncated 等状态用一行明确文字替代成功摘要。
- 该行可选择复制；不得用超大字号。

### 6.3 共同上下文第二行：Current mapping

有精确坐标映射时：

```text
Current  output 1,573–1,575 · pass 0 / row 16 · Block #2 · Events #35–#36
```

只有 scanline 映射时：

```text
Current  pass 3 / row 8 · output row 920–1,047 · pixel byte mapping unavailable
```

没有锁定坐标时：

```text
No pixel is locked · select a block or decode event to inspect compression
```

没有输出事件时仍允许手动浏览所有 Block，不把页面整体禁用。

### 6.4 固定底部动作

- `Show in Hex`
  - Block：选择该 Block 的压缩输入范围；
  - Trace event：选择该事件的压缩输入范围；
  - Huffman codebook row：默认禁用，因为码表定义不对应唯一 occurrence；提供说明
    `Choose an occurrence in Decode Trace`。
- `Show inflated output`
  - 只对 Literal、Match、Stored data 或带输出范围的 Block 启用；
  - 跳到 Hex 的 `Inflated` source 并选中精确输出范围；
  - 不使用含义模糊的 `Show in DEFLATE`。
- 如果 WP-5U11 最终把这些动作放在中央 Hex 工具区，本页只发 navigation request，
  不复制另一组常驻按钮。

### 6.5 窄 Inspector 响应式规则

以 320–380 px 的可用正文宽度作为最低设计目标：

- 子标签允许 Qt 原生滚动或溢出，不提高 Inspector 最小宽度。
- 上下文行允许自然换行成两行。
- 表格优先保留关键列，次要字段进入下方详情区。
- 横向滚动只能发生在表格视口内部，不能撑宽整个 Dock。
- 详情文本可换行；bit sequence 使用可选择的等宽字体并允许软换行。
- 不用三个并排统计卡占据首屏。

## 7. 子页 A — DEFLATE Blocks

### 7.1 页面问题

回答：

> 这个 zlib stream 包含哪些 DEFLATE Block？当前输出由哪个 Block 产生？

它是进入 Compression 的默认子页，也是选择后续 Huffman/Trace 上下文的唯一 Block
主入口。

### 7.2 页面布局

```text
┌─ Blocks ─────────────────────────────────────────────────────┐
│ #  Type       Final   Input bits       Output bytes          │
│ 0  Dynamic    no      0–407            0–511                 │
│ 1  Fixed      no      408–785          512–1,535             │
│▶2  Dynamic    yes     786–1,205        1,536–3,103           │
├─ Block #2 details ───────────────────────────────────────────┤
│ Header       BFINAL=1 · BTYPE=10 Dynamic Huffman             │
│ Dynamic      HLIT=286 · HDIST=18 · HCLEN=16                  │
│ Tokens       96 literals · 77 matches · 1 end-of-block       │
│ Output       1,568 B · scanlines 16–31                       │
│ Current      output +37 · event #35 · match offset +4        │
│ IDAT         zlib bytes 100–151 → file ranges …              │
│                                       Open Decode Trace →    │
└──────────────────────────────────────────────────────────────┘
```

上半区使用虚拟化表格，下半区使用可滚动、可复制的详情。可使用垂直 splitter，但切换
文件和页面不得重置用户在同一次运行中的分配比例。

### 7.3 Block 表格字段

最低宽度必须显示：

| 列 | 内容 |
|---|---|
| `#` | 0-based Block index |
| `Type` | `Stored`、`Fixed`、`Dynamic`、`Reserved/Error` |
| `Final` | BFINAL 的 `yes/no`，不可只用颜色 |
| `Input` | DEFLATE payload 的 compressed bit range |
| `Output` | Inflated output byte range |

宽度允许时增加：

| 列 | 内容 |
|---|---|
| `Events` | Literal + Match + EOB 等语义事件数量 |
| `Scanlines` | 覆盖的 pass/scanline 范围；不能准确映射时显示 `mixed` 或 `—` |

不要默认显示含义不清的 `Ratio`。若后续确需显示，命名为 `Savings`，公式必须固定为
`1 - ceil(inputBits / 8) / outputBytes`，输出为 0 时显示 `—`；它属于 P2。

### 7.4 Block 详情

所有 Block：

- BFINAL、BTYPE 和原始 header bits；
- 精确 input bit range、zlib byte range、PNG file range(s)；
- output byte range；
- 当前 selection 与 Block 的关系；
- 是否跨 IDAT Chunk 边界。

Dynamic Huffman Block 额外显示：

- `HLIT`、`HDIST`、`HCLEN` 的原始字段值和解释后数量；
- code-length、literal/length、distance 有效 symbol 数量；
- Literal、Match、EOB 事件数量。

Fixed Huffman Block 额外显示：

- `Uses the RFC-defined fixed literal/length and distance tables`；
- Literal、Match、EOB 事件数量。

Stored Block 额外显示：

- header 后 alignment padding bits；
- LEN、NLEN 及一致性结果；
- stored payload input/output 范围。

Reserved/Error Block：

- 显示 BTYPE=`11`；
- 标出准确停止 bit；
- 保留此前成功 Block，不清空整表。

### 7.5 选择与高亮

- 单击 Block 更新全局 `selectedBlockId`。
- 双击或 `Enter` 进入 `Decode Trace` 并定位该 Block 第一个事件。
- `Open Huffman` 仅对 Fixed/Dynamic 启用；Stored 显示无 Huffman 的解释。
- 当前像素命中的 Block 使用浅黄色整行背景；键盘选中仍使用原生 selection。
- 当前关联背景与用户 selection 必须保持可区分。
- 当当前输出跨多个 Block 时，各 Block 都使用浅黄色整行背景，主 Block 为最早相交 Block。

## 8. 子页 B — Huffman

### 8.1 页面问题

回答：

> 当前 Block 用哪套码表？每个实际 bit pattern 会得到什么 symbol？

Huffman 页面展示规则，不负责重复列出完整 token 时间线。

### 8.2 页面布局

```text
┌─ Block #2 · Dynamic Huffman ─────────────────────────────────┐
│ HLIT 286 · HDIST 18 · HCLEN 16                              │
│ Table:  Literal / Length | Distance | Code Length            │
├──────────────────────────────────────────────────────────────┤
│ Symbol  Meaning       Bits  Canonical  Read order  Uses*     │
│ 65      literal A       4   0110       0110          12      │
│ 256     end-of-block    7   1111110    0111111        1      │
│▶268     length 17–18    6   110101     101011         3      │
├─ Symbol 268 ─────────────────────────────────────────────────┤
│ Base length 17 · 1 extra bit                                 │
│ Used by events #35, #71, #98                                 │
│ Current occurrence #35 · input bits 922–928                  │
│                                      Open occurrence →       │
└──────────────────────────────────────────────────────────────┘
```

`Uses*` 在 P0 中表示 `Uses in current trace`，不是 whole-block count；标题或 tooltip 必须
显示 scope。只有另有完整且预算化的 Block occurrence index 时，才允许省略 scope。

`Table` 是页面内的局部数据选择器，不是第四级 Inspector 导航。默认选择
`Literal / Length`。切换 Table 保留 Block，不改变 X/Y、Hex source 或其他页面状态。

### 8.3 Table 类型

#### Literal / Length

至少显示：

| 字段 | 说明 |
|---|---|
| Symbol | 0–285；286–287 若出现必须标为 reserved/invalid |
| Meaning | literal byte、end-of-block 或 length range |
| Bit length | 0 表示未进入当前 codebook；默认可不列 0-length row |
| Canonical code | canonical code |
| Read-order bits | 实际解码读取顺序 |
| Uses in result | 当前 bounded Deep Trace Window 中实际解码次数；不得冒充整块次数 |

#### Distance

Meaning 必须展开为 distance base/range 和 extra-bit 数量。Reserved distance symbol 必须
明确标记，不显示为普通距离。

#### Code Length

仅 Dynamic Block 有效。按 DEFLATE 指定的 code-length alphabet 顺序展示 symbol、
bit length、code、uses，并对 16/17/18 显示其重复语义。详情区展示 code-length
sequence 的 repeat 展开结果或关联 trace event，不在表格单元格中塞入长数组。

### 8.4 Fixed、Stored 与损坏状态

- Fixed：显示 `Fixed Huffman table`；只列 core 已提供的码表项和 bounded Uses，不在 Qt
  中重建整张 RFC 码表。
- Stored：正文显示：

  ```text
  Block #1 is stored without Huffman coding.
  Inspect its LEN/NLEN and byte range in DEFLATE Blocks.
  ```

- Reserved/Error：说明无法建立有效 codebook，并保留可用的 header 事实。
- Dynamic codebook 构建中途失败：显示已读取字段和失败 bit，不伪造不完整码表为有效结果。

### 8.5 Symbol occurrence 导航

- 选中 symbol 后，详情区显示带 scope 的 `Uses in current trace`、first/last bounded event
  和当前 occurrence。
- `Open occurrence` 跳到 Decode Trace 中：
  1. 与当前 output selection 相交的 occurrence；否则
  2. 当前 bounded result 中已记住的 occurrence；否则
  3. 当前 bounded result 的第一次 occurrence。
- `Previous/Next occurrence` 可作为 P1 的紧凑动作，不作为首版 P0 必需按钮。
- codebook row 本身没有唯一 file range，不能让 `Show in Hex` 随意跳到“定义位置”。
  Code Length 表中若 row 确实对应动态 header event，则应通过 occurrence/event 跳转。

## 9. 子页 C — Decode Trace

### 9.1 页面问题

回答：

> 解码器按什么顺序消费输入位，并产生了哪些输出？当前输出字节为什么是这些值？

主表按语义事件展示，不默认拆成逐 bit 行。

### 9.2 页面布局

```text
┌─ Block #2 trace ─────────────────────────────────────────────┐
│ Current output 1,573–1,575 · events #35–#36                 │
│ Step  Input bits  Event                 Output               │
│ 34    918–922     Literal 0x41          1,568                │
│▶35    922–937     Match len 18 / dist 7 1,569–1,586          │
│ 36    937–944     End of block          —                    │
├─ Event #35 · Length/Distance Match ──────────────────────────┤
│ 1  Literal/length symbol 268                                 │
│ 2  Base 17 + extra bits 1 = length 18                        │
│ 3  Distance symbol 5                                         │
│ 4  Base 7 + extra bits 0 = distance 7                        │
│ 5  Copy output [1,562, 1,580) → [1,569, 1,587)              │
│    Overlapping copy: yes                                     │
│ Current byte 1,573 = match offset +4, copied from 1,566      │
└──────────────────────────────────────────────────────────────┘
```

### 9.3 事件类型

P0 只要求与当前 bounded token decoder 和交互原型一致的语义事件：

- `Literal`；
- `LengthDistanceMatch`；
- `EndOfBlock`；
- `StoredRun`：只有 core 已能安全聚合时才使用，否则保留 bounded literal 行；
- `DecodeError`：错误类别、停止 input bit 和已产生 output range。

Block header、BFINAL/BTYPE、Stored LEN/NLEN、Dynamic HLIT/HDIST/HCLEN 属于 Block/Huffman
详情，不要求为了时间线再复制一套 event。`ZlibHeader`、alignment、codebook build phase、
Adler trailer 等完整 decoder phase timeline 作为独立 P2 候选，不属于本轮 UI 一致性修复；
若未来加入，必须保持 bounded/on-demand，且 wrapper byte 域与 DEFLATE payload bit 域分列。

### 9.4 Trace 主表字段

最低宽度：

| 列 | 内容 |
|---|---|
| `Step` | Block 内或 stream 内稳定事件序号；必须明确采用哪一种 |
| `Input` | DEFLATE bit range，wrapper event 标明 `zlib bytes` |
| `Event` | 人类可读的 Literal、Match、EOB、Header 等摘要 |
| `Output` | Inflated output byte range；无输出显示 `—` |

详情区显示而非强塞入主表：

- raw/read-order bits；
- Huffman symbol、code length；
- extra bits 及 base + extra 的计算；
- match source range、target range、overlap；
- 与 current output 相交的 byte 及 match 内偏移；
- pass、scanline、row byte、Filter byte 或 sample/channel 映射；
- zlib byte range 和 PNG file range(s)。

### 9.5 Literal 详情

```text
Literal/length code: symbol 65
Read-order bits: 0110
Output: byte 1,568 = 65 (0x41)
Mapping: pass 0 · scanline 16 · row byte 32 · channel G
```

只有映射真实存在时才显示 channel；不得根据最终 RGBA 猜测。

### 9.6 Match 详情

必须完整展示：

```text
Literal/length symbol: 268
Length: base 17 + extra 1 = 18
Distance symbol: 5
Distance: base 7 + extra 0 = 7
Source: output [1562, 1580)
Target: output [1569, 1587)
Overlapping copy: yes
```

若当前 output selection 与 target 相交，再显示每个当前 byte 的 provenance：

```text
output 1573 = target offset +4 ← source logical offset 1566
```

对 overlap copy，“source logical offset”必须遵循 DEFLATE 逐字节复制语义。不得简单把
整个 source range 当作复制前已存在的独立、不重叠 buffer。

### 9.7 Current 与手动选择

- 表格使用原生 selection 表示用户当前选中的 event。
- 与 X/Y output range 相交的事件使用浅黄色整行背景。
- `Jump to current output` 仅在用户已滚离 current event 时显示为紧凑文本动作。
- 用户手动浏览另一个 Block/event 后，普通 X/Y 刷新不应强行抢走选择；只有明确的
  图像点击、坐标提交或 Lock 规则要求 follow 时才重新定位。
- follow 行为沿用全局 Lock 语义，不新增第二个意义相近的 Auto-follow checkbox。

## 10. 三页与外部区域的状态联动

### 10.1 稳定 selection model

语义上至少包含：

```text
CompressionSelection
├─ streamGeneration
├─ selectedBlockId?
├─ selectedCodebookKind?
├─ selectedSymbol?
├─ selectedEventId?
├─ currentOutputRanges[]
├─ currentBlockIds[]
├─ currentEventIds[]
└─ navigationOrigin
```

`current*` 表示 X/Y/scanline 上下文，`selected*` 表示用户正在浏览的对象。两者可以不同。
不得用一个 table currentIndex 同时承担这两种语义。

### 10.2 导航矩阵

| 来源 | 动作 | 目标结果 |
|---|---|---|
| 图像/X/Y | 明确选择坐标 | 更新 current output/block/events；Lock 开启时定位主 event |
| Blocks row | Open Huffman | 切 Huffman，保留 block，选择默认 table |
| Blocks row | Open Trace | 切 Decode Trace，定位 Block 首事件或 current event |
| Huffman row | Open occurrence | 切 Decode Trace，定位确定的 occurrence |
| Trace row | Show in Hex | Hex source=`File` 或 `IDAT Stream`，选择准确 input ranges |
| Trace row | Show inflated output | Hex source=`Inflated`，选择 output range |
| Central Filtered | Show compression origin | 切 Compression/Trace，定位产生当前 range 的事件 |
| 左侧 IDAT Chunk | Select | Compression 保持完整 stream；附加 chunk-range context |
| 左侧非 IDAT Chunk | Select | 不清空、不重置 Compression selection |

### 10.3 循环更新防护

- 所有 selection/navigation request 必须携带 origin 或使用等价 reentrancy guard。
- Hex selection 更新不得再次触发相同 Compression event 的无限跳转。
- 切换子页不改变 stream generation。
- 加载新文件后，旧 generation 的异步结果和 selection request 必须被丢弃。

## 11. 只读分析数据契约

以下是语义契约，不强制具体 C++ 类型名。实现必须分成 generation 级
`FastCompressionIndex` 与 selection 级 `DeepTraceWindow`：Stream summary 和完整 Block
列表属于前者；codebook 和 Decode event 属于后者，必须携带 query scope、budget、
completion status。下列结构不得被解释成默认生成并长期保留 whole-file event list。

### 11.1 Stream summary

```text
ZlibStreamSummary
├─ cmf, flg
├─ compressionMethod
├─ windowSize
├─ fcheckValid
├─ presetDictionaryFlag
├─ zlibByteCount
├─ idatChunkCount
├─ inflatedByteCount
├─ expectedAdler32
├─ actualAdler32
├─ checksumStatus
├─ completionStatus
└─ diagnostic?
```

### 11.2 Block summary

```text
DeflateBlock
├─ stableBlockId
├─ bfinal
├─ type
├─ inputBitRange
├─ zlibByteRange
├─ fileRanges[]
├─ outputByteRange
├─ header / stored / dynamic metadata
├─ literalCount
├─ matchCount
├─ boundedEventRange? / traceAvailability
└─ completionStatus
```

### 11.3 Codebook

```text
HuffmanCodebook
├─ blockId
├─ kind: code-length | literal-length | distance
├─ source: fixed | dynamic
├─ entries[]
│  ├─ symbol
│  ├─ semantic
│  ├─ bitLength
│  ├─ canonicalCode
│  ├─ readOrderBits
│  ├─ useCountInResult
│  └─ boundedOccurrenceIndex
├─ queryOutputRange
├─ completeForSelectedBlock
└─ completionStatus
```

### 11.4 Decode event

```text
DeflateEvent
├─ stableEventId
├─ blockId
├─ type
├─ inputDomain
├─ inputBitOrByteRange
├─ zlibByteRange
├─ fileRanges[]
├─ outputByteRange?
├─ symbol / rawBits / extraBits?
├─ literalValue?
├─ length / distance?
├─ matchSourceRange?
├─ overlap?
├─ scanlineMapping?
├─ queryOutputRange
└─ diagnostic?
```

### 11.5 查询索引

必须提供只读查询，而不是让 GUI 线性扫描全部 trace：

- `blockAtInputBit(bit)`；
- `blocksIntersectingOutput(range)`；
- `eventsForBlockWindow(blockId, outputRange, rowRange, budget)`；
- `eventsIntersectingOutput(range)`；
- `boundedOccurrences(blockId, codebookKind, symbol, queryScope)`；
- `fileRangesForZlibRange(range)`；
- `scanlineMappingForOutput(range)`。

选择/切页不得重新解析 raw trace string。GUI 不能从展示文本中反向提取任何字段。

## 12. 性能与生命周期

- 文件打开时生成并发布一次不可变 Fast Compression Index；它包含 wrapper/IDAT/checksum
  摘要和完整 Block 列表，但不包含 whole-file token trace。
- 只有明确提交的 X/Y/Lock 或 `Open Decode Trace` 操作才生成 bounded、可取消、带 budget
  的 Deep Trace Window；切页、选行、调整宽度和 DEC/HEX 不触发 replay。
- 先发布 stream/block 摘要；Deep Trace 异步期间保留 Fast Index 和上一份同 generation
  verified window，并显示明确 Loading/Partial scope。
- 使用 `QAbstractTableModel` 或等价虚拟化 model/view；禁止每个 event 创建一个 QWidget。
- 大 trace 不一次性生成每行完整格式化字符串；按可见 row/详情按需格式化。
- Match 只保存 source/target range 和必要 provenance，不复制输出 payload。
- raw bit string 只为选中事件按需产生，避免为全部事件保存重复文本。
- Block/output index 在 Fast Index 构建时形成；occurrence 只能索引 bounded result，除非
  另有独立、预算化且获批的整块索引。禁止每次移动 X/Y 在 GUI 全表扫描。
- 保持 WP-5U0/WP-5U9 的 generation/stale-result 规则。
- Release 构建不得依赖 Debug-only trace 字符串；面向产品的结构化 trace 是正式能力。
- 若产品设置安全上限，达到上限时必须返回 `Partial` result、已覆盖范围和原因；不得退化
  成无说明的 `no trace`。

## 13. 状态、错误与降级

### 13.1 页面状态

| 状态 | 用户可见内容 |
|---|---|
| 未加载 PNG | `Open a PNG to inspect its compressed IDAT stream.` |
| 分析中 | `Analyzing the zlib/DEFLATE stream…`，保留已可用摘要 |
| 无 IDAT | `This PNG does not contain IDAT data.` |
| 无当前坐标 | 完整 Blocks 可浏览；上下文提示选择像素是可选动作 |
| Stored Block/Huffman | 明确说明该 Block 不使用 Huffman |
| 无 occurrence | `This symbol is defined but not used by Block #n.` |
| Partial trace | 显示已覆盖 Block/event 和停止原因 |
| Truncated stream | 显示最后成功 bit/output，保留此前内容 |
| BTYPE=11 | 显示 Reserved Block Type 和停止 bit |
| Invalid distance | 显示 symbol、distance、当前 output size 和停止 bit |
| Adler-32 mismatch | Block/trace 仍可浏览，Stream summary 显示 expected/actual |
| 功能未编入当前构建 | `Structured DEFLATE trace is unavailable in this build.` |

### 13.2 错误文案规则

- 页面正文不直接显示内部枚举名、异常栈或 `no trace`。
- 首行给人类可理解的结论；详情可复制准确 offset、expected/actual 和内部错误 code。
- 一个后期错误不得抹掉此前成功分析的 Block、codebook 或 event。
- 不能精确映射到 scanline/channel 时显示 `mapping unavailable`，不得猜测。

## 14. 视觉、主题与可访问性

- 保持当前 Qt/macOS 原生密度，不把 Inspector 改成网页式 dashboard。
- 页面标题、表头、详情标题形成三级层级；同级字体和间距一致。
- 表格使用原生 row selection、键盘上下移动、Home/End、Page Up/Down。
- `Enter` 执行默认钻取，`Esc` 不清空全局 current context。
- 所有值和详情文本可选择、复制；建议支持复制选中行和复制详情。
- 数值、offset、bit sequence 使用等宽字体；说明文字使用 UI 字体。
- Current、用户 Selection、Error 至少同时使用文字/图标/边框差异，不能只靠颜色。
- 浅色和深色主题均使用集中 theme token，不散落硬编码 RGB。
- Focus ring 不得被 Current 高亮覆盖。
- 表格和上下文条提供可访问名称，例如 `DEFLATE block 2, dynamic, final`。
- 不用符号 `✓/✕` 作为唯一语义；辅助技术应读出 `valid/invalid/not checked`。

## 15. 推荐实施拆分

### P0-A — 能力审计与统一偏移契约

- 完成第 5 节能力矩阵。
- 固定 File/zlib/DEFLATE/Inflated 四域 offset 语义。
- 确认现有 Inflate 是否可安全产生结构化观察结果。

### P0-B — Compression core result 与索引

- 提供 generation 级 stream/full-block Fast Index projection。
- 保留现有 selection 级 bounded codebook/event result，不扩大默认 replay 范围。
- 提供 output-range → block、bounded event 和 bounded symbol occurrence 查询。
- 覆盖 stored/fixed/dynamic 和 partial/error。

### P0-C — 共同 selection 与导航

- 分离 current context 和 manual selection。
- 接通 X/Y、scanline、Hex 和 Compression 三页跳转。
- 增加 generation 与循环更新防护。

### P0-D — DEFLATE Blocks 页面

- 完成 stream summary、虚拟化 Block 表和详情。
- 完成 Stored/Fixed/Dynamic/Reserved 状态。
- 完成 Current 与用户 Selection 的双重语义。

### P0-E — Huffman 页面

- 完成三类 codebook、canonical/read-order 分列和带 bounded scope 的 Uses。
- 完成 Fixed/Stored/partial 状态及 occurrence 导航。

### P0-F — Decode Trace 页面

- 完成语义事件表、Literal/Match 展开和 input/output/file mapping。
- 完成 current event、manual event、Hex 与 Inflated 导航。

### P1-G — 大文件性能、主题与可访问性

- 完成 table virtualization、按需格式化和性能基线。
- 完成窄宽、深色主题、键盘与复制验证。

### P1-H — 自动化与完整回归

- core golden tests、model tests、GUI tests、损坏 corpus 和跨页回归。

P0-A/B 必须先于页面正文；页面不得先通过解析 debug string 或硬编码示例“做出效果”。

## 16. 测试要求

### 16.1 Core golden tests

至少准备或复用可追溯来源的微型 fixture：

- 单个 Stored Block；
- 单个 Fixed Huffman Block；
- 单个 Dynamic Huffman Block；
- 多 Block 且 BFINAL 只在最后一个；
- Block 跨两个 IDAT Chunk；
- Literal-only；
- 包含非重叠 Match；
- 包含 overlap Match；
- Dynamic header 使用 code-length repeat 16、17、18；
- 空 distance tree 合法边界（如适用）；
- truncated header/token；
- BTYPE=11；
- invalid distance；
- Adler-32 mismatch。

逐 fixture 验证：

- exact input bit ranges；
- output byte ranges；
- Block type/BFINAL；
- codebook bit lengths/codes/read order；
- literal/match/EOB 序列；
- length/distance extra-bit 计算；
- match source/target 与 overlap；
- zlib → file range mapping；
- partial/error stop location。

### 16.2 Output/scanline mapping tests

至少覆盖：

- non-interlaced RGB 8-bit；
- RGBA 8-bit；
- grayscale；
- 16-bit sample；
- packed sample/indexed color；
- 第一行 Filter byte 和每行第一个 sample；
- Adam7 多 pass；
- 一个 pixel selection 跨多个 decode event；
- 一个 Match 覆盖多个 scanline；
- 无法精确映射时的明确降级。

### 16.3 GUI/model tests

- 打开文件默认进入 Blocks 并显示非空 summary/table。
- Blocks → Huffman → Trace 钻取保持同一 Block。
- 切回 Blocks 保留原选择和滚动上下文。
- 选中 Stored Block 时 Huffman 显示正确说明。
- canonical code 与 read-order bits 不混列。
- symbol occurrence 跳到正确 event。
- 图像/X/Y 选择高亮相交 Block/events。
- Manual selection 与当前关联行浅黄色背景可同时存在。
- `Show in Hex` 选择准确 file/IDAT range。
- `Show inflated output` 使用正确 source/range。
- 选择非 IDAT Chunk 不清空 Compression。
- DEC/HEX 切换只改变数值格式，不改变 raw bits。
- 320–380 px 宽度下页面可用且不撑宽 Dock。
- 大 trace 滚动不创建逐行 QWidget，不阻塞主线程。
- 新文件加载时旧 generation 结果不污染新页面。
- Loading、Partial、Error、Adler mismatch 等文案准确。

### 16.4 回归

- PNG 打开、关闭、重新加载和快速切换。
- Chunk List、Reconstruction、Pixels/Filtered/Defiltered。
- Image click、X/Y、Lock、DEC/HEX。
- Hex 的 File、IDAT Stream、Inflated、Defiltered source。
- Inspector `Reconstruction | Compression` 工作区恢复。
- Debug 与 Release 构建。
- 浅色/深色主题、macOS 高分屏和普通缩放。
- 无重复信号、越界访问、主线程长阻塞和关闭时崩溃。

## 17. 验收场景

### 场景 A：从像素追到 Match

1. 打开包含 Dynamic Block 和 LZ77 Match 的 PNG。
2. 在图像中选择一个由 Match 产生的像素字节。
3. 进入 Compression。
4. Blocks 高亮相交 Block，并显示 current output range。
5. Decode Trace 定位相交 Match。
6. 详情准确显示 length、distance、source/target、overlap 和当前 byte provenance。
7. `Show in Hex` 跳到压缩输入；`Show inflated output` 跳到输出。

### 场景 B：从码表追到 occurrence

1. 选择 Dynamic Block。
2. 打开 Huffman 的 Literal/Length table。
3. 选择一个 `Uses in current trace > 0` 的 length symbol。
4. Open occurrence 定位到正确 Match event。
5. 返回 Huffman 后 Block、symbol 和 occurrence 保持。

### 场景 C：跨 IDAT Chunk

1. 打开一个 Block/token 输入范围跨 IDAT Chunk 边界的 PNG。
2. Block/Trace 显示一个连续逻辑范围和多个 file ranges。
3. Hex 跳转不包含 Chunk length/type/CRC 字节，只选择真实 IDAT data ranges。

### 场景 D：错误后保留部分结果

1. 打开 Adler mismatch 或中途 truncated 的测试文件。
2. Stream summary 显示错误。
3. 错误点之前的 Block、码表和事件仍可浏览。
4. 停止 input bit/output offset 准确，页面不显示笼统 `no trace`。

## 18. 交付物

- 完整实现代码和聚焦测试。
- 第 5.1 节实际能力矩阵。
- File/zlib/DEFLATE/Inflated offset 契约说明。
- Compression analysis result 与查询接口说明。
- 三页 selection/navigation 状态图。
- 性能数据：至少一个小文件和一个大 trace 的分析耗时、事件数、峰值内存、首屏时间。
- 修改前后截图，至少包含：
  - Blocks 正常状态及 current 标记；
  - Dynamic Huffman Literal/Length table；
  - Match event 展开；
  - Stored Block 的 Huffman 状态；
  - 跨 IDAT Chunk 的 file ranges；
  - partial/error 状态；
  - 320–380 px 窄 Inspector；
  - 深色主题。
- 手工回归记录及未覆盖限制。

## 19. 完成定义

只有同时满足以下条件，WP-5U12 才可标记完成：

- 三个子页分别稳定回答“块结构、码表规则、解码过程”，内容无重复堆砌。
- 打开有效 PNG 后不再出现无解释的 `Block trace: no trace`。
- Stored、Fixed、Dynamic 和错误 Block 都有正确内容或明确状态。
- canonical Huffman code 与 read-order bits 被清晰区分并经 golden test 验证。
- Literal、Match、EOB 事件的 input/output range 准确。
- Match 的 length、distance、source/target、overlap 和当前 byte provenance 准确。
- 所有 IDAT 作为一个逻辑 zlib stream 分析，跨 Chunk 映射准确。
- 当前像素可反向定位到 output range、event 和 Block；不能精确映射时明确降级。
- Blocks/Huffman/Trace/Hex/Inflated 的双向跳转准确且无循环更新。
- Current context 与用户手动 selection 可同时存在并能辨识。
- 页面切换和坐标变化不重新解析、不重新 Inflate、不线性扫描完整 trace。
- 完整 Blocks 表使用虚拟化/惰性 model；bounded Huffman/Trace 可保留经对象数和响应时间
  门禁证明安全的 model/view 实现。窄 Inspector、深浅主题、键盘和复制均可用。
- partial/error 保留此前成功事实，不伪造、不清空、不暴露原始调试字符串。
- 相关 core、model、GUI 和回归测试全部通过。

## 20. 规范性 UI 实现契约

本节用于约束开发 Agent 的视觉自由度。第 6～9 节线框定义信息结构，本节定义组件层级、
尺寸范围、列优先级、状态外观和响应式行为。除平台原生字体栅格、系统控件边框和主题
产生的合理差异外，未经产品评审不得偏离本节。

### 20.1 规范性与建议性边界

以下内容是规范性要求：

- 页面组件层级及从上到下的顺序；
- 两行共同上下文；
- 三页主表与详情区的分工；
- Current、Selection、Current+Selection 的同时表达；
- 必需字段、默认列顺序和窄宽列优先级；
- 320、360、480、600 px 下的响应式行为；
- 正常、Loading、Empty、Stored、Partial、Error 状态；
- 底部动作的语义、顺序和启用条件；
- 文字标签和数值/bit 显示规则；
- 不撑宽 Inspector、不裁切主要动作、不产生无说明空表。

以下内容允许跟随平台/现有主题：

- 字体 family 和具体字形栅格；
- Qt/macOS 原生 focus ring、scrollbar、checkbox 和 segmented control 细节；
- 由集中 theme token 解析出的确切 RGB；
- 1–2 个逻辑像素以内的边框、行高和间距调整，但不得改变层级或造成裁切。

### 20.2 固定组件树

```text
CompressionInspector
├─ CompressionSubTabBar
│  ├─ DEFLATE Blocks
│  ├─ Huffman
│  └─ Decode Trace
├─ CompressionContext
│  ├─ StreamSummaryLine
│  └─ CurrentMappingLine
├─ CompressionStack
│  ├─ BlocksPage
│  │  ├─ BlocksTable
│  │  └─ BlockDetails
│  ├─ HuffmanPage
│  │  ├─ BlockAndTableSelector
│  │  ├─ HuffmanTable
│  │  └─ SymbolDetails
│  └─ DecodeTracePage
│     ├─ TraceTable
│     └─ EventDetails
└─ CompressionNavigationActions
   ├─ Show in Hex
   └─ Show inflated output
```

- 不在 StreamSummaryLine 上方增加统计卡、搜索条或页面局部 DEC/HEX。
- 不把 Block details、Symbol details 或 Event details 改成弹窗作为唯一入口。
- 不把三个页面合并成长滚动报告，也不增加第四个 Compression 子标签。
- `CompressionNavigationActions` 若由 WP-5U11 的共享 Hex 工具区承载，原位置不留空
  footer；组件树语义保持，视觉上只出现一组动作。

### 20.3 基础 geometry

以下均为逻辑像素，允许平台主题产生 ±2 px 的合理差异：

| 元素 | 目标 | 允许范围 |
|---|---:|---:|
| 子标签栏控件高度 | 28 | 26–30 |
| 页面水平内边距 | 12 | 10–14 |
| 上下文区垂直内边距 | 8 | 6–10 |
| 上下文两行间距 | 4 | 3–6 |
| Section 标题与正文间距 | 8 | 6–10 |
| Table header 高度 | 28 | 26–31 |
| Table row 高度 | 28 | 26–32 |
| Detail header 高度 | 30 | 28–34 |
| 主表与详情区分隔线命中高度 | 6 | 5–8 |
| 底部动作区高度 | 44 | 40–48 |
| 按钮高度 | 28 | 26–32 |

- 使用当前应用 UI 字号；页面不得自行整体缩小字体以塞入更多列。
- 普通正文和表格不得低于系统默认 UI 字号的 90%。
- bit、code、offset 和公式使用现有等宽字体 token。
- 表格与详情区使用同一个垂直 splitter 或等价布局；默认约 `55:45`，每区最小可用高度
  以至少显示 4 行主表和 4 行详情为准。
- 用户调整详情比例后，同一文件和同一次应用运行中切换子页不得无故重置。

### 20.4 参考宽度与响应式行为

`480 px` 是规范参考宽度，`360 px` 是最低常用宽度，`320 px` 是降级宽度。

#### 600 px 及以上

- 共同上下文原则上保持每项一行；内容过长仍可换行。
- 显示各表 P0 全部列。
- 详情 label/value 使用两列布局。

#### 420–599 px

- Stream summary 和 Current mapping 各自最多自然换成两行。
- Blocks 隐藏 `Scanlines`，保留 `Events`。
- Huffman 保留所有必需列；必要时只让表格内部横向滚动。
- Trace 保留 Step/Input/Event/Output。

#### 360–419 px

- Compression 子标签使用原生横向滚动或溢出，标签文字不得被省略成无法辨认的相同前缀。
- Blocks 隐藏 `Events`、`Scanlines`；保留 Current/#/Type/Final/Input/Output。
- Huffman 首屏保留 Symbol/Meaning/Bits/Read order；Canonical 和 Uses 可在表格内部横向滚动，
  选中 row 的详情必须同时显示 canonical/read-order，避免语义丢失。
- Trace 保留 Current/Step/Input/Event/Output；详情承担长内容。
- 底部两个动作保持可点击且标签完整，可等宽并排。

#### 320–359 px

- 允许上下文行、detail label/value 和 footer 动作自然换行。
- 允许表格内部横向滚动，但不得提高 Inspector minimum width。
- footer 两个动作在横向标签放不下时改为纵向堆叠，顺序仍为 `Show in Hex`、
  `Show inflated output`。
- 页面不得通过隐藏当前关联行、Input、Event 或 Output 语义换取宽度。

小于 320 px 不作为完整可用目标，但仍不得崩溃、无限扩宽或覆盖相邻 Dock。

### 20.5 默认列顺序与 resize 优先级

#### Blocks

```text
Current | # | Type | Final | Input bits | Output bytes | Events | Scanlines
```

- `Current`、`#`、`Final` 使用紧凑固定宽度。
- `Type` 使用内容宽度并设置合理上限。
- `Input bits`、`Output bytes` 优先共享剩余宽度。
- `Events`、`Scanlines` 为第一批响应式隐藏列。

#### Huffman

```text
Symbol | Meaning | Bits | Canonical | Read order | Uses in result
```

- `Meaning` 是主要 stretch 列。
- Canonical 与 Read order 必须同时存在于页面语义中；不能合并为 `Code`。
- `Uses in result` 最先进入横向滚动区域；其 tooltip/accessibility 必须声明 bounded
  query scope，不能用隐藏该列的方式改变 occurrence 行为。

#### Decode Trace

```text
Current | Step | Input bits | Event | Output
```

- `Event` 是主要 stretch 列。
- `Input bits` 和 `Output` 不得因宽度变成无单位、无域的裸数。
- 额外 bits、symbol、source/target、scanline mapping 全部进入详情，不继续横向加列。

### 20.6 状态视觉契约

#### Current

- 使用专用紧凑列显示 `●` 或项目统一的 Current 图标。
- 必须提供 accessible text `contains current output`。
- 可使用轻微主题色背景，但不能与原生 row selection 相同。

#### Selection

- 使用 Qt/主题原生选中行背景和 focus ring。
- Selection 改变详情内容；Current 不一定改变详情内容。

#### Current + Selection

- 同时保留 Current 图标和原生 selection。
- 不得因为 row 被选中而隐藏 Current 图标，也不得用第三种整行高饱和颜色覆盖 focus。

#### Error / Partial

- Stream summary 或详情首行显示文字状态和错误图标。
- Error 颜色只作辅助，必须保留错误类型和 offset 文本。
- Partial 保留可浏览表格；不得把整个页面替换成居中错误插画。

#### Loading / Empty

- Loading 保留已可用的 Stream/Block 摘要，不用大面积骨架屏模拟未知表格。
- Empty 状态使用一段短文字和一个明确原因；不显示空表头加 `no trace`。
- 无当前像素不是 Empty：Blocks/Huffman/Trace 仍可手动浏览。

### 20.7 文案锁定

首版英文 UI 使用以下标签，除全局本地化策略外不得随意改写：

```text
DEFLATE Blocks
Huffman
Decode Trace
Show in Hex
Show inflated output
Literal / Length
Distance
Code Length
Canonical
Read order
Input bits
Output bytes
Current
```

- 不再使用 `Show in DEFLATE`：压缩输入统一为 `Show in Hex`，Inflated 输出统一为
  `Show inflated output`。
- 不使用 `Block trace: no trace`。
- 不把 `Huffman` 改成含义不同的 `Tree`，也不把 `Decode Trace` 改成只有 token 的
  `Tokens`。

### 20.8 视觉基线与变形门禁

开发流程必须先用固定 fixture 完成静态三页，再绑定真实数据。静态页面在以下组合中保存
基线截图：

```text
Blocks: 360 / 480 / 600 px, light
Huffman: 360 / 480 / 600 px, light
Decode Trace: 360 / 480 / 600 px, light
三页: 360 / 480 px, dark
Stored Huffman、Loading、Partial/Error: 360 / 480 px
Current + Selection: Blocks 和 Trace 各一张
```

截图差异测试可以容忍平台字体抗锯齿、原生 scrollbar 和 1–2 px 边框差异，但以下变化
不得通过扩大 tolerance 忽略：

- 标签、表头、按钮或详情文字被裁切；
- 主表/详情顺序改变；
- 必需列消失或列顺序改变；
- Current 与 Selection 无法同时辨认；
- 上下文条被移除或变成多张统计卡；
- footer 重复、覆盖内容或动作顺序改变；
- Inspector minimum width 被内容撑大；
- 错误状态清空此前有效表格；
- 360/480 px 下出现控件重叠。

任何规范性偏差必须在实现前记录原因、替代方案和影响，并经过产品评审；开发 Agent
不能仅以“更符合 Qt 默认行为”或“实现更简单”为由自行改变。

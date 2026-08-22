# WP-5U0 UI 规格、状态矩阵与验收样本

> Work Package: `WP-5U0`
> Milestone: M5 UI
> Status: frozen and approved for implementation
> Scope: docs-only
> Depends on: WP-306、WP-406、WP-504

本文档冻结坐标驱动 PNG 分析工作台的第一版交互契约。它是后续
`WP-5U1`、`WP-5U2`、`WP-5U3*`、`WP-5U4*` 和 `WP-5U5*` 的输入；不新增
生产 API，不改变 ADR，也不授权 GUI 解析 PNG 或 DEFLATE。

## 1. 目标与边界

### 1.1 本工作包交付

- 左侧 Chunk List、中央 Preview/Hex、右侧坐标工具栏/Inspector 的低保真布局。
- 默认尺寸、最小尺寸、缩窗退化、dock/splitter、浮动、持久化和 Reset Layout 规则。
- Selection、hover/locked coordinate、Chunk 选择和 Hex 驱动权的状态转移表。
- 五个基础 Preview 标签及条件标签的适用性矩阵。
- Filtered 数值、DEC/HEX 和 bit 字段的显示语义与图例。
- loading、not applicable、replaying、partial、error、cancelled 的文案和恢复动作。
- 受控 UI 验收样本矩阵和可重复的性能测量预算。

### 1.2 非目标

- 不修改 C++、CMake、Qt widget、analysis engine 或测试实现。
- 不实现 Compare、First Difference、Statistics UI、APNG UI 或视觉主题精修。
- 不新增外部 PNG、第三方代码或未经 manifest 登记的 corpus 文件。
- 不把 hover 变成 Deep Trace 请求，也不把完整阶段长期物化为多张全尺寸 `QImage`。

### 1.3 不变量

1. `libs/` 保持 Qt-free；GUI 只消费 immutable analysis model。
2. IDAT 仍是 Virtual IDAT Stream，不创建与全部 payload 等大的拼接缓冲区。
3. Deep Trace 仅按 selection/block/row 按需 replay；默认路径不保存全文件 token trace。
4. 一个 selection 可以同时保留 image、node、logical span 和多个 physical/bit spans；
   不理解某个维度的面板不得清空该维度。
5. 所有阶段和 provenance 的状态必须能映射到已有或明确计划中的 Qt-free 数据接口。
6. 文件读取、解码和 replay 不在 UI 线程执行；旧 document generation 的结果不得发布。

## 2. 控件布局与 Workspace

### 2.1 默认布局

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ Menu / toolbar                                                           │
├───────────────┬──────────────────────────────────────┬───────────────────┤
│ Chunk List    │ Preview                              │ X / Y             │
│ (dock)        │ ┌──────────────────────────────────┐ │ [lock] [DEC/HEX]  │
│               │ │ Image | Pixels | Filter Map | … │ │ [Hex follows pixel]│
│               │ ├──────────────────────────────────┤ ├───────────────────┤
│               │ │ viewport / pixel selection       │ │ Inspector tabs     │
│               │ │                                   │ │ Reconstruct        │
│               │ └──────────────────────────────────┘ │ Pixel / Scanline   │
│               │ Hex                                  │ Source / Format    │
│               │ ┌──────────────────────────────────┐ │                   │
│               │ │ windowed bytes + bit ruler        │ │                   │
│               │ └──────────────────────────────────┘ │                   │
└───────────────┴──────────────────────────────────────┴───────────────────┘
```

| 项目 | 冻结值/规则 |
|---|---|
| 默认窗口 | `1200×760` logical px；首次启动采用该尺寸，已有用户设置优先 |
| 最小窗口 | `900×600` logical px；不得因字体或 DPI 导致控件截断 |
| 左 Chunk dock | 默认可见，初始宽约 `260` px；可隐藏、拖动、浮动 |
| 中央 splitter | Preview/Hex 初始比例 `60%/40%`；比例属于 Workspace settings |
| 右侧区域 | 顶部坐标工具栏，主体 Inspector；初始宽约 `360` px |
| 首个 Inspector tab | `Reconstruct`，始终是第一个标签 |
| Preview 基础 tab | `Image`、`Pixels`、`Filter Map`、`Filtered`、`Defiltered`，顺序固定 |
| Compare 入口 | 本轮完全不创建菜单项、按钮、禁用项或占位标签 |

### 2.2 缩窗、浮动和持久化

- 窗口接近最小尺寸时，右侧工具栏允许换成两行；Inspector tab 允许横向滚动，
  不得缩写成无法辨认的单字母。
- Chunk dock 或 Inspector dock 浮动时，Preview/Hex 仍保持中央垂直关系；浮动状态
  和 dock area 一并保存。
- 保存内容包括主窗口 geometry、dock visibility/area/floating、splitter sizes、
  当前 Preview tab、当前 Inspector tab、numeric base 和 `hexFollowPixel`。
- settings 缺失、版本不匹配或解析失败时，整组 Workspace settings 回退到本节默认值，
  不尝试部分恢复损坏数据。
- `View → Reset Layout` 恢复默认 geometry、dock、splitter、tab 和右侧宽度；不清除
  当前文件、analysis cache 或 Selection。

键盘焦点顺序固定为：`X` → `Y` → 锁定/解锁 → `DEC/HEX` → `Hex 跟随像素` →
Preview tabs → viewport → Hex → Inspector tabs。方向键在 viewport 或锁定坐标控件
获得焦点时移动坐标；文本输入获得焦点时只移动光标。

## 3. 状态所有权与 Selection 契约

| 状态/事实 | 所属层 | 发布或更新规则 |
|---|---|---|
| 锁定 pixel、channel/sample、stage、node、logical/physical spans | `trace-model::Selection` | 跨面板共享；保留一对多和不连续 spans |
| hover `(x,y)` 与轻量值 | Preview widget 本地瞬时状态 | 不发布 Selection，不触发 Deep Trace、Hex 跳转或 Inspector 重算 |
| 当前 Chunk | 共享 Selection 的 node/physical span 维度 | 与 image 维度并存；Chunk 面板不清空 image |
| Hex source、`hexFollowPixel`、定位历史 | Qt view state | 不改变领域事实；仅改变 Hex 呈现和导航 |
| DEC/HEX | Qt 用户偏好 | 全局一致；bit 字段仍以 binary 为主 |
| dock/splitter/tab/layout | Qt settings | 可恢复默认；不进入持久领域 Selection |
| loading/replaying/error/cancelled | analysis-engine 状态经 Qt adapter 呈现 | UI 不根据字符串猜测 decoder 结果 |

### 3.1 选择事件转移

| 事件 | Selection 变化 | Hex 跟随像素开 | Hex 跟随像素关 |
|---|---|---|---|
| 图片 hover | 仅更新 widget 本地 hover | 仅显示 hover | 仅显示 hover |
| 图片单击/坐标输入 | 发布 locked image coordinate；默认不指定 channel | 跳到当前 HexSource 的关联范围 | 保持当前位置 |
| Inspector 选择 channel/sample | 在 image 上补充 channel/sample 维度 | 跳到该 sample 的 spans | 保持当前位置 |
| 方向键移动锁定坐标 | 替换 image coordinate，保留 stage 与可兼容维度 | 跳到新坐标范围 | 保持当前位置 |
| `Esc` | 清除 locked image coordinate；不删除 Chunk selection | 保持最后位置 | 保持最后位置 |
| 切换 Preview stage | 只更新 stage，保留 image/Chunk 维度 | 切换并定位对应数据层 | 保持当前位置 |
| 选择 Chunk | 更新 node/physical spans，保留 image 维度 | 保留像素导航位置 | 跳到 Chunk header/data/CRC |
| Hex 点击 byte/bit | 更新 physical/logical span 和 stage；不伪造 image coordinate | 不自动反向锁定 pixel | 不自动反向锁定 pixel |
| 新文件打开 | generation 递增，清空旧 Selection 和 hover | 显示 loading | 显示 loading |
| 旧 generation 结果返回 | 丢弃，不改变当前 Selection/view | 不变 | 不变 |

点击像素默认选中“整个像素”；Inspector 内再选择 channel/sample 后才发布更细粒度
选择。这样不会把一个多通道像素错误压缩成某个任意 channel，也允许一个像素映射到
多个 filtered bytes、tokens 或 physical spans。

现有 `ImageCoordinate` 的 `channel=0` 不能区分“未指定 channel”和“选择第 0 个
channel”。因此 `WP-5U1` 必须为 channel/sample 引入明确的 presence 语义；在该契约
完成前，任何 widget 都不得用私有字段补出第二套坐标模型。

### 3.2 领域接口映射

| UI 需求 | Qt-free 数据来源 | UI 责任 |
|---|---|---|
| 当前 document/generation | analysis-engine document/task state | 丢弃 stale result，呈现状态 |
| pixel/stage/Chunk 联动 | `trace-model::Selection`、`SemanticNode` | 合并事件，不重算事实 |
| Image/Pixels/Filtered/Defiltered | `StageArtifact`/rendering viewport provider | 请求可见范围并缓存 |
| 文件/IDAT/Inflated/Defiltered Hex | `ByteSource`、`VirtualIDATStream`、provenance | 显示窗口和 spans，不拼接数据 |
| replay/not indexed/error/cancelled | analysis-engine query result | 显示稳定状态和恢复动作 |
| X/a/b/c、predictor、recon | reconstruction artifact/view-model（WP-5U5A） | 只读展示公式步骤 |

## 4. Preview 语义与适用性矩阵

### 4.1 基础标签

| 标签 | 语义 | 不适用/不可用时 |
|---|---|---|
| `Image` | 最终 Delivered Image；只代表显示结果 | 解码未完成时显示 loading/error |
| `Pixels` | 文件原生 sample/pixel 数值，不是第二张最终图像 | 未取得 native artifact 时显示 replaying/not indexed |
| `Filter Map` | 每条实际 scanline/pass 的 Filter Type | 没有有效 scanline 时显示 error/partial |
| `Filtered` | Inflate 输出的过滤字节，含 filter byte | 对应行未 materialize 时显示 replaying |
| `Defiltered` | reverse filter 后的 reconstructed bytes，不等同 RGBA | 对应行未 materialize 时显示 replaying |

基础标签位置永远不变。阶段不适用与暂时不可用必须使用不同文案：前者说明格式原因，
后者说明正在等待 artifact 或存在可恢复错误。

### 4.2 条件标签

| 条件 | 追加标签 | 说明 |
|---|---|---|
| `interlace_method=1` | `Adam7 Passes` | 显示 pass、pass-local 坐标与最终坐标映射 |
| `color_type=3` | `Palette Index/Resolved` | 同时显示 index、PLTE RGB；有 `tRNS` 时显示 alpha |
| `color_type=4/6` 或存在 `tRNS` | `Transparency/Alpha` | 区分文件原生 alpha、tRNS 派生 alpha 和 delivered alpha |
| `bit_depth=1/2/4` | `Packed Bytes/Unpacked Samples` | 显示 bit offset、packed byte 和解包 sample |
| `bit_depth=16` | `16-bit Byte/Sample` | 显示网络字节序 high/low byte 与组合 sample |

### 4.3 PNG 格式覆盖

| Color type | 合法 bit depth | `Pixels` 原生值 | 重点条件 |
|---|---|---|---|
| 0 grayscale | 1/2/4/8/16 | 一个 gray sample | packed（1/2/4）或 16-bit 视图；可有 tRNS |
| 2 truecolor | 8/16 | R、G、B | 16-bit 视图；可有 tRNS |
| 3 indexed-color | 1/2/4/8 | palette index | 必须有 PLTE；可选 tRNS；packed（1/2/4） |
| 4 gray+alpha | 8/16 | gray、alpha | 16-bit 视图 |
| 6 truecolor+alpha | 8/16 | R、G、B、A | 16-bit 视图 |

Adam7 是正交维度：只要 `interlace_method=1`，上述所有适用标签都必须能显示
pass-local 与 image-global 坐标；不允许用一张“去交错后的伪 scanline”替代 pass 事实。

## 5. 数值、颜色和位字段显示

### 5.1 Filtered/Defiltered

- 原始过滤字节是规范事实，canonical 显示为无符号 `0..255`。
- DEC 模式显示 `254`；HEX 模式显示 `0xFE`。HEX 的字节统一两位大写。
- 同一行可附带有符号 residual 解释 `s8=-2`；该解释是二补数视图，不是另一份数据。
- 逆过滤计算始终以无符号 byte 加 predictor 后模 256；UI 不以 signed residual 重算结果。
- 图例固定写明：`byte: unsigned 0..255`、`residual: signed int8 interpretation`、
  `recon: (predictor + byte) mod 256`。

`X/a/b/c` 的固定强调色为：`X #B45309`、`a #2563EB`、`b #15803D`、`c #7E22CE`；
文字标签必须同时显示，颜色不是唯一语义。颜色需在浅色和深色主题下保持可辨认，
并在无障碍模式下继续保留文字和边框形状差异。

### 5.2 DEC/HEX 范围

DEC/HEX 影响坐标、row/byte/channel/sample 数值、地址、长度和普通 provenance offset。
以下字段保持二进制为主，并可附十进制/十六进制辅助值：`BFINAL`、`BTYPE`、Huffman
code、extra bits、bit pattern、bit offset ruler。

`byte:bit` 地址始终使用稳定的 `byte-offset:bit-index` 形式；bit index 为该 byte
内从低位到高位的 `0..7`，bitstream 展示另附读取顺序说明。

## 6. Inspector 标签与最小首版内容

右侧标签固定为：`Reconstruct`、`Pixel`、`Scanline`、`Source`、`Format Context`。

| 标签 | WP-5U0 冻结的最小内容 |
|---|---|
| `Reconstruct` | image/pass/row/byte/channel/sample 定位；Filter Type；`X/a/b/c`；predictor；recon；边界原因；Show in Hex/DEFLATE |
| `Pixel` | 原生 sample、channel 值、packed/16-bit 解释、palette/tRNS/alpha 到 delivered RGBA 的摘要 |
| `Scanline` | pass/row、filter byte、filtered/defiltered 窗口、row byte range、replay 状态 |
| `Source` | Chunk、Virtual IDAT、Inflated、Defiltered source；logical/physical/bit spans；定位命令 |
| `Format Context` | IHDR、color type、bit depth、interlace、PLTE/tRNS/alpha、当前适用性原因 |

## 7. 可见状态与恢复行为

| 状态 | 用户文案 | 保留内容 | 可恢复动作 |
|---|---|---|---|
| loading | `Loading document…` | 文件身份和已验证 Chunk 结构 | 等待或取消当前打开 |
| not applicable | `Not applicable: <format reason>` | 其他适用阶段与 Selection | 切换适用标签 |
| not indexed | `Not indexed for this selection` | 结构索引与已验证 provenance | 触发明确的按需查询 |
| replaying | `Replaying <stage/row/block>…` | 旧 artifact、结构和 Selection | 取消当前 replay 或等待 |
| partial | `Partial result: stopped at <stable boundary>` | 边界前所有已验证结果 | 查看已验证部分、重试 |
| error | `Error: <stable issue id>` | 已验证结构和 provenance | 重试、切换 selection、查看来源 |
| cancelled | `Cancelled; verified result retained` | 取消前已验证结果 | 显式 Retry |
| ready | `Ready` | 完整当前查询结果 | 继续导航 |

错误文件不得因为局部失败而清空已验证 Chunk、scanline 或 provenance；错误详情必须
包含稳定 issue id，不能依赖 locale、时间或线程调度。

## 8. UI 验收样本矩阵

WP-5U0 只冻结样本类别，不把外部文件写入仓库；实际 fixture 必须在后续 WP 中按
`tests/corpus/manifest.yaml` 登记来源、许可、SHA-256、分类和关联测试。

| 编号 | 样本 | 必验内容 |
|---|---|---|
| S01 | 1-bit grayscale | packed sample、bit offset、Pixels/Filtered/Defiltered |
| S02 | 4-bit indexed + PLTE + tRNS | palette index/resolved、alpha、packed bytes |
| S03 | RGB8 | 五种 filter、pixel→scanline 基础路径 |
| S04 | RGBA16 | high/low byte、DEC/HEX、channel/sample 选择 |
| S05 | grayscale 2-bit/8-bit/16-bit | color type 0 的 bit-depth 适配 |
| S06 | truecolor16 | RGB 多 byte sample 和重建窗口 |
| S07 | gray+alpha8/16 | 原生 alpha 与 delivered alpha |
| S08 | Adam7 RGB/RGBA | pass-local/global 坐标、空 pass/边界 |
| S09 | Stored block | BTYPE、LEN/NLEN、bit ruler |
| S10 | Fixed Huffman | token/source 状态入口 |
| S11 | Dynamic Huffman | table 状态入口和 replay 状态 |
| S12 | 多 IDAT、跨 block | Virtual IDAT segment 边界、多 physical spans |
| S13 | 截断 IDAT / CRC / Adler 错误 | partial/error、已验证结构保留 |
| S14 | 非法 filter / 非法 Huffman | stable issue、不可用阶段文案 |
| S15 | 大图 | viewport/tile/Hex 窗口化、内存预算、UI 不阻塞 |

每个样本至少走一次：hover → click lock → stage tab switch → Inspector → Hex source
定位。S08、S12、S13、S15 还必须覆盖取消和旧 generation 丢弃。

## 9. 性能预算与测量方法

### 9.1 初始预算

| 场景 | 预算/判定 |
|---|---|
| hover 事件处理 | p95 ≤ 16 ms；不提交 Deep Trace/replay，不触发 Hex 跳转 |
| 已缓存 locked selection | p95 ≤ 50 ms 发布可见 Selection；UI 线程不读文件 |
| 首次 replay | 先在 ≤100 ms 内显示 `replaying`，完成时间单独记录；可取消 |
| UI 线程阻塞 | 文件读取、Inflate、reverse filter、Trace 均为 0 ms 同步执行 |
| 派生阶段缓存 | 每个打开文档初始上限 64 MiB；不得按阶段创建多张全尺寸 `QImage` |
| Hex 窗口 | 只保留可见窗口及 bounded look-around，单次读取上限 1 MiB |
| 选择风暴 | 100 次交替选择无回环；旧 generation 不发布；内存不无界增长 |

### 9.2 测量记录

每次记录必须包含机器型号、操作系统、Qt 版本、构建类型、样本编号、冷/热缓存、
P50/P95 延迟、峰值 RSS、取消耗时和 UI 线程阻塞证据。性能结果不得用当前机器
“感觉流畅”替代数值记录。

## 10. 验收与后续入口

### 10.1 产品决策记录

- Filtered 采用双模式：无符号原始 byte 为主，有符号 residual 作为明确标注的辅助
  解释；DEC/HEX 只改变显示基数，不改变数据事实。
- 点击像素默认聚焦整个 pixel；channel/sample 由 Inspector 显式细化，后续可记忆上次
  channel，但不得把 channel 0 当作“未指定”。
- Inspector 首版标签固定为 `Reconstruct`、`Pixel`、`Scanline`、`Source`、
  `Format Context`，其中 Reconstruct 是唯一要求在首个代码 WP 完整交付的标签。
- 默认窗口采用 `1200×760`、最小 `900×600` 和 Preview/Hex `60%/40%`；跨 DPI 验收
  若证明这些值会截断内容，只能在后续 UI Gate 以证据调整。
- Statistics 不作为首个单文件 v1 的强制验收项；在 M5 Gate 再决定是否启动
  `WP-602A/B`。Compare/First Difference 继续延后。

WP-5U0 通过条件：

1. 本文档覆盖计划列出的九类交付物，并且每个 UI 状态有明确层级归属。
2. Preview/Inspector 标签对所有 color type、bit depth、Adam7、palette/tRNS/alpha
   情况都有适用、禁用原因或等待状态。
3. Selection 合并规则不会清除未理解的维度；hover 不发布领域选择。
4. 文档没有要求 GUI 解析 PNG/DEFLATE、拼接 IDAT 或保存全文件 token trace。
5. 样本矩阵覆盖正常、边界、错误和大图；性能预算可按本节方法复测。
6. 项目负责人确认本文档后，才可开始 `WP-5U1` 或其他依赖 WP。

最小人工走查：S03（RGB8 + Dynamic）和 S12（多 IDAT + 跨 block）各完成一次
“锁定坐标 → 切换 Filtered/Defiltered → Reconstruct → Source/Hex”流程，并逐项核对
Selection、状态文案、span 保留和无 UI 线程解码。

## 11. 相关约束

- [当前开发计划](../architecture/png-analyzer-current-development-plan-2026-08-22.md)
- [仓库布局契约](../architecture/REPOSITORY_LAYOUT.md)
- [ADR-0003：Core 不依赖 Qt](../adr/ADR-0003-core-is-qt-free.md)
- [ADR-0004：统一 Analysis/Selection 模型](../adr/ADR-0004-unified-analysis-model.md)
- [ADR-0005：Virtual IDAT Stream](../adr/ADR-0005-virtual-idat-stream.md)
- [ADR-0006：Fast Index + On-demand Deep Trace](../adr/ADR-0006-fast-index-on-demand-deep-trace.md)

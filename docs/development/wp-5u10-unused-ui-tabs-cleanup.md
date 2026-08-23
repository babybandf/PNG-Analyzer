# WP-5U10 — 无有效内容标签清理与工作区迁移

> Work Package：`WP-5U10`
> Milestone：M5 UI Refinement
> Status：implemented (2026-08-23)
> Depends on：WP-5U2、WP-5U7
> 推荐执行顺序：在 WP-5U9 之后实施；也可独立提前实施

本工作包只精简当前没有形成有效用户功能的 UI 入口，不删除任何 PNG
scanline、Filter Type、filtered/unfiltered stage 或 provenance 数据。

本工作包获批后，在“标签是否可见及 Inspector 导航层级”这一窄范围内取代
`WP-5U0` 的五个固定 Preview 标签约束，以及 `WP-5U7` 的
`Image → Reconstruction / Pixel / Format Context`、
`Scanline → Scanline / Source` Inspector 映射；其他 Selection、异步 generation、
坐标、数值语义、数据来源和性能约束继续有效。

## 1. 背景与现状

### 1.1 中央 `Filter Map`

当前 `Filter Map` 使用 `StagePreviewView::kFilterMap`，实际只显示：

- `Filter Map` 标题；
- scanline 数量；
- 当前坐标；
- Adam7、Palette、Alpha、Packed、16-bit 等格式条件。

它没有显示每条 scanline/pass 的 Filter Type，也没有形成空间分布图、行列表、颜色图例
或行级导航。因此当前页面名称与实际能力不匹配，用户无法从中获得有效的 Filter Map。

`Filter Map` 在产品概念上仍有独立价值：它可用于查看整张 PNG 每行采用的 None、Sub、
Up、Average、Paeth 分布，这一能力不会被面向单个坐标的 Reconstruction、Filtered 或
Defiltered 页面完全替代。因此本任务只隐藏入口，不删除底层能力和未来恢复位置。

### 1.2 Inspector `Scanline`

当前 Inspector 的一级 `Scanline` 分类包含两个二级页：

- `Scanline`；
- `Source`。

两个页面均为固定的 `Not available for current selection` 占位标签，没有消费当前坐标、
scanline、source span 或 replay 状态。当前 Reconstruction 已显示选中像素的 pass、
stream row、filtered/unfiltered offset、Filter Type 和重建公式，所以保留一个完全不可用
的一级分类只会增加导航噪声。

### 1.3 Inspector `Image / Pixel` 与 `Image / Format Context`

两个页面原有设计语义分别是：

- `Pixel`：原生 sample、通道值、packed/16-bit 解释，以及 palette/tRNS/alpha 到
  delivered RGBA 的转换摘要；
- `Format Context`：IHDR、color type、bit depth、interlace、PLTE/tRNS/alpha 和
  当前功能适用性原因。

当前实现中，两者同样只是固定的 `Not available for current selection` 占位标签。
中央 `Pixels` 页面及 WP-5U9 已覆盖原生 sample、通道、packed/16-bit 的主要解释；
Reconstruction 已展示目标像素格式、Pass、源通道、过滤过程与最终 RGBA；左侧 Chunk
Detail 已提供 IHDR 及相关格式 Chunk 字段。在当前产品范围内继续保留两个空白二级页
没有独立用户价值。

## 2. 决策

### 2.1 Preview

中央 Preview 的可见基础标签调整为：

```text
Image | Pixels | Filtered | Defiltered
```

- 从 UI 中隐藏 `Filter Map`。
- 不创建禁用标签、空白页、`Coming soon` 或其他替代占位入口。
- 保持其他四个标签的语义、顺序和页面实例不变。
- 底层 Filter Type、scanline spans、pass layout 和 stage artifacts 继续保留。
- 未来只有在实现真正的行/Pass Filter Type 分布视图后，才能通过独立工作包恢复
  `Filter Map`。

### 2.2 Inspector

Inspector 一级分类调整为：

```text
Reconstruction | Compression
```

- 删除仅用于承载 `Reconstruction / Pixel / Format Context` 的 `Image` 二级标签容器。
- 将现有 `StageInspector` 直接作为 Inspector 一级 `Reconstruction` 页面；不复制实例，
  不改变其数据和信号接线。
- 删除 `Pixel`、`Format Context` 两个占位页。
- 删除一级 `Scanline` 分类及其 `Scanline`、`Source` 两个占位页。
- 不删除 Reconstruction 中已有的 pass、row、offset、Filter Type 和 replay 状态。
- 不删除 analysis-engine 中的 scanline/source/provenance 数据结构或查询。
- 将来只有在 Scanline 页面至少具备第 4 节规定的恢复门槛后，才能重新增加该入口。

## 3. 用户可见目标

完成后：

- 用户不会再进入只有计数摘要或 `Not available` 的标签页。
- Preview 保留四个与当前 PNG 解码阶段直接相关的页面。
- Inspector 一级直接显示 `Reconstruction | Compression`，不再为单个有效页面套一层
  `Image` 二级标签。
- 删除标签不会改变当前文件、锁定坐标、三栏宽度、Hex 数据源或数值进制。
- 已保存的旧工作区不会因为标签索引变化而恢复到错误页面。

## 4. 未来恢复入口的最低门槛

### 4.1 `Filter Map`

恢复前至少必须提供：

- 每个实际 scanline/pass 的真实 Filter Type；
- None、Sub、Up、Average、Paeth 的文字和非单一颜色图例；
- Adam7 pass 与 image-global row 的明确映射；
- 选择某行后与坐标、Reconstruction 或 Scanline Inspector 的有效联动；
- partial/error/unavailable 状态；
- 大图下的窗口化或有界呈现，不能创建无界 UI 对象。

### 4.2 Inspector `Scanline`

恢复前，`Scanline` 分类中至少一个页面必须真实消费当前 selection，并提供：

- pass、pass-local row、image row 和 stream row；
- Filter byte/type；
- filtered/defiltered row byte range 或有界窗口；
- replay/materialization 状态；
- 至少一个有效导航动作，例如定位 Hex 或切换到关联 Reconstruction。

只显示 `Not available`、静态说明或重复 Reconstruction 文本，不满足恢复条件。

## 5. 实施范围

### 5.1 UI 创建与接线

- 不再创建 `filter_map_view_`，不再把它加入 `preview_tabs_`。
- 删除 `filter_map_view_` 的 coordinate、StageSet、clear 和 reset 接线。
- 不再创建 `scanline_inspector_tabs_`。
- 删除 `Scanline`、`Source` 两个占位实例和 Inspector 一级 `Scanline` 标签。
- 不再创建 `image_inspector_tabs_`；删除 `Pixel`、`Format Context` 两个占位实例。
- 将现有 `inspector_`/`StageInspector` 直接加入一级 `inspector_tabs_`，标签名为
  `Reconstruction`。
- 删除仅用于上述实例的成员、默认页设置和 tab order/focus 依赖。
- 保持 `StageSet.scanlines`、`filter_formula()`、scanline query coordinator 和
  Reconstruction 的 row-status 接线不变。

### 5.2 工作区设置迁移

当前工作区使用数字 tab index。删除中间标签后：

- 旧 Preview index `3` 会从 `Filtered` 错误指向新的 `Defiltered`；
- 旧 Preview index `4` 将越界；
- 旧 Inspector index `2` 会从 `Compression` 越界；
- `workspace/imagePage`、`workspace/scanlinePage` 将失去对应页面。

因此实现不得只删除标签而继续原样恢复旧 index。

推荐将 `workspace/version` 从 `1` 升级为 `2`，并保存稳定页面 ID：

```text
workspace/previewTabId: image | pixels | filtered | defiltered
workspace/inspectorPageId: reconstruction | compression
```

必须迁移旧 version 1 设置：

| 旧 Preview index | 旧页面 | 新页面 ID |
|---:|---|---|
| 0 | Image | `image` |
| 1 | Pixels | `pixels` |
| 2 | Filter Map | `image` |
| 3 | Filtered | `filtered` |
| 4 | Defiltered | `defiltered` |

| 旧 Inspector index | 旧分类 | 新分类 ID |
|---:|---|---|
| 0 | Image | `reconstruction` |
| 1 | Scanline | `reconstruction` |
| 2 | Compression | `compression` |

- 忽略旧 `workspace/imagePage`、`workspace/scanlinePage`，不得因这些字段存在而判定
  整个 workspace 损坏。无论旧 `imagePage` 选择 Reconstruction、Pixel 或 Format
  Context，迁移后均进入一级 `Reconstruction`。
- 尽量保留旧 geometry、dock state、splitter state、Compression 子页、numeric
  base 和 Hex source。若与 WP-5U11 同时实施，旧 Hex-follow 设置按 WP-5U11 安全忽略。
- 未知 ID 或损坏值回退到对应区域的默认页，不应为了一个标签值清空整组窗口尺寸设置。
- `View → Reset Layout` 默认进入 `Image` Preview 和一级 `Reconstruction`。

若开发者选择继续保存整数，也必须实现与上表等价的显式 version 1 → version 2
迁移；不得依赖新的碰巧索引。

## 6. 非目标

- 不实现真正的 Filter Map。
- 不实现 Scanline 或 Source Inspector。
- 不把旧 `Pixel`、`Format Context` 占位页实现为新功能。
- 不删除或修改 PNG scanline layout、Inflate、reverse filter、StageSet、row query、
  provenance 或 Hex source。
- 不改变 `Pixels`、`Filtered`、`Defiltered` 的计算和展示内容；这些属于 WP-5U9。
- 不改变 Inspector Dock、三栏布局或主题。
- 不新增依赖、顶层目录、第三方代码或 corpus 文件。

## 7. 任务拆分

### T1 — 标签与接线清理（P0）

- 调整 Preview 和 Inspector 创建逻辑。
- 删除无效成员和信号接线。
- 确认底层 scanline/filter 数据路径仍被 Reconstruction 和其他阶段页面使用。

### T2 — Workspace version 2 与迁移（P0）

- 引入稳定 tab/group ID 或等价显式迁移。
- 按第 5.2 节迁移 version 1 设置。
- 确保旧 Filter Map 回退到 Image Preview；旧 Image/Scanline Inspector 选择迁移到
  Reconstruction，而不是错误页面。

### T3 — 自动化测试与文档（P0）

- 更新布局、设置恢复、DPI 和可访问性测试。
- 更新用户指南和当前开发计划中的可见 UI 列表。
- 保留历史工作包内容，不回写或伪造旧实施记录；通过本工作包声明窄范围取代关系。

### T4 — 回归验证（P0）

- 验证文件加载、坐标选择、阶段刷新、Hex、Inspector、布局恢复和 Reset Layout。
- 检查是否仍存在 orphan widget、重复信号、旧设置误恢复或运行时警告。

最便宜的判别测试：预置 version 1 设置
`previewTab=3, inspectorTab=2`，启动新版后必须恢复到 `Filtered` 与
`Compression`，而不是按新索引恢复到 `Defiltered` 或回退整个 workspace。

## 8. 允许和禁止路径

主要允许路径：

- `docs/development/wp-5u10-unused-ui-tabs-cleanup.md`
- `docs/user-guide.md`
- `docs/architecture/png-analyzer-current-development-plan-2026-08-22.md`
- `apps/png-analyzer-gui/src/main_window.h`
- `apps/png-analyzer-gui/src/main_window.cpp`
- `tests/gui/main_window_layout_test.cpp`
- `tests/gui/cross_platform_gui_gate_test.cpp`
- 与 workspace 设置迁移直接相关的现有 GUI 测试文件

条件允许路径：

- `ui/qt/include/pnga/ui/qt/stage_preview_view.h`
- `ui/qt/src/stage_preview_view.cpp`
- `ui/qt/CMakeLists.txt`
- `tests/gui/stage_preview_view_test.cpp`
- `tests/gui/CMakeLists.txt`
- `ui/qt/README.md`

只有在 WP-5U9 已使 `StagePreviewView` 不再被任何可见页面使用时，才允许删除该组件及
对应测试/CMake 条目；否则必须保留它供现有 `Filtered`、`Defiltered` 页面使用。

禁止路径：

- `libs/png-reconstruction/**`
- `libs/deflate-trace/**`
- `libs/png-format/**`
- 与 UI 状态迁移无关的 `libs/analysis-engine/**`
- `third_party/**`、依赖清单、打包配置、无关工作包和 ADR

## 9. 验收标准

### 9.1 Preview

- Preview 标签数量为 4。
- 顺序严格为 `Image`、`Pixels`、`Filtered`、`Defiltered`。
- UI 中不存在 `Filter Map` 标签、禁用项或占位入口。
- 打开 PNG、修改坐标、重载文件时不访问已删除的 widget。
- Filter Type 和 scanline 数据仍能在 Reconstruction/阶段计算中正常使用。

### 9.2 Inspector

- Inspector 一级分类数量为 2。
- 顺序严格为 `Reconstruction`、`Compression`。
- `StageInspector` 直接作为一级 `Reconstruction` 页面，不存在
  `imageInspectorPages` 二级容器。
- UI 中不存在 `Pixel`、`Format Context` 占位页。
- UI 中不存在一级 `Scanline` 分类及 `Scanline`、`Source` 占位页。
- Reconstruction 与三个 Compression 页面功能不回退。
- 当前像素、DEFLATE 选择和 Hex 导航上下文保持正确。

### 9.3 Workspace

- 新安装默认页正确。
- version 1 的 Image、Pixels、Filtered、Defiltered、Compression 选择按表迁移。
- version 1 的 Filter Map Preview 选择回退到 Image；Image/Scanline Inspector 选择迁移
  到 Reconstruction。
- 旧 `imagePage`、`scanlinePage` 不导致 workspace 全量重置。
- 损坏/未知页面 ID 安全回退。
- 保存、重启、Reset Layout 往返通过。

### 9.4 回归

- 文件打开、关闭、连续切换至少 10 次不崩溃。
- X/Y、Lock、Esc、方向键和 DEC/HEX 行为不变。
- Hex source、dock、splitter 和 Chunk Detail 状态不回退；若与 WP-5U11 同时实施，
  `Hex follows pixel` 按该工作包移除。
- 100%、150%、200% DPI 下标签可见且布局正常。
- 无新增编译警告、Qt runtime warning、重复信号或 stale generation 发布。

## 10. 验证命令

从 PNG-Analyzer 仓库根目录运行：

```sh
cmake --build --preset dev -j2
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/verify_repository_layout.py
git diff --check
```

## 11. 交付物

- UI 标签与无效接线清理代码。
- Workspace version 2 或等价迁移实现。
- version 1 → version 2 的自动化迁移测试。
- 更新后的布局、DPI、可访问性和回归测试。
- 更新后的用户指南与当前 UI 计划说明。
- 修改前后截图：
  - 四个 Preview 标签；
  - `Reconstruction | Compression` 两个 Inspector 一级页面；
  - 旧设置迁移后的 Filtered/Compression 恢复结果。
- 验证命令与结果记录。

## 12. 完成定义

只有同时满足以下条件才能标记完成：

1. `Filter Map` 不再作为当前无效入口出现在中央 Preview。
2. Inspector 一级直接显示 `Reconstruction | Compression`。
3. `Image` 二级容器及 `Pixel`、`Format Context`、`Scanline`、`Source` 四个占位页
   完全移除。
4. 底层 scanline/filter/provenance 能力未删除、未弱化。
5. 旧工作区的有效标签选择不会因索引变化恢复到错误页面。
6. 用户指南和当前计划与新 UI 一致。
7. 全部相关自动化和 GUI Gate 通过。

## 13. 实施记录

- Preview 已收敛为 `Image | Pixels | Filtered | Defiltered`，保留
  `StagePreviewView` 供 Filtered/Defiltered 使用，未删除底层 scanline/filter 数据。
- Inspector 一级已收敛为 `Reconstruction | Compression`，StageInspector 直接作为
  Reconstruction 页面，旧占位页与 Scanline 分类已移除。
- workspace 已升级为 version 2，保存稳定的 `previewTabId` 与
  `inspectorPageId`；version 1 的旧索引按本 WP 表格迁移，未知页面回退到区域默认值。
- 验证：`pnga_gui_main_window_layout_tests`、`pnga_gui_cross_platform_gate_tests`
  均通过（offscreen CTest）。

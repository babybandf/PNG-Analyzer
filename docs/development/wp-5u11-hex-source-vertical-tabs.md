# WP-5U11 — Hex 数据源选择器移位与竖向标签栏

> Work Package：`WP-5U11`
> Milestone：M5 UI Refinement
> Status：implemented (2026-08-23)
> Depends on：WP-5U2、WP-5U4A、WP-5U4B、WP-5U7
> Coordination：可在 WP-5U10 前后独立实施；如同时开发，应合并处理 MainWindow 与 GUI 测试冲突
> Product reference：用户提供的 2026-08-23 标注截图

## 1. 需求理解

截图标号 1 指向的控件实际是一个 Hex Source 下拉框，其中包含四个数据源选项：

1. `File`
2. `IDAT`
3. `Inflated`
4. `Defiltered`

本任务将该下拉框从 Inspector 顶部坐标工具栏移除，并在中央下方 Hex View 的左边缘
增加四个 West 风格竖向标签面。点击标签后，右侧同一个 Hex View 立即切换到对应数据源。

目标结构：

```text
Central Area
┌────────────────────────────────────────────────────────────┐
│ Preview                                                    │
├───────┬────────────────────────────────────────────────────┤
│ File  │                                                    │
│ IDAT  │                                                    │
│ Stream│                 Shared Hex View                    │
│Inflated                                                    │
│Defilt.│                                                    │
└───────┴────────────────────────────────────────────────────┘
```

正式 UI 使用完整标签 `File`、`IDAT`、`Inflated`、`Defiltered`，不得使用
无法辨识的单字母或无说明图标。标签沿 Hex View 左边缘由上到下排列，标签文字使用
West tab 的竖向朝向，以控制横向占用。

Inspector 工具栏保留：

```text
X | Y | Lock | DEC/HEX
```

其中 `HEX`/`DEC` 是全局数值进制按钮，不是 Hex 数据源选项，不得随下拉框一起移走。
现有 `Hex follows pixel` 复选框当前只有状态保存，没有像素到 Hex 的实际定位接线；
本工作包按产品决定暂时将其一并移除。

## 2. 可行性结论

本需求可行，且不需要修改任何 PNG、Inflate 或 reverse-filter 算法。

现有代码已经具备：

- `HexSource` 四值状态模型；
- File、Virtual IDAT、Inflated、Defiltered 四种 `HexDataSource`；
- `MainWindow::updateHexSource()` 数据源切换入口；
- `view/hexSource` 工作区持久化；
- Hex 数据源 unavailable/error 状态显示；
- 单个窗口化 `HexView`，只读取当前可见字节范围。

本任务只替换选择控件和布局位置，并保持上述数据契约不变。风险主要集中在竖向标签的
尺寸策略、键盘焦点、旧工作区恢复和异步 StageSet 到达后的选中状态同步。

## 3. 用户可见目标

- Inspector 顶部不再显示 Hex Source 下拉框。
- Inspector 顶部不再显示无实际效果的 `Hex follows pixel` 复选框。
- Hex View 左侧始终显示四个竖向数据源标签。
- 当前标签有清晰的 selected 状态；hover、focus、disabled/unavailable 状态可辨认。
- 切换标签只改变 Hex 数据源，不改变当前 PNG、像素坐标、Lock、DEC/HEX、Inspector
  页面或中央 Preview 页面。
- Inspector 变窄时工具栏更紧凑，不再为四项数据源下拉框预留宽度。
- Hex 数据源的归属更直观：选择入口与被切换的数据视图位于同一区域。

## 4. 信息架构与语义

### 4.1 标签顺序和含义

顺序固定为：

| 标签 | 数据语义 | 现有数据源 |
|---|---|---|
| `File` | PNG 物理文件字节 | `make_file_hex_source()` |
| `IDAT` | 多个 IDAT payload 组成的虚拟逻辑流 | `make_idat_hex_source()` |
| `Inflated` | Inflate 输出的 filtered scanline 字节，包含 filter byte | `make_inflated_hex_source()` |
| `Defiltered` | reverse filter 后的 reconstructed packed scanline 字节 | `make_defiltered_hex_source()` |

不得改变四个数据源的含义、顺序或 `HexSource` 枚举值。

### 4.2 标签形态

- 标签栏固定在 Hex View 左侧，不放到 Preview、Inspector 或状态栏。
- 使用一个竖向 `QTabBar` 或具有等价键盘、可访问性和主题行为的组件。
- 推荐 `QTabBar::RoundedWest`；文字旋转但必须完整可读。
- 标签栏只占用一个紧凑的固定/有界宽度，不得显著缩小 Hex 内容。
- 标签高度由内容和 DPI 决定，不硬编码设备像素。
- 默认窗口与 `900×600` 最小窗口下四个标签均应可到达；空间不足时允许使用原生滚动
  按钮，但不得静默裁掉标签或增加主窗口最低宽度。
- 标签栏与 Hex View 之间保持清晰分界；不使用下拉箭头。

### 4.3 状态

- `File` 为首次启动和 Reset Layout 的默认选择。
- 没有打开文件时，四个标签仍可见，Hex View 显示无文件/不可用状态。
- 文件已打开但 StageSet 尚未完成时，`Inflated`、`Defiltered` 仍允许选择，并在 Hex
  View 中显示稳定的 unavailable/loading 状态；不因为暂时不可用而改变用户选择。
- StageSet 异步到达后，如果当前仍选择 `Inflated` 或 `Defiltered`，Hex View 自动刷新
  为 ready 数据，不跳回 `File`。
- StageSet error 时保留当前标签并显示 error，不自动切换数据源。
- 打开新文件后保留当前用户偏好，除非现有文档 generation/reset 契约明确要求默认
  `File`；最终行为必须与当前产品契约一致并由测试冻结。

## 5. 组件和架构方案

### 5.1 单一 HexView 不变量

四个标签必须控制同一个 `HexView` 实例：

```text
Hex panel
├─ Hex source tab bar
└─ HexView (single shared instance)
```

禁止以下实现：

- 为四个数据源创建四个 `HexView`；
- 为四个标签复制完整 stage bytes；
- 在 tab widget 中长期保留四份大数据视图；
- 在 UI 线程重新 Inflate 或 reverse filter；
- 通过标签文字反向猜测 `HexSource`。

每个标签应通过稳定 item data/property 映射到 `HexSource`。

### 5.2 推荐组件边界

推荐新增一个轻量 Qt 组件，例如 `HexSourceTabBar` 或 `HexPanel`：

- 创建和展示四个竖向标签；
- 暴露当前 `HexSource`；
- 发出强类型 `sourceChanged(HexSource)` 信号；
- 支持程序化设置当前 source，并可在设置恢复时阻止重复信号；
- 提供可访问名称、说明和键盘选择。

`MainWindow` 继续负责：

- 持有 `SelectionViewState::hex_source`；
- 创建对应 `HexDataSource`；
- 调用 `HexView::setSource()`；
- 应用 document generation、StageSet 和 workspace 状态。

若不新增独立类，也必须把标签栏封装为 Hex 区域的明确子组件，避免将四项映射逻辑
散落在多个 lambda 中。

### 5.3 中央布局

当前中央垂直 splitter 的上半部分仍是 Preview。下半部分由直接的 `HexView` 改为一个
Hex panel 容器：

```text
previewHexSplitter
├─ previewTabs
└─ hexPanel
   ├─ hexSourceTabs (west)
   └─ hexView
```

- `previewHexSplitter` 仍只有两个直接子项。
- 原来的 Preview/Hex 比例、拖动行为和持久化保持不变。
- `hexPanel` 的 minimum size hint 不得由完整标签文本撑大中央区域。
- Hex View 仍获得剩余全部可用空间。

## 6. 交互与状态契约

### 6.1 鼠标和键盘

- 点击标签立即选择数据源。
- 标签栏获得焦点后，方向键可在四个标签间移动；Enter/Space 采用 Qt 原生激活行为。
- 为四个标签提供可访问名称：`Hex source: File` 等。
- 提供 tooltip，简述第 4.1 节的数据语义。
- 不用颜色作为唯一 selected/focus/unavailable 提示。

推荐焦点顺序：

```text
X → Y → Lock → DEC/HEX
→ Preview tabs/view → Hex source tabs → Hex view → Inspector tabs
```

删除原 `hexSource` 下拉框和 `hexFollowPixel` 复选框后，焦点链不得指向已删除对象。

### 6.2 Selection 与 Hex 导航

- 切换数据源更新 `view_state_.hex_source`，随后调用现有 Hex source 更新路径。
- 选择图片像素、修改 X/Y 或移动锁定坐标时，暂不自动定位 Hex；只更新像素相关页面。
- 选择 Chunk 时，由于 Chunk offset 属于物理文件空间，必须同步选择 `File` 标签，再
  高亮并定位该 Chunk 的 header/data/CRC；不得把物理 offset 应用到 IDAT、Inflated 或
  Defiltered 地址空间。
- 切换 Hex source 标签不得改变 image/Chunk Selection。
- 本任务不新增跨数据源历史栈，也不改变 `HexView::setSource()` 当前清除 highlight/
  navigation history 的行为；如需每个 source 独立历史，应另开工作包。

### 6.3 异步和 generation

- 选中 `Inflated`/`Defiltered` 后等待 StageSet 时，不启动额外重复分析。
- 新 StageSet 只刷新当前标签对应的 Hex data source。
- 旧 document generation 返回时不得改变标签选择或当前 Hex 内容。
- 切换标签本身不得执行文件解析、Inflate、reverse filter 或 Deep Trace replay。

## 7. Workspace 设置

沿用现有 `view/hexSource` 语义和枚举值，不需要改变保存格式：

```text
0 = File
1 = IDAT
2 = Inflated
3 = Defiltered
```

要求：

- 保存时从 `SelectionViewState::hex_source` 写入，而不是依赖视觉 tab index。
- 恢复时先校验枚举值，再按标签 item data 选择对应标签。
- 恢复选择时使用 signal blocker 或等价机制，最终只执行一次有效
  `updateHexSource()`。
- 损坏或未知值回退到 `File`，不重置窗口 geometry、dock 或 splitter。
- `View → Reset Layout` 选择 `File`。
- 与 WP-5U10 同时实施时，workspace version/迁移由 WP-5U10 统一升级，但
  `view/hexSource` 的值和含义保持不变。
- 删除旧 `view/hexFollowPixel` 设置；读取旧工作区时忽略该键，不得因此重置其他设置。

## 8. 工具栏调整

从 Inspector 坐标工具栏删除：

- `QComboBox hexSource`；
- `QCheckBox hexFollowPixel`；
- 两个控件对应的 accessible name；
- 下拉框的 currentIndexChanged lambda；
- `hexFollowPixel` 的 toggled lambda；
- 与两个控件直接关联的 tab order、signal blocker、成员和持久化接线。

保留并重新排列：

```text
X  [value]  Y  [value]  [Lock]  [DEC/HEX]
```

- 删除下拉框后，工具栏自身的横向滚动机制可以继续保留，以适应窄 Inspector。
- 不借此任务改动 X/Y spinbox 宽度、Lock 语义或 DEC/HEX 按钮行为。

同时从 `SelectionViewState` 删除未被业务逻辑消费的 `hex_follow_pixel` 字段，并更新
对应单元测试。未来只有在像素到四种 HexSource 的 provenance 定位完整实现后，才能
通过独立工作包重新引入该控制项。

## 9. 空状态与可见文案

| 场景 | 选中标签 | Hex View 文案/行为 |
|---|---|---|
| 无文件 | 任意 | `No file loaded` 或稳定的 source unavailable 文案 |
| 文件已索引 | File | 显示物理文件字节 |
| 文件已索引 | IDAT | 显示虚拟 IDAT payload 流 |
| StageSet 未就绪 | Inflated/Defiltered | 显示对应 source unavailable/loading，不跳页 |
| StageSet ready | Inflated | 显示 filtered scanline bytes |
| StageSet ready | Defiltered | 显示 reconstructed packed bytes |
| StageSet error | Inflated/Defiltered | 保留标签并显示 error |

标签自身始终保持可见，不通过禁用标签掩盖暂时不可用状态。

## 10. 任务拆分

### T1 — Hex source 竖向标签组件（P0）

- 实现四个 West 标签、稳定 `HexSource` 映射、信号和程序化选择。
- 完成鼠标、键盘、tooltip、accessible name 和主题状态。
- 增加组件级测试。

### T2 — Hex panel 布局与 MainWindow 接入（P0）

- 用 `hexPanel` 包装单一 `HexView` 和竖向标签栏。
- 移除 Inspector 工具栏下拉框、`Hex follows pixel` 及旧连接/状态。
- 连接 `SelectionViewState`、`updateHexSource()`、document reset 和 StageSet ready。
- 修正 Chunk selection：先选中 `File` source，再使用物理文件 offset 导航。
- 修正焦点顺序和 accessible metadata。

### T3 — Workspace 恢复与状态测试（P0）

- 验证四种 source 保存/恢复。
- 验证损坏值回退、Reset Layout 和异步 StageSet ready。
- 与 WP-5U10 合并开发时，复用同一个 workspace migration 测试矩阵。

### T4 — DPI、布局和完整回归（P0）

- 验证默认、最小窗口、150% 和 200% DPI。
- 验证中央 splitter、Inspector 窄宽、Hex 可视面积和键盘焦点。
- 运行第 13 节完整门禁。

最便宜的判别测试：构造 MainWindow 后确认 Inspector 中不存在 `hexSource` 下拉框，
也不存在 `hexFollowPixel` 复选框；Hex panel 左侧存在四个标签。激活
`IDAT` 后，保存的
`SelectionViewState::hex_source` 为 `kIdatStream`，且原有单一 `HexView` 实例未被替换。

## 11. 允许和禁止路径

主要允许路径：

- `docs/development/wp-5u11-hex-source-vertical-tabs.md`
- `docs/user-guide.md`
- `docs/architecture/png-analyzer-current-development-plan-2026-08-22.md`
- `ui/qt/include/pnga/ui/qt/hex_view.h`
- `ui/qt/src/hex_view.cpp`
- 新增的轻量 Hex source tab/panel Qt 文件
- `ui/qt/CMakeLists.txt`
- `ui/qt/README.md`
- `apps/png-analyzer-gui/src/main_window.h`
- `apps/png-analyzer-gui/src/main_window.cpp`
- `tests/gui/main_window_layout_test.cpp`
- `tests/gui/cross_platform_gui_gate_test.cpp`
- 新增的 Hex source tab/panel GUI 测试
- `tests/gui/CMakeLists.txt`

条件允许路径：

- `ui/qt/include/pnga/ui/qt/selection_view_state.h`
- `ui/qt/src/selection_view_state.cpp`（如存在）
- `tests/gui/selection_view_state_test.cpp`

允许删除未被业务逻辑消费的 `SelectionViewState::hex_follow_pixel` 字段和对应测试；
除此之外，只有在现有强类型状态无法与标签 item data 安全映射时，才允许做最小扩展。
不得改变四个 `HexSource` 的枚举值或持久化语义。

禁止路径：

- `libs/**` 中的 parser、Inflate、reconstruction、trace 和 provenance 实现
- `third_party/**`
- 依赖清单、打包配置、无关工作包和 ADR
- PNG corpus、生成文件和无关 UI 页面

## 12. 验收与测试矩阵

### 12.1 结构

- Inspector 工具栏不存在 `QComboBox` 类型的 `hexSource` 控件。
- Inspector 工具栏不存在 `hexFollowPixel` 复选框或替代占位入口。
- Hex panel 左侧存在且仅存在四个数据源标签。
- 标签顺序和名称严格符合第 4.1 节。
- 应用中始终只有一个业务 `HexView` 实例。
- `previewHexSplitter` 仍有两个直接子项，尺寸恢复不回退。

### 12.2 切换

- File、IDAT、Inflated、Defiltered 分别切换到正确数据源。
- 标签 item data 与 `HexSource` 一一对应，不使用显示文本判断。
- 连续循环切换四个标签 100 次，无崩溃、重复分析、对象增长或信号倍增。
- StageSet 到达前选中 Inflated/Defiltered，ready 后保持标签并正确显示数据。
- 新文件和 stale generation 不会发布旧 source 数据。

### 12.3 状态保持

- 四种 source 分别保存、关闭、重建窗口后恢复正确。
- 损坏 source 值回退 File，不影响 geometry/dock/splitter。
- Reset Layout 回到 File。
- 切换 source 不改变 X/Y、Lock、DEC/HEX、Preview 或 Inspector 页。
- 旧 `view/hexFollowPixel` 设置被安全忽略，不影响其他 workspace 状态。

### 12.4 交互与可访问性

- 鼠标、方向键和焦点导航可选择四个标签。
- selected、hover、focus 在浅色/深色主题下可辨认。
- 四个标签具有完整 accessible name 和 tooltip。
- 默认、`900×600`、150% 和 200% DPI 下标签可到达且 Hex 内容仍有可用宽度。
- Inspector 工具栏移除下拉框和复选框后无空洞、裁切或残留焦点项。

### 12.5 回归

- File/IDAT/Inflated/Defiltered `HexDataSource` 原有单元测试继续通过。
- Pixel lock 不自动移动 Hex；Chunk selection 切换到 File 并正确定位物理范围。
- Source provenance 和其他 Selection 维度不回退。
- Hex 滚动、highlight、navigate/back/forward 保持既有 source-switch 语义。
- 文件打开、重载和连续切换至少 10 次无崩溃或 Qt runtime warning。

## 13. 验证命令

从 PNG-Analyzer 仓库根目录运行：

```sh
cmake --build --preset dev -j2
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/verify_repository_layout.py
git diff --check
```

另需在 macOS 正常 DPI 和至少一个高 DPI 比例下手工验证竖向文字、标签命中区域、
键盘焦点和中央 splitter 拖动。

## 14. 交付物

- Hex source 竖向标签组件和 Hex panel 布局。
- Inspector 工具栏下拉框、`Hex follows pixel` 清理及新接线。
- Workspace 状态恢复与错误回退测试。
- 组件级、MainWindow、DPI 和可访问性测试。
- 更新后的用户指南和当前 UI 计划。
- 修改前后截图，至少包含：
  - File selected；
  - IDAT selected；
  - Inflated unavailable 与 ready；
  - Defiltered selected；
  - 窄 Inspector 和 `900×600` 窗口。
- 验证命令及结果。

## 15. 完成定义

只有同时满足以下条件才能标记完成：

1. Hex Source 下拉框已从 Inspector 工具栏完全移除。
2. `Hex follows pixel` 控件、状态字段和持久化键已移除，旧设置可安全忽略。
3. 四个竖向标签位于 Hex View 左侧，顺序、文案和语义正确。
4. 四个标签控制同一个窗口化 HexView，没有复制 stage 数据或解码工作。
5. Pixel 选择不自动定位 Hex；Chunk 选择在 File source 中正确定位物理范围。
6. unavailable/error/StageSet ready 和 stale generation 行为正确。
7. `view/hexSource` 保存、恢复、损坏回退和 Reset Layout 正确。
8. X/Y、Lock、DEC/HEX、Selection 和布局无回退。
9. 自动化测试、GUI Gate、DPI 检查和仓库布局审计全部通过。

## 16. Implementation evidence (2026-08-23)

- Added `HexSourceTabBar` with stable `HexSource` item data, West tabs,
  keyboard focus, tooltips and accessible names.
- Reduced tab padding and constrained the bar to its content-sized width so
  the four source labels leave maximum space for the shared HexView.
- MainWindow now owns one shared HexView inside `hexPanel`; the Inspector
  source combo and inactive Hex-follow control were removed while workspace
  source persistence and chunk-to-File navigation remain intact.
- Added component and MainWindow/Gate regression tests. Focused dev builds and
  offscreen GUI tests pass; the required app bundle is rebuilt under
  `build/dev/apps/png-analyzer-gui/pnga_analyzer_gui.app`.

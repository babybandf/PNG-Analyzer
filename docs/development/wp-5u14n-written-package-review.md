# WP-5U14N 书面工作包审查与冻结

- 审查日期：2026-09-04
- 审查对象：`docs/development/wp-5u14n-native-theme-evidence.md`（design approved; pending written-package review, 2026-09-01）
- 基线：main `f8951a1`（WP-5U12F PASS、WP-5U15 PASS、WP-5U14 自动化主题测试 PASS——三项依赖全部满足）

## 一、结论

**APPROVED WITH FREEZE AMENDMENTS（批准并冻结）**。包的目标、矩阵与完成定义成立；发现 1 处陈旧目标名缺陷（R6）与 1 处允许路径缺口（R1），经下列裁决修正后冻结，实施计划见
`docs/superpowers/plans/2026-09-04-wp-5u14n-native-theme-evidence.md`。

## 二、依赖与现状核验

| # | 前提 | 结果 |
|---|---|---|
| 1 | WP-5U15 PASS | ✓（完成记录在库） |
| 2 | WP-5U12F PASS（证据覆盖最终静态 UI） | ✓（completion doc PASS，main `ea5d8fa` 起） |
| 3 | WP-5U14 自动化主题测试 PASS | ✓（`gui_application_theme_tests` 等常绿） |
| 4 | 可复用基座：WP-607C 语料（fixture SHA 可入记录）、`ApplicationTheme::ThemeMode{kSystem,kLight,kDark}` 可编程切换、既有 native/offscreen gate 模式 | ✓ |

## 三、冻结裁决（Rulings）

- **R1 允许路径修订（授权新捕获目标）**：原生截图必须由真实平台渲染，Python 无法驱动。裁决：授权新增 **test-only** 捕获目标 `pnga_gui_wp_5u14n_native_capture_tests`（+ CTest 注册），作为捕获基础设施的一部分；禁止任何生产代码改动。包 §Allowed paths 的 "focused GUI test changes only for a reproducible native defect" 按此修订为 "test-only capture target and its CMake registration included"。
- **R2 记录模式与 fixture 钉死**：捕获记录 schema 定名 `pnga-wp5u14n-native-capture-v1`，字段 = 包 §Required matrix 记录清单（OS build/arch/Qt/请求+生效主题/逻辑 DPI/DPR/窗口尺寸/commit/fixture SHA-256/UTC 时间戳）+ capture PNG SHA-256 + per-cell result。fixture 钉死 WP-607C ids：`ui-rgb8-five-filters`（默认/Compression 三页）、`ui-gray1-none`（narrow Inspector）、`trace-stored-literals`（Stored 视图）。
- **R3 矩阵拆分（自动化 vs 手工）**：自动化捕获 = 各主题模式 × 视图 × 100%（Windows）/ 原生 Retina（macOS）。手工单元 = Windows 150%/200% 系统缩放、macOS 一个逻辑缩放案例、以及包 §Manual checks 全部六项交互检查。缺平台/缺硬件的单元按包定义记 `BLOCKED`，不得冒充 PASS。
- **R4 System 模式自动化边界**：自动化捕获按"每模式全新启动"采集（System 单元在 OS 外观翻转后的新进程中采集）；"System 模式对 OS 配色变化实时响应（不重开文件/不重置选择）"属于包 §Manual checks 手工单元，不做自动化。
- **R5 Windows 执行策略**：优先 GitHub `windows-latest`（真实桌面会话可运行并抓取原生窗口；System Dark 经注册表 `AppsUseLightTheme` + `WM_SETTINGCHANGE` 广播实现）自动化 100% 主题单元；150/200% 与交互检查需用户 Windows 硬件手工执行。两条路径都不可用时相应单元记 `BLOCKED`。
- **R6 缺陷修正**：包 §Verification 引用的 `pnga_gui_tests` 目标不存在（grep 无定义）。冻结修正为真实目标：`pnga_analyzer_gui` 与 `pnga_gui_application_theme_tests`（及聚焦正则维持原语义）。
- **R7 offscreen 隔离**：捕获测试在 `QT_QPA_PLATFORM=offscreen` 下必须显式跳过（skip），保证常规 dev 套件（53/53）在无显示环境保持确定性全绿；捕获条目仅在原生平台执行。

## 四、执行拓扑（冻结）

- macOS 原生：本机执行（Retina 原生 DPR 2；逻辑缩放案例 = 手工单元）。
- Windows 原生：CI `workflow_dispatch` 专用工作流（不进常规 push 流水线）自动化 100% 主题单元；DPI/交互 = 手工单元。
- 证据：`build/evidence/wp-5u14n/`（不入库），评审清单与记录 SHA 入 `docs/development/` 证据文档。

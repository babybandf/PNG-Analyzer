# WP-607A 书面工作包审查与冻结

- 审查日期：2026-09-04
- 审查对象：`docs/development/wp-607-cross-platform-quality-evidence.md` 的
  WP-607A 概要，以及独立任务包
  `docs/development/wp-607a-native-gui-accessibility.md`
- 审查基线：main `d7933da`
- 依赖状态：WP-5U15、WP-5U12F、WP-5U14N、WP-607C 全部 PASS

## 一、结论

**APPROVED WITH FREEZE AMENDMENTS（批准并冻结）**。WP-607A 可以进入实施
计划。下列 R1–R10 是实施计划和执行 Agent 的强制边界。

## 二、已核验的复用基础

| # | 基础 | 核验结果 |
|---|---|---|
| 1 | `MainWindow::openFile`、原生 drag/drop、File/View actions、dock float/reset 已有可观察接口与测试 | ✓ |
| 2 | Preview、Hex、Inspector、坐标栏及 Compression 三页已有稳定 objectName/accessibleName | ✓ |
| 3 | QAccessible name/role、键盘、clipboard、rapid-switch 和 trace pipeline 已有 offscreen 自动测试先例 | ✓ |
| 4 | WP-5U14N 已提供原生窗口捕获、平台拒绝、运行超时、证据哈希及 Windows workflow_dispatch 先例 | ✓ |
| 5 | WP-607C 提供全部所需静态 fixture、稳定 id、SHA-256 与 corpus revision | ✓ |
| 6 | Statistics/APNG 尚未完成，父包允许 static-v1 将对应工作流记为 `out_of_scope` | ✓ |

## 三、冻结裁决

- **R1 独立证据契约**：WP-607A 使用
  `pnga-wp607a-native-gui-v1`，不得扩写或改称 WP-5U14N 主题截图证据。
  WP-5U14N 可证明主题/原生渲染先例，不能替代行为和屏幕阅读器单元。
- **R2 平台冻结**：Windows stable x64、macOS stable arm64、Ubuntu 24.04
  LTS x86_64。记录真实 OS build；未执行的版本不形成支持声明。
- **R3 Linux native 边界**：Ubuntu 必须记录 `xcb` 或 `wayland` 以及
  X11/Wayland 会话。Xvfb/offscreen/minimal 只可跑自检，不得满足 native 单元。
- **R4 自动化/人工分界**：A01–A11 在三平台原生基础尺度自动执行；
  M01–M06 和真实桌面缩放由产品负责人执行。QAccessible 元数据测试不等于
  Narrator/VoiceOver/Orca 实际播报。
- **R5 屏幕阅读器判断**：冻结 Narrator、VoiceOver、Orca。通过标准是名称、
  role、state/value 和关键 selection/status 变化的语义完整；不要求平台话术逐字
  相同，不要求录音，也不得只凭截图判 PASS。
- **R6 fixture 冻结**：只使用任务包列出的五个 WP-607C id；不得以开发者本机
  文件、网络下载或新二进制替代。每条记录携带 fixture SHA 和 corpus revision。
- **R7 数据最小化**：证据禁止绝对路径、用户名、hostname、剪贴板内容和音频；
  machine label 必须非敏感，原始证据留在 ignored build 目录，仓库只存摘要和哈希。
- **R8 不改生产代码**：新建 test-only target、runner、dispatch workflow 与证据
  文档已获授权；生产缺陷使单元/包 FAIL，修复另开任务并先有失败回归测试。
- **R9 状态诚实性**：平台/硬件/屏幕阅读器缺失为 BLOCKED，执行后暴露缺陷为
  FAIL，`NOT_CONFIGURED` 不能满足单元；Statistics/APNG 只能是显式
  `out_of_scope`，不可写 PASS。
- **R10 执行顺序**：先冻结 record/self-test，再实现 native target 和 runner；
  随后依次收集 macOS、Windows、Ubuntu 自动化证据，最后执行人工矩阵和结项。
  WP-607D 不得提前把部分平台结果推断成总体验收。

## 四、替代方案评估

1. **复用 WP-5U14N 执行拓扑、建立独立 WP-607A target/runner（采用）**：
   复用已验证的平台启动和证据防伪策略，同时保持工作包、schema、结果边界独立。
2. 扩展 WP-5U14N target：改动较少，但污染已关闭包并混淆主题与无障碍证据。
3. 全人工清单：启动快，但不能稳定证明 rapid-switch、typed navigation 和证据完整性。

## 五、实施计划约束

- 计划保存为
  `docs/superpowers/plans/2026-09-04-wp-607a-native-gui-accessibility.md`。
- 每个任务均以失败契约或 runner self-test 开始，拥有独立 gate 与提交。
- 不新增生产 test hook；只消费公开接口、稳定 objectName 和现有 corpus registry。
- 实施计划必须覆盖 A01–A11、M01–M06、三平台 scale、证据 schema 验证、
  隐私字段拒绝和最终 WP-607 父状态更新。

## 六、生效

本审查与独立任务包入库后，WP-607A 允许按冻结计划串行实施。若执行发现需要
修改生产路径、依赖、平台范围或完成定义，必须停止并提交独立裁决，不得在实现中
自行放宽。

# WP-APNG-INSPECT 完成记录

状态：IN PROGRESS（T00 完成；T01–T12 未开始）。

## T00：静态基线与验收 runner

日期：2026-09-09。工作分支：`wp-apng-inspection`（worktree `.worktrees/wp-apng-inspection`）。

### 产出

- 新增 `scripts/run_apng_inspection_gate.py`：
  - `--phase baseline|candidate --out PATH`（强制 out 为 build 目录子目录，逐级查找 `CMakeCache.txt`）。
  - 仅使用 subprocess 参数列表，无 shell 插值。
  - 必需测试存在性断言：`analysis_engine_artifact_store_tests`、`statistics_engine_tests`、`gui_stage_inspector_tests`、`gui_selection_view_state_tests`。
  - baseline 记录 HEAD、dirty 文件、工具链（cmake/ctest/python/Qt）、64 项 ctest 逐测试状态（`--output-junit`）、静态期望文件 SHA-256、5 次性能语料运行（每次独立 stdout/stderr/record.json）。
  - candidate 比较 HEAD、期望 hash、新增失败、缺失测试与性能中位数（时间 max(5%,1ms)、RSS max(2%,1MiB)），并复跑 `--enforce-thresholds`。
- 本任务未修改任何生产源码。

### 验证证据

- 空跑拒绝：以包含 `bogus_nonexistent_tests` 的临时副本运行 `--phase baseline`，输出 `required tests missing: ['bogus_nonexistent_tests']`，退出码 1。
- baseline 实跑：`python3 scripts/run_apng_inspection_gate.py --phase baseline --out build/dev/apng_inspection_gate/baseline` 退出码 0。
  - HEAD = `2df248bb3bfb472980bad89e41dcb389bbfe52df`。
  - 工具链：cmake/ctest 4.4.2、Python 3.9.6、Qt 6.11.1、Darwin arm64。
  - ctest 64 项全跑，退出码 8（3 个测试失败，全部为已归因的既有环境性失败，见下）。
  - 静态期望 hash 清单 7 项：`tests/unit/statistics/golden/{ready,partial}-v1.{json,csv}`、`tests/unit/statistics/serialization_test.cpp`、`tests/unit/trace-model/selection_test.cpp`、`tests/integration/cli/cli_test.cpp`（后三者为内联 golden 的测试源文件）。
  - 性能语料 5 次全部 exit 0，corpus revision `5df99ad8…`，每跑 8 个 scenario，记录存于 `build/dev/apng_inspection_gate/baseline/perf-run-{1..5}/`。
- dev preset 配置确认：Qt 6.11.1 GUI target 已启用，64 项 ctest 中包含全部必需 Qt/GUI 测试，未退化为纯 CLI 构建。

### 基线既有失败归因（offscreen 环境）

全部 3 项失败在主仓库同环境复跑结果一致（预存在，非本包引入），均为 `QT_QPA_PLATFORM=offscreen` 下的真实窗口/焦点/渲染依赖：

| 测试 | 失败用例 | 归因 |
|---|---|---|
| `gui_main_window_layout_tests` | `coordinateInteractionUsesToolbarAndKeyboard`、`dockSeparatorsShowThreeDotAffordance` | hover 状态更新与 dock 分隔点视觉检查需原生窗口渲染 |
| `gui_wp607a_native_gui_gate_tests` | `keyboardFocus` | Tab 焦点遍历需原生窗口 |
| `gui_statistics_inspector_tests` | `keyboardTabOrderCoversActionsAndTables` | Tab 焦点遍历需原生窗口 |

最终 PASS 仍须按 T11 提供原生 GUI 证据；本归因不豁免任何必要门槛。

### 环境说明

- worktree 通过符号链接 `.deps -> 主仓库 .deps` 复用 vcpkg checkout；依赖安装走本仓库 build 目录与 vcpkg binary cache，未改动依赖清单。
- 基线 manifest 中 dirty 列表包含本包任务文档与 gate 脚本本身（均为本包允许路径的新增文件），无未知脏文件。

### Exit 判定

T00 Exit 条件满足：基线证据存在，runner 能拒绝空测试/缺失必需测试；本包生产代码尚未开始。

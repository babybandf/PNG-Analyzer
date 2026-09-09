# WP-APNG-INSPECT 完成记录

状态：IN PROGRESS（T00、T01 完成；T02–T12 未开始）。

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

## T01：身份、ticket 与坐标契约

日期：2026-09-09。

### 产出

- `libs/trace-model/include/pnga/trace-model/inspection_context.h` + `src/inspection_context.cpp`：
  `AnalysisKey`、`InspectionTicket`、`PublicationScope`、`accepts_publication`（C1）。
- `libs/analysis-engine/include/pnga/analysis-engine/analysis_target.h` + `src/analysis_target.cpp`：
  `LocalPoint`、`AnalysisTarget`、`TargetResult`、`make_frame_target`（仅声明，T02 实现）、
  `frame_local_point`、`query_frame_coordinate`（C1 坐标与 publication 部分）。
- 模块 README 新增对应 Responsibility 条目；两模块 CMake 加入新源文件。
- 测试：`tests/unit/trace-model/inspection_context_test.cpp`、
  `tests/unit/analysis-engine/analysis_target_test.cpp`；`apng_inspection_identity_tests`、
  `apng_inspection_engine_tests`（`[apng-inspect]` 标签过滤）注册进既有测试可执行程序。
- 实现说明：`query_frame_coordinate` 先检 identity（不匹配 → kNotApplicable）与矩形（越界 →
  kOutOfRange），内部以中性 StaticImage 身份调用既有 `query_coordinate`（其内核仅接受静态身份），
  输出坐标恢复全局并恢复帧身份；pass-local 字段（local_x/row_in_pass/stream_row）保留帧局部语义。
  `frame_local_point` 全程 checked 比较/减法，拒绝零尺寸矩形，无溢出路径。

### red/green 证据

- red：新增测试先于实现构建，首次编译失败精确符号为
  `pnga/trace-model/inspection_context.h: file not found` 与
  `pnga/analysis-engine/analysis_target.h: file not found`；随后修复一次实现期编译错误
  （`analysis_target.cpp:30` ImageCoordinate 与 ImageIdentity 误比较）。
- green：`ctest --preset dev -R "apng_inspection_"` → 2/2 通过。新增 6 个用例：
  trace-model 3 个（Target scope 按 key/epoch 接受、Pixel scope 额外要求同 stage、相同 ticket
  双 scope 接受）；analysis-engine 3 个（frame_local_point 矩形映射含 uint64 极值、
  query_frame_coordinate 全局恢复、身份不匹配与矩形外拒绝）。

### 静态门槛

G-static：`cmake --build --preset dev --parallel 4` 无错误；`ctest --preset dev` 仅剩 3 个
T00 已归因的既有环境性失败（gui_main_window_layout / gui_wp607a_native_gui_gate /
gui_statistics_inspector），与基线一致，无新增失败；`trace_model_selection_tests` 通过，
旧 Selection 持久格式未变；`verify_repository_layout.py`、`verify_dependencies.py`、
`git diff --check` 全部通过。

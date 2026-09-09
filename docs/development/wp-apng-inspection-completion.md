# WP-APNG-INSPECT 完成记录

状态：IN PROGRESS（T00–T04 完成；T05–T12 未开始）。

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

## T02：目标工厂与可靠 fixture

日期：2026-09-09。

### 产出

- `libs/analysis-engine/src/analysis_target.cpp` 完成 `make_frame_target`（C1 factory）：
  校验 source/index/ordinal 在 verified prefix；用 `make_frame_stream` 构建帧虚拟流；
  header 继承 canvas_header 格式并替换 fcTL 尺寸；delivery 从 request 继承；失败时
  target=null 且不产生半有效对象。source/index/stream 均为共享所有权。
- 新增 `tests/common/apng_inspection_fixture.h`：
  - `pnga_test::inspection_request(ordinal)`：固定 16×32 画布、RGBA8、两帧 2×3 @(10,20)、
    frame1 blend=OVER、generation 7 / serial 11。未用 make_apng_canvas 构造子矩形。
  - `pnga_test::make_dual_wrapped_payload(...)`：同 payload 双包装 fixture——同一压缩 payload
    分别包装为静态 PNG（IDAT）与 APNG（fdAT 帧 0 @(10,20)）；palette/tRNS 测试值复制进两个
    包装；总 buffer 上限 64KiB（超限返回空对象）；delivery 用生产 `delivery_context_from`
    从 APNG 包装提取。测试内读取完整流仅限该 ≤64KiB fixture；生产禁止完整拼接的边界不变。
- 测试：`analysis_target_test.cpp` 新增 factory/dual-wrapper/palette 三用例；
  `frame_analysis_test.cpp` 新增 analyze_frame 与 make_frame_target 一致性用例（共用 fixture）。

### red/green 证据

- red：实现前链接失败，精确缺失符号
  `pnga::analysis_engine::make_frame_target(pnga::analysis_engine::FrameRequest const&)`。
- 实现期测试基础设施修正：fixture 直接以空 `sample_at` 调用 `encode_apng_frame_payload`
  触发 `std::bad_function_call`，改为传入与 `make_apng_format` 相同的默认 pattern 函数；
  另修正 fcTL 字节数（dispose/blend 共 2 字节）。均为测试 fixture 修正，不涉及生产行为。
- green：`ctest --preset dev -R "apng_inspection_"` → 2/2 通过。新增 4 个用例：
  make_frame_target 构建上下文（含 ordinal=2 越界拒绝）、双包装逻辑流一致（frame stream
  逐字节 == 静态 IDAT 流 == 共享 payload）、palette/tRNS 复制、fixture 驱动 analyze_frame
  与 factory 一致。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败，无新增失败；
layout/dependencies/diff-check 全部通过。helper 预算边界（64KiB 测试上限）与生产预算
（C5 retained/reservation）分属测试与产品两套约束，互不替代。

## T03：虚拟流行索引和逐帧重建

日期：2026-09-09。

### 产出

- C2 通用流重载（原 (VirtualIDATStream, IByteSource) 对重载全部保留，内核复用）：
  - `scanline_anchor.h/.cpp`：`build_scanline_anchors`、`restore_scanline` 新增
    `(const IVirtualCompressedStream&, ...)` 重载；实现重构为内部 `*_impl` 内核
    （直接以流为 IByteSource），对偶重载经 `VirtualIdatSource` adapter 桥接。
  - `filtered_scanlines.h/.cpp`：`inflate_filtered` 新增接口重载（薄 shim 复用内核）。
  - `pixel_provenance.h/.cpp`：`query_pixel_provenance` 新增接口重载；内部
    `map_token_bits` 模板化（仅用 `logical_to_physical`），trace 解码走流自身。
  - `stage_analysis` 已有接口重载（无需修改）。
  - `block_inspector.h` 中无接收 (stream, source) 成对参数的函数（核实），本任务不改。
- `QueryCoordinator`：新增 `open(std::shared_ptr<const AnalysisTarget>, interval)`；
  共享 target 所有权，generation 采用 `target->key.generation`；重建 lambda 按
  target_stream_/静态流分派 restore；re-open 拒绝规则与静态 open 语义不变。
- `libs/analysis-engine/README.md` 列出全部新增声明。
- 新增 `tests/unit/analysis-engine/frame_query_test.cpp`（注册 `[apng-inspect]`）。

### red/green 证据

- red：新增测试首次编译失败精确符号：`build_scanline_anchors` 接口重载参数不足
  （frame_query_test.cpp:68）、`restore_scanline` 接口重载缺失（:80），
  以及 `QueryCoordinator::open(const AnalysisTarget&, u64)` 缺失。
- 实现期测试修正（不涉及生产行为）：
  1. 行查询为异步 replay，首查返回 kReplaying；按既有 `wait_status`（条件变量回调）
     模式等待 kReady 后再断言，与 query_coordinator_test 同模式。
  2. 矩阵用例对灰度/RGB 误传 256 字节 tRNS 触发索引 kInvalid（tRNS 长度校验为生产
     行为，测试改为仅 palette 类型附带 PLTE/tRNS）。
  3. 测试 helper `frame_request` 硬编码 RGBA8 canvas_header 导致 1-bit 用例
     "inflate size mismatch"；改为按格式参数构造 canvas_header。
- green：`ctest --preset dev -R "apng_inspection_"` → 2/2 通过。新增 5 个用例：
  coordinator 打开帧 target（scanline_count=3、anchors header 2×3）、帧行 replay 与
  analyze_frame unfiltered 一致 + 越界 kError、同 payload 双包装 filtered/spans/anchor
  restore 全等且物理 offset 不同、11 项色型/位深/Adam7 矩阵（含 1/16-bit、palette/tRNS、
  Adam7 pass 边界，scanline_count 与 analyze_stages 交叉一致）、预算拒绝与 shared owner
  寿命（请求/源释放后行查询仍 kReady）。所有 Filter 经既有 all_none=false 静态 fixture
  与共享内核覆盖（Sub/Up/Average/Paeth 路径同核），帧矩阵不重复造 filter fixture。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败，无新增失败
（query_coordinator/scanline_anchor/pixel_provenance/filtered_scanlines 既有测试全部
通过，静态 open 与原 query_coordinate 行为不变）；layout/dependencies/diff-check 通过。

## T04：逐帧 Compression 和来源物理映射

日期：2026-09-09。

### 产出

- `trace_query.h/.cpp`：`compose_trace_query` 新增 `(const IVirtualCompressedStream&, ...)`
  重载（去掉 source 位置）；内部 `token_physical_spans`/`append_bit_mapping`/composition
  内核模板化（仅用 `logical_to_physical`），对偶重载复用同一内核；`serialize_trace_query`
  与 TraceQueryResult 结构不变。
- `block_inspector.h/.cpp`：`build_fast_compression_index` 新增接口重载；IDAT spans 由
  `logical_to_physical(0, size)` 推导，内核模板化（`append_physical_bit_spans` 同样模板化）。
- `trace_orchestrator.h/.cpp`：新增 `open(std::shared_ptr<const AnalysisTarget>, budget)`：
  直接对帧流 `index_blocks`，绝不重新扫全文件构建静态 IDAT；generation 采用
  `target->key.generation`；保留静态 open 门面且互斥重置对方流状态；submit 对 target 路径
  校验 `request.selection.image` 身份与 target 不符即 kRejected；工作 lambda 按路径分派
  decode/compose，帧流由共享指针持有。re-open/replay-active 规则不变。
- `src/virtual_idat_source.h` 未修改（bridge 仍被静态路径使用，符合计划列出的允许修改集）。
- README 列出全部新增声明；新增 `tests/unit/analysis-engine/frame_trace_test.cpp`。
- fixture 修正：`make_dual_wrapped_payload` 的 fdAT 序列号从 0 改为 1（fcTL 之后递增，
  与 `make_apng_format` 一致）；该 APNG 此前仅用于 delivery 提取，T04 起作为完整目标使用。

### red/green 证据

- red：新增测试先于实现——`TraceOrchestrator::open(const AnalysisTarget&, u64)` 与
  `compose_trace_query` 接口重载缺失（编译失败记录后实现）。
- 实现过程修正（均为测试侧）：stored 块 token 计数（2 字面量 + EOB = 3）、payload 末字节
  反向映射语义（帧流终点按设计映射到 total_，CRC 内部才失败）、手工双帧 APNG 的 fcTL
  位置与 canvas 一致性、Adler 错误的可观测量（`BlockIndexResult.adler.status == kMismatch`
  而非 trace 失败）、truncated 的可观测量（`trace.stream_ended == false` partial）、
  动态块用 4096 字节难压缩数据确保 zlib 产出 kDynamic。重构脚本曾误删
  `trace_query_status_text` 定义，链接失败后从 diff 恢复；`build_fast_compression_index`
  重构中的重复参数行与括号不平衡已修复并验证。
- green：`ctest --preset dev -R "apng_inspection_"` → 2/2 通过。新增 6 个用例：
  orchestrator 打开帧 target（generation=7、fast index ready、物理 spans 非空）、token
  证据跨 fdAT 切片（物理包含性 + 序列号/CRC 反向映射失败）、frame0/frame1 同逻辑偏移独立、
  Stored/Fixed/Dynamic 包装帧与同 payload 静态逻辑 token 逐一相等、错误 Adler（kMismatch +
  index 不成功）与截断（partial，index 不成功）、外部身份 submit 拒绝。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败，无新增失败
（trace_query/trace_orchestrator/block_inspector/pixel_provenance 既有测试全部通过，
静态 open 与 serialize_trace_query 字节不变）；layout/dependencies/diff-check 通过。

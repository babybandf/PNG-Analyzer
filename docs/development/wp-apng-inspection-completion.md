# WP-APNG-INSPECT 完成记录

状态：IN PROGRESS（T00–T09 完成；T10–T12 未开始）。

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

## T05：帧统计与独立导出 schema

日期：2026-09-09。

### 产出

- `libs/statistics`：新增 `frame_statistics.h/.cpp`（`FrameStatistics` +
  `serialize_frame_statistics_json/csv`，schema `pnga.frame-statistics` v1，字段顺序
  schema/schema_version/document/identity/geometry/bytes/sections；ratio 仅在 snapshot
  complete 且分母>0 时输出 "payload/inflated"，否则 JSON null/CSV 留空；StaticImage 输入
  返回 error）。section 编码从 `serialization.cpp` 机械提取到共享的
  `serialization_sections.h/.cpp`（envelope 各自保留；提取为纯搬移，v1 golden 逐字节不变，
  由既有 `statistics_engine_tests` golden 对比验证）。
- `libs/analysis-engine`：新增 `frame_statistics.h/.cpp`（`collect_frame_statistics`，
  C3）：复用 `StatisticsAccumulator`；payload_bytes=stream.size；chunk_overhead=38(fcTL)
  + 每 IDAT 12 / 每 fdAT 16（由 span 前的字节窗口读取物理 chunk type 验证）；
  inflated=各非空 pass height*(1+row_bytes) checked sum，且与 filtered.size() 互验后才计入
  overview totals；chunks 统计仅含 fcTL(26)+帧自身数据块（fdAT data 含 4 字节序列号）；
  进度沿用 100ms 节流 + monotonic_millis seam；每 256 样本与阶段边界检查取消，取消保留
  verified prefix。不修改 `collect_document_statistics` 语义。
- C7：`statistics_occurrence_query.h/.cpp` 新增 `query_frame_statistics_occurrence`；
  `map_logical_bits`/`run_block_occurrence`/`run_filter_occurrence`/token 扫描内核模板化，
  静态路径行为不变（segment 回退经 `if constexpr` 保留，帧流经零长映射锚定）；帧模式
  chunk 域仅覆盖帧自身数据块 + 推导定位的 fcTL（38 字节固定布局，位置经 type 读取验证），
  其余 key kNotFound；`attach_frame_identity` 使所有帧模式结果的 image 坐标携带
  `target.key.identity`，filter 行提示转为画布全局行；File chunk 域不转换。全文件 chunk
  跳转继续走原 `query_statistics_occurrence`。
- 模块 README 更新；`tests/unit/statistics/frame_statistics_test.cpp`（4 用例）与
  `tests/unit/analysis-engine/frame_statistics_test.cpp`（5 用例）；CMake 注册
  `apng_inspection_statistics_tests`。

### red/green 证据

- red：新增测试先于实现——`serialize_frame_statistics_json`、`collect_frame_statistics`、
  `query_frame_statistics_occurrence` 符号缺失（编译失败记录后实现）。
- 实现过程修正（测试侧为主）：CSV 行写入器 leading cells 后补逗号（该错误曾破坏 v1
  golden，修复后 golden 恢复逐字节一致）；fdAT 数据 span 从序列号后开始，chunk type 位
  置按 IDAT(-4)/fdAT(-8) 双候选窗口读取；fcTL 锚定算术按 fdAT 完整 chunk 布局（-12-38）
  修正并由测试固定；注入时钟测试改为每次查询 +60ms 才能跨过 100ms 节流阈值；静态侧
  token 对比改用 `scan_tokens`（`decode_stored_and_fixed` 不解 dynamic）；共享编码
  提取的机械搬移曾两次误删相邻函数（`trace_query_status_text`、token 域解析段），链接
  /编译失败后从 git diff 恢复。
- green：`ctest --preset dev -R "apng_inspection_|statistics_engine"` → 4/4 通过。
  statistics 新增 4 用例：envelope 字段顺序、ratio 可用性规则（complete+分母>0，零与
  不可用均 null/留空但 inflated 行可区分）、CSV 固定列、StaticImage/fingerprint 拒绝。
  analysis-engine 新增 5 用例：字节账目（payload/inflated=27/overhead=54）+ 全 section
  ready + complete()、身份/预算/取消拒绝、时钟 seam 节流、双包装 token/block/filter
  计数一致、帧 occurrence 四域 + fcTL 锚定 + 非帧 key kNotFound。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败，无新增失败；
`statistics_engine_tests`（v1 golden 逐字节）通过，`collect_document_statistics` 及
occurrence 静态路径行为不变；layout/dependencies/diff-check 通过。

## T06：有界 canvas 像素来源

日期：2026-09-09。

### 产出

- 新增 `libs/analysis-engine/include/pnga/analysis-engine/canvas_pixel_query.h` +
  `src/canvas_pixel_query.cpp`（C4 全接口）：`CanvasOperation`、`CanvasPixelNode`、
  `CanvasPixelPending`、`CanvasPixelCursor`、`CanvasPixelRequest`、`CanvasPixelResult`、
  `query_canvas_pixel`。
- 语义实现：
  - 阶段解释：PostBlend（矩形内 blend 节点/矩形外 kCarry→PreBlend）、PreBlend(0)=kClear 叶、
    PreBlend(N>0)=alias→PostDispose(N-1)（历史帧步计费）、PostDispose（dispose 1=kClear 叶、
    2=kRestore→本帧 PreBlend、0 或矩形外=alias→PostBlend）、FrameOutput（矩形内
    kFrameSample 叶——经 `analyze_frame` 解码；矩形外 kCarry 叶无贡献）。
  - 数值 RGBA 复用生产 compositor：`blend_into` 以 1×1 图像逐位复用整数公式（SOURCE 复制/
    OVER 舍入）；测试预期用测试内独立 oracle（APNG 规范公式重实现），不调用被测路径。
  - 每页闭合局部 DAG：显式 child-slot 记录，边只引用本页较小索引；先当前帧源、再
    destination 历史的确定性 DFS；自环经 in-flight 集检测报 error。
  - 预算（C5 固定值）：每页 ≤4096 节点、≤1024 历史帧步、≤4MiB 节点内存、pending ≤1024；
    超限把未展开状态截断为 kCarry 叶（页保持闭合），状态按 DFS 序进入 next.pending；
    继续时整个 pending 栈作为新页初始工作栈，不重遍历已完成前缀、不跨页引用。
  - cursor 校验：version/ticket/x/y 一致、非空 pending、非法 stage、超出 verified prefix
    或晚于目标帧均拒绝；visited_steps checked 递增，溢出按预算截断。
- `animation_replay.h/.cpp` 未修改：混合公式在 `png-reconstruction/canvas_composition`
  （公开 API）与 `analyze_frame`（公开 API）中已可复用，无需暴露新接口——这是满足
  "仅暴露/复用已有数值回放" 的最小改动。
- README 新增条目；新增 `tests/unit/analysis-engine/canvas_pixel_query_test.cpp`。

### red/green 证据

- red：新增测试先于实现——`CanvasPixelRequest/Result/query_canvas_pixel` 符号缺失
  （编译失败记录后实现）。
- 实现过程修正：第一版以"末尾 N 个节点=子节点"假设计算 DAG 边，在混合子树非连续发射时
  产生错误输入边（SEGVEV 暴露），改为显式 child-slot 记录；早期取消返回补上
  `stop=kCancelled`；测试侧两次坐标口径修正（fixture pattern 以帧局部坐标取样）与
  矩形外链路/目标节点断言按实际闭合 DAG 语义修正（root 为 kCarry、frame1 目的地为
  frame0 的 kRestore）。
- green：`ctest --preset dev -R "apng_inspection_"` → 3/3 通过。新增 8 个用例：
  首帧 PreBlend=kClear 叶（计划最小测试）、SOURCE 替换、OVER+BACKGROUND+首帧 PREVIOUS+
  NONE alias+多帧 OVER（独立 oracle 值）、矩形外跨帧 Carry 链到 kClear、与
  AnimationReplay 数值一致、请求校验（缺源/静态身份/非法 stage/越界坐标/取消）、
  2000 帧 PREVIOUS 链分页（partial→继续推进→ready，边全部指向较小索引、pending≤1024）、
  cursor 六类拒绝。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败，无新增失败
（animation_replay/pixel_provenance 既有测试全部通过，compositor 算法零改动）；
layout/dependencies/diff-check 通过。

## T07：APNG 分析会话、预算与后台生命周期

日期：2026-09-09。

### 产出

- 新增 `apps/png-analyzer-gui/src/frame_inspection_session.h/.cpp`：
  `FrameInspectionSession`（QObject）持有 C1 目标与 C5 调度/缓存：
  - `selectTarget(target, ticket)`：采纳后台构建的不可变目标并重置发布门；旧上下文的
    队列/在途请求取消、retained 释放；仍在运行的旧 worker 线程后台自然结束
    （finished→deleteLater），UI 线程不 join。
  - `clear(next_generation)`：全部取消 + 状态清零 + generation 推进。
  - `accepts(ticket, scope)`：统一使用 C1 `accepts_publication`（mutex 保护）。
  - `requestFrameAnalysis` / `requestFrameStatistics`：C5 固定额度
    （retained 64MiB、在途 reservation 64MiB、单执行 worker、队列上限 8、优先级
    kSelection→kViewport→kBackground）；同帧重复请求合并（保留最新、取消被替换者）；
    满队列丢弃最低优先级最旧请求并报告取消，后台请求自身被弃、不挤占用户选择；
    单项 reservation 超预算直接拒绝。
  - 每次分析入口先记 reservation，完成/失败/取消/关闭全部路径恰好释放一次；
    完成后经 `accepts(ticket, kTarget)` 门发布（shared immutable model+ticket），
    generation 不符的迟到结果直接丢弃。
  - 帧分析复用公开内核 `analyze_stages` + `deliver_rgba8`（以 target 的流/格式/交付
    上下文为准，无需重扫文件）。
  - 测试 seam：`setExecutorForTesting` 注入确定性 executor（Jobs 手动逐个执行），
    completion 永不在持锁状态调用；生产路径为一次性 QThread worker。
- `animation_controller.h`：新增 `replayRetainedBytes()`（C5 共享预算报告可见性，
  播放路径零行为变化）。`animation_worker.*`/`document_session.*` 本任务无需修改
  （会话接入在 T08/T09 接线时进行）——最小改动原则。
- 静态文档不创建该会话：当前无任何静态路径构造 `FrameInspectionSession`（结构上保证）。
- 新增 `tests/gui/frame_inspection_session_test.cpp`（QtTest + QSignalSpy + 注入
  executor，无 sleep；注册 `gui_frame_inspection_session_tests` 与 `pnga_gui_tests`
  依赖）。

### red/green 证据

- red：新增测试先于实现（类不存在时编译失败记录后实现）。
- 实现过程修正：executor 同步运行 completion 与会话互斥锁重入的死锁风险（改为
  deferred completion + 解锁后派发）；删除 debug 输出时误删闭括号（编译失败立即修复）；
  `analyze_frame` 需要 AnimationIndex 而 AnalysisTarget 不携带——改为经 target 字段
  复用公开内核；早期取消返回补 `stop=kCancelled`。测试侧修正三处场景语义：
  A1→B→A2 序列按 C1 epoch 递增建模（同帧身份、epoch 1/2/3）、队列容量测试用不同帧
  key 避免去重折叠、重复请求合并断言对齐"在途之外合并"语义（在途第一个完成、最新
  合并者随后运行、中间者取消）。
- green：`ctest --preset dev -R "gui_frame_inspection_session_tests"` → 1/1（7 个
  用例全过）：C1 门双 scope（hover 不改 ticket 则同帧 kTarget 统计可接受）、A1→B→A2
  迟到结果不发布、reservation 全路径释放（含 1 字节超配额拒绝）、关闭后不发布、
  满队列弃低优先级不丢选择、同 key 合并、retained 记账与清理。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败，无新增失败
（静态会话/worker 新增计数为 0——静态路径未构造本会话；document_session 既有测试
全部通过）；layout/dependencies/diff-check 通过。

## T08：Inspector/Hex 接收完整帧上下文

日期：2026-09-09。

### 产出

- `StageInspector::setFrameContext(std::shared_ptr<const FrameStageSet>)` +
  `StageInspectorModel::setFrameContext`：一次提交 stages（零拷贝 aliasing 共享所有权）+
  delivered 像素 + identity；x_/y_/stage_ 与静态 setStageSet 相同地复位（切帧无残留
  行/坐标高亮）；identity 经 `model()->identity()` 暴露；clear() 一并清理帧上下文。
  静态 `setStageSet`/`setDeliveredPixels` 调用路径与报告渲染逐字节不变。
- `hex_data_source.h/.cpp`：新增 `make_frame_inflated_hex_source` /
  `make_frame_defiltered_hex_source`（帧 StageSet aliasing，File 源保持文档字节不变）。
- `hex_source_tab_bar.cpp`：tab 映射改为表驱动（`SourcePresentation` 携带 `HexSource`
  身份 + `index_for_source` 查表），删除 `sourceForIndex` 硬编码 switch 与
  `kStreamTabIndex` 常量——"source tabs 不按硬编码索引定位"。
- `trace_inspector_binding.h/.cpp`：新增 `setPublicationGate(std::function<bool()>)`，
  publish/publishFastIndex 在门拒绝时跳过（未设置门时静态路径不变）。
- `statistics_worker.h`：新增 `FrameStatisticsWorker`（一次性 QThread 跑 C3
  `collect_frame_statistics`，结果携带 InspectionTicket，发布决策归 session C1 门）。
- 测试（均加入既有测试类 slots）：stage_inspector（帧上下文原子提交/复位/身份/清理）、
  hex_data_source（帧 Inflated/Defiltered 逐字节 == 帧 StageSet、null 拒绝）、
  trace_inspector_binding（门拒绝抑制发布、门通过正常发布 generation=91）、
  statistics_controller（FrameStatisticsWorker 交付 ticketed 结果：inflated=27、
  overhead=54、ticket 字段齐全）。

### red/green 证据

- red：新增测试先于实现（setFrameContext/帧 Hex 源/发布门/FrameStatisticsWorker
  符号缺失，编译失败记录后实现）。
- 实现过程修正：RgbaImage 的 uint8_t 像素到模型 byte 缓冲的显式转换；binding 测试按
  现有 per-test widget 构造模式修正；FrameStatisticsRequest 需同时携带 target 与
  frame（测试补 analyze_frame）；统计 worker 信号用 Qt::DirectConnection 以便
  wait() 后断言（无 UI 访问）。
- green：受影响 7 个测试可执行程序全过（stage_inspector / hex_data_source /
  hex_source_tab_bar / trace_inspector_binding / statistics_controller /
  compression_selection_store / frame_inspection_session），旧 StageInspector
  报告断言与 Compression selection 历史测试无一改动地保持通过。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败，无新增失败；
layout/dependencies/diff-check 通过。

## T09：完整鼠标键盘交互与目标状态机

日期：2026-09-09。

### 产出（C6/D2–D4 落实）

- D2（bindAnimationUi）：四个动画视图的全部用户事件补齐连接——click→
  onAnimationPixelSelected、hover→新增 onAnimationFrameHovered（帧局部→画布全局转换）、
  leave→onPixelHoverLeft、nudge→nudgeLockedCoordinate、Escape/selectionCancelled→
  clearLockedCoordinate；全部 Qt::UniqueConnection，重复挂载不重复发布。
- D3（活动视图状态）：`activePixelView()` 返回当前活动视图（animation_view_ 优先）；
  setPixelStatus/restorePixelStatus 读取活动视图（动画视图经 origin 转换全局→局部）；
  clearLockedCoordinate 同时清除动画视图十字线；onAnimationPixelSelected 状态文本
  统一为静态格式 "pixel (x, y) RGBA(...)"（修复 D3 状态格式分歧）。
- D4（tab<4 保持帧身份）：mountAnimationUi 的 currentChanged 分为三支——tab 4–7 暂停+
  selectStage；tab 1–3（Pixels/Filtered/Defiltered）仅暂停、保持帧身份（不再调用
  selectStaticFallback）；tab 0（默认图像）暂停+selectStaticFallback（C6：tab0 才进
  StaticImage）。pause 连接同样 UniqueConnection。
- C1 转换：点击/悬停坐标经 frame rectangle origin（animation_origin_x_/y_）转换；
  悬停新增专用槽避免静态 canvas 坐标语义混用。
- 静态槽签名全部保留（onPixelSelected/onPixelHovered/onPixelHoverLeft/publishLockedCoordinate/
  clearLockedCoordinate/nudgeLockedCoordinate 等）；静态视图连接未改动。
- 测试：selection_navigation_controller_test 新增 2 个用例（帧点击全局锁定+活动视图
  状态+Escape 清除；UniqueConnection 去重——每次事件恰发布一次）。

### red/green 证据

- red：新增测试先于实现——帧点击全局锁定断言在 D3 转换前失败（状态读取用静态视图、
  坐标未转换），D2/D4 无新连接时动画事件无路由。
- 实现过程修正：动画视图 hover 为帧局部坐标，与静态 canvas 坐标语义不同——新增
  onAnimationFrameHovered 专用槽转换后委托，静态槽不改动；测试经
  QMetaObject::invokeMethod 发射 protected 信号；测试 bus 需先 setDocumentGeneration
  对齐文档代（bus publish 的 stale 门）。
- green：`ctest --preset dev -R "gui_selection_navigation_controller_tests"` → 1/1
  （14 个用例全过）；gui_apng_controller_tests / gui_apng_ui_tests 全过。

### 静态门槛

G-static 全套：构建无错误；`ctest --preset dev` 仅剩 3 个已归因基线失败（与 T00 基线
一致，无新增失败）；静态视图连接与槽签名逐字节保留；layout/dependencies/diff-check
通过。D5（QueryCoordinator/trace/statistics 绑定静态链路）的帧上下文部分已由
T03/T04/T05/T08 的 target 流与帧上下文接口承接，T10/T11 完成剩余接线。

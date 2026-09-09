# APNG 逐帧完整分析设计与工作包规划

日期：2026-09-09。状态：设计与工作包路线图，已补充逻辑风险审查；不具备直接交付 agent 编码的执行细度，尚未实施，也不代表已批准的仓库 Work Package。

目标仓库：`/Users/lijiangbo/project/PNG-Analyzer`。本文已随正式任务包落盘到目标仓库。第7节保留上一轮路线图审查的历史结论；新增执行细化见 [正式WP](../../development/wp-apng-inspection.md)、[接口契约](../../development/wp-apng-inspection-contract.md) 和 [详细计划](../plans/2026-09-09-apng-per-frame-inspection.md)。执行以正式WP/契约的确定规则为准。

## 1. 目标与交付边界

选中任意已验证动画帧后，预览、Reconstruction、Compression（Block/Huffman/Decode Trace）、Statistics、Hex、悬停、点击锁定与数字坐标输入均使用该帧对应的数据。合成画布提供真实像素值、合成/处置过程和可继续追溯的来源。

不采用隐藏 Compression、静态数据占位、仅替换 RGBA 等过渡交付。工作包可以依赖顺序落地，但只有全部产品验收通过才宣布此功能完成。按需计算、加载状态和预算受限状态是正式能力的组成部分，不等于功能缺失。

不扩展 Compare、动画编辑、导出动画、新解码器或第三方依赖。不增加打开文件时全动画逐帧深度解码。

用户新增硬约束：本次所有修改不能改变非 APNG 的既有行为、结果、输出契约或引入 APNG 专属开销。共享代码的修改必须先建立静态基线，并逐包证明兼容；仅通过 APNG 测试不允许合并。性能按冻结阈值和修改前后的同环境测量验证，不能仅凭设计保证绝对零副作用。

## 2. 已核对的现状

- `frame_analysis.h/.cpp` 已有 `FrameStageSet` 和 `analyze_frame()`，内部通过虚拟帧流复用 `analyze_stages()`。
- `animation_replay.h` 的 `ReplayResult` 只带阶段 RGBA，没有将帧分析证据交给 Inspector。
- `main_window_ui.cpp::presentAnimationFrame()` 替换 Inspector delivered RGBA，没有同时替换其静态 StageSet。
- `animation_controller.cpp::bindAnimationUi()` 对动画视图只连接 pixelSelected。
- `selection_navigation_controller.cpp::setPixelStatus()` 和恢复状态逻辑固定读取静态 image_view；动画点击又进入原有 Inspector。
- `QueryCoordinator`、`TraceOrchestrator` 持有具体 VirtualIDATStream。scanline anchor、trace query、pixel provenance、block inspector 等辅助接口也存在同样耦合，不能只改顶层入口。
- `statistics_collector.cpp` 和 occurrence query 仍使用静态流；Statistics schema v1 是既有固定契约。
- ADR-0004 已规定 ImageCoordinate 的 x/y 是全局坐标，局部转换由 analysis-engine 负责；ADR-0007 已区分 StaticImage 与 AnimationFrame(0)。

## 3. 核心设计

### 3.1 两层上下文

不要用一个可随意变更的全局 StageSet 承担全部状态。

1. AnalysisTarget：文档 generation、ImageIdentity、共享所有权的 IVirtualCompressedStream、有效图像 header、交付上下文。它表示一个真实的压缩图像对象；静态图使用 IDAT adapter，动画帧使用 FrameStream。
2. InspectionContext：AnalysisTarget 标识、当前预览 Stage、交互 revision、当前选择。它表示用户正在检查的画面。

上下文描述符不可变；不要求把全部分析产物打包完成后才能显示。帧索引、行数据、trace、统计可以独立就绪，但每份产物都携带可验证的对象身份。

缓存键使用 document generation + image identity + artifact kind + 必要的 stage/pass/row/range/options。交互 revision 用于拒绝迟到 UI 更新，不进入可复用内容缓存键，避免每次选择都使缓存失效。

帧解码与合成共享不可变产物；不把每帧一个独立完整 worker pool 作为常驻架构。复用现有调度器，在分析会话层控制任务和预算。

### 3.2 提交与异步一致性

- 选帧或切阶段先生成新的请求上下文，清除旧 hover、lock 和像素/流高亮，标记目标正在加载。
- 手动切换目标尚未就绪时固定呈现目标 loading 空态，不保留可误认为目标的旧画面。播放保持已提交帧直到新帧准备好，标题/像素状态与已提交帧一致；待请求帧另列状态，不提前改写已展示帧身份。
- 画面提交时，同步提交活动视图、对象身份、阶段、坐标边界和已经就绪的同对象证据。
- 迟到的行查询、trace、统计进度、统计完成、Hex 导航和历史恢复都校验身份与请求 revision；检查不只放在 AnimationController。
- 播放期间只优先生成展示所需结果；昂贵面板按当前帧显示待分析状态，暂停后请求，不能把上一帧内容伪装为当前帧。
- 关闭文档或切换文件取消任务；后台退出/回收不阻塞 UI；有效但失去焦点的产物仅在预算允许时进入缓存。

### 3.3 预览与 Inspector 行为

在 APNG 文档中，时间轴决定分析对象。选择 Static Fallback 才进入静态对象。Pixels、Filtered、Defiltered 跟随该对象；不得切到这些标签就悄悄检查 fallback。现有静态 PNG 的标签顺序、对象名、交互保持兼容。

| 表面 | Frame Output | PreBlend / PostBlend / PostDispose |
| --- | --- | --- |
| 预览与状态栏 | 帧原始输出像素 | 当前阶段画布像素 |
| Reconstruction | 当前帧 Filter、邻居、native sample、交付 RGBA | 当前阶段运算和来源；选择来源后展开相应帧的重建 |
| Compression | 当前帧 zlib、Block、Huffman、Decode Trace | 默认仍展示当前帧编码流，明确范围；像素 trace 先选择贡献来源 |
| Statistics | 默认当前帧编码统计，可切整个文件 | 默认当前帧编码统计；标题明确它与画布像素不是同一范围 |
| Animation | 当前帧控制字段 | 当前帧控制字段和当前阶段 |
| Hex | File / Frame Stream / Inflated / Defiltered 全部正确绑定 | 字节源绑定明确的编码帧；合成像素不自动制造唯一字节地址 |

Inspector 常驻标题例：`动画帧 2 / 2 · PostBlend`。Compression 子标题例：`编码数据：动画帧 2`。若沿画布来源进入较早帧，子标题和面包屑明确显示来源帧，提供返回当前选择；预览不被暗中切帧。

文件 Chunk 列表保持全文件。点击 fcTL/fdAT 能定位所属帧；文件中的共享 metadata 不强迫改变帧。由 Inspector 导航引起的物理 Hex 高亮不能再反向触发无穷切帧。

### 3.4 坐标与锁定

- 保持 ADR-0004 的规范：Selection 中 x/y 为画布全局坐标，不重定义现有序列化。
- Frame Output 的视图局部点转换为 global=(local+fcTL offset)；反向查询检查 global 在 frame rect 内再做减法，使用 checked arithmetic。
- 帧视图状态显示全局和局部坐标；画布视图只在点位于当前帧矩形内时附加该帧局部坐标。
- X/Y 输入始终为画布坐标；输入不落在 Frame Output 范围时给出范围说明，不静默 clamp，也不读其他图像。
- 所有活动 DeliveredImageView 统一连接 hover、hover-left、click、lock-clear；事件携带视图身份，拒绝非活动或过期视图事件。
- 点击与 X/Y 锁定暂停播放。锁定包含 frame + stage + global point；切帧/切阶段清除锁定。播放恢复时清除锁定；悬停不自动暂停。
- 鼠标移出恢复当前对象的锁定状态；无锁定时恢复当前对象提示。

### 3.5 逐帧重建和压缩

有效帧 header 使用 fcTL 尺寸，继承 IHDR 格式和文档 palette/tRNS。Filter/Adam7 行索引必须基于帧尺寸，不能使用画布行宽。

底层分析入口统一接收 IVirtualCompressedStream 和有效 header。frame decode、row replay、fast block index、Deep Trace、byte provenance 和统计复用同一适配边界，不复制第二套 decoder。

fdAT 的 sequence number 不属于 zlib 数据。跨 fdAT 的逻辑范围映射返回所有物理 payload spans；禁止把有间隙的物理区间合成单一连续高亮。若首动画帧使用 IDAT，允许共享不可变底层字节缓存，但 StaticImage 与 AnimationFrame(0) 的选择身份保持不同。

像素证据链：全局坐标 → 帧局部坐标 → pass/scanline/native sample → unfilter 依赖 → inflated byte range → token/block/Huffman → zlib bit/byte range → 文件 spans。packed、indexed、16-bit、Adam7 复用已有通道/样本/位语义；调色板和 tRNS 作为共享文档来源单独列出。

### 3.6 合成像素来源是正式交付内容

由 analysis-engine 输出可按需展开的运算节点，UI 只格式化，不重算合成公式。

- FrameSample：某帧局部像素，可进入重建与压缩追踪。
- Blend：SOURCE 替换或 OVER；保存输入、输出及实际整数舍入语义。
- Carry：矩形外或沿用已有画布的值。
- Clear：初始透明黑或 BACKGROUND 清除，关联产生它的控制操作；没有压缩样本来源。
- Restore：PREVIOUS 恢复到本帧 PreBlend 状态，而不是“上一帧 Frame Output”。

PreBlend 指向前序 PostDispose（首帧为初始化）；PostBlend 描述本帧应用；PostDispose 描述处置。OVER 可以依赖多个帧，不能强制返回单一来源帧。透明/不透明快捷情况遵循现有 compositor 的精确语义。

按所选像素逐步回溯，复用已有检查点和有界 replay；不为每帧每像素保存完整依赖图。每次查询有帧步数、节点数、工作字节和取消预算；预算耗尽返回带继续位置的部分证据，不能把不完整链标记完整，也不能仅报“unsupported”。

### 3.7 Statistics 范围及导出

- Frame 范围：帧矩形、流字节数、inflated 字节数、Filter、Block、token/Huffman 等既有编码统计，复用原有累计器。
- Document 范围：全文件 Chunk/物理字节信息；不把全部帧和 fallback 的压缩流合并成一条统计流。
- 帧统计中分别列压缩 payload 和所属 Chunk 开销；共享 IHDR/PLTE/tRNS 不重复算作该帧独占成本。
- 比率显示明确分子分母，以该帧有效尺寸和实际字节数为准，不能使用 IHDR 画布面积代替帧面积。
- 保持已有静态 schema v1 输出不变；增加显式版本化的帧统计导出契约，携带 document、image identity、scope 和完整度。不得把帧数据塞进 v1 的 whole-document 语义。
- occurrence 跳转、导出动作和历史项携带 scope/identity。帧切换后不允许导出旧统计冒充当前帧。

## 4. 执行顺序与工作包

下列为拟议包名，不占用或假定现有 WP 编号。每包均先增加可失败的针对性测试，再实现、运行测试和评审；前置契约包确定后再冻结具体 C++ 签名。

### P1：对象/会话/选择契约

范围：`libs/trace-model/` 的选择与产物标识、`libs/analysis-engine/` 的目标/会话接口；GUI selection store；对应接口文档与 ADR-0004/0007 的必要澄清。

产物：AnalysisTarget/InspectionContext 契约；数据键与交互 revision 分离；坐标转换结果明确 inside/outside/error；压缩导航、统计和历史使用完整对象键。

验收：同坐标 static/frame0/frame1 不相等；stage 切换不能复用旧 lock；旧版静态序列化兼容；坐标偏移及溢出检查。复用 `tests/unit/trace-model/selection_test.cpp`、`tests/gui/selection_view_state_test.cpp`，增加有归属的目标上下文测试。

### P2：虚拟流贯通行查询与逐帧重建

修改重点：analysis-engine 的 `query_coordinator`、`scanline_anchor`、`filtered_scanlines`、`frame_analysis`、`stage_analysis`、`pixel_provenance`；保留旧静态入口作为兼容 adapter。会话发布共享 FrameStageSet，不让 GUI 拼装分析对象。

验收：相同像素的独立静态编码与帧编码产生等价行/重建结果；偏移帧使用局部行宽；所有合法颜色/位深、palette/tRNS、16-bit、Adam7；取消和部分数据可区分。扩展 `frame_analysis_test.cpp`、`query_coordinator_test.cpp`、`pixel_provenance_test.cpp`。

### P3：逐帧 Compression 与物理来源

修改重点：analysis-engine 的 `trace_orchestrator`、`trace_query`、`block_inspector` 和逻辑字节源 adapter；GUI `trace_controller`、compression selection store、trace binding 与 Hex source。

验收：Stored/Fixed/Dynamic 帧；zlib 头、token、Adler 跨多个 fdAT；所有物理范围排除 sequence/CRC；不同帧相同逻辑偏移不会命中错误缓存；选择像素可连通 Block/Huffman/Decode Trace 和 Hex。扩展 `trace_orchestrator_test.cpp`、`virtual_frame_stream_test.cpp`、`compression_selection_store_test.cpp` 和 GUI trace pipeline 测试。

### P4：逐帧 Statistics 与版本化导出

修改重点：analysis-engine 的 `statistics_collector`、`statistics_occurrence_query`、adapter/view；`libs/statistics/` 的 scope 与 serialization；GUI statistics controller/worker/inspector。

验收：两个差异明显帧的统计与独立预计算值匹配；scope 切换、late progress、occurrence 和导出身份正确；现有 v1 golden 字节不变，新帧 JSON/CSV 输出确定。扩展 statistics collector/occurrence/serialization 和 GUI statistics 测试。

### P5：合成像素 provenance

修改重点：`animation_replay` 与 `pixel_provenance`，新增归属 analysis-engine 的 canvas pixel query；必要运算证据由 `libs/png-reconstruction/` 现有 compositor 暴露，公共关系类型归 trace-model。

验收：所有 blend/dispose 组合、首帧 PREVIOUS、矩形外继承、多帧半透明叠加、全透明和舍入边界；来源值重放匹配现有画布结果；可从来源叶子进入 P2/P3；超预算可继续、可取消。增加 canvas provenance 单元测试，扩展 animation replay 测试。

### P6：统一工作区交互

修改重点：GUI `animation_controller`、`document_session`、`selection_navigation_controller`、`main_window_ui`、`trace_controller`、`statistics_controller`；Qt stage inspector/model、animation inspector、Hex 和 selection view state。

验收：选帧后不换掉 Inspector 当前标签；所有分析内容跟随新帧；动画四个阶段 hover/click/leave/lock/X/Y 都取当前画面；支持来源展开和返回；Pixels/Filtered/Defiltered 不读 fallback；loading/error 与部分就绪清晰；静态文档不新增 APNG UI。扩展 animation controller/UI、selection navigation、stage inspector 和 APNG product gate。

### P7：整体验收与性能

样例 `/Users/lijiangbo/project/png_overview/examples/apng/support/027.png` 只作为外部人工验收输入，不擅自复制入 corpus。新增自动测试使用 `tests/common/apng_fixture.h` 和已有受控 fixture 机制；外部 corpus 导入必须先补来源/许可/hash。

执行 fallback → frame0 → frame1 → 四阶段 → 来源 trace → fallback，覆盖当前标签保留、Hex、导出、鼠标/键盘操作。另用偏移小帧证明局部坐标正确，快速 A→B→A/切文件证明请求 revision 防串帧。

覆盖 1000 帧随机定位、100000 元数据帧列表、播放时持续鼠标移动、重复统计/trace/来源查询、取消后资源回收。沿用既有静态 WP-604 阈值；新分析缓存和合成缓存统一记账，保留现有 64 MiB 合成 LRU 约束，不按帧数累加配额。工作内存与 retained cache 分开计量，避免只算缓存而遗漏在途大对象。

所有 P2–P6 必须包含资源限额、错误与迟到结果测试，不能全部推迟到 P7 才发现接口不支持。

## 5. 实施治理与核验

本任务需建立新的正式 Work Package，允许必要的 `libs/statistics/**` 及逐帧导出契约更新。旧 APNG WP 明确未包含这些能力，不能借旧包完成记录声称已授权或已完成。

保持 C++20 / Qt6；libs Qt-free；不新增依赖、不改变模块依赖方向；GUI 不读取/解析/解码文件；不得连接 libpng 私有 API；虚拟流不能完整拼接。公共接口变化写入所属接口文档，涉及已接受 ADR 的变化先形成明确修订。

目标仓库检查时已有一个无关未跟踪审计文件，实施前重新检查 working tree 并保留。

验证从目标仓库执行，首先用 `ctest --preset dev -N` 确認匹配到真实测试，禁止把空匹配视作通过：

```sh
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev -N
ctest --preset dev -R 'analysis_engine|statistics|selection|apng|animation|trace|hex|inspector' --output-on-failure
ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
git diff --check
```

最终必须给出原生 GUI 的真实交互证据；当前本文仅完成代码调查和规划，未执行以上构建或 GUI 检查。产品完成条件是七包闭环，既有已支持静态能力在每帧上的对应查询有效，画布合成关系可解释，不存在静态 fallback 静默替代。

## 6. 依据

- 目标仓库 AGENTS.md、REPOSITORY_LAYOUT.md、ADR-0004/0006/0007。
- `docs/development/wp-699-706-apng-first-release.md` 和 completion 文档的已记录限制。
- W3C PNG 第三版 §11.3.6：https://www.w3.org/TR/png-3/#11AnimationChunks 。每帧独立数据流、尺寸继承、blend/dispose 与默认图像角色按该规范核对。

## 7. 2026-09-09 逻辑缺陷与执行就绪审查

### 7.1 已有问题的证据分级

下列是代码路径确认，不等同于本次已经运行 GUI 复现全部现象：

| 编号 | 问题 | 证据 | 必须添加的回归断言 |
| --- | --- | --- | --- |
| D1 | 动画 delivered 与静态 StageSet 混用 | presentAnimationFrame 只更新 delivered | 同一报告内 frame/stream/StageSet 一致 |
| D2 | 动画缺少 hover/leave/nudge/cancel 绑定 | bindAnimationUi 对动画仅接 click，静态 main_window 有完整连接 | 动画四阶段的全部事件走活动上下文 |
| D3 | 状态恢复/取消锁定固定使用静态视图 | setPixelStatus、restorePixelStatus、clearLockedCoordinate | 动画移出/取消后无静态 RGBA、无残余十字线 |
| D4 | 切 Pixels/Filtered/Defiltered 自动回 fallback | mountAnimationUi currentChanged 对 tab<4 调 selectStaticFallback | 帧身份不因编码阶段标签切换而丢失 |
| D5 | 行查询、trace、统计、派生 Hex 仍绑定静态链路 | QueryCoordinator/TraceOrchestrator/collector 与 selection stage_set | 各帧独立分析与窗口化字节映射正确 |

待测试验证、不能直接宣称已发生：重复挂载是否重复连接、A→B→A 迟到结果覆盖、关闭期间视图残留、异步 static StageSet 覆盖 frame、历史导航循环。每项必须采用可控任务顺序的自动测试证明。

### 7.2 原设计可能引入的新逻辑问题及约束

1. 单一 revision 若随 hover 递增，会使耗时 trace/统计一直作废。必须分开 target epoch、committed selection serial 与 hover 状态。hover 不取消锁定 trace、当前帧统计或目标分析；target epoch 在 A→B→A 每次切换都递增。
2. 追溯较早来源帧不能替换预览当前帧。单独维护可选 evidence focus，包含来源帧、字节范围及 parent selection serial。换主目标/主像素清除 focus，返回操作恢复父选择；相关 Compression/Hex 子标题必须显示来源身份。
3. 程序写 X/Y 可能触发暂停或二次锁定。上下文提交时阻断控件反馈信号，用户事件才发导航命令；命令 origin/serial 防止 Chunk↔Hex↔Frame 循环。
4. APNG 模式不得按扩展名开启。完全静态、伪装 .apng 的静态文件、无可用帧的损坏 APNG、verified-prefix partial APNG 必须有独立明确的路由测试。
5. 区分独立 fallback 与 IDAT 同时为动画首帧两种布局。无独立 fallback 时不提供虚假的独立 fallback 时间轴入口；同一 IDAT 的静态身份与 frame0 身份仍不得合并。
6. 旧第0个 Image/Static Fallback 标签的语义必须冻结：在 APNG 中它是默认图像的显式入口，点击它切 StaticImage；Pixels/Filtered/Defiltered 保持当前图像身份。普通静态文档沿用原标签语义。没有独立 fallback 的默认图像需明确标注与动画首帧共用 IDAT。
7. Filtered/Defiltered/Pixels 来自当前编码帧，不能套用 PostBlend RGBA。它们的坐标点击明确产生编码阶段选择；返回画布阶段重新选择阶段，不能悄悄把局部点当画布点。
8. 不同编码参数即使像素相同，也可能有不同 Filter/token。P2 等价测试必须使用相同 header 与相同压缩 payload 的静态/帧双包装来比较底层证据；任意重编码只比较最终像素。
9. 合成 RGBA 检查点不包含历史来源；不能把命中 RGBA cache 当作完整 provenance。来源查询必须从操作/帧信息重建来源路径，明确继续令牌的 generation/identity/stage/coordinate/算法版本和预算，不接受跨上下文令牌。
10. 不能只保留静态 API 的函数名而改变行为。adapter 必须保留错误分类、partial、预算、取消、行索引、回调线程与输出顺序等原契约；泛化后对同一静态输入进行基线比较。
11. 默认不得缓存全部帧 StageSet 或在切帧时同步等待 worker 析构。共享调度、产物所有权和回收需有具体生命周期设计；累计内存、在途 reservation 与 LRU 淘汰一致。
12. 新统计 schema 仅经显式帧导出入口使用；静态 CLI/GUI 默认 v1 与 golden 字节不变。共享文件开销、payload 计数、部分帧状态必须有精确公式后才能实现。

### 7.3 非 APNG 零行为回归门槛

先记录当前目标仓库 commit、未提交改动、构建配置、测试清单和结果，再进行实现。现有失败作为基线缺陷单列，不允许删除/改弱测试，也不能借基线失败掩盖新增失败。

必须冻结以下静态基线：

- 解码 RGBA、Filter/重建、Block/Huffman/token、逻辑/物理 spans、错误与 partial 状态。
- CLI 输出、selection 序列化、Statistics v1 JSON/CSV golden，逐字节一致。
- 普通静态 PNG 的标签顺序/可见性、默认选择、hover/leave/click/lock/nudge/Escape/X/Y、Hex、Chunk、统计与导航行为。
- 静态启动不创建 APNG 专属视图、会话、任务或帧缓存；既有静态任务调度策略保持。
- static→static、static→APNG、APNG→static、partial APNG→static、关闭→static；尤其在旧帧/统计/trace worker 尚未完成时切换。
- 合法色型/位深、palette/tRNS、16-bit、Adam7、多 IDAT、大文件及坏 CRC/zlib/截断输入。
- 同机器同构建模式的冷/暖运行、P50/P95、峰值内存与 UI 阻塞。使用 `tests/performance/thresholds-v1.json` 和 `--enforce-thresholds`；禁止调宽阈值让新实现通过。绝对阈值未超限仍需检查相对基线的新增开销，并在执行计划中冻结测量容差。

任何共享路径修改都在所在包结束前执行相关静态测试；完整静态测试在集成后再执行。保留既有静态门面，APNG 新上下文与 UI 状态只在解析出的动画能力下启用；这不豁免共享底层的回归验证。

### 7.4 Agent 执行就绪条件

当前结论：尚未就绪。本文是架构设计和工作包路线图，之前“形成可执行计划”的表述超过了实际交付细度。

执行前必须补齐并审查：

- 正式 WP 的 allowed/forbidden 文件路径、前置包状态，以及公共接口文档/ADR 的实际修订内容。
- 每个新增/修改接口的精确 C++ 签名、归属、所有权、线程、取消与发布契约；不能交给不同任务临时各自决定。
- 目标/预览/来源 focus 的完整事件状态转换表，覆盖加载、播放、暂停、错误、返回默认图像和销毁。
- 缓存、工作内存、回溯步数/节点数、继续令牌和性能容差的确定数值及拒绝行为。现有合成 64 MiB 不等于新整套系统已有完整预算。
- 每任务精确修改文件、最小失败测试代码、实际测试目标与预期结果、实施步骤和完成断言；P1–P7 不能原样作为一次性编码任务。
- 测试目标需在配置后核验，使用有测试存在性断言的 runner；单独列出 ctest -N 不能自动防止后续过滤匹配为空。
- 修改前静态基线证据及逐包静态兼容检查矩阵。
- 依赖顺序固定为 P1→P2→P3→P4→P5→P6→P7；本次不派生并行 agent 任务。

本次审查仅修改规划文档，未运行产品测试，不能给出“没有新 bug”或“非 APNG 已证明无副作用”的结论。

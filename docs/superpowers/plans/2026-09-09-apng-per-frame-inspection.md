# APNG Per-frame Inspection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use executing-plans to implement this plan task-by-task. Steps use checkbox syntax. 默认顺序执行；不要自行派生agent。先完成T00，再按T01–T12进行；不得从路线图直接跨包编码。

**Goal:** 完整逐帧重建、压缩、统计、来源和交互，非APNG零行为回归。

**Architecture:** 不可变AnalysisTarget和独立InspectionTicket贯通共享虚拟流算法。静态门面保留，APNG新入口按需运行；画布像素通过来源focus连接编码帧。

**Tech Stack:** C++20、Qt6、CMake、现有vcpkg依赖、Catch2/QtTest、Python3。

**Spec:** [设计](../specs/2026-09-09-apng-per-frame-analysis-design.md)、[正式WP](../../development/wp-apng-inspection.md)、[冻结契约C1–C7](../../development/wp-apng-inspection-contract.md)。三者必须一同阅读。

## Global Constraints

- 保持既有静态API、调度、错误状态、GUI与序列化；共享算法改动逐任务静态回归。
- libs Qt-free；不新建依赖、模块或顶层目录；不修改Accepted ADR、静态golden和现有阈值。
- 文件/流共享所有权；虚拟流不完整拼接；UI不读取或解码、不阻塞join。
- 预算使用C5数值；发布使用C1/C6完整身份；hover不递增epoch/selection serial。
- 工作目录是目标仓库；以下路径均相对仓库。文档编写不是生产执行记录。

## 执行/验证约定

每任务都按 red→实现→green→G-static→review 的顺序；下面代码是最小判别测试/关键实现，不是全部产品实现。额外验收矩阵同样必须变成测试，不能只执行示例。

**G-static**：

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev --no-tests=error --output-on-failure
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
git diff --check
```

T01起新增Catch测试均标 `[apng-inspect]`；CMake给 `pnga_analysis_engine_tests "[apng-inspect]"` 注册独立 `apng_inspection_engine_tests`。trace-model/statistics同样给既有对应测试可执行程序注册 `apng_inspection_identity_tests`、`apng_inspection_statistics_tests`。Qt新增场景放入已有相关测试class的slots，保留既有target。没有对应测试注册视为失败。

```sh
ctest --preset dev -R '^apng_inspection_' --no-tests=error --output-on-failure
```

red阶段预期只因新增契约/行为缺失而失败；工具链错误不能算red。针对未实现声明的首次编译失败记录精确缺失符号，然后继续实现。每任务green必须包含新增测试数量及名称。提交前逐文件stage，不使用git add .；是否commit遵循当时用户授权，未提交也要记录完成diff，不因未commit冒充未完成。

## T00：建立静态基线和不空跑的验收runner

**Files:** Create `scripts/run_apng_inspection_gate.py`；后续证据记录到 `docs/development/wp-apng-inspection-completion.md`。本任务不改生产源码。

**Consumes/Produces:** 现有CMake dev preset → `python3 scripts/run_apng_inspection_gate.py --phase baseline|candidate --out PATH`。baseline保存HEAD、dirty文件列表、ctest JSON清单与退出码、工具链、静态测试和性能原始记录；candidate比较同环境基线。out必须是build目录子目录。

- [x] 记录HEAD/dirty，不碰已有无关文件；配置dev并确认Qt测试确实存在，不能接受自动退化为纯CLI构建。
- [x] runner最小实现使用subprocess参数列表，不用shell插值；以下存在性断言必须保留：

```python
p = subprocess.run(['ctest','--preset','dev','--show-only=json-v1'],
                   check=True, capture_output=True, text=True)
names = {t['name'] for t in json.loads(p.stdout)['tests']}
required = {'analysis_engine_artifact_store_tests', 'statistics_engine_tests',
            'gui_stage_inspector_tests', 'gui_selection_view_state_tests'}
if not required <= names:
    raise SystemExit('required tests missing: ' + repr(sorted(required - names)))
subprocess.run(['ctest','--preset','dev','--no-tests=error',
                '--output-on-failure'], check=True)
```

- [x] 模拟required含不存在名称，验证runner非零退出；恢复required后跑baseline。
- [x] 跑5次 `python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds`，每次stdout/stderr/JSON按独立编号存档。禁止在本任务更改基准输入或阈值。
- [x] 保存 `tests/unit/statistics/golden/` 和静态selection/CLI golden实际文件hash清单；列表由现有测试源码引用确定，记录到baseline manifest。禁止记录可变时间字段作为语义差异。
- [x] 对全套现有静态GUI场景记录原始结果；baseline既有失败逐项归因并标注，未解决必要失败则最终不能PASS。

**Exit:** 基线证据存在且runner能拒绝空测试；本包代码尚未开始。

## T01：身份、ticket与坐标契约

**Files:** Create `libs/trace-model/include/pnga/trace-model/inspection_context.h`、`libs/trace-model/src/inspection_context.cpp`、`tests/unit/trace-model/inspection_context_test.cpp`；Create `libs/analysis-engine/include/pnga/analysis-engine/analysis_target.h`、`libs/analysis-engine/src/analysis_target.cpp`、`tests/unit/analysis-engine/analysis_target_test.cpp`；Modify 两模块CMakeLists.txt/README.md和对应tests/CMakeLists.txt。

**Interfaces:** 产出C1所有接口；AnalysisTarget factory暂由T02实现，T01只实现坐标和publication，声明factory。既有 FrameRequest/FrameStageSet/CoordinateSummary 均显式include对应public header。

- [x] 写身份判别测试：

```cpp
using namespace pnga::trace_model;
InspectionTicket a{{7, AnimationFrame{0}}, Stage::kFrameOutput, 1, 3};
auto b = a; b.target_epoch = 2;
REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kTarget));
b = a; ++b.selection_serial;
REQUIRE(accepts_publication(b, a, PublicationScope::kTarget));
REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kPixel));
b = a; b.key.identity = StaticImage{};
REQUIRE_FALSE(accepts_publication(b, a, PublicationScope::kTarget));
```

- [x] 写坐标测试：rect={sequence0,width2,height3,x10,y20,delay1/100,dispose0,blend0}，global(11,22)→local(1,2)，(9,22)/(12,22)越界。最大uint64坐标不得溢出。
- [x] 运行red；实现C1。publication实现比较key、epoch，再按scope比较stage/serial；frame_local_point先检查global>=origin再减，比较local<尺寸，拒绝零尺寸。
- [x] query_frame_coordinate转换前验证frame identity；调用原query_coordinate处理pass/channel，完成后恢复全局坐标，保留pass-local字段。
- [x] 运行identity新测试和G-static；检查旧Selection格式没有变化。

## T02：目标工厂与可靠fixture

**Files:** Modify T01的analysis_target.cpp；Create `tests/common/apng_inspection_fixture.h`；Modify `tests/unit/analysis-engine/analysis_target_test.cpp`、`frame_analysis_test.cpp`。

**Interfaces:** 完成 `TargetResult make_frame_target(const FrameRequest&)`。新增测试helper `pnga_test::inspection_request(uint32_t ordinal=0)`，返回现有FrameRequest，固定generation7/serial11。

- [x] helper使用以下构造，不能用make_apng_canvas构造子矩形：

```cpp
inline pnga::analysis_engine::FrameRequest inspection_request(std::uint32_t ordinal=0) {
  using namespace pnga;
  std::array<png_format::FrameControl,2> frames{
    png_format::FrameControl{0,2,3,10,20,1,100,0,0},
    png_format::FrameControl{0,2,3,10,20,1,100,0,1}};
  auto source = std::make_shared<const io::MemoryByteSource>(
    make_apng_format(false, frames, ApngFormat{}, {}, {}, {}, 16, 32));
  analysis_engine::FrameRequest r;
  r.generation=7; r.request_serial=11; r.ordinal=ordinal; r.source=source;
  r.index=std::make_shared<const png_format::AnimationIndex>(
    png_format::index_animation(*source, {}, []{return false;}));
  r.canvas_header={16,32,8,6,false};
  return r;
}
```

- [x] 最小测试：

```cpp
auto r=pnga_test::inspection_request();
auto target=pnga::analysis_engine::make_frame_target(r);
REQUIRE(target.target);
REQUIRE(target.target->header.width==2);
REQUIRE(target.target->header.height==3);
REQUIRE(target.target->stream->size()>0);
r.ordinal=2;
REQUIRE_FALSE(pnga::analysis_engine::make_frame_target(r).target);
```

- [x] red后实现factory，source/index寿命由共享指针保持；将delivery从文档提取，失败不产生半有效target。
- [x] 增加同payload双包装fixture：用append_apng_chunk将帧虚拟payload小测试数据包装到相同格式静态PNG；该测试buffer大小限定64KiB。非测试生产仍禁止完整拼接。palette/tRNS复制相同测试值。
- [x] 运行新target/frame测试、G-static；明确helper和生产预算边界不同。

## T03：虚拟流行索引和逐帧重建

**Files:** Modify analysis-engine `include/pnga/analysis-engine/`及`src/`的 `query_coordinator`、`scanline_anchor`、`filtered_scanlines`、`pixel_provenance`、`stage_analysis` 对应.h/.cpp；Modify `libs/analysis-engine/README.md`；Create `tests/unit/analysis-engine/frame_query_test.cpp`并注册CMake。

**Interfaces:** C2规则对应重载 + QueryCoordinator::open(target,interval)。静态open及原query_coordinate不改变。

- [x] 编写最小帧行查询：

```cpp
auto request=pnga_test::inspection_request();
auto t=pnga::analysis_engine::make_frame_target(request);
REQUIRE(t.target);
pnga::analysis_engine::QueryCoordinator q(1,1u<<20);
REQUIRE(q.open(t.target,32768));
REQUIRE(q.scanline_count()==3);
REQUIRE(q.anchors().header.width==2);
```

- [x] red后把索引/replay底层字节读取统一到IVirtualCompressedStream；原静态入口用生命周期明确的局部adapter桥接，不改变旧generation自增规则。
- [x] 对同payload双包装比较filtered/unfiltered/native、filter_formula和scanline spans；物理偏移预期不同，不能直接比较文件offset相等。
- [x] 矩阵：每种合法color/depth、16-bit、palette/tRNS、Adam7；所有Filter通过已有受控fixture覆盖，不能全部只用None；非零offset和pass边界。
- [x] 验证行查询取消/越界/预算、shared owner寿命和帧间同行不同数据；G-static。

## T04：逐帧Compression和来源物理映射

**Files:** Modify analysis-engine `trace_orchestrator.h/.cpp`、`trace_query.h/.cpp`、`block_inspector.h/.cpp`、`src/virtual_idat_source.h`；Modify README.md；Create `tests/unit/analysis-engine/frame_trace_test.cpp`并注册。

**Interfaces:** C2 TraceOrchestrator::open(target,max_output)和通用compose/provenance；现有TraceQueryResult保留，提交ticket由会话捕获，不更改静态serialize_trace_query。

- [x] 最小red测试：

```cpp
auto t=pnga::analysis_engine::make_frame_target(pnga_test::inspection_request());
pnga::analysis_engine::TraceOrchestrator trace(1,1u<<20);
REQUIRE(trace.open(t.target,1u<<20));
REQUIRE(trace.has_index());
REQUIRE(trace.queued_tasks()==0);
std::vector<pnga::png_format::PhysicalRange> spans;
REQUIRE(t.target->stream->logical_to_physical(0,t.target->stream->size(),spans));
REQUIRE_FALSE(spans.empty());
```

- [x] 实现新增open使用target stream，绝不重新扫全文件构建静态IDAT；保留原静态open门面。
- [x] 针对zlib头、Dynamic表、token、Adler跨fdAT切片，逐span检查payload包含性；边界落在sequence/CRC必须反向映射失败；同逻辑偏移的frame0/frame1不能共享选择结果。
- [x] 使用现有Stored/Fixed/Dynamic测试数据包装为帧，比较同payload静态逻辑token与Huffman；错误Adler和截断返回明确partial/error。
- [x] 运行新测试、已有trace_query/orchestrator/pixel_provenance和G-static。

## T05：帧统计与独立导出schema

**Files:** Create `libs/statistics/include/pnga/statistics/frame_statistics.h`、`libs/statistics/src/frame_statistics.cpp`；Create analysis-engine `frame_statistics.h/.cpp`；Modify analysis-engine `statistics_collector.cpp`、`statistics_occurrence_query.h/.cpp`、模块README/CMake；Create `tests/unit/statistics/frame_statistics_test.cpp`和`tests/unit/analysis-engine/frame_statistics_test.cpp`，对应CMake；旧serializer仅允许共用section编码提取。

**Interfaces:** C3。occurrence使用C7的query_frame_statistics_occurrence新入口；所有APNG occurrence GUI回调附完整ticket。不得将png-format类型放入statistics库。

- [x] 最小serializer测试：

```cpp
pnga::statistics::FrameStatistics s;
s.identity=pnga::trace_model::AnimationFrame{1};
s.width=2; s.height=3; s.payload_bytes=21; s.inflated_bytes=27;
auto json=pnga::statistics::serialize_frame_statistics_json(s);
REQUIRE(json.success);
REQUIRE(json.bytes.find("pnga.frame-statistics")!=std::string::npos);
s.identity=pnga::trace_model::StaticImage{};
REQUIRE_FALSE(pnga::statistics::serialize_frame_statistics_json(s).success);
```

- [x] red后实现C3；header/identity与FrameStageSet不匹配返回error，不能套用current缓存。
- [x] 2x3 RGBA8 None fixture inflated=3*(1+8)=27；fdAT所属开销=38+16*N，IDAT首帧=38+12*N；检验checked overflow与未完成统计不填完整比率。
- [x] 针对新schema写独立golden字符串测试（放新测试文件，禁止改旧golden）；JSON/CSV字段顺序、LF和frame_index明确；进度取消保留verified prefix。
- [x] 同payload双包装统计逻辑计数一致；旧v1全部逐字节一致；G-static。

## T06：有界canvas像素来源

**Files:** Create analysis-engine `canvas_pixel_query.h/.cpp`、`tests/unit/analysis-engine/canvas_pixel_query_test.cpp`；Modify `animation_replay.h/.cpp`仅暴露/复用已有数值回放，不改compositor算法；模块README/CMake、测试CMake。

**Interfaces:** C4。现有compositor为数值事实来源；本包不新增其依赖或修改png-reconstruction模块。

- [x] 最小可观察red：

```cpp
pnga::analysis_engine::CanvasPixelRequest r;
r.document=pnga_test::inspection_request();
r.ticket={{7,pnga::trace_model::AnimationFrame{0}},
          pnga::trace_model::Stage::kPreBlend,1,1};
r.x=11; r.y=22;
auto out=pnga::analysis_engine::query_canvas_pixel(r,nullptr);
REQUIRE(out.stop==pnga::analysis_engine::CanvasPixelResult::Stop::kReady);
REQUIRE_FALSE(out.nodes.empty());
REQUIRE((out.nodes.back().rgba==std::array<std::uint8_t,4>{0,0,0,0}));
```

- [x] 实现C4确定性来源遍历。源码解码由既有analyze_frame/replay负责；来源query将FrameSample连接到帧身份，不把Clear伪造压缩范围。
- [x] 独立手算RGBA goldens覆盖SOURCE、OVER舍入、NONE、BACKGROUND、PREVIOUS、首帧PREVIOUS、矩形外Carry及多帧半透明；输出与现有replay数值一致，但测试预期不能调用被测公式生成。
- [x] 4096节点/1024步/4MiB边界产生partial+next；继续必须推进，取消不发布ready；错generation/stage/坐标cursor报error；检查DAG边有界无环。
- [x] G-static和已有animation replay/composition测试保持通过。

## T07：APNG分析会话、预算与后台生命周期

**Files:** Create `apps/png-analyzer-gui/src/frame_inspection_session.h/.cpp`；Modify `animation_controller.h/.cpp`、`animation_worker.h/.cpp`、`document_session.h/.cpp`、应用CMake；Create `tests/gui/frame_inspection_session_test.cpp`并注册 `gui_frame_inspection_session_tests`。

**Interfaces:** 新会话QObject持有C1目标、C5调度/缓存：

```cpp
void selectTarget(std::shared_ptr<const pnga::analysis_engine::AnalysisTarget>,
                  pnga::trace_model::InspectionTicket);
void clear(std::uint64_t next_generation);
bool accepts(const pnga::trace_model::InspectionTicket&,
             pnga::trace_model::PublicationScope) const;
std::uint64_t retainedBytes() const;
std::uint64_t reservedBytes() const;
```

上述成员归 `FrameInspectionSession`；其构造 `explicit FrameInspectionSession(QObject* parent=nullptr)`。结果信号使用shared immutable model+ticket。以C1 accepts_publication实现统一接收门；提供测试注入完成回调的seam而非使用sleep。

- [x] 写假完成顺序A1→B→A2，在A2后投递A1结果，断言accepts=false；hover不变ticket则同帧统计仍可接受。
- [x] 实现C5固定额度、队列8和优先级；open目标在后台；old coordinator join在后台回收；static文档不创建该会话。
- [x] 测试1字节超配额、shared_ptr pinned未释放、取消/失败路径reservation回零、队列满不丢当前选择、关闭期间回调不接触被删除QObject。
- [x] 使用QSignalSpy/可控executor验证逻辑；总测试超时作为死锁保护，不以墙钟sleep控制顺序。
- [x] G-static并检查静态会话/worker新增计数为0。

## T08：Inspector/Hex接收完整帧上下文

**Files:** Modify GUI `trace_controller.h/.cpp`、`statistics_controller.h/.cpp`、`statistics_worker.h/.cpp`、`main_window_ui.h/.cpp`；Qt对应public header/src中的 `stage_inspector`、`stage_inspector_model`、`trace_inspector_binding`、`compression_selection_store`、`statistics_inspector`、`hex_data_source`、`hex_source_tab_bar`；Modify `tests/gui/stage_inspector_test.cpp`、`trace_inspector_binding_test.cpp`、`statistics_controller_test.cpp`、`compression_selection_store_test.cpp`、`hex_data_source_test.cpp`。

**Interfaces:** 为StageInspector新增原子入口：

```cpp
void setFrameContext(
  std::shared_ptr<const pnga::analysis_engine::FrameStageSet> frame);
```

一次提交stages+delivered+identity；内部仍用原model，静态setStageSet/setDeliveredPixels调用路径不改。其余panel结果携ticket，在session接受后才更新；source focus由T10补充。

- [x] red：setFrameContext(frame)后读取报告，Filter/native/delivered全部与该frame一致；切frame不得残留旧row或坐标高亮。
- [x] Hex Frame Stream/Inflated/Defiltered均取该frame，File保持原文件；source tabs不按硬编码Inspector索引定位。
- [x] 统计正在运行时切帧，旧进度和导出不得更新新帧；默认图像恢复完整静态context；保留当前Inspector标签。
- [x] 接通P3/P4所有Block/Huffman/token/occurrence导航，错误/partial显示同目标而非fallback。
- [x] G-static，尤其旧StageInspector golden/UI布局与Compression selection历史。

## T09：完整鼠标键盘交互与目标状态机

**Files:** Modify GUI `selection_navigation_controller.h/.cpp`、`animation_controller.h/.cpp`、`main_window.cpp`、`main_window_ui.cpp`；Qt `include/pnga/ui/qt/selection_view_state.h`（该类型为header-only）；Modify `tests/gui/selection_navigation_controller_test.cpp`、`animation_controller_test.cpp`、`animation_ui_test.cpp`、`selection_view_state_test.cpp`。

**Interfaces:** 统一事件以active view身份路由；原静态槽签名保留，APNG分支提交C6命令。所有坐标使用C1转换；程序更新阻断控件signal。

- [x] red GUI场景：显示offset(10,20)的2x3 FrameOutput，点击local(1,2)，断言X=11,Y=22、lock identity=frame0、状态RGBA等于active view；移出后仍为该lock，Escape清活动十字线。
- [x] hover/leave/click/nudge/selectionCancelled在四动画view一对一连接；挂载/卸载两次后每用户事件只发布一次。
- [x] 落实C6全部事件行；Pixels/Filtered/Defiltered保持frame身份，tab0才进StaticImage；无独立fallback用“Default Image”说明共享IDAT角色。
- [x] Play后画面身份随已提交PostBlend更新；Pause取消未提交下一帧，不把旧下一帧结果当暂停帧；X/Y在范围外不clamp/读fallback。
- [x] static→APNG→static在途切换、partial→static、错误扩展名识别；G-static。

## T10：来源focus、返回和跨面板导航

**Files:** Modify frame_inspection_session.h/.cpp、selection_navigation_controller.h/.cpp、trace_controller.h/.cpp；Qt stage_inspector.h/.cpp、animation_inspector.h/.cpp、hex_data_source.cpp；Create `tests/gui/canvas_provenance_navigation_test.cpp`并注册 `gui_canvas_provenance_navigation_tests`。

**Interfaces:** evidence focus=(来源AnalysisKey、来源Selection、parent InspectionTicket)。主ticket保持不变；focus另有独立单调serial。帧来源通过C4节点和C1 target查询，不直接改变timeline。

- [x] red：frame1 PostBlend点由frame0贡献，点FrameSample后Compression/Hex标题为frame0，timeline/画布仍frame1；返回后恢复frame1编码流。
- [x] 实现Clear/Restore/Carry/Blend显示；仅FrameSample提供“查看编码来源”，partial提供“继续追溯”；token→File Hex多span联动。
- [x] 切主像素清focus；focus旧结果不能发布；程序Hex导航不能触发新的timeline命令。无来源字节时显示操作原因，不虚构offset0。
- [x] G-static及来源DAG测试。

## T11：产品gate与性能/资源回归

**Files:** Modify `tests/gui/apng_product_gate_test.cpp`、`scripts/run_apng_inspection_gate.py`；Create `tests/performance/apng_inspection_test.cpp`并注册对应performance CMake；只新增APNG测试case，不改已有性能阈值。

**Interfaces:** gate candidate读T00 baseline，检查WP中的静态容差及C5预算；新APNG性能程序输出JSON包括frames/cold_warm_ms/p50/p95/retained/reserved/peak_rss/ui_max_block_ms。

- [ ] 自动跑完整fallback→frame0→frame1→四阶段→来源→fallback→普通PNG；每步验证身份、像素、所有Inspector、四Hex源、锁定/输入/导出。
- [ ] 样例027.png仅外部手工输入；自动fixture必须验证fallback/两帧像素确实不同，不以三个同色帧验收数据隔离。
- [ ] 1000帧随机选择、100000元数据timeline、连续100小帧播放；所有缓存总数按C5，取消后reserved=0，UI线程不执行文件读取/解码。冷/暖结果逻辑一致。
- [ ] native GUI记录普通PNG和027.png真实交互，截图包含当前帧标题和Inspector。没有原生GUI证据不能写PASS。
- [ ] candidate同环境5次性能比较baseline，并跑现有冻结threshold；必要环境不具备记BLOCKED，不能把skip当pass。

## T12：总验收与完成记录

**Files:** Create/Update `docs/development/wp-apng-inspection-completion.md`；Modify 本计划checkbox与正式WP状态；仅完成后给旧 `wp-699-706-apng-completion.md` 的Deferred issue加本包证据链接。

- [ ] 运行所有G-static及APNG独立测试，检查实际test count；运行 `python3 scripts/run_gui_gate.py`、`python3 scripts/run_sanitizer_fuzz_gate.py`、candidate gate。
- [ ] 阅读diff，逐项核查外层allowed paths、无静态golden/阈值变化、无Qt进libs、无新依赖、无UI join、无巨大payload拼接。
- [ ] 对照C1–C7和D1–D5列证据映射：每条必须指向测试名/结果，而不只指源码。
- [ ] 填PASS/FAIL/BLOCKED唯一状态；PASS须所有必须门槛实跑，禁止将文档完成算产品完成。汇报真实未执行项。

## 自审检查表

- [ ] T00先建基线；T01–T12每项均有前置接口、精确路径、判别测试与静态门槛。
- [ ] C1身份/坐标→T01/T02/T09；C2流/trace→T03/T04；C3统计→T05/T08；C4来源→T06/T10；C5预算→T07/T11；C6事件→T08–T10。
- [ ] 静态v1输出与默认API未被帧版本替换；APNG→静态迟到结果包含在T09/T11。
- [ ] 实施者发现契约冲突必须更新计划/证据，不能省略测试或自行改变Accepted ADR。

## 文档编写自审记录（2026-09-09）

已核对现有源码路径、真实CMake测试名、fixture尺寸语义和occurrence实际签名；已修正occurrence错误泛化规则、cursor前缀重复遍历风险和Catch宏逗号问题。任务代码片段仅经文档静态审查，未声称编译验证。T00需要在真实环境采集基线，后续每任务以red/green证明实现，不使用本文自审替代产品测试。

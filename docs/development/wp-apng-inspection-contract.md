# WP-APNG-INSPECT：逐帧检查接口与状态契约

日期：2026-09-09。任务状态：NOT STARTED。本文是新任务包的规范输入，不改变既有 ADR 的 Accepted 状态。当前用户授权编写并落盘任务包；不是代码已经实施或测试通过的记录。

## 范围与兼容底线

保持 ADR-0004 的全局坐标、ADR-0006 的按需查询和 ADR-0007 的静态/帧身份；不修改上述已接受决策。新增接口记录在本契约及对应模块 README。静态 API、默认参数、调度方式、返回分类、回调线程、序列化、CLI 和 GUI 行为不得改变。新 APNG 接口是增量入口；允许共享底层算法，禁止复制解码器。

所有类型以下列 namespace 为准。头文件必须显式包含所用现有类型与标准库依赖；不依赖 transitive includes。公开描述符通过 shared_ptr<const T> 传递；构造后不改变内容。未列出的私有成员由实现者局部选择，但不得改变下列接口和语义。

## C1：身份、发布及坐标

新增 `libs/trace-model/include/pnga/trace-model/inspection_context.h`：

```cpp
namespace pnga::trace_model {
struct AnalysisKey {
  std::uint64_t generation = 0;
  ImageIdentity identity = StaticImage{};
  bool operator==(const AnalysisKey&) const = default;
};
struct InspectionTicket {
  AnalysisKey key;
  Stage stage = Stage::kUnknown;
  std::uint64_t target_epoch = 0;
  std::uint64_t selection_serial = 0;
  bool operator==(const InspectionTicket&) const = default;
};
enum class PublicationScope { kTarget, kPixel };
bool accepts_publication(const InspectionTicket& current,
                         const InspectionTicket& result,
                         PublicationScope scope) noexcept;
}
```

Target 比较 key/target_epoch，忽略 stage/selection_serial（编码数据可跨画布阶段复用）；Pixel 额外比较 stage/selection_serial。画面发布在 Target 条件之外必须 stage 相同。hover 不改变 ticket。epoch/serial 在递增溢出时拒绝新请求并关闭检查会话，禁止回绕。持久静态 Selection 格式不加这些瞬态字段。

新增 `libs/analysis-engine/include/pnga/analysis-engine/analysis_target.h`：

```cpp
namespace pnga::analysis_engine {
struct LocalPoint { std::uint64_t x = 0, y = 0;
  bool operator==(const LocalPoint&) const = default; };
struct AnalysisTarget {
  pnga::trace_model::AnalysisKey key;
  std::shared_ptr<const pnga::io::IByteSource> source;
  std::shared_ptr<const pnga::png_format::IVirtualCompressedStream> stream;
  pnga::png_reconstruction::ImageHeader header;
  pnga::png_reconstruction::DeliveryContext delivery;
  std::optional<pnga::png_format::FrameControl> control;
};
struct TargetResult {
  std::shared_ptr<const AnalysisTarget> target;
  std::string error;
};
TargetResult make_frame_target(const FrameRequest& request);
std::optional<LocalPoint> frame_local_point(
    const pnga::png_format::FrameControl& rect,
    std::uint64_t global_x, std::uint64_t global_y) noexcept;
CoordinateSummary query_frame_coordinate(
    const FrameStageSet& frame,
    const pnga::trace_model::Selection& global_selection);
}
```

make_frame_target 在 worker 使用：验证 source/index/ordinal 在 verified prefix，使用 make_frame_stream，继承 canvas_header 的格式但换 fcTL 尺寸，delivery_context_from 继承 PLTE/tRNS；失败 target=null。禁止 UI 文件读取。query_frame_coordinate 先检 identity/矩形，再局部 query_coordinate，输出 image/selection 坐标恢复全局，local_x 保持 pass 局部语义。不同身份返回 kNotApplicable，矩形外 kOutOfRange。

## C2：虚拟流增量接口

现有 `VirtualIDATStream` API 保留。泛化重载规则是机械且封闭的：下列函数现有参数中的 `(const VirtualIDATStream& stream, const IByteSource& source)` 替换为 `(const IVirtualCompressedStream& stream)`，其他参数、返回值和顺序不变；原重载继续存在并复用内核。

- `build_scanline_anchors` / `restore_scanline`，scanline_anchor.h/.cpp。
- `compose_trace_query`，trace_query.h/.cpp。
- `query_pixel_provenance`，pixel_provenance.h/.cpp。
- `filtered_scanlines.h`、`block_inspector.h` 中直接接收上述成对参数的所有函数，执行时逐一列出新增声明到模块 README；不改其他重载。

新增协调器重载：

```cpp
// QueryCoordinator
bool open(std::shared_ptr<const AnalysisTarget> target,
          std::uint64_t anchor_interval_bytes);
// TraceOrchestrator
bool open(std::shared_ptr<const AnalysisTarget> target,
          std::uint64_t max_index_output_bytes);
```

open 仍不允许对有在途任务的对象重新打开。APNG 目标变化取消旧目标任务；旧对象由后台回收，不在 UI 析构 join。原静态 open 的 generation 语义不变；新 open 采用 target.key.generation，回调由 owning session 附带提交时 ticket，不依赖可变 current target。trace submit 拒绝 request.selection.image 身份与 target 不同的请求。

帧流所有权包含文件和帧索引；fdAT sequence 不属于流。logical_to_physical 返回全部片段；字节/位原点保持现有 Zlib/Deflate 强类型转换，不将 zlib 头偏移重复相加。

## C3：统计和输出

新增 `libs/statistics/include/pnga/statistics/frame_statistics.h`：

```cpp
namespace pnga::statistics {
struct FrameStatistics {
  DocumentIdentity document;
  pnga::trace_model::ImageIdentity identity =
      pnga::trace_model::AnimationFrame{0};
  std::uint32_t width = 0, height = 0;
  std::uint64_t payload_bytes = 0, chunk_overhead_bytes = 0;
  std::uint64_t inflated_bytes = 0;
  StatisticsSnapshot snapshot;
};
SerializationResult serialize_frame_statistics_json(const FrameStatistics&);
SerializationResult serialize_frame_statistics_csv(const FrameStatistics&);
}
```

新增 analysis-engine 的 `frame_statistics.h`：

```cpp
namespace pnga::analysis_engine {
struct FrameStatisticsRequest {
  std::shared_ptr<const AnalysisTarget> target;
  std::shared_ptr<const FrameStageSet> frame;
  pnga::statistics::DocumentIdentity document;
  pnga::statistics::StatisticsLimits limits;
  std::uint64_t max_working_bytes = 64ull << 20;
  std::function<std::uint64_t()> monotonic_millis;
};
struct FrameStatisticsResult {
  pnga::trace_model::AnalysisKey key;
  pnga::statistics::FrameStatistics value;
  std::string error;
};
using FrameStatisticsProgress = std::function<void(const FrameStatisticsResult&)>;
FrameStatisticsResult collect_frame_statistics(
    const FrameStatisticsRequest&, const CancellationToken*,
    FrameStatisticsProgress on_progress = {});
}
```

进度限制沿用每100ms最多一次、首条无条件发布；时钟测试 seam 复用 StatisticsCollectionRequest 机制，使用请求中的 monotonic_millis 字段。每256 samples/token batch及阶段边界检查取消。统计 accumulator 共用，不修改 collect_document_statistics 语义。

payload_bytes=stream.size（含 zlib wrapper）；chunk_overhead_bytes=本帧 fcTL 的38字节 + 每 IDAT 的12字节或每 fdAT 的16字节，不计共享块；inflated_bytes=各非空 Adam7 pass 的 height*(1+row_bytes) checked sum。帧压缩比仅在完整且分母>0时输出 payload/inflated；零和不可用分开。Document 范围在 APNG UI 仅显示文件物理统计并明确注明；不把 fallback filter/token 当全动画统计。

JSON 新 schema 名 `pnga.frame-statistics`、schema_version=1（独立 schema，不是旧 schema 的隐式v2），字段顺序 schema/schema_version/document/identity/geometry/bytes/sections。identity={kind:"animation_frame",index:0}；geometry={width,height}；bytes={payload,chunk_overhead,inflated}。sections 的状态和值复用原 section 编码。CSV 固定列 schema,schema_version,frame_index,section,metric,key,value,unit；缺失值留空；UTF-8、LF、十进制、结尾单LF。StaticImage 输入新 serializer 返回 error，不调用旧 serializer 顶替。原静态 schema/golden 一字节不变。

## C4：合成像素查询

新增 `libs/analysis-engine/include/pnga/analysis-engine/canvas_pixel_query.h`：

```cpp
namespace pnga::analysis_engine {
enum class CanvasOperation { kFrameSample, kBlendSource, kBlendOver,
  kCarry, kClear, kRestore };
struct CanvasPixelNode {
  CanvasOperation operation = CanvasOperation::kClear;
  std::uint32_t frame = 0;
  pnga::trace_model::Stage stage = pnga::trace_model::Stage::kUnknown;
  std::array<std::uint8_t, 4> rgba{};
  std::vector<std::uint32_t> inputs; // indices into this page's nodes
};
struct CanvasPixelPending {
  std::uint32_t frame = 0;
  pnga::trace_model::Stage stage = pnga::trace_model::Stage::kUnknown;
};
struct CanvasPixelCursor {
  std::uint32_t version = 1;
  pnga::trace_model::InspectionTicket ticket;
  std::uint64_t x = 0, y = 0;
  std::uint64_t visited_steps = 0;
  std::vector<CanvasPixelPending> pending;
};
struct CanvasPixelRequest {
  FrameRequest document;
  pnga::trace_model::InspectionTicket ticket;
  std::uint64_t x = 0, y = 0;
  std::optional<CanvasPixelCursor> cursor;
};
struct CanvasPixelResult {
  enum class Stop { kReady, kPartial, kCancelled, kError };
  Stop stop = Stop::kError;
  std::vector<CanvasPixelNode> nodes;
  std::optional<CanvasPixelCursor> next;
  std::string error;
};
CanvasPixelResult query_canvas_pixel(const CanvasPixelRequest&,
                                    const CancellationToken*);
}
```

每页生成闭合局部 DAG；边只引用本页较小索引。跨页未展开来源用 kCarry 叶表示，其 frame/stage 精确定位未展开状态；next.pending 是按确定性深度优先顺序保留的未展开(frame,stage)工作栈，最多1024项；先当前帧源，再 destination 历史。继续从该栈推进，不重新遍历整个已完成前缀，不跨页引用失效索引。visited_steps为累计已展开步数，checked递增；不把它用作分配长度。拒绝ticket/坐标/version不符、非法stage、超出verified prefix或比目标更晚帧的cursor；pending为空的cursor也拒绝。不得把 partial kCarry 声称完整来源，UI 明示“继续追溯”。

SOURCE 源替换；OVER 复用 compositor 的整数公式；PREVIOUS 指向本帧 PreBlend，首帧按 BACKGROUND；矩形外 Carry；初始/清除是无压缩来源叶。仅 FrameSample 可直接进入逐帧 pixel provenance。运算依赖向更早阶段/帧推进，检测非法自环并报 error。RGBA 检查点用于数值 replay，来源仍从控制记录回溯。

## C5：预算、调度和退出

仅 APNG inspection 使用：合成 retained 64MiB（保留现有）；帧分析/index/trace/statistics retained 共64MiB；所有 APNG 检查在途 reservation 共64MiB；每个 canvas 来源页最多4096节点、1024个历史帧步骤、4MiB节点内存。画布和分析总 retained 上界128MiB，不含文件映射和既有元数据索引；两类缓存都将仍被外部 shared_ptr 持有的对象继续计为 pinned，不能 evict 后漏算。

工作预算不能增加旧静态路径配额。单项大于限额返回预算 partial，不悄悄扩大配额。默认一个 APNG inspection 执行 worker，队列上限8；优先当前像素→当前可见面板→统计；重复同key请求合并，满队列丢弃尚未运行的低优先级请求并报告取消，不丢当前用户选择。现有动画播放worker继续负责显示，二者共享预算管理并防止互相无限重试。每次分析入口必须先保留 reservation，所有退出路径释放；不在 UI 线程 wait/join。

这些是本新包的固定初始预算。若现有实现无法在预算内支持既有合法输入，报告限制与测量，不自行增加数字。

## C6：事件状态转换

用户的静态行为完全沿用现有路径；下表只适用于已解析有可用帧的 APNG。主状态 Closed/Loading/Ready/Playing/Error，另有独立 evidence focus。

| 事件 | 变更 | 发布/交互要求 |
|---|---|---|
| 打开APNG | epoch++，清除focus/lock/hover，Loading(frame0) | worker确认前不使用静态数据 |
| 选帧 | pause，epoch++，Loading | 保留Inspector标签，清掉旧流高亮 |
| 切画布阶段 | pause，selection_serial++，Loading | 同帧编码缓存可复用；旧阶段画面拒绝 |
| 切Pixels/Filtered/Defiltered | pause，selection_serial++ | 保持帧身份；解释帧编码数据 |
| 点击默认图像tab0 | pause，epoch++，切StaticImage | 完整恢复静态分析上下文 |
| hover/leave | 只改hover | 不取消trace/statistics；leave恢复同对象lock |
| 点击/X/Y/nudge | pause，serial++，更新lock | 只接受活动视图；坐标检界后发布 |
| Escape/unlock | serial++，清lock和该视图标记 | 取消像素查询，不取消帧统计 |
| Play | 清lock/focus，serial++，Playing | 以已提交PostBlend帧为显示身份 |
| 新播放帧ready | 同步提交图像/身份/坐标/标题 | 目标未ready时不提前修改显示身份 |
| Pause | Ready(当前已显示帧) | 取消尚未提交的下一帧；开始可见面板按需查询 |
| 来源叶点击 | 主预览不变，建立focus | evidence ticket独立绑定来源key及parent serial |
| 返回来源/换主像素 | 清focus | 恢复主帧流；旧focus结果拒绝 |
| 错误 | 当前目标Error | 保留明确标记的已验证部分；不回退伪装ready |
| 换文件/关闭 | generation++，Closed，取消会话 | 清视图/连接/缓存引用；后台退出不得覆写新文件 |

scope发布规则：统计和编码索引只绑定target；像素/trace绑定selection；画面绑定target+stage；来源结果另检focus identity+parent serial。程序修改控件使用 QSignalBlocker，不被识别为用户事件。File Hex始终物理地址；来源导航带origin和serial防循环。

## C7：统计occurrence实际重载

现有occurrence接口接收source/chunks并在内部建IDAT，不能应用C2成对参数机械规则。增加以下精确入口到statistics_occurrence_query.h；返回selection.image必须包含target.key.identity，全局坐标由frame control转换，File chunk范围查询不转换：

```cpp
namespace pnga::analysis_engine {
StatisticsOccurrenceResult query_frame_statistics_occurrence(
    const AnalysisTarget& target, const StageSet* stages,
    const pnga::deflate_index::BlockIndexResult* blocks,
    const StatisticsNavigationRequest& request,
    const CancellationToken* cancellation);
}
```

帧模式的chunk统计只使用frame stream物理spans和所属fcTL；全文件chunk跳转继续调用原路径，不将全部chunks传成当前帧数据。

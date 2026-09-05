# WP-602B–H 书面工作包审查与冻结

- 审查日期：2026-09-05
- 审查对象：`docs/development/wp-602b-statistics-ui-export-reentry.md`
- 审查基线：main `00ee0aa`
- 依赖状态：WP-602A、WP-5U15、WP-5U12A 均已完成

## 一、结论

**APPROVED WITH FREEZE AMENDMENTS（批准并冻结）**。WP-602B–H 可以进入
实施计划。执行顺序固定为 B → C → D → E → F → G → H；每一阶段必须以独立、
可回归的提交结束。下列 R1–R13 是实施计划和执行 Agent 的强制边界。

## 二、现有基础审计

| # | 可复用基础 | 审计结果 |
|---|---|---|
| 1 | `pnga::statistics::collect` 已提供 Qt-free、有界、确定性的 Chunk/Filter/Block/Token 聚合 | ✓ |
| 2 | `statistics_adapter` 已能从 ChunkIndex、StageSet、BlockIndexResult 和 TokenDecodeResult 投影标量 | ✓ |
| 3 | `VirtualIDATStream` 保持多 IDAT 的虚拟读取，现有 Trace 路径已有 IByteSource 适配先例 | ✓ |
| 4 | `JobScheduler::kBackground`、DocumentSession generation gate 和 QThread worker 已存在 | ✓ |
| 5 | CLI 已有稳定参数/退出码/确定性报告与真实二进制集成测试 | ✓ |
| 6 | MainWindow 已完成拆分，可新增独立 StatisticsController 而不把逻辑塞回 facade | ✓ |
| 7 | SelectionBus、Fast Compression Index 和现有 bounded Trace 可承载统计行导航 | ✓ |
| 8 | 当前缺少逐 section 状态、共享 serializer、whole-document Token 流式聚合、Statistics UI/CLI 和 occurrence query | ✓ |

## 三、冻结裁决

- **R1 分阶段交付**：WP-602B–H 是一条依赖链，不拆成相互独立的并行实现。
  B 冻结结果模型；C 提供全文件流式收集；D 冻结字节输出；E 接入 CLI；F 冻结
  view/navigation；G 接入 GUI；H 只做收口 Gate。
- **R2 section 状态**：固定 section 顺序为 `overview`, `chunks`, `filters`,
  `blocks`, `tokens`, `lengths`, `distances`。每个 section 都有独立的 `status`,
  `complete`, `scope`, `error`；缺数据是 `unavailable`，不得序列化为 ready 的零值。
- **R3 状态词汇**：固定为 `unavailable`, `ready`, `partial`, `cancelled`,
  `budget_exceeded`, `invalid_input`, `overflow`, `error`。scope 固定为 `none`,
  `whole_document`, `verified_prefix`。只有 `ready + complete=true +
  whole_document` 表示完整 section。
- **R4 文档身份**：JSON 的 `document.file_size` 使用源的 64 位大小；
  `document.fingerprint` 固定为 `fnv1a64-v1:<16 lowercase hex>`，以 64 KiB
  窗口顺序读取完整源计算。它仅用于同内容结果关联，不作为安全哈希或缓存信任依据；
  不包含路径、mtime、locale 或时钟。
- **R5 聚合内存**：whole-document Token 通过同步 scalar observer 输入
  StatisticsAccumulator；observer 不得持有借用数据或 TokenEvent。解码器只保留 RFC
  1951 所需 32 KiB 窗口和固定工作区，不保留全文件 token/output/table 列表。
  现有 `decode_stored_and_fixed` 兼容接口必须继续通过原测试。
- **R6 工作预算**：默认 sample/bucket 上限沿用 WP-602A；GUI background job 的
  声明工作内存上限为 64 MiB。Token occurrence query 固定为 4,096 token、
  8 MiB 输入窗口。达到预算返回 verified-prefix Partial，不得返回伪完整结果。
- **R7 进度与取消**：收集只在 Statistics tab 首次打开、Refresh 或 Export 时启动；
  不进入文件打开和 hover 热路径。进度发布最多每秒 10 次。关闭/替换文档、Cancel
  或 generation 变化必须合作式取消，旧 generation 结果不得发布。
- **R8 serializer 唯一性**：JSON/CSV 只由 `libs/statistics` 的一个 Qt-free
  serializer 实现。GUI 和 CLI 不拼接自己的字段。输出使用 UTF-8、LF、无 BOM、
  十进制 ASCII；JSON 字段顺序和 CSV 行顺序固定，输出恰好一个结尾 LF。
- **R9 CSV 契约**：列严格为
  `schema_version,section,metric,key,value,unit`。每个 section 先输出 status、
  complete、scope，随后输出固定顺序 totals/buckets；空值与数值 0 必须可区分。
- **R10 CLI 契约**：命令严格为
  `pnga statistics <file> --format json|csv`。stdout 只含报告字节；stderr 只含诊断。
  退出码为 0 ready、1 I/O、2 参数/格式、3 validation issues 且有可用统计、
  4 partial/cancel/budget。`--help` 明示 schema v1 和退出码。
- **R11 GUI 契约**：Inspector 顶层顺序固定为 Reconstruction、Compression、
  Statistics。Statistics 内页固定为 Overview、Chunks、Filters、DEFLATE；DEFLATE
  页面以分组行呈现 Blocks/Tokens/Lengths/Distances，不新增第五个内部页。
  操作为 Refresh、Cancel、Export JSON、Export CSV、Show occurrence。
- **R12 导航契约**：Chunk、Filter、Block 使用已有索引直接定位；Token、Length、
  Distance 使用显式 first/previous/next occurrence query。请求携带 generation、
  bucket identity、direction 和预算；结果通过已有 SelectionBus/typed navigation
  发布，不建立第二套选择状态。
- **R13 导出与 Gate**：GUI 用 QSaveFile 原子替换；序列化或写入失败不覆盖目标。
  WP-602H 必须覆盖 malformed/large/cancel/rapid-switch、三个 locale、CLI/GUI 字节
  一致、CSV 可导入、320 px Inspector、键盘/无障碍、峰值内存及 open/hover 非回归。

## 四、方案比较

1. **分层扩展现有 Statistics 引擎并共享 collector/serializer（采用）**：保留 Qt-free
   边界，CLI/GUI 同源，能证明 O(1) Token 保留和确定性字节输出。
2. GUI 直接读取现有 WP-602A snapshot、CLI 单独实现导出：改动较少，但无法覆盖
   whole-document Token，也会形成两套格式和完成状态，拒绝。
3. 一次性实现 B–H：交付快但接口、取消、UI、导出和导航无法独立审查，出现回归时
   难以定位，拒绝。

## 五、文件边界

- `libs/statistics/**`：结果模型、accumulator、JSON/CSV serializer。
- `libs/deflate-trace/**`：只增加兼容的 scalar token scan/observer，不改变 Deflate
  语义或现有 rich trace 输出。
- `libs/analysis-engine/**`：文档 fingerprint、whole-document collector、view rows 和
  occurrence query。
- `apps/pnga-cli/**`：参数和命令组合，不拥有 schema/serializer。
- `ui/qt/**`：Statistics model/widget，只格式化 immutable view。
- `apps/png-analyzer-gui/**`：lazy worker、generation gate、controller、QSaveFile。
- 对应 unit/integration/gui/performance 测试及用户/CLI 文档。

不允许修改 PNG parser/reconstruction 语义、`third_party/**`、Compare/APNG schema、
引入 Qt 到 `libs/**`、增加依赖、拼接完整 IDAT、保留 whole-document Token 列表或
在 UI 线程执行文件读取/解码。

## 六、实施计划约束

- 计划保存为
  `docs/superpowers/plans/2026-09-05-wp-602b-h-statistics-ui-export.md`。
- 每个任务从能够区分缺失行为的失败测试开始；先 RED，再做最小实现，再跑 focused
  和相关回归测试，最后提交。
- 所有公开名称、schema 字段、objectName、退出码、预算和状态词必须来自本审查或
  原任务包，不允许执行 Agent 临时改名。
- 若实现需要改变依赖方向、增加第三方库、修改解析/解码语义或扩大 APNG/Compare
  范围，立即返回 BLOCKED 并提交独立裁决。

## 七、生效

本审查、更新后的任务包和实施计划入库后，WP-602B–H 具备 agent 按计划串行执行的
条件。文档完成不等于功能 PASS；只有 WP-602H 全部 Gate 通过后才能关闭整个包。

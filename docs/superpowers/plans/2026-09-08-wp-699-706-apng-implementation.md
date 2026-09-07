# WP-699–706 APNG First Release Implementation Plan

> **For agentic workers:** Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax. 默认串行推进，每个任务独立验证与提交；本计划本身不启动实现。

**Goal:** 在保持静态 PNG 行为与性能的前提下，交付可检查帧数据、合成阶段、随机跳帧与播放的 APNG 首版。

**Architecture:** 在现有 trace-model 中明确图像身份；png-format 负责动画结构和虚拟帧流，png-reconstruction 负责像素交付与合成，analysis-engine 负责受限任务、缓存和不可变结果。Qt 仅负责展示、输入和线程结果接入，静态 Reference Backend 保持现有边界。

**Tech Stack:** C++20、现有 CMake dev/asan/release presets、Qt 6、Catch2、固定 vcpkg 依赖和现有 Python Gate 脚本。不增加依赖。

**Spec:** `docs/development/wp-699-706-apng-first-release.md`；`docs/superpowers/specs/2026-09-01-png-analyzer-next-work-packages-design.md`；ADR-0004/0007；`AGENTS.md` 与 `docs/architecture/REPOSITORY_LAYOUT.md`。

**Plan status:** 完整实施初稿，供计划审查；不是功能 PASS。2026-09-08 用户已确认 WP-699 审查完成。实际仓库尚无单独 WP-699 审查文件，工作包顶部仍保留旧的 pending 标记；不把这个标记当作推翻用户确认的理由，也不虚构审查修订。

## 1. 基线、范围和实施选择

- 实际执行仓库：`/Users/lijiangbo/project/PNG-Analyzer`，编写时 HEAD `7e755b7`。执行前重新记录 HEAD、分支、工作树与最终审查结论。
- WP-699 是“已审查”，不是“已实现”：当前 `ImageCoordinate::frame` 仍为 uint64，ArtifactKey 没有图像身份。若执行时这些已落地，验证契约和回归后跳过对应实现，不能重复迁移。
- WP-602H 已有重新验收记录；后续 `9fde15b` 修复 Statistics 到 Hex 导航，`c26b1e7` / `84b4b4e` 修复跨平台测试问题。它们是本计划的静态回归基线。
- 编写时唯一未跟踪文件为 `docs/development/wp-5u12-stages-a-e-execution-audit.external-FAIL.md`；保留，不删除、不作为本任务输出提交。
- 新类、新方法、新测试路径均为本计划拟新增内容，现有接口在第 3 节明确列出。下文代码是要编入对应目标的接口/算法和测试核心，不声称仓库已经存在这些 API。
- WP-700～706 的本轮具体选择随本计划审查，不自动把 WP-699 审查外推为整条功能实现已经批准。

采用“底层契约 → 可测试的逐帧能力 → GUI”的串行方案。一次性贯通所有界面会使身份、像素与缓存问题难以定位；先做只支持 RGBA8 的演示则会推迟 palette/tRNS/16-bit/Adam7 的核心风险。本计划从解码任务开始覆盖全部合法格式。

### 本次具体化的选择

1. APNG 交付为 straight-alpha RGBA8，与当前静态 Reference Backend 的 expand、gray-to-RGB、strip-16 规则对齐；native 样本保留原始 16-bit。tRNS 在缩位前比较，未启用的色彩管理不在此引入。合成使用这个已交付的 RGBA8 空间，UI 清楚区分 native 与 delivered。
2. 所有 frame-local stage 的 Selection x/y 仍为 canvas-global；引擎用 fcTL offset 转为局部坐标，pass/row 属于帧自身布局。矩形外的 frame-local 查询返回不可用，不钳位至边界，也不冒用前一帧的数据。
3. 当前没有 APNG 播放配置持久化；每次打开默认暂停在 AnimationFrame(0)。有效独立 fallback 可以显式选择，保持动画控件可见。
4. 活动作业默认最多一个解码/重放任务和一个下一帧预取请求；新选择取消或替换旧预取。预取只能在同一个预算内运行，不能另设隐形缓存。
5. 64 MiB retained metadata 和 64 MiB artifact/checkpoint LRU 分别度量。解码临时内存另用显式 64 MiB 默认作业预算，覆盖 filtered、passes、target、native、RGBA 和扩容峰值；这是本计划新增的保守资源政策，不是宣称旧 StageSet 已满足该限制。超限返回 Partial/too-large，不能 OOM 后才报错。
6. 每个 Task 首次失败必须由其目标断言或新增接口缺失引起；禁止通过删除静态测试、改低阈值或把失败归为跳过完成任务。

## 2. Global Constraints

- `libs/**` Qt-free；GUI 不解析 PNG、不 Inflate、不计算混色；libpng 仅在现有 backend 和明确的 oracle tests 使用公开 API。
- 不复制完整 IDAT/fdAT 压缩流；逐帧虚拟窗口读取，fdAT 的四字节序号不进入压缩流和物理数据映射。
- metadata 最多保留 100,000 FrameRecords、1,000,000 animation chunks、64 MiB；容器 capacity、索引和诊断字符串也计入。合法但超过预算为 Partial，不是格式错误。
- canvas artifacts 与 post-dispose checkpoints 共用 64 MiB LRU；每 32 帧尝试 checkpoint；不能存下时减少 checkpoint 并重放，不能突破限制。
- frame dimensions、offsets、sequence increments、总帧数、时长、row bytes、像素分配和重放工作量全部 checked arithmetic。
- 取消检查在读取、行/固定处理块和帧边界；发布必须同时匹配 document generation、image identity、request serial。旧帧结果不能覆盖同一文档内的新选择。
- 帧顺序/数据出错保留已验证前缀；partial 允许检查已验证帧但禁播，不自动跳过坏帧。
- 支持 SOURCE/OVER 与 NONE/BACKGROUND/PREVIOUS；首次 PREVIOUS 按 BACKGROUND。每轮开始清为透明黑，有限循环停在最后一帧 Post-Blend。
- delay denominator 0 按 100；raw numerator 0 保留；速度只有 0.25x、0.5x、1x、2x，调速后有效 delay 最少 10 ms。Ctrl+G 输入帧号。
- 静态文档没有 Timeline、Animation Inspector、APNG stage 或 Frame Stream 入口；旧对象名、tab 索引、键盘顺序不变。
- 不添加 Compare、动画导出/编辑、音频、额外速度档和运行位置持久化；不改 Statistics schema v1，不把整文档统计标成当前帧统计。
- 允许路径沿用工作包：png-format、png-reconstruction、trace-model、validation、analysis-engine、ui/qt、GUI app、相关 tests/corpus/performance/fuzz/scripts、ADR-0004/0007 澄清及文档。不修改第三方、CLI 生产功能、其他解码模块；若必要修改超出范围，报告具体最小补充。
- 不新建生产模块、不反转依赖方向；png-format 不依赖 png-reconstruction/analysis-engine；png-reconstruction 的新函数只接收自己的基本几何/像素类型，不能反向依赖 png-format。

## 3. 当前可复用入口与文件职责

| 当前接口/文件 | 实施处理 |
|---|---|
| `trace-model/selection.h`：serialize/deserialize、ImageCoordinate、Stage | T1 迁移身份，保留函数签名，版本化文本 |
| `trace-model/stage_artifact.h`：ArtifactKey | T2 把身份加入比较、缓存键；generation 由 document-scoped cache 隔离 |
| `png-format/chunk_index.h`：index_chunks(IByteSource) | T3 增加受限扫描入口，保留旧接口；避免先完整索引再检查预算 |
| `png-format/virtual_idat_stream.h` | T5 保留旧静态 API，通过公共虚拟压缩流适配器复用 |
| `analysis-engine/filtered_scanlines.h`：inflate_filtered | T6 增加 generic stream 入口，旧入口委托同一实现 |
| `analysis-engine/stage_analysis.h`：analyze_stages、StageSet | T6 保留旧签名，T8 组合为 FrameStageSet |
| `png-reconstruction/native_samples.h`：NativeImage | T7 转 RGBA，T9 用独立像素算法合成 |
| `analysis-engine/artifact_store.h`：ArtifactStore | T10 受限缓存复用，补充元数据清理与生命周期能力 |
| `analysis-engine/job_scheduler.h`：CancellationToken、JobScheduler | T8/T10 复用调度；typed results 不塞入无界 payload 副本 |
| `apps/png-analyzer-gui/src/document_session.*` | T12 动画探测、模型生命周期，避免 UI 线程索引 |
| `main_window_ui.*`、`main_window.*`、`workspace_controller.*` | T13–15 仅接线、动态挂载、统一打开与拖放 |
| `selection_navigation_controller.*`、`ui/qt/src/hex_data_source.cpp` | T14 身份/坐标/逻辑与物理映射 |

所有新 public header 放 `libs/<module>/include/pnga/<module>/` 或现有 `ui/qt/include/pnga/ui/qt/`，实现放该模块 `src/`。任务中的文件列表加上所属现有 CMakeLists.txt 和模块 README.md，即是该任务文件范围；不能借“更新 CMake”修改无关目标。

## 4. 顺序与阶段验收

| 阶段 | 任务 | 可独立验收的交付 |
|---|---|---|
| WP-699 | T1–T2 | 身份/Stage/缓存兼容迁移，全静态回归 |
| WP-700 | T3–T4 | bounded animation index、规则和验证前缀 |
| WP-701 | T5 | 生命周期安全、可逆映射的帧压缩流 |
| WP-702 | T6–T8 | 通用解码、像素转换、按帧不可变结果 |
| WP-703 | T9–T10 | 独立合成算法、LRU 与随机重放 |
| WP-704 | T11 | 可由假时钟驱动的播放模型 |
| WP-705 | T12–T15 | 探测/窗口/导航/打开路径完整接入 |
| WP-706 | T16–T17 | conformance、fuzz、性能、原生界面与最终证据 |

不并行修改共享身份、StageSet、DocumentSession。T16 所需的最小生成样本在 T3 即建立；不是最后才补边界测试。

### 通用验证与提交约定

每个任务：先写测试 → 构建并确认失败 → 最小实现 → 重跑相同测试 → 记录实际数量及退出码 → 路径/预算/生命周期审查 → 仅提交该任务文件。首次执行先运行基线：

```bash
git status --short
git rev-parse HEAD
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
```

任务下方给出的 ctest 命令均须使用 `--no-tests=error`；为新增测试注册实际名称，不能用空匹配当成功。Catch2 新测试加 `[apng]` 标签，但 CTest 匹配的是注册名，两个概念不可混淆。

## T1 — WP-699：图像身份与 Selection 文本迁移

**Files:** 修改 `libs/trace-model/include/pnga/trace-model/selection.h`、`libs/trace-model/src/selection.cpp`、`tests/unit/trace-model/selection_test.cpp`；更新 ADR-0004/0007 的已批准澄清。

**Interfaces:** 保留 serialize(const Selection&) / deserialize(string_view)。新增类型以下列形式表达，不保留可以独立漂移的旧整数 frame 字段：

```cpp
struct StaticImage { bool operator==(const StaticImage&) const = default; };
struct AnimationFrame {
  std::uint32_t index = 0;
  bool operator==(const AnimationFrame&) const = default;
};
using ImageIdentity = std::variant<StaticImage, AnimationFrame>;
// ImageCoordinate 的首个成员：
// ImageIdentity identity = StaticImage{};
```

- [ ] 首先将旧测试中所有 ImageCoordinate aggregate 初始化分类，明确旧整数 0 是 StaticImage，不机械替换为 AnimationFrame(0)。新增断言：

```cpp
ImageCoordinate a;
ImageCoordinate b;
b.identity = AnimationFrame{0};
REQUIRE(a != b);
Selection s; s.image = b;
REQUIRE(deserialize(serialize(s)) == s);
REQUIRE(s.merged_with(s) == s);
auto legacy = deserialize("image:0,0,0,3,4;stage:filtered");
REQUIRE(legacy.has_value());
REQUIRE(std::holds_alternative<StaticImage>(legacy->image->identity));
```

- [ ] 冻结文本方案：非空新记录带 `version:2`；image token 的第一个数值移除，使用独立 `identity:static` 或 `identity:animation,<uint32>`，其余 image 为 pass,row,x,y。空 Selection 仍序列化为空串。若已完成审查明确了另一语法，以审查为准并同步本计划例子，不双轨输出。
- [ ] 旧无版本 image 的 frame=0 解读为 StaticImage；旧非零 frame 没有可证明的动画语义，严格拒绝。拒绝未知版本、缺失/重复身份、负数、溢出、animation index 大于 UINT32_MAX、半截 token。对 node-only 新记录不强制加入无意义 identity。
- [ ] 新增 `kFrameOutput/kPreBlend/kPostBlend/kPostDispose`；显式 stage 文本映射，保留旧 stage 名称及含义，不依赖数组下标偶然对应枚举。
- [ ] 修改代码并执行：`QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'trace_model|selection' --no-tests=error --output-on-failure`。
- [ ] 更新公共契约与提交：`feat: distinguish static images from animation frames`。

## T2 — WP-699：ArtifactKey 和静态消费者兼容

**Files:** 修改 `libs/trace-model/include/pnga/trace-model/stage_artifact.h`、`libs/trace-model/src/stage_artifact.cpp`、`libs/analysis-engine/src/artifact_store.cpp`、`tests/unit/analysis-engine/artifact_store_test.cpp`；新建 `tests/unit/trace-model/stage_artifact_test.cpp` 并加入该目录现有测试目标；只更新编译器定位到的允许路径内坐标/Stage 使用者。

**Interfaces:** ArtifactKey 增加默认 StaticImage 的 `identity`；排序使用 identity kind、frame index、stage、row_begin、row_end 的稳定 tuple，StaticImage 没有伪造的索引。

- [ ] 新增两个键只因身份不同而共存的测试：

```cpp
ArtifactKey a; a.stage = Stage::kDelivered;
ArtifactKey b = a; b.identity = AnimationFrame{0};
REQUIRE(a != b);
REQUIRE((a < b) != (b < a));
```

- [ ] 覆盖两个不同帧、同帧不同 stage、相同行范围、evict/rebuild、文档换代 cache 清空。保留 static row cache 既有行为。
- [ ] 搜索所有 `.frame`、ImageCoordinate/ArtifactKey aggregate 和 Stage switch；逐个迁移，不能靠默认 switch 掩盖新 stage。CLI 若只消费 serialize 不做生产修改；若实际编译强迫超出允许路径，明确报告最小范围补充。
- [ ] 构建全部 dev 并跑全量 CTest，确认静态 golden 只出现已解释的 Selection 文本版本变化；Statistics schema goldens 不变。
- [ ] 提交：`feat: key stage artifacts by image identity`。T1/T2 全静态 Gate 通过后才开始 T3。

## T3 — WP-700：受限的结构扫描与动画 body parser

**Files:** 新建 `libs/png-format/include/pnga/png-format/animation_index.h`、`libs/png-format/src/animation_index.cpp`、`tests/unit/png-format/animation_index_test.cpp`、`tests/common/apng_fixture.h`；修改 `libs/png-format/include/pnga/png-format/chunk_index.h`、`libs/png-format/src/chunk_index.cpp`、`tests/unit/png-format/chunk_index_test.cpp`。

**Interfaces:** 类型放 png_format；不引用 reconstruction ImageHeader 或 trace-model 身份。FrameRecord 的 ordinal 在引擎映射为 AnimationFrame。

```cpp
enum class AnimationStatus { kStatic, kComplete, kPartial, kInvalid, kCancelled };
enum class AnimationStop { kNone, kFormat, kBudget, kCancelled };
struct AnimationControl { std::uint32_t num_frames = 0, num_plays = 0; };
struct FrameControl {
  std::uint32_t sequence = 0, width = 0, height = 0, x = 0, y = 0;
  std::uint16_t delay_num = 0, delay_den = 0;
  std::uint8_t dispose = 0, blend = 0;
};
struct FrameDataSpan { std::uint64_t offset = 0, length = 0; };
struct FrameRecord {
  std::uint32_t ordinal = 0;
  FrameControl control;
  bool uses_idat = false;
  std::vector<FrameDataSpan> data;
};
struct AnimationIssue { std::string rule_id; std::uint64_t offset = 0; };
struct AnimationLimits {
  std::uint64_t max_frames = 100000, max_animation_chunks = 1000000;
  std::uint64_t max_metadata_bytes = 64ull * 1024 * 1024;
};
struct AnimationIndex {
  AnimationStatus status = AnimationStatus::kStatic;
  AnimationStop stop = AnimationStop::kNone;
  std::optional<AnimationControl> control;
  bool default_is_frame = false;
  std::vector<FrameRecord> frames;
  std::vector<AnimationIssue> issues;
  std::uint64_t retained_bytes = 0;
};
AnimationIndex index_animation(const pnga::io::IByteSource& source,
    const AnimationLimits& limits, const std::function<bool()>& cancelled);
```

- [ ] 新增精确 8/26/至少 4-byte parser；源 offset 使用 uint64，fcTL 值用 unsigned big-endian，不允许 reinterpret_cast 未对齐读取。对每个长度的短一字节、精确长度、长一字节写断言；fdAT 4-byte body 代表零 payload，不单独判成短 body。
- [ ] 测试工具仅生成小 PNG：`std::vector<std::byte> make_apng(bool default_is_frame, std::span<const FrameControl> frames)`，每帧生成合法透明 RGBA scanlines、public zlib 压缩、独立 chunk CRC。工具额外提供 `set_sequence(bytes, chunk_ordinal, sequence)` 并重算 CRC，用于测试语义错误而不是意外 CRC 错误。工具只在 tests，生产不能调用。
- [ ] 为 index_chunks 增加有预算/取消参数的兼容 overload，或从现有 envelope scanner 提取单步读取私有 helper 给两个入口复用。index_animation 不得调用无界 index_chunks 后再截断；不保留全部无关 chunk，未知 ancillary body 不分配。
- [ ] 读取和追加前检查预算，容器扩容时同时计入 old+new capacity，诊断数量也有界。以小 limits 测试边界，不在单元测试中分配百万帧。
- [ ] 最小红绿判别：两帧 fixture 设 max_frames=1，结果只能保留第一帧，stop=kBudget；读取计数源使取消在第二次 refill 触发，后续不得持续扫描。
- [ ] 执行 `ctest --preset dev -R 'png_format' --no-tests=error --output-on-failure`；提交 `feat: parse APNG metadata with bounded scanning`。

## T4 — WP-700：顺序状态机与稳定验证报告

**Files:** 修改 T3 文件；新建 `libs/validation/include/pnga/validation/animation.h`、`libs/validation/src/animation.cpp`、`tests/unit/validation/animation_test.cpp`；修改 `libs/analysis-engine/src/validation.cpp`。

**Interfaces:** 新增 `ValidationReport validate_animation(const png_format::AnimationIndex&)`，转译稳定 issue，不重复解析；沿用 validate_document 作为 GUI/CLI 聚合入口。AnimationIndex 的 frames 仅为结构完整的前缀，像素完整性由 T8 再确认。

- [ ] T3 fixture 的两种 layout 均作为成功基线；验证下面所有单变量变体：acTL 缺失/重复/晚于 IDAT；num_frames=0/计数不符；fcTL/fdAT seq 从非零开始、重复、跳号、UINT32_MAX 之后不可回绕；无 fcTL 的数据、空帧、frame data 归属错误；width/height=0、offset+size 越界；IDAT 首帧非全画布；非法 blend/dispose；truncated envelope 与 CRC 问题。
- [ ] 将规则编码为显式状态转移：before-IDAT、in-default-data、between-frames、in-frame-data、ended、stopped；每个 fcTL 到来前结束并验证前一帧，IEND 验证最后帧及总数。当前不完整帧不得加入 verified prefix。
- [ ] 规则 ID 冻结为 `apng.actl.order/count`、`apng.sequence`、`apng.frame.data/geometry`、`apng.frame.control`、`apng.frame.count`、`apng.resource.limit`；保留错误 chunk 的 header_offset。CRC 由现有 integrity 模块产生；CRC 不可信帧不能在 engine capability 中标为 valid。

```cpp
// 对 make_apng(false, 两帧) 的第二个 fcTL 序号置为 9，保持 CRC 正确：
// index_animation 的共同断言：
REQUIRE(result.status == AnimationStatus::kPartial);
REQUIRE(result.stop == AnimationStop::kFormat);
REQUIRE(result.frames.size() == 1);
REQUIRE(result.issues.front().rule_id == "apng.sequence");
```

- [ ] 无动画控制的普通 PNG 返回 kStatic；出现不合法动画结构而零 verified frames 返回 kInvalid。预算/取消与格式错误用独立 stop，不依赖字符串匹配。
- [ ] 执行 `ctest --preset dev -R 'png_format|validation|cli' --no-tests=error --output-on-failure`；提交 `feat: validate APNG sequence and frame prefixes`。

## T5 — WP-701：虚拟帧流与映射

**Files:** 新建 `libs/png-format/include/pnga/png-format/virtual_compressed_stream.h`、`virtual_frame_stream.h`，`libs/png-format/src/virtual_frame_stream.cpp`、`tests/unit/png-format/virtual_frame_stream_test.cpp`；按需修改 virtual_idat_stream 的适配部分。

**Interfaces:** 公共流继承 IByteSource，持有 shared source。定义 `IVirtualCompressedStream : public io::IByteSource`，新增与现有 IDAT 同签名的 logical_to_physical 和 physical_to_logical；view 返回 nullopt。新 `VirtualFrameStream` 实现该接口，工厂：

```cpp
std::shared_ptr<const IVirtualCompressedStream> make_frame_stream(
    std::shared_ptr<const io::IByteSource> source,
    std::shared_ptr<const AnimationIndex> index, std::uint32_t ordinal);
std::shared_ptr<const IVirtualCompressedStream> make_idat_stream(
    std::shared_ptr<const io::IByteSource> source, const ChunkIndex& index);
```

- [ ] 工厂拒绝无 owner、ordinal 超 verified prefix、span 溢出/越文件结尾；构造只保留受限 span 元数据，不保留压缩字节副本。frame 工厂借用 immutable index 中 spans，避免每次查询复制整张表。
- [ ] 跨 payload 拼接点读取：把 zlib header、一个 token、Adler 分别拆开，结果与单 span 完全一致。物理映射只含 payload；fdAT 序号、chunk header、CRC 和空 payload 不映射。
- [ ] 零宽锚点：中间边界取下一个非空数据 span 的首字节；流末尾取最后非空 payload 的 end；全空流无有效锚点。复用 WP-602 EOB 回归语义。

```cpp
std::vector<PhysicalRange> spans;
REQUIRE(stream->logical_to_physical(0, stream->size(), spans));
for (const auto& p : spans) {
  REQUIRE(stream->physical_to_logical(p.offset).has_value());
}
// 释放调用者 source/index 后 stream 仍可读；请求 size()+1 必须失败。
```

- [ ] 用计数源、空中间 fdAT、大逻辑 offset 和随机窗口验证；旧 VirtualIDATStream 全部回归。运行 png_format CTest，提交 `feat: add lifetime-safe virtual frame streams`。

## T6 — WP-702：通用解码入口、真实预算与取消

**Files:** 修改 `libs/analysis-engine/include/pnga/analysis-engine/filtered_scanlines.h`、`stage_analysis.h` 和对应 src；修改 `libs/png-reconstruction/include/pnga/png-reconstruction/pass_reconstruction.h`、`native_samples.h` 及对应 src，新增 `libs/analysis-engine/src/cancellable_stream.h`；测试 `tests/unit/analysis-engine/filtered_scanlines_test.cpp`、`stage_analysis_test.cpp` 及对应 reconstruction tests。

**Interfaces:** 新增 engine `DecodeLimits { uint64_t max_working_bytes=64*1024*1024; }`、`StageStop { kReady, kBudget, kCancelled, kInvalid }`；StageSet 增加 `StageStop stop = StageStop::kInvalid`，所有旧/新出口与 success 同步赋值。新 overload 返回 StageSet：`analyze_stages(const IVirtualCompressedStream&, const ImageHeader&, const DecodeLimits&, const CancellationToken*)`。旧签名保持既有静态政策，委托公共 pipeline；不能把取消伪装坏 zlib。

- [ ] 同一压缩数据分别通过 IDAT adapter 和 frame stream 解码，assert filtered、unfiltered、native、scanlines 完全一致。
- [ ] layout 计算后、首次 resize 前计算所有同时存活 buffer 的 checked 总量，包含 uint16 native、pass buffers 和行表。先测小预算拒绝，并用分配计数证明拒绝发生在大分配前。移除不必要 const 导致的伪 move/copy。
- [ ] 通过 IByteSource read wrapper 在 refill 检查 CancellationToken，标记主动取消原因；继续复用 public inflate_stream，不复制 Inflate。不为 APNG 修改 deflate-runtime。
- [ ] reconstruction/native 新增兼容的可取消 overload，接受 `std::function<bool()>`，不依赖 engine token；每行/固定像素块检查。测试用计数 predicate，在处理中触发，不能只验证入口已取消。

```cpp
CancellationToken token; token.request_cancel();
const auto out = analyze_stages(*stream, header, DecodeLimits{}, &token);
REQUIRE_FALSE(out.success);
REQUIRE(out.stop == StageStop::kCancelled);
// 读取计数源还必须断言 read counter == 0。
```

- [ ] corrupt zlib、Adler mismatch、多/少解压字节、非法 filter 必须失败；不能返回部分像素为完整 frame。运行 `ctest --preset dev -R 'analysis_engine|png_reconstruction|differential' --no-tests=error --output-on-failure`。
- [ ] 提交 `refactor: share bounded stage decoding across image streams`。

## T7 — WP-702：palette/tRNS 和 RGBA 交付

**Files:** 新建 `libs/png-reconstruction/include/pnga/png-reconstruction/rgba_delivery.h`、`libs/png-reconstruction/src/rgba_delivery.cpp`、`tests/unit/png-reconstruction/rgba_delivery_test.cpp`；在 png-format animation 解析中保存受限 PLTE/tRNS bytes，在引擎转换为 reconstruction 自有上下文。

**Interfaces:** reconstruction 定义 `RgbaImage { uint32_t width,height; vector<uint8_t> pixels; }`、`DeliveryContext { vector<array<uint8_t,3>> palette; vector<uint8_t> palette_alpha; optional<uint16_t> transparent_gray; optional<array<uint16_t,3>> transparent_rgb; }`、`DeliveryResult { bool success; string error; RgbaImage image; }`。新增 `deliver_rgba8(const NativeImage&, const DeliveryContext&, uint64_t max_bytes, const function<bool()>& cancelled)`，返回 DeliveryResult。AnimationIndex 增加 `vector<byte> palette_bytes` 和 `transparency_bytes`，分别最多 768/256 bytes（gray/RGB tRNS 必须恰为 2/6）；只存全局上下文一次并计入 metadata，不放到每个 FrameRecord。

- [ ] 先覆盖类型 0:1/2/4/8/16，2:8/16，3:1/2/4/8，4:8/16，6:8/16；palette 越界、缺少 palette、tRNS 缺省项 alpha=255、非法长度和 gray/RGB tRNS 原深度比较。

```cpp
NativeImage n; n.width=1; n.height=1; n.bit_depth=16;
n.color_type=6; n.channels=4; n.samples={0x1234,0xabcd,0xffff,0x8001};
const auto r=deliver_rgba8(n, DeliveryContext{}, 4, []{return false;});
REQUIRE(r.success);
REQUIRE(r.image.pixels == std::vector<std::uint8_t>{0x12,0xab,0xff,0x80});
```

- [ ] 小位深灰度 `sample*255/((1<<depth)-1)`；16-bit `sample>>8`；alpha 不存在为 255；灰度复制至 RGB。逐帧复用同一全局 palette/tRNS，不在 GUI 计算。
- [ ] 在 tests/differential 的现有允许 oracle 目标中，用生成的等价静态 PNG 验证 RGBA 与现有公开 libpng backend 一致；测试侧重封装小帧可行，生产不得创建完整临时 PNG 解码。
- [ ] 测试工作预算不足、native 样本数量不符及处理中取消；运行 reconstruction/differential CTest。提交 `feat: deliver frame native samples as RGBA`。

## T8 — WP-702：FrameStageSet、请求身份与来源

**Files:** 新建 `libs/analysis-engine/include/pnga/analysis-engine/frame_analysis.h`、`libs/analysis-engine/src/frame_analysis.cpp`、`tests/unit/analysis-engine/frame_analysis_test.cpp`；修改 engine `pixel_provenance`、`coordinate_query`、`stage_viewport` 的帧上下文适配部分。

**Interfaces:** 引擎新增 `FrameRequest { generation, request_serial, ordinal, shared source, shared AnimationIndex, ImageHeader canvas_header, DeliveryContext delivery, DecodeLimits limits }`；generation/serial 为 uint64，ordinal 为 uint32。`FrameStageSet { ImageIdentity identity; FrameControl control; StageSet stages; RgbaImage delivered; }`；`FrameResult { generation,request_serial; ImageIdentity identity; enum Stop {ready,partial,cancelled,error}; shared_ptr<const FrameStageSet> frame; string reason; }`。公开 `FrameResult analyze_frame(const FrameRequest&, const CancellationToken*)`。

- [ ] Frame header 复制已验证 IHDR 的 depth/type/interlace，仅 width/height 替换为 fcTL；compression/filter method 在 T4 验证为 0。first IDAT frame 与独立 fallback 分开请求身份。
- [ ] 对两种 layout、Adam7、palette/tRNS、全部合法 depth、multi-fdAT、坏流分别测试；stage 与 pixel provenance 使用该帧虚拟流，不再构造整文档 VirtualIDATStream。
- [ ] Selection 的 global x/y checked 减 offset，局部 pass/row 通过现有 scanline layout 算；物理/逻辑 spans 带正确 frame identity。frame rectangle 外请求稳定不可用。

```cpp
// 同一文件同一坐标的 static fallback 与 animation frame 0：
REQUIRE(static_selection.image->identity != frame_selection.image->identity);
REQUIRE(frame_result.identity == ImageIdentity{AnimationFrame{0}});
// fdAT 来源中每个 physical span 都落在对应 data_offset+4 之后。
```

- [ ] typed immutable result 由 shared_ptr 传递，不复制整份 StageSet 到 JobResult payload；发布三重比对 generation/identity/serial。用延迟受控 worker 测 A→B→A 请求，旧 A 结果也不能发布。
- [ ] 运行 analysis_engine/selection/differential CTest；提交 `feat: analyze requested APNG frames with provenance`。

## T9 — WP-703：独立 Canvas 混色与 Dispose

**Files:** 新建 `libs/png-reconstruction/include/pnga/png-reconstruction/canvas_composition.h`、`libs/png-reconstruction/src/canvas_composition.cpp`、`tests/unit/png-reconstruction/canvas_composition_test.cpp`。

**Interfaces:** reconstruction 自有 `FrameRect { uint32_t x,y,width,height; }`、`Blend {source,over}`、`Dispose {none,background,previous}`；`CompositionResult { bool success; string error; }`。`blend_into(RgbaImage& canvas,const RgbaImage& frame,FrameRect,Blend,const function<bool()>&)`；`dispose_into(RgbaImage& canvas,FrameRect,Dispose,span<const uint8_t> saved_rect,bool first_frame,const function<bool()>&)`。引擎负责保存 rectangle 和 stage 请求，算法层不依赖 png-format FrameControl。

- [ ] SOURCE 覆盖含 alpha=0 的 RGBA；OVER 使用 uint64 intermediates，round-to-nearest、half-up。对单个通道，M=255，以下数值仅作用于已交付 RGBA8：

```cpp
const std::uint64_t den = sa*255ull + da*(255ull-sa);
const auto oa = (den + 127) / 255;
const auto oc = den == 0 ? 0 :
    (sc*sa*255ull + dc*da*(255ull-sa) + den/2) / den;
```

- [ ] 独立手算 oracle：红 `(255,0,0,128)` OVER 不透明蓝 `(0,0,255,255)` → `(128,0,127,255)`；透明 OVER 透明→透明黑；不透明源→源；矩形外字节逐字节不变。测试不能调用 blend_into 生成 expected。
- [ ] PREVIOUS 只保存受影响的 Pre-Blend rect；BACKGROUND 清透明黑；NONE 保留；first PREVIOUS 清空。检验保存矩形与画布不别名，重叠后恢复精确。
- [ ] 每轮初始 canvas 清透明黑，fallback 从不作为动画背景。检查 offset 加法与像素乘法后才索引，取消不得把半成品当成可发布阶段。
- [ ] 运行 reconstruction CTest；提交 `feat: compose APNG canvas stages deterministically`。

## T10 — WP-703：统一 LRU、四阶段按需物化和随机重放

**Files:** 新建 `libs/analysis-engine/include/pnga/analysis-engine/animation_replay.h`、`libs/analysis-engine/src/animation_replay.cpp`、`tests/unit/analysis-engine/animation_replay_test.cpp`；修改 `artifact_store.h/.cpp` 增加 erase/clear 和受限条目元数据清理。

**Interfaces:** `ReplayRequest { FrameRequest frame; Stage requested_stage; }`；`ReplayResult { FrameResult::Stop stop; uint64_t generation,request_serial; ImageIdentity identity; Stage stage; shared_ptr<const RgbaImage> image; string reason; }`；`AnimationReplay` 文档级对象持有预算、checkpoint 元数据和 source，公开 `ReplayResult materialize(const ReplayRequest&,const CancellationToken*)`、`void clear()`、`uint64_t retained_bytes() const`。

- [ ] ArtifactKey identity + stage 区分四个请求。Frame Output 只为 rectangle；Pre/Post-Blend/Post-Dispose 是 canvas。不要每帧同时创建/保存四张画布。
- [ ] checkpoint 语义固定为“第 i 帧完成 dispose 后”，恢复后下一次解码 i+1。找不到 checkpoint 从透明画布 replay；每 32 帧尝试保存，不能适配预算即不存。
- [ ] LRU 的 64 MiB 覆盖 retained pixels、snapshot handle 实际所有权、checkpoint 和 key/entry 元数据；evicted 条目的 metadata 也回收，不允许 ArtifactStore 仅像素有界而 tombstone 无界增长。
- [ ] GUI 持有 shared_ptr 的已淘汰 image 仍算存活预算，不能 remove-from-map 后减计数。用有 lifetime 的 reservation/lease 回收计费；临时 canvas、PREVIOUS rect 和旧/新缓冲重叠计入作业预算，分别报告。

```cpp
// 顺序重放与冷随机跳转同一帧应完全一致，不能互为 expected 生成器：
REQUIRE(cold.image->pixels == independently_calculated_pixels);
REQUIRE(warm.image->pixels == independently_calculated_pixels);
REQUIRE(replay.retained_bytes() <= 64ull*1024*1024);
```

- [ ] 小预算迫使驱逐后重复跳转 0/31/32/33/last，覆盖冷/暖与 PREVIOUS 链；持有旧 UI lease 时请求新帧应拒绝/等待而非超额。单 artifact 大于预算返回 partial/too-large。
- [ ] 在中途取消 replay、换文档、换帧、更新 serial；只发布请求阶段的完整结果。每帧 progress，频率沿用现有节流方式，测试不依赖 sleep。
- [ ] 运行 analysis_engine CTest；提交 `feat: replay APNG frames within a shared cache budget`。

## T11 — WP-704：时间线与确定性播放模型

**Files:** 新建 `libs/analysis-engine/include/pnga/analysis-engine/animation_playback.h`、`libs/analysis-engine/src/animation_playback.cpp`、`tests/unit/analysis-engine/animation_playback_test.cpp`。

**Interfaces:** `PlaybackState {paused,playing,waiting_for_frame,ended,partial,error}`；`PlaybackSpeed {quarter,half,normal,double_speed}`；`TimelineEntry { uint32_t ordinal; uint16_t raw_num,raw_den; uint64_t start_ns,duration_ns; }`；`AnimationTimeline { vector<TimelineEntry> entries; uint32_t num_plays; bool complete; }`。`make_timeline(const AnimationIndex&,PlaybackSpeed)` 返回 optional，checked overflow 失败；`AnimationPlayback` 提供 play(now_ns)、pause()、seek(ordinal,now_ns)、tick(now_ns)、frame_ready(ordinal,serial,now_ns)、set_speed(speed,now_ns)，now_ns 为 uint64 单调时间，实例绑定 generation，内部持有请求 serial。

- [ ] duration 使用整数有理数计算，正分数向上取整为 ns 后 clamp 10,000,000 ns；乘法 checked；speed 是精确倍率而非 float。累积 start checked，overflow 形成 timeline error，不回绕。
- [ ] tick 不直接解码；产生请求后进入 waiting，收到匹配 result 才显示、开始该帧持续时间。等待期间不跳过未显示帧，不无限追赶墙钟。
- [ ] seek 在 verified 范围内且暂停；play 从 ended 重新从 frame0 和透明 canvas 开始；num_plays=0 无限，有限循环停止于最后 Post-Blend；速率改变保留当前帧并重置该帧显示计时，不改变 raw delay。

```cpp
// raw 1/100 秒，2x 得 5ms 再 clamp 为 10ms：
REQUIRE(timeline.entries.at(0).duration_ns == 10000000);
// 注入 now=0/9999999/10000000，临界点前不请求下一帧，点上请求一次。
// raw 0/0 保留为 0/0，而有效时间至少 10ms。
```

- [ ] 测试首/前/后/末、零分母/零分子、所有四速、单帧、无限/有限循环、stale ready、快速 seek、partial 禁播和 error；不使用 sleep。
- [ ] 运行 analysis_engine CTest；提交 `feat: add deterministic APNG timeline and playback`。

## T12 — WP-705：DocumentSession 与后台动画控制器

**Files:** 新建 `apps/png-analyzer-gui/src/animation_controller.h/.cpp`、`animation_worker.h/.cpp`、`tests/gui/animation_controller_test.cpp`；修改 `document_session.h/.cpp`、`document_workers.h/.cpp`。

**Interfaces:** `AnimationController` 持有 engine playback/replay，QObject signals 仅携带 generation、request_serial、immutable result。公开 `setDocument(const FrameRequest& document_context)`、`selectFrame(uint32_t)`、`selectStaticFallback()`、`play()`、`pause()`、`setSpeed(PlaybackSpeed)`、`close()`；`document_context.ordinal` 不触发请求，由 valid capability 后显式选择。新增 signal `framePublished(std::shared_ptr<const ReplayResult> result)`，在 Qt 注册该元类型；worker 不触及 QWidget。

- [ ] 先测试 static、detecting、valid、partial、有错误零帧、切文档状态；GUI 中 capability 只由解析+验证结果产生。detecting 不显示动画控件。
- [ ] 把 APNG 识别/索引放后台，不能先在 DocumentSession::replace 同步调用无界 index_chunks；复用 T3 的受限扫描结果给 Chunk List。只移动与新打开流程必要相关的索引工作，不重构所有 session 功能。
- [ ] valid 后默认请求 frame0；primary static stage/reference worker 不能晚到后覆盖动画选中帧。fallback 结果独立存储，选择 fallback 暂停且保留动画 capability。
- [ ] 当前帧优先，最多一个下一帧预取；关闭时先增 generation，再取消/清 cache、断开发布。worker completion 不在 UI 线程做任何解码。

```cpp
// QtTest：受控旧 worker 在新请求之后完成。
QSignalSpy published(&controller, &AnimationController::framePublished);
// selectFrame(1), selectFrame(2), complete(1, oldSerial), complete(2, newSerial)
QCOMPARE(published.count(), 1);
// 断言唯一结果为 frame2/newSerial。
```

- [ ] 新 GUI target 注册 `gui_apng_controller_tests`；运行 `QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'gui_apng_controller|document_session|statistics' --no-tests=error --output-on-failure`。
- [ ] 提交 `feat: coordinate APNG document and frame workers`。

## T13 — WP-705：按 capability 挂载时间线与 Inspector

**Files:** 新建 `ui/qt/include/pnga/ui/qt/animation_timeline_model.h`、`animation_timeline.h`、`animation_inspector.h` 和对应 `ui/qt/src/*.cpp`；新建 `tests/gui/animation_ui_test.cpp`；修改 `main_window_ui.h/.cpp` 与 `main_window.cpp` 的组装接线。

**Interfaces:** model 基于 QAbstractListModel，行对应 TimelineEntry，不一帧一个 QWidget；widget 发 frameRequested(uint32_t)、playRequested/pauseRequested、speedRequested，Inspector 接 immutable FrameControl/timeline/status 数据。

- [ ] 普通 static 打开后 findChildren 检查 Timeline/Animation Inspector/APNG stage 对象不存在，旧四个 Preview tabs 和 Inspector 顺序完全不变。静态从动画切换回来要销毁动态对象、移除 action/连接和禁用项，不仅隐藏。
- [ ] valid 挂载四个 stage tabs（Frame Output、Pre-Blend、Post-Blend、Post-Dispose），Preview 下方可折叠虚拟化 timeline；Inspector 顺序 Reconstruction、Compression、Statistics（存在时）、Animation。
- [ ] raw/effective delay、frame rect、sequence、blend/dispose、num_plays/status 字段齐全；timeline 有首/前/播放/后/末、直接帧输入、时间/循环、四档速度；Ctrl+G 聚焦帧号输入。显示帧号明确采用 0-based，与日志/identity 一致。
- [ ] partial 有 verified frame 则可选前缀帧但不能播放；fallback 显示 `Static fallback · not an animation frame`，不将 fallback 加为 timeline 第 0 行。
- [ ] 对 100,000 行模型统计委托/控件数与 viewport 大小相关，滚动不创建全部 QWidget；320px Inspector 可读/横向滚动、键盘 focus、浅/深主题和 DPI 1/1.5/2。
- [ ] 注册 `gui_apng_ui_tests`；运行 gui_apng_ui/main_window/gui_layout CTest，保留 main_window 行数 Gate。提交 `feat: show adaptive APNG timeline and inspector`。

## T14 — WP-705：Stage、坐标和 Hex 导航闭环

**Files:** 修改 `selection_navigation_controller.h/.cpp`、`ui/qt/include/pnga/ui/qt/hex_data_source.h`、`ui/qt/src/hex_data_source.cpp`、`hex_source_tab_bar.cpp`、`stage_preview_view.cpp`；新建 `tests/gui/apng_navigation_test.cpp`，补充 `tests/gui/hex_data_source_test.cpp`。

**Interfaces:** 新增 `make_frame_hex_source(shared_ptr<const IVirtualCompressedStream>)`；Inflated/Defiltered 沿用 StageSet 来源但 owner 绑定 FrameStageSet；SelectionBus 继续使用统一 Selection，禁止引入独立 GUI frame ID。

- [ ] 动画 Hex 为 File / Frame Stream / Inflated / Defiltered；静态保留原实际 IDAT 标签。文件偏移导航总指向 File；逻辑偏移指向当前 frame stream；不能误把 fdAT 序号纳入偏移。
- [ ] x/y 或任一 manual pixel 选择先暂停，再发带 image identity 的查询；canvas stage 点击矩形外不得回退至该帧局部像素。canvas provenance 表达合成操作/先前帧贡献，不能假称其像素只由当前压缩流某一个字节生成。
- [ ] Frame Output 的显示 extent 是局部矩形，发布时加 offset；canvas stages 已是 global，不能重复加 offset。pass/row 仍由帧 reconstruction 结果计算。
- [ ] 多 fdAT、跨 payload token、空 fdAT、EOB 零宽锚点逐一覆盖，assert exact physical spans；切帧后旧 HexDataSource 仍有正确 owner，但 stale 结果不可重新挂载。
- [ ] Statistics 保持整文档静态/default 范围，Animation Inspector 承担动画元数据。APNG 下不伪造当前帧统计或注入 schema v1 frame 字段，显示其原有范围；导航到静态统计 occurrence 必须显式切为 StaticImage。
- [ ] 注册 `gui_apng_navigation_tests`，运行 apng_navigation/selection/hex/statistics CTest；提交 `feat: route APNG frame selections through shared navigation`。

## T15 — WP-705：统一文件打开与拖放

**Files:** 修改 `apps/png-analyzer-gui/src/main_window.cpp`、`workspace_controller.h/.cpp`；新建 GUI 内部 `png_file_filter.h`；补充 `tests/gui/main_window_layout_test.cpp` 和 `tests/gui/animation_ui_test.cpp`。

**Interfaces:** GUI helper `bool hasSupportedPngSuffix(const QString&)`，仅为 picker/drop 筛选，不用于 animation capability；picker 字符串精确为 `PNG/APNG files (*.png *.PNG *.apng *.APNG)`。

- [ ] `.png/.PNG/.apng/.APNG`、混合大小写走同一 predicate；改名为 .png 的真实 APNG 识别动画，.apng 静态 PNG 不出现动画 UI，伪 PNG signature 仍拒绝。
- [ ] openFile 的内容验证保留，recent-file/open/drop 统一 session 入口；失败不覆盖现有有效文档。
- [ ] 快速连续 drop 两文件、关闭后旧 worker 完成、动画→静态→动画，确保控件、source 和 generation 不串用。
- [ ] 验证示例：`QVERIFY(hasSupportedPngSuffix("example.ApNg")); QVERIFY_FALSE(hasSupportedPngSuffix("example.apng.txt"));`，并用真实生成字节检查 capability。
- [ ] 运行 main_window/apng_ui CTest；提交 `feat: accept APNG files through shared open and drop paths`。

## T16 — WP-706：完整样本、独立 oracle 与 fuzz

**Files:** 扩展 `tests/common/apng_fixture.h`、`tests/corpus/generate_controlled_corpus.cpp`、`tests/corpus/manifest.yaml`；新建 `tests/differential/apng_frame_test.cpp`、`tests/fuzz/apng_fuzz_test.cpp`；修改 `tests/fuzz/coverage/coverage_fuzz_driver.cpp` 和所属 CMake；必要时扩展现有 corpus verifier/generator runner。

- [ ] 固定样本矩阵：两种 default layout；1/2/100 帧；所有 Blend×Dispose；subrect/offset；透明/不透明；raw delay 0/denominator 0；有限/无限循环；多 fdAT；Stored/Fixed/Dynamic；所有颜色类型与合法位深；Adam7；序号 gap/duplicate/order、frame count/geometry、truncation、Adler/CRC mismatch。
- [ ] 生成结果包含来源、license、SHA-256、generator revision、classification、expected pixels/规则 ID 和链接测试；大压力图只存生成参数，在 build 下生成。增加 manifest 项后运行现有 verifier，不能为了新增样本跳过既有 schema。
- [ ] differential 只用现有公开 libpng 的等价静态帧结果比较 T7/T8，不能宣称验证 APNG composition；合成由 T9 手算数组和独立 reference calculation 覆盖。oracle 不调用生产合成函数。
- [ ] fuzz body 长度、序号构造、stream read/map、矩形 arithmetic；先做固定种子 deterministic replay，coverage driver 也接到新路径。异常只能得到稳定错误/预算结果，不 crash、不读越界、不无界分配。
- [ ] 运行 dev/asan 中的 apng、differential、fuzz suites；记录 replay 数、coverage runtime 的实际结果与未配置项，不能用 fuzz smoke 替代 coverage-guided evidence。
- [ ] 提交 `test: cover APNG conformance and hostile inputs`。

## T17 — WP-706：性能、原生界面与最终 Gate

**Files:** 修改 `tests/performance/performance_runner.cpp`、`tests/performance/README.md`、必要的 runner/threshold schema；新增 `tests/gui/apng_product_gate_test.cpp`、`docs/development/wp-699-706-apng-completion.md`；更新 APNG 工作包、当前开发进度、用户文档。

- [ ] 性能场景：100,000 metadata-only frames；100 small-frame 播放；1,000-frame 的固定种子随机跳转；分别记录 cold/warm replay P50/P95、timeline scroll、实际 metadata/cached/live/scratch peak、进程 RSS、GUI thread blocking。记录 OS/CPU/Qt/构建/输入 hash，不用这一次测量自动生成宽松阈值。
- [ ] 硬性断言来自冻结预算：retained metadata ≤64 MiB，frames ≤100,000，animation chunks ≤1,000,000；所有 canvas/checkpoint leases ≤64 MiB；单作业预算拒绝与取消响应按计数测试。时间指标当前没有已批准 APNG 数值阈值，作为基线报告明确列出，不谎称“APNG 性能阈值已过”。静态 WP-604 阈值必须继续 enforce。
- [ ] product Gate 覆盖整条打开→默认 frame0 暂停→播放/跳帧→查看四阶段→物理 Hex→partial 禁播→fallback→关闭→静态无动画控件。offscreen 测试之外，在具备 Qt kit 的原生窗口实际操作并保存带环境信息的截图/记录。
- [ ] 原生证据至少明确当前执行平台的交互结果；Windows/Linux 的未执行项逐项标记，不能由 macOS 推断。Release package smoke 只验本地产物，不自动 tag、push、merge、发布。
- [ ] 在最终代码树上执行完整命令矩阵：

```bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'selection|png_format|validation|reconstruction|apng|main_window|gui' --no-tests=error --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --no-tests=error --output-on-failure
python3 scripts/run_gui_gate.py
cmake --preset asan
cmake --build --preset asan --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset asan -R 'apng|png_format|reconstruction|analysis_engine|fuzz' --no-tests=error --output-on-failure
python3 scripts/run_sanitizer_fuzz_gate.py
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
python3 scripts/run_package_smoke.py --preset release --jobs 2
git diff --check
```

- [ ] 在 completion 文档填入实际 commit/tree、每条命令退出码和数量、fixture hashes、性能记录路径、native evidence、已覆盖/未覆盖项、变更路径清单。全部必需项满足才报告 PASS；失败报告 FAIL；架构/范围依赖未满足报告 BLOCKED。
- [ ] 更新路线图顶部当前 overlay 与工作包首行状态，保留历史记录但明确已被什么提交/验收取代；避免重复 WP-602 的“顶部 reopened、末尾 PASS”矛盾。
- [ ] 提交 `test: close APNG first release acceptance gate`。

## 5. 跨任务审查重点与停止条件

| 风险 | 最早判别任务 | 必须看到的证据 |
|---|---|---|
| 用户审查与本地旧标记不同步 | T1 | 在工作包记录用户确认及真实修订，不能补造批准历史 |
| 静态 fallback 与 frame0 混同 | T1/T2/T8 | 不同 identity、不同 key、不同 GUI request |
| 先无界索引/解码后检查预算 | T3/T6/T12 | 首次大分配前拒绝、扫描中取消、GUI 无同步扫描 |
| 部分 frame 被当完整 | T4/T8 | verified structural prefix 与 decoded success 分离 |
| 16-bit/tRNS 转换错误 | T7 | 原位深比较、静态公开 oracle 字节一致 |
| 合成错 alpha/错 dispose | T9 | 独立 RGBA 数组与矩形外不变 |
| 共享指针让 LRU 实际超额 | T10 | 外部持有时仍计费、所有权释放后才退预算 |
| 同一文档旧帧覆盖新选择 | T8/T12 | generation+identity+serial 三重 Gate |
| 动画控件渗入静态文档 | T13/T15 | 静态对象不存在和动画→静态销毁验证 |
| 旧 IDAT trace 错用于 fdAT | T8/T14 | 当前帧 stream+physical mapping 的精确断言 |
| 动画元数据污染 Statistics v1 | T14 | schema golden 不变、明确静态/default 范围 |
| 全量 Gate 只使用历史二进制 | T17 | 最终树重新构建、实际命令与证据绑定 |

对未经批准的新生产模块、依赖方向变化、第三方 decoder 或超出允许路径的修改，执行者必须提出最小具体调整。不要把本计划的建议名称当成既有 API；不要根据附带历史文档的“下一步发布”文字擅自发布。

## 6. 计划自审与交付边界

- 覆盖：WP-699→T1/T2，700→T3/T4，701→T5，702→T6/T7/T8，703→T9/T10，704→T11，705→T12/T13/T14/T15，706→T16/T17。
- 接口方向：png-format 持有格式字段；png-reconstruction 只用独立像素/rect 类型；analysis-engine 作转换和组合；Qt 只消费不可变结果。
- 兼容：旧静态 API 保留入口；Selection 文本有明确迁移；静态 UI、Statistics goldens、WP-604 阈值继续验证。
- 本次交付仅为文档，不执行这些实现步骤、不声称任何 APNG 功能已通过。默认执行入口为 T1；如果 WP-699 实现已在后续提交完成，则先完成 T1/T2 验证再进入 T3。

格式依据为 [W3C PNG 第三版固定版本](https://www.w3.org/TR/2025/REC-png-3-20250624/)，重点查阅 §4.9、§11.3.6 与 §13；本计划以仓库工作包为产品范围依据，协议规范不替代本地资源和 UI 决策。

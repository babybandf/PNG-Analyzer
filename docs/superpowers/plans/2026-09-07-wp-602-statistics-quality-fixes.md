# WP-602 Statistics 三项质量修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. 默认串行执行，不要求启动子 agent。

**Goal:** 修复 Overview 伪完整零值、Block 扫描预算与取消漏洞、Stored EOB 物理导航错误，然后重新验收 WP-602H。

**Architecture:** 沿用 Qt-free collector、单一 JSON/CSV serializer、VirtualIDATStream 与 SelectionBus。保留 schema v1 和旧 `index_blocks(source, max_output_bytes)` 调用兼容性，为 Statistics 的 Block 前置工作增加有界、可取消路径；不改 PNG/DEFLATE 解码语义。

**Tech Stack:** C++20、CMake、Qt 6、Catch2、现有 vcpkg 固定版本依赖、Python 3 Gate 脚本。

**Spec:** 本计划第 1–3 节是本轮修复约束；背景证据见`docs/development/wp-602b-h-quality-review-2026-09-07.md`。执行时同时读取真实仓库的 `docs/development/wp-602b-statistics-ui-export-reentry.md` 和 `docs/development/wp-602b-h-written-package-review.md`。

## 1. 执行入口与基线

- **真实代码仓库：** `/Users/lijiangbo/project/PNG-Analyzer`。
- 此计划已放入真实仓库 `docs/superpowers/plans/`；所有下文相对代码路径、构建命令均以真实仓库或其隔离 worktree 为根。
- 审查基线 main：`6d4226e`；B–H 合入提交 `4898d11`，原 Gate 提交 `95510bb`。
- 用户已同意三项修复的必要性，本次交付为实施计划；执行者按收到的实施指令开始工作，不把本文件的存在当作自动运行或发布授权。
- 历史基线存在未提交的 `tests/gui/CMakeLists.txt`、`tests/gui/tmp_export_probe.cpp` 和另一份外部审查记录。重新检查，保留这些工作，不能清理、覆盖或混入本轮提交。
- 源码可能继续变化；先验证缺陷仍存在。如果某一项已修复，则检查独立回归证据并记录，不重复修改。

## 2. 全局约束与范围补充

1. 读取 `AGENTS.md`、`REPOSITORY_LAYOUT.md`、`docs/architecture/REPOSITORY_LAYOUT.md`、ADR-0003/0004/0005/0006 及原 B–H 包。
2. `libs/**` 保持 Qt-free；不增加依赖，不改 libpng/zlib 私有实现，不拼接完整 IDAT，不保留全文件 Token 列表。
3. schema 仍为 `pnga.statistics`、version 1；section 顺序、状态词、CSV 六列、UTF-8/LF、确定性输出均不变。
4. 默认统计样本上限仍为 2^20，Chunk types 1024，length/distance buckets 65536；默认工作内存最多 64 MiB。
5. occurrence 的 4096-token / 8 MiB 输入约束必须覆盖查询前置扫描；不能把耗时工作放在预算计数开始之前。
6. 进度最多 10 Hz；关闭文档、Cancel、generation 变化后不得发布陈旧结果；不用 UI 线程做读取或解码。
7. 所有输入派生加法、乘法、容器容量和偏移映射做 checked arithmetic。
8. 不修改 Compare、APNG、发布资产、签名或安装器；不创建 tag、推送、合并或发布。
9. 不以删除测试、降低门槛或只验证声明常量的方式获得 PASS。

**本轮所需的窄范围补充：** 原 B–H 没列入 `libs/deflate-index/**`。本修复计划将其限定纳入预算/取消接口、验证边界前缀、受限存储及相应测试；执行 Task 1 时先把该补充写入 re-entry 包。保留旧入口及正常输入行为，不改变解码算法、模块归属或依赖方向。超出这一补充才需要另行裁决，不得默默扩大范围。

## 3. 冻结的修复行为

### 3.1 Overview：schema v1 采用成对 totals

`has_compression_totals` 是两个数值的共同可用性标志。本轮不添加 per-field availability，不升级 schema。

| 条件 | Overview 输出 |
|---|---|
| compressed/inflated 均为全文件验证值 | has_compression_totals=true，ready/complete/whole_document，输出两个数值 |
| 任一数值未知、仅有未完成扫描前缀 | has_compression_totals=false；JSON 两值 null；CSV 两值空；GUI 两值不可用；不得 complete=true |
| totals 已被独立完整验证，但 Token 后续超预算 | Overview 可以保持 ready；Token/Length/Distance 如实保留各自状态 |
| 文件损坏、预算耗尽或取消导致 totals 未知 | 分别报告 invalid_input、budget_exceeded 或 cancelled，complete=false，scope=none，保留稳定原因 |

不从 IHDR 推算值假装实际解压结果。已知压缩大小仍能从 Chunks 证据读取；本轮接受 Overview 成对隐藏，避免引入 schema 变更。若多个失败同时存在，Overview 使用阻止 totals 建立的阶段的实际停止原因，不能用后续状态覆盖已经验证的完整 totals。

### 3.2 预算：限制真实工作，而非声明

- 在读取/分配/追加 Block 之前检查相应限制；不能先完成完整索引再检查数量。
- Block、输入字节和输出字节预算互相独立；零输出 Block 也消耗 Block/输入预算。
- 正常 EOF 与最后一个允许的 Block 恰好结束时应允许 complete；只有确有未扫描部分时才报预算耗尽。
- 取消检查至少在输入 refill、inflate 循环和 Block 边界；不能只检查扫描前后。
- 受限停止保留已经验证的 Block 前缀和停止坐标；没有验证数据则 scope=none，不能伪造 verified_prefix。
- 区分 cancelled、budget_exceeded、invalid_input，禁止通过匹配错误字符串做状态分派。
- 记录容器 capacity 的实际存储，不只记录 size；预分配与扩容瞬时峰值也计入上限。优先增量、受限分配，不因 max_samples 很大而预分配整个上限。
- 64 MiB 是声明的工作内存上限，不是整个 GUI/CLI 的 RSS 上限。已存在、借用的 source/StageSet 与作业新增工作内存分开说明；请求副本、Block 索引、虚拟流分段、accumulator、同时存活的快照/进度副本及解码工作区均需审计。
- 不能用“借用 StageSet”掩盖 CLI 在调用 collector 前新建整份 StageSet 的分配。记录此路径；若它会突破本轮声明的工作范围，优先避免新增全图重建，或明确拒绝超预算的预处理。不要声称未覆盖的分配已满足硬上限。

### 3.3 Stored EOB：零宽锚点仍需真实文件映射

- 保留 logical.length=0、physical.length=0、Stage::kTrace。
- IDAT 逻辑边界位于某个字节前时，映射到该字节的文件位置。
- 恰在两个 IDAT payload 交界时，选择下一个非空 payload 的首字节，跳过 CRC、chunk header 和空 IDAT。
- 若边界恰好为逻辑流末尾，用最后一个非空 payload 的末尾作为零宽锚点；不能映射到任意 CRC 内容字节或凭空使用逻辑偏移。
- 不存在可验证锚点或越界时返回稳定 error，不返回错误的 ready。

## 4. 文件职责地图

| 文件 | 本轮职责 |
|---|---|
| `libs/analysis-engine/src/statistics_collector.cpp` | totals 状态、有界 Block 收集、前缀/取消传播 |
| `libs/statistics/src/serialization.cpp` | 用 totals availability 控制 JSON null / CSV 空值 |
| `libs/analysis-engine/src/statistics_view.cpp` | 验证/必要时修正不可用显示 |
| `libs/deflate-index/include/pnga/deflate-index/block_index.h`、`src/block_index.cpp` | 兼容的有界索引 API、停止原因、限制与取消 |
| `libs/analysis-engine/include/pnga/analysis-engine/statistics_collector.h` | 真实预算语义及必要的度量契约 |
| `libs/analysis-engine/src/statistics_occurrence_query.cpp` | 前缀可用性、有界查询、零宽逻辑到物理映射 |
| `apps/png-analyzer-gui/src/statistics_controller.cpp` | occurrence 前置预算与取消、generation 保持 |
| `apps/pnga-cli/src/statistics_command.cpp` | 只在预处理预算必须落实时调整组合逻辑，不拥有 serializer |
| `tests/unit/statistics/serialization_test.cpp`、`golden/*` | 空值/真实零值与确定性 |
| `tests/unit/analysis-engine/statistics_collector_test.cpp`、`statistics_occurrence_query_test.cpp`、`statistics_view_test.cpp` | 三项直接回归 |
| `tests/unit/deflate-index/block_index_test.cpp` | 有界索引的兼容与边界测试 |
| `tests/integration/cli/cli_test.cpp` | 真实 CLI 报告与退出码 |
| `tests/gui/statistics_controller_test.cpp`、`statistics_product_gate_test.cpp` | 查询取消、导出一致性和产品行为 |
| `tests/performance/performance_runner.cpp`、`thresholds-v1.json`、`README.md` | 真实度量及压力场景 |
| `docs/development/wp-602b-statistics-ui-export-reentry.md` | 范围补充、修复契约、重新关闭证据 |
| `docs/architecture/png-analyzer-current-development-plan-2026-08-22.md` | 关闭状态与后续任务同步 |

必要时修改上述测试所属现有 CMakeLists，不能加入无关 probe。不必触碰表中所有文件，只修改实际需要的文件。

## Task 1：复现并修复 Overview 伪完整值

**Interfaces:** 保持 `collect_document_statistics(request, cancellation, callback)`、`StatisticsSnapshot`、`StatisticsAccumulator::set_compression_totals` 和 serializer 签名不变。

- [ ] 在隔离 worktree 或干净受控 checkout 中确认 HEAD、diff 和未提交文件；将本计划及必要的范围补充放入真实仓库文档。不把历史 PASS 记录删除，追加“质量复审未通过，重新验收中”。
- [ ] 在 `statistics_collector_test.cpp` 使用现有 `make_fixture` / `make_request` 写入损坏样本回归；使用原 corpus generator 的截断 Token case，不能依赖 `/private/tmp` 的历史文件。
- [ ] 编写有效多空 Block + 小 max_samples 的回归。生成方式见附录 A；unit test 可用几十个 Block，压力 Gate 才用 1,500,000。
- [ ] 加入以下判别断言，先运行确认至少一项因旧行为失败：

```cpp
const auto result = collect_document_statistics(request, nullptr, {});
REQUIRE_FALSE(result.snapshot.overview.state.complete);
REQUIRE_FALSE(result.snapshot.overview.data.has_compression_totals);
REQUIRE(result.snapshot.overview.state.status != SectionStatus::kReady);
```

- [ ] 在 serializer 测试中构建 status=partial 或 invalid_input、内部标量=0、has_compression_totals=false 的 snapshot。JSON 断言两字段 null；CSV 用现有 CSV 解析检查 value 为空。另加 has_compression_totals=true 的真实零值，要求仍输出数字 0。
- [ ] 实施最小修复：只有两个 totals 都有完整证据才调用 set_compression_totals；否则根据阻止 totals 完成的原因 finish Overview。JSON/CSV 的数值可用性同时检查 section 和 has_compression_totals：

```cpp
const bool totals_available =
    section_available && snapshot.overview.data.has_compression_totals;
// numeric(compressed_bytes, totals_available)
// numeric(inflated_bytes, totals_available)
```

`section_available` 使用现有 begin_json_section/section_header 的返回值；本轮不改变其他 section 的可用性规则。

- [ ] 检查取消分支也不调用缺少证据的 set_compression_totals；完整 totals 不被后续 Token 失败抹掉。
- [ ] 用 `statistics_view_test.cpp` 检查两行的 raw value 为空，不把未知值格式化为 0；GUI/CLI 共享 snapshot 的 null/空值一致。malformed CLI 仍按既有映射退出 4，不能为通过测试改成 0。
- [ ] 构建并执行下方 focused 命令；检查所有 golden 差异，正常 ready golden 必须不变。仅修正与新空值契约直接相关的 golden，不批量接受输出。

```bash
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'statistics|analysis_engine|cli' --output-on-failure
git diff --check
```

- [ ] 检查目标名称是否全部匹配到测试，零测试不能算通过。单独提交本项，建议消息 `fix: preserve unknown statistics totals`。

## Task 2：给 Block 扫描及 occurrence 前置工作落实预算和取消

**Interfaces:** 旧 `index_blocks(const IByteSource&, uint64_t)` 保持可调用及兼容；新增 API 在 `pnga::deflate_index` 中定义，以下是本计划的新接口名称，不是假定现有接口：

```cpp
enum class BlockScanStop { kComplete, kBudgetExceeded, kCancelled, kInvalidInput };
struct BlockScanLimits {
  std::uint64_t max_input_bytes;
  std::uint64_t max_output_bytes;
  std::uint64_t max_blocks;
  std::uint64_t max_retained_bytes;
};
struct BoundedBlockIndexResult {
  BlockIndexResult index;
  BlockScanStop stop;
};
BoundedBlockIndexResult index_blocks_bounded(
    const pnga::io::IByteSource& source, const BlockScanLimits& limits,
    const std::function<bool()>& cancelled);
```

`index.success` 仅在完整成功时为 true。Partial prefix 通过 index.blocks 和 stop_input_bit/stop_output_byte 提供。新增签名需要 `<functional>`。内部可共享现有扫描实现，禁止复制第二套 Inflate 算法；旧入口保持原错误语义。

- [ ] 先写 bounded API 测试：空 Stored blocks 超 max_blocks、输入恰好/超预算、输出超预算、retained bytes 太小、取消于扫描中、正常完整文件。首次失败应来自新 API 缺失或边界断言，而非测试样本损坏。
- [ ] 压力/取消测试用读取计数包装的 IByteSource：在指定 read 次数后使取消 predicate 为 true，断言后续最多完成当前固定工作单元，不用 sleep 或不稳定的毫秒门槛。
- [ ] 加入以下语义断言（变量 types 来自上述新接口，fixture 使用附录 A）：

```cpp
REQUIRE(result.stop == BlockScanStop::kBudgetExceeded);
REQUIRE_FALSE(result.index.success);
REQUIRE(result.index.blocks.size() <= limits.max_blocks);
REQUIRE(result.index.stop_input_bit.has_value());
REQUIRE(result.index.stop_output_byte.has_value());
// 由计数 source 验证实际读取范围不超过 max_input_bytes。
// 由分配度量验证 retained capacity 与增长时峰值不超过分配给索引的预算。
```

- [ ] 在原扫描循环引入 checked 限制：refill 前裁剪读取长度；Block append 前检查数量及容器容量；每个 Inflate 工作单元检查取消；错误保留最后验证边界。不能把总输入很大直接当成 invalid_input。
- [ ] `max_retained_bytes` 必须约束真实容量/扩容，不只在 push 后判断 size。需要新容量时，先计算旧容量与新容量同时存在的峰值是否允许，再增长；不满足就停止并保留现有前缀。
- [ ] 更新 collector：在调用有界索引前计算剩余工作预算，扣除同时存活的固定/动态工作内存；不能把全部 64 MiB 都分配给 Block vector。保留 2^20 作为上限，允许因内存先达到而提前停止。
- [ ] 映射 typed stop 为 Statistics section 状态。已验证的 Block 前缀仍聚合并展示；取消或超预算不能被旧 `!blocks.success => invalid_input` 分支覆盖。完整扫描结果可以独立提供 totals；未完成扫描的 output_bytes 只能是前缀值。
- [ ] 更新 `StatisticsOccurrenceWorker::run`：取消状态在前置 scan 中可见；使用同一请求的 8 MiB 输入范围，索引容量受限；不能完整扫描文件后才开始 query 的 4096-token 计数。
- [ ] 审查 `statistics_occurrence_query.cpp` 对 `blocks.success` 的两处依赖。允许消费经过验证的受限前缀，但不能把前缀内没找到解释为全文件 not_found；超范围返回 Partial，previous 查询没有到达 cursor 时不能返回“最近的前一个”。
- [ ] 前置阶段和 token replay 如需重读同一窗口，分别记录总读取工作与允许搜索范围，契约中明确两者含义；不得扫描 8 MiB 范围之外，也不得借重启扫描无限重试。
- [ ] 检查 GUI Filter/Chunk 直接导航是否确实需要 Block 索引；不需要的域不要启动这一前置扫描。不要因此改动现有选择状态体系。
- [ ] 审计 collector 的 request、快照、accumulator、VirtualIDATStream、index、解码工作区以及 CLI StageSet 预处理的内存生命周期。通过受控 allocator/分配计数或可审查的容量计数测真实峰值；记录测量覆盖范围。若实际峰值无法计量，必须明确未达成，不得用 request.max_working_bytes 替代证据。
- [ ] 更新性能场景：保留 Token records <= 1，同时新增多空 Block 输入、实际工作内存、读取范围、取消工作量度量。进程 RSS 作为辅助证据单列，不以 RSS <64 MiB 作为整个 GUI 的硬门槛。
- [ ] 执行 focused、现有 index 兼容/差分测试与性能 Gate：

```bash
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'deflate_index|analysis_engine|statistics|cli|differential' --output-on-failure
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
git diff --check
```

- [ ] 单独提交，建议消息 `fix: bound statistics block scans and cancellation`。提交应包含新接口说明及 Gate 度量说明，不只包含代码。

## Task 3：修正 Stored EOB 的零宽文件锚点

**Interfaces:** 不改 `Selection`、`StreamSpan`、`BitSpan` 公共结构。使用现有 `VirtualIDATStream::logical_to_physical`、`segment_count`、`segment`；本轮不需要修改 png-format 模块。

- [ ] 在 `statistics_occurrence_query_test.cpp` 修改已有零宽 EOB 测试的错误 oracle：logical.start 仍为 13，physical.offset 应为该 fixture 的 IDAT payload 文件起点加 13。独立根据 fixture chunk 布局计算，不能用待测映射函数生成全部 expected 值。
- [ ] 新增四类样本：单 IDAT；EOB 与下一 payload 起点重合；中间有空 IDAT；连续零输出 Block。断言真实 payload 位置，不能只断言 physical != logical。
- [ ] 先运行旧代码确认物理偏移断言失败；保留逻辑偏移、零长度和 generation 的原正确断言。
- [ ] 在零宽分支按以下算法实现；以下为明确算法步骤，不新增公共 API：

```text
p = checked(wrapper_bits + best.deflate_begin) / 8
if p > stream.size(): error
if p < stream.size():
    ranges = logical_to_physical(p, 1)
    require exactly one mapped byte
    physical = ranges[0].offset
else:
    find last non-empty segment
    physical = checked(segment.physical_offset + segment.length)
    if no non-empty segment: error
selection.logical = StreamSpan{p, 0}
selection.physical_spans = [BitSpan{physical, 0, 0, false}]
```

映射 1 字节只用来查锚点，最终不得高亮消费 1 字节。该分支保持字节对齐语义；若遇到非字节对齐的假设冲突，报告错误，不截断伪装精确。

- [ ] 加入 range 超界、无有效 segment 的测试，以及既有非零长度跨 IDAT Token 回归；确保不改变普通 token mapping。
- [ ] 在 GUI controller 测试检查发布到 SelectionBus 的 physical span 正确；若原生环境可用，手动点击 Stored EOB 核对 Hex/Source。原生检查不能由 offscreen 结果冒充。
- [ ] 运行并独立提交：

```bash
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'analysis_engine|statistics|selection' --output-on-failure
git diff --check
```

建议消息：`fix: map stored EOB anchors to physical IDAT offsets`。

## Task 4：联合验收并重新关闭 H

**Files:** 三项修复对应测试、产品/性能 Gate、re-entry completion record、当前路线图。只做回归补全与证据收口，不追加功能。

- [ ] 在当前最终源码上重新构建，不运行工作区中遗留的旧二进制来声称通过。
- [ ] 产品 Gate 增加 malformed/预算未知 totals 的 GUI/CLI JSON/CSV 字节一致性；保留收集中导出、模态保存、写失败不覆盖、320 px、键盘/无障碍现有覆盖。
- [ ] 联合运行多空 Block 样本、Cancel、快速切换、连续 occurrence 请求；既检查旧 generation 不发布，也检查旧工作确实停止。
- [ ] 记录正常 ready 文件与修复前字节一致、partial 变化符合空值契约；三个 locale 下输出仍确定。
- [ ] 完整执行原包矩阵：

```bash
python3 scripts/verify_repository_layout.py
python3 scripts/verify_dependencies.py
cmake --preset dev
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=offscreen ctest --preset dev -R 'statistics|cli|main_window|selection|gui' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --preset dev --output-on-failure
python3 scripts/run_gui_gate.py
python3 scripts/run_performance_corpus.py --preset dev --enforce-thresholds
python3 scripts/run_sanitizer_fuzz_gate.py
git diff --check
```

- [ ] 验证 CTest 实际运行数，不能硬编码旧 60 项作为当前期望。若脚本需要 ASan preset/环境，依照仓库脚本说明准备；缺少工具链记录 BLOCKED，不能当 PASS。
- [ ] 复核允许路径、公开接口说明、算术溢出、容量峰值、借用对象生命周期、取消与确定性。
- [ ] 在 re-entry 包追加证据：最终代码 commit、平台/工具链、每条命令/退出码/测试数、回归样本生成方法、实际工作内存覆盖范围及峰值、RSS 辅助值、原生与 offscreen 检查边界。
- [ ] 仅所有要求通过才重新标记 PASS。若有复现缺陷为 FAIL；必要环境或边界裁决缺失为 BLOCKED。不要因为 focused tests 全绿就关闭 H。
- [ ] 独立提交 Gate 与文档，建议消息 `test: revalidate statistics quality gate`。报告最终代码提交与证据文档提交，不宣称提交哈希本身包含生成它之后的内容。

## 附录 A：可重现的有效多空 Block 样本

以下 Python 只用于生成本地压力输入，不引入外部资产，也不修改生产代码。在执行工作目录的 `build/quality-audit` 下保存；unit test 将同一字节结构迁移到现有 C++ fixture helpers，小样本和压力样本共用明确结构。

```python
from pathlib import Path
import struct
import zlib

def chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data
            + struct.pack('>I', zlib.crc32(kind + data)))

root = Path('build/quality-audit')
root.mkdir(parents=True, exist_ok=True)
raw = bytes([0, 127])  # 1x1 grayscale8: filter byte + one sample
for count in (0, 32, 1_500_000):
    stream = (bytes.fromhex('7801')
              + bytes.fromhex('000000ffff') * count
              + bytes.fromhex('010200fdff') + raw
              + struct.pack('>I', zlib.adler32(raw)))
    assert zlib.decompress(stream) == raw
    png = (bytes.fromhex('89504e470d0a1a0a')
           + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 0, 0, 0, 0))
           + chunk(b'IDAT', stream) + chunk(b'IEND', b''))
    (root / f'empty-blocks-{count}.png').write_bytes(png)
```

```bash
build/dev/apps/pnga-cli/pnga statistics build/quality-audit/empty-blocks-1500000.png --format json
build/dev/apps/pnga-cli/pnga statistics build/dev/tests/corpus/wp-607c/malformed/error-truncated-token.png --format json
```

预期：压力输入安全停止或在预算内完成；若停止，输出如实标记受限部分，未知 totals 不得是完整 0。截断 Token 样本保持既有 CLI 退出码 4。正常 0 空 block 样本应完整输出 inflated_bytes=2、退出 0。

## 执行完成时交付

1. 一个总状态：PASS / FAIL / BLOCKED。
2. 三个问题各自的旧行为、新行为和回归证据。
3. 修改文件、独立提交、测试与实际内存/取消结果。
4. 明确未覆盖的平台或人工 GUI 项，不冒充已验收。
5. 全部通过后推荐下一项为既有 WP-605G 发布资产验证；本修复任务不执行发布。

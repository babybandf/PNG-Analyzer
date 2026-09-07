# WP-602B–H 状态、质量审查与下一步

审查日期：2026-09-07。结论：**FAIL**（交付已合入，但质量关闭条件不成立）。

## 基线与完成状态

实际仓库：`/Users/lijiangbo/project/PNG-Analyzer`，main `6d4226e`。
WP-602B–H 通过 `4898d11` 合入，原 Gate 提交为 `95510bb`。
re-entry 工作包于 2026-09-06 标记 PASS，并记录 60/60 CTest、GUI、性能和 sanitizer Gate。
此后有导出提示、收集中导出、表格宽度和模态保存对话框修复。

B–G 的结果模型、流式 Token 聚合、共享 serializer、CLI、查询和 GUI 均已有实现。
本次发现违反冻结 R2/R3/R6/R13 的问题，因此 H 需要重新打开；不是要求重做整条功能链。

仓库已有未提交的 GUI 测试修改、临时 export probe 和另一份外部审查记录；本次没有修改它们或生产代码。

## 阻止关闭的发现

### P1：Overview 将未知解压大小标成完整的 0

- 位置：`libs/analysis-engine/src/statistics_collector.cpp:568`。
- 根因：条件为 `has_compressed || has_inflated`，随后同时写入两个 totals 并标记 ready/complete/whole_document；未取得的 inflated 值保留初始 0。
- 复现：当前 `pnga statistics build/dev/tests/corpus/wp-607c/malformed/error-truncated-token.png --format json` 返回 4，Filters/Blocks 为 invalid_input，但 Overview 为 ready、complete=true、whole_document，inflated_bytes=0。
- 另一个有效样本含 1,500,001 个 Stored block、实际解压为 2 字节；因 Block 样本超预算，同样导出“完整的 0”。样本已用 Python zlib 成功解压验证。
- 影响：报告误导用户及消费 JSON/CSV 的程序；共享 collector 使 GUI 同样受影响。CLI 非零退出码不能修复 section 内错误语义。

### P1：64 MiB 只有声明校验，Block 前置扫描未受实际预算和取消约束

- 位置：`libs/analysis-engine/src/statistics_collector.cpp:165`、`:357`、`:378`；`libs/deflate-index/src/block_index.cpp:258`。
- 根因：先 `index_blocks` 并保留所有 Block，再检查 max_samples；index 接口没有取消参数，输出字节预算无法约束大量零输出 Block。
- 实测：7,500,070 字节的有效 PNG（1,500,000 个空 Stored block 加一个含 2 字节输出的最终 block），CLI 退出 4，子进程峰值 RSS 为 182,403,072 字节，约 174 MiB。
- 注意：RSS 是整个 CLI 进程峰值，并不等于 collector 独占工作内存；但代码的无界 Block 保留和事后检查本身已证明预算没有覆盖此路径。
- `apps/png-analyzer-gui/src/statistics_controller.cpp:109` 的 occurrence worker 也在进入受限查询前执行完整索引，需要一并纳入修复验收。
- Gate 漏洞：`tests/performance/performance_runner.cpp:511` 校验声明的 max_working_bytes，而不是实际分配峰值；Token records <= 1 不能证明整个作业内存有界。

### P2：Stored EOB 的物理文件偏移使用了 IDAT 逻辑偏移

- 位置：`libs/analysis-engine/src/statistics_occurrence_query.cpp:616`。
- 根因：零宽 EOB 分支直接把 logical_begin/8 放入 physical_spans，没有经过 VirtualIDATStream 的文件映射。
- 现有 `tests/unit/analysis-engine/statistics_occurrence_query_test.cpp:917` 测试同时期望 logical.start 和 physical.offset 为 13；13 是逻辑位置，不是 PNG 中的 IDAT 文件位置。
- 影响：SelectionBus 的 Hex/Source 导航定位到错误文件区域。此结论来自当前源码和测试审查，本次未另做原生 GUI 点击复现。

## 本次重新验证

对下面 6 个目标执行 Ninja dry-run，结果 `no work to do`，确认其现有二进制无需因当前源码更新而重建。随后在临时目录执行，GUI 使用 `QT_QPA_PLATFORM=offscreen`，全部退出 0：

| 目标 | 结果 |
|---|---|
| pnga_statistics_tests | 18 cases / 240 assertions |
| pnga_analysis_engine_tests | 133 cases / 5,992 assertions |
| pnga_deflate_trace_tests | 26 cases / 43,795 assertions |
| pnga_cli_tests | 13 cases / 52 assertions |
| pnga_gui_statistics_inspector_tests | 12 passed |
| pnga_gui_statistics_controller_tests | 13 passed |

`pnga` 也在同一次 dry-run 中确认无需重建。`git diff --check` 退出 0。

全量 build dry-run 仍有 60 步待执行，包含更新后的 statistics_controller 与产品 Gate。
因此本次没有把旧产品 Gate 二进制当成当前版本证据，也没有声称重新通过全部 60 项、原生 GUI、跨平台或 sanitizer Gate。
复现样本、JSON 和测试日志当前保存在 `/private/tmp/pnga-602-current-audit/`。

## 下一步决定

下一项应为 **WP-602 质量修复与 H 重新验收**（建议任务名称，不冒用已批准的新编号），按以下顺序执行：

1. **状态正确性**：先加入上述 malformed 和有效超预算样本回归，禁止未知值伪装为已验证零值。明确部分 totals 的可用性表达；若 schema v1 现有模型无法表达，应先在所属接口文档/工作包中冻结兼容方案。JSON、CSV、Overview 显示和退出码联合验收。
2. **预算与取消**：Block 收集和 occurrence 前置阶段也必须在分配/扫描前受限，在达到预算时保留可验证前缀并及时取消。优先复用已有流式能力。若确需修改 `libs/deflate-index/**`，先更新工作包路径范围，现有 B–H 包未授权该路径；不改变解码语义或架构依赖方向。
3. **物理导航**：对零宽 EOB 做真实文件映射，修正错误测试 oracle；覆盖单 IDAT、跨 IDAT 边界、连续空 block，分别断言逻辑与物理位置。
4. **重新关闭 H**：重新构建当前版本，执行工作包完整命令矩阵；补充实际工作内存度量、空 block 压力、扫描中取消、未知值导出、EOB 导航回归，并将当前导出交互修复纳入 GUI 验收。

放行条件：以上问题全部修复、回归通过、Gate 证据绑定最终提交，才恢复 PASS。
之后返回现有路线图的 **WP-605G Release assets 验证**，随后处理具备 Qt kit 的真实 GUI 发布环境；正式 tag/发布需在实际发布任务中处理。本次不创建 tag、不发布、不启动新功能开发。

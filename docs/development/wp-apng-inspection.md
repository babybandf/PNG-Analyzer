# WP-APNG-INSPECT — 完整逐帧检查任务包

日期：2026-09-09。状态：PASS（T00–T12 完成并验证；native GUI 交互验证由用户于 2026-09-10 原生确认——见 wp-apng-inspection-completion.md）。

## 执行入口

1. 阅读根 AGENTS.md、完整 REPOSITORY_LAYOUT、ADR-0004/0006/0007。
2. 阅读 [接口契约](wp-apng-inspection-contract.md)，它是本包实施接口定义。
3. 顺序执行 [详细计划](../superpowers/plans/2026-09-09-apng-per-frame-inspection.md)，从 T00 建立真实基线开始，不跳到 GUI 改动。
4. [设计与风险审查](../superpowers/specs/2026-09-09-apng-per-frame-analysis-design.md)作为背景；执行中的确定选择以本契约与详细计划为准。若与 Accepted ADR 冲突则停止，不自动改ADR。

研究起点 commit：`2df248bb3bfb472980bad89e41dcb389bbfe52df`。已存在无关未跟踪 `docs/development/wp-5u12-stages-a-e-execution-audit.external-FAIL.md`，不得覆盖、删除或包含在提交中。开始时重新记录实际 HEAD、dirty diff 与工具链，不把研究起点当作冻结用户工作树的要求。

## 目标和完成定义

每帧 Reconstruction/Compression/Statistics/Hex/像素交互贯通，画布来源可追溯；全部静态行为和序列化保留。不能以隐藏功能、静态占位或更改测试预期来通过。生产完成只在T00–T12全通过后声明；单项预算partial是显式状态，不是静默降级。

不做Compare、编辑、动画导出、新backend、新依赖或整库重构。不改变静态配额和默认调度。不把本计划的编写授权解释为提交、合并或发布授权。

## Allowed paths

以下为外层白名单，每任务的Files进一步收窄：

- `libs/trace-model/include/pnga/trace-model/inspection_context.h`、`libs/trace-model/src/inspection_context.cpp`、对应CMakeLists.txt和README.md；现有selection/offset类型不改变持久格式。
- `libs/analysis-engine/include/pnga/analysis-engine/`、`libs/analysis-engine/src/` 中计划逐项列明的文件，模块CMakeLists.txt、README.md。
- `libs/statistics/include/pnga/statistics/frame_statistics.h`、`libs/statistics/src/frame_statistics.cpp`、模块CMakeLists.txt、README.md；现有serializer仅允许提取共用section编码且golden不变。
- `ui/qt/include/pnga/ui/qt/`、`ui/qt/src/` 中计划列明的 Inspector/Hex/selection 文件及模块CMakeLists.txt。
- `apps/png-analyzer-gui/src/` 中计划列明的controller/session/UI文件及该应用CMakeLists.txt。
- `tests/unit/trace-model/`、`tests/unit/analysis-engine/`、`tests/unit/statistics/`、`tests/gui/`中本计划的测试文件和各CMakeLists.txt。
- `tests/common/apng_inspection_fixture.h`、`scripts/run_apng_inspection_gate.py`、`tests/performance/apng_inspection_test.cpp`及其CMakeLists.txt。
- 本任务的spec、contract、plan、completion文档，以及 `docs/development/wp-699-706-apng-completion.md` 的“Deferred issue”段落（仅T12加已解决证据链接，不能重写历史通过记录）。

## Forbidden paths/actions

third_party、依赖清单、根架构/Accepted ADR、已有performance阈值、静态golden预期、非本包审计文件、外部027.png、用户未相关文件；禁止新增顶层目录、Qt进libs、libpng私有API、完整拼接帧流和在UI执行decode/read/join。

如果某共享helper必须修改但未在任务Files列明，先核对接口规则，将精确路径和必要性补入本任务文档；若超出外层白名单或改变模块职责则停止报告，不擅自扩大。

## 非APNG硬门槛

每包共享代码改动后运行G-static，T12再跑全套；静态RGBA/证据/CLI/Selection/Statistics v1字节一致，静态UI事件和默认行为不变；APNG→静态转换含旧worker迟到时不串状态。静态文档新增APNG worker/cache/widget计数必须为0（相对原基线无额外创建）。

性能用同环境5次基线与5次候选测量，比较每case中位数及P95；时间差须不超过max(基线5%,1ms)，峰值RSS差不超过max(基线2%,1MiB)，且冻结thresholds-v1仍全部通过。该容差仅用于测量噪声，不授权有意新增静态工作。超出即FAIL，不调宽阈值；全5次记录保存。

## 最终报告

`docs/development/wp-apng-inspection-completion.md`：PASS/FAIL/BLOCKED之一；每T任务commit/文件、red/green测试命令、测试数量、静态基线对比、GUI截图路径、性能、未执行项。未执行的必要门槛禁止报告PASS。报告不要收录用户绝对私有路径之外的敏感文件内容；样例只记录路径与hash。

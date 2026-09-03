# WP-607C 书面工作包审查（written-package review）

- 审查日期：2026-09-03
- 审查对象：
  - 开发包：`docs/development/wp-607c-controlled-static-ui-trace-corpus.md`（commit `7d30e3c`）
  - 实施计划：`docs/superpowers/plans/2026-09-03-wp-607c-controlled-static-ui-trace-corpus.md`（commit `67d36a9`）
- 审查基线：分支 `wp-5u12-compression-inspector` @ `9cec67c`
- 性质：关闭 `wp-607-cross-platform-quality-evidence.md` 所载 "pending written-package review" 门

## 一、结论

**APPROVED（批准实施）** —— 附 3 项实施起点裁决（R1–R3，见第四节）与 2 项备忘（R4–R5）。未发现架构越界、AGENTS.md 违规或计划-包不一致；所有可机械核验的代码假设已逐一命中。

## 二、已核验成立的关键前提（逐项对代码验证）

| # | 假设 | 核实结果 |
|---|---|---|
| 1 | 19-case 矩阵覆盖 F Task 6 Step 1 的全部受控类别（stored/fixed/dynamic/cross-idat/adler-mismatch/truncated/reserved/invalid-distance/overlap/narrow/large） | ✓ 逐类比对 §7 |
| 2 | 诊断串 `"truncated block header"` / `"truncated huffman code"` / `"reserved deflate block type"` / `"distance beyond available output"` 均为现行稳定诊断 | ✓ token_decoder.cpp:585,664,745；block_index.cpp:251,290 |
| 3 | 规则 id `chunk_crc_mismatch` / `idat_adler_mismatch` 存在 | ✓ integrity.cpp:61,108 |
| 4 | `MemoryByteSource`、`VirtualIDATStream::logical_to_physical`、`decode_stored_and_fixed`、`index_blocks` 可用 | ✓ byte_source.h:65；virtual_idat_stream.h:55；token_decoder.cpp:547 |
| 5 | reserved BTYPE 停止位 19 与 A 阶段先例一致 | ✓（A 阶段测试已断言 bit 19） |
| 6 | `tests/CMakeLists.txt` 存在 `add_subdirectory(differential)`，计划插入点可行 | ✓ :16 |
| 7 | 包-计划一致性：19 id、接口（`ControlledFixture`/`ExpectedFacts`/ErrorFacts 演进）、5 目标/CTest 项、门禁步骤、基线声明（`9cec67c` / `7d30e3c` 或其直接文档后继） | ✓ 逐节比对 |
| 8 | AGENTS.md：Qt 不进 libs（全部 test-only）；libpng 仅经 differential oracle（显式 oracle 测试属允许边界）；zlib 仅公开校验和 API；生成产物不入库（kind schema）；checked 算术与确定性要求齐备；产品缺陷走 `BLOCKED` 独立缺陷包（无隐式生产修改权）；既有测试仅迁移精确重复构造、不删除不弱化 | ✓ |

## 三、设计要点确认

- 双目录生成 + 字节级等值后才 blessing `expected_sha256`——诚实解决哈希自举问题。
- 显式 DEFLATE 位写入器（禁止 zlib 压缩启发式决定块布局/token）保证跨平台字节一致。
- `FIXTURES_SETUP/REQUIRED wp607c-generated-corpus` + `wp607c` 标签 + 单一操作入口 `run_wp607c_corpus_gate.py`，与 F 的 preflight 重验要求（F 必须首先重跑 corpus gate）闭环。
- `NOT_CONFIGURED` 不可满足 Qt GUI 单元、`--refresh-generated-hashes` 拒绝无双代证明、拒绝未知键/重复 id/路径穿越/源树输出——防呆完备。

## 四、实施起点裁决（Rulings，实施 agent 必须携带）

- **R1 corpus revision 文件清单**：Task 5 Step 4 的聚合哈希 `manifest_sha:controlled_fixture_sha:generator_sha` 未包含 `controlled_fixture.h`——事实类型变更可能改变输出而不更新 revision。裁决：revision 组件固定为 manifest + `controlled_fixture.h` + `controlled_fixture.cpp` + `generate_controlled_corpus.cpp` 四项，Python `--print-revision` 与 CMake 双侧一致，Task 1 即冻结该清单。错则 revision 失真，F 证据链可信度受损。
- **R2 fresh-build 命令**：Task 8 Step 5 的 `cmake --preset dev -B build/wp607c-fresh` 在 preset 定义 binaryDir 时会被 CMake 拒绝。裁决：改用显式 `-S/-B` 配置（复制 dev 预设的 toolchain/设置）或经 gitignored `CMakeUserPresets.json` 临时预设；不得改动受控 `CMakePresets.json`。
- **R3 sanitizer 调用**：Task 8 Step 4 / 包 §14 的 `run_sanitizer_fuzz_gate.py` 未带 `--preset asan`（master/F 先例为 `--preset asan --jobs 4`）。裁决：实施者确认脚本默认行为；若默认非 asan，必须显式携带 `--preset asan --jobs 4`，否则 asan 层被静默跳过。
- **R4 备忘（诊断常量权威性）**：六条诊断串/规则 id 已核实为现行值；test-side 常量是 corpus 权威。未来合法修改诊断文案时必须同步刻意更新 corpus 记录，不得静默。
- **R5 备忘（decoder_message 断言与"禁止解析诊断恢复数值事实"的关系）**：常量相等断言不属于"解析恢复数值事实"，可接受；无需修改计划文本。

## 五、备忘（非阻塞）

- Task 7 将 performance runner 迁到共享 `perf-large-rgba8`（1024x768 RGBA8 Stored）可能改变测量内容相对 WP-604A 生成器；若触及阈值，`thresholds-v1.json` 归 WP-5U12F Task 4 所有，门禁会暴露。
- 计划未断言新 CTest 总数（"report the actual discovered total"）——符合仓库现状，无风险。

## 六、生效

- 本审查记录入库存档后，WP-607C 允许在基线 `9cec67c`（或其直接文档后继）上按计划 Task 1–8 串行实施。
- WP-607 父文档状态更新仍归 WP-607C Task 8 Step 7（PASS 时仅标 WP-607C）。

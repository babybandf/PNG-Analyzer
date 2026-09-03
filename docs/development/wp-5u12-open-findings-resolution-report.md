# WP-5U12 行为类发现修复报告（三项 OPEN 发现闭环）

- 报告日期：2026-09-03
- 分支：`wp-5u12-compression-inspector`（worktree `.worktrees/wp-5u12-compression-inspector`）
- 范围：`e442293`（审计关闭）之前的修复循环：`d69d7b4` → `76bd0d8` → `92239c4` / `59ebffc` / `1dc8385` → `e442293`
- 需求溯源：`docs/development/wp-5u12-stages-a-e-execution-audit.md` §7.1–7.3（外部核对补充的复现序列，commit `76bd0d8`）
- 过程记录：`.superpowers/sdd/2026-09-01-wp-5u12-compression-inspector-master/`（ledger、`task-openfindings-report.md`、`review-76bd0d8..1dc8385.diff`）

## 一、结论摘要

外部核对质疑审计 PASS 范围（`e84f80d` 为纯文档提交）并登记三项未决行为类发现后，已按"失败回归先行 → 最小修复 → 独立审查 → 退出门禁重放"完成闭环。三项发现全部 **CLOSED**；审计 §7 已更新处置结果（`e442293`）。

WP-5U12 完成状态**不变：未完成**——Task 6（WP-5U12F Product Gate）仍待 WP-607C（manifest 为空、工作包未批准）。

## 二、逐项闭环明细

| 项 | 发现 | RED（失败回归证据） | 修复 commit | 审查 |
|---|---|---|---|---|
| 7.2 | 查询边界的最终 EOB 被半开区间过滤（`[3,3)` 与 `[0,3)` 不相交），违反 flow-ui.md:643-645 | `tests/unit/analysis-engine/trace_query_test.cpp` 扩展为 4-token 断言，RED 原因与审计记载一致 | `92239c4`：闭包规则 `query_begin <= pos <= query_end`，仅零宽 EOB 生效；窗口外 EOB 仍排除；非 EOB 过滤不变 | Approved，无过度包含；兄弟测试被强化（3→4 token + `back()` 断言） |
| 7.3 | Match 行 Current byte 缺 source logical offset（模型无字段、UI 无文案） | 模型编译失败（缺字段）+ GUI 第二子串失败 | `59ebffc`：`DecodeTraceStep` 新增当前字节源偏移（`current - distance`，checked 减法，Match 且 contains-current 门控）；UI 追加 `source logical offset %1` | Approved，审计 fixture 逐字落地（target `[100,118)`、distance 7、current 104 → `+4` / `97`） |
| 7.1 | Blocks `Open Decode Trace` 与 Huffman occurrence 打开不切到 Decode Trace 页 | GUI 集成断言失败（点击后 `currentIndex()` 停留原页） | `1dc8385`：Blocks 路径在既有 `decodeTraceRequested` 连接内切页（interval-dedup 分支之前，fresh-submit 与 republish 均生效）；Huffman 路径经 store `navigationRequested` 按 `origin == kHuffman` 过滤切页 | Approved，两路径均生效且零额外 trace 提交 |

修复顺序：7.2 → 7.3 → 7.1（模型层先行，UI/编排层后行）；每项 RED 输出与审计记载的失败原因逐一对应（详见 `task-openfindings-report.md`）。

## 三、门禁重放证据（2026-09-03）

```
python3 scripts/verify_repository_layout.py → 0 failure(s), 0 warning(s)
python3 scripts/verify_dependencies.py      → 0 failure(s), 0 warning(s)
cmake --build --preset dev --parallel 4     → exit 0
QT_QPA_PLATFORM=offscreen ctest --preset dev → 100% tests passed out of 47
git diff --check                             → 无输出
git status --short                           → 空
```

逐 commit 文件归属核验（`git show --name-status`）：三个 fix commit 文件集互斥、全部落在审计发现点名的文件及最小接线文件内。

## 四、裁决与缓办项

- **裁决**：`goBack()`/`goForward()` 导航至 Huffman-origin 目标同样切页——裁定为 drill-down 意图的一致扩展（Back/Forward 恢复完整导航上下文）；错则轻微 UX 不一致，可在 F 阶段调整。
- **新缓办 Minor**：① 边界 EOB 行 `block_index = -1`（归因循环对空区间不命中；后续可按闭端包含归因）；② 序列化器不输出新字段 `selected_byte_source_offset`（保持 `decode-trace-v2` 输出与既有 golden 稳定，视为正确取舍）。

## 五、当前状态

- 分支 HEAD：`e442293`，工作树干净，CTest 47/47。
- 三项行为发现：**CLOSED**（含实现与回归测试证据）。
- **外部审计确认（2026-09-03）：通过**——外部核对方对三项修复完成情况审计通过，本闭环正式结束。
- WP-5U12：**未完成**——Task 6（WP-5U12F Product Gate）仍待 WP-607C（`tests/corpus/manifest.yaml` 为空；工作包状态 "design approved; pending written-package review"）。
- 恢复路径：完成 WP-607C → 批准 → 实施 → 在本 worktree 重验并执行 F 的完整 product gate。

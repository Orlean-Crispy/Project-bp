# 外部审查记录（2026-11-01）

> 输入：`Project-bp_项目审查与修改建议.md`（外部 AI 逐文件审查，快照 `7194b1e`，187 个文件）。
> 本文件是**我们的裁决**：哪些接受、哪些已核实、哪些要改口径。审查原文不在仓库里，结论在这里。

## 1. 总评

**审查质量高，且指出了真问题。** 它没有停在“建议加注释/加文档”，而是用探针复现了三条**生产路径**上的缺陷，
而这正是自测最容易漏的地方：我们的测试大量直接调用 `update_weapon` / `hitscan`，
**绕过了 `World::step` 的真实顺序**。

## 2. 已核实并已修（S81）

| # | 审查结论 | 我的核实 | 修法 | 判据 |
|---|---|---|---|---|
| 1 | 真实生产路径无法开始换弹 | ✅ 属实。`world.cpp` 在武器步之前把 `reloading` 覆写成本 tick 输入，`shoot.cpp` 又要求 `!reloading` 才算“刚按下” | `reloading` 拆成 **`reload_held`（按住，进快照）** 与 **`reload_edge`（按下沿，派生、不进哈希/快照）**；武器步用 edge | `--test-damage`：`a reload press starts on the real step order` / `holding reload does not restart the timer` / `reload refills the magazine from the reserve` |
| 2 | NaN/Inf 可造成错误命中并扣血 | ✅ 属实。`dir_length < 1e-6` 对 NaN 恒为 false | 三层：`update_weapon` 校验 `input.yaw/pitch` 与 `shooter.yaw`；`hitscan_penetrating` 校验 origin/direction/max_distance/scale；再把 `<` 改成 `!(x > eps)` | `a NaN pitch never fires or damages` / `a NaN yaw …` / `a NaN direction hits nothing` / `a NaN origin …` |
| 3 | 空 hitbox 历史 = 原点的活体幽灵 | ✅ 属实。`HitboxSample{}` 默认 `alive=true, height=1.42` | 默认改成 **`alive=false`**（失败即安全）；`record()` 改成 `active && hp>0`；`sample()` 的合并从 `&&` 改成 `||`（与注释一致：不吞子弹）；`update_weapon` 再与**当前权威** active/alive 求交 | `an empty target list hits no player`；`--test-damage` 全绿 |

## 3. 接受但延后（进 DEBT，不在本轮混改）

| 审查项 | 我们的判断 | 去向 |
|---|---|---|
| 收拢唯一权威 `GameStep` | **同意，且这是本轮之后的第一优先级**。现在的 `World::step → update_weapon` 由调用方各自拼接，正是 #1 的根因 | AUD-36 |
| 事件合同（回滚重复交付、远端权威事件、overflow 指标） | 同意，且必须在 M7 音频之前定，否则音频会建在沙上 | AUD-37 |
| 连接状态机（join 终态、UDP 黑洞、Retry/Leave、代际） | 同意 | AUD-38 |
| hash/snapshot/demo 的“权威字段清单”驱动 + 永久 mutant | 同意 | AUD-39 |
| 网络：SIGPIPE、零长分片、无票据重 ID、5 s 清理 vs 120 s 票据、旧 FULL_STATE 回退、hub 不清理、ACK 回绕、真实可靠流、RTT | 大部分同意（部分已在 DEBT） | AUD-40 ~ AUD-43（合并同类） |
| 配置/地图 finite 与跨字段不变量 | 同意 | AUD-44 |
| smoke 允许必需项 SKIP 后仍 ALL GREEN；Godot 子进程退出码未判断 | **同意，这条最伤“验收可信度”** | AUD-45 |
| demo 缺战斗阶段、边界 EOF 当正常完成 | 同意（依赖 GameStep） | AUD-39 内 |

## 4. 不接受 / 要改口径

| 审查说法 | 我们的裁决 |
|---|---|
| “`--test-net` 在 Linux 上 FAIL（第二 UDP socket 能绑同端口）” | **平台差异，不是当前缺陷**：Windows 上 `SO_EXCLUSIVEADDRUSE` 已独占（DEBT-11 的教训）。但审查提出“POSIX 分支没有等价保护”成立，记进 AUD-40 的跨平台项。 |
| “建议现在上 sanitizer/fuzz/CI” | 方向同意，**范围要收**：先做 `--test-net` 的 fuzz 入口与 ASan 构建脚本，不等于引入完整 CI 流水线。 |
| “release 导出” | 与 `DESIGN-08` 同一条，仍待所有者决定 editor-only 还是 release；**不在正确性硬化里做**。 |

## 5. 结论

审查的**判断**（“单元测试通过的路径与生产路径不一致”）被本轮三条实锤证明，代价是：
我们之前写的 `--test-damage` 的“换弹 30 发填满”一直是**直接调函数**跑出来的绿。
本轮把它改成走 `World::step → update_weapon`，并新增 7 条回归。

**流程教训**（已进 AGENTS 教训 18）：测试必须至少有一条走**和服务器相同的调用顺序**；
只测函数不测顺序 = 假绿。
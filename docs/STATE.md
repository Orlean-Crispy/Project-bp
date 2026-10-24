# STATE — Project Bp（唯一"现在"）

> **规则**：本文件**覆盖式更新**（不是追加），≤120 行。
> 每个数字后面都要有能复现它的命令；写不出命令的东西不写这里。
> 历史过程 → `docs/PROGRESS.md`；待办 → `docs/DEBT.md`；预算 → `docs/budgets.md`；
> 设计待定与功能缺口 → `docs/BACKLOG.md`；**这一段工作的叙事交接 → `docs/HANDOFF.md`**（不重复本文件的数字）。

**更新**：2026-10-24 · S76 · 维护者：谢政霖

---

## 0. 当前

| 项 | 值 |
|---|---|
| 里程碑 | **M6 代码完成、待手测与签字**：E1–E8 有证据，E9 差人手一局；M7 契约待签 |
| 契约 | `docs/contracts/M6.md`（**active**） |
| 本阶段 | E1–E8 达标；E9 的自动化半边（10 分钟双客户端 soak）已建，人手那半待测 |
| 协议版本 | **v9**（Weapon 组带后坐力；新增 Hit 组：命中目标/伤害/部位/序号） |
| 宿主 | Godot **4.5**（计划书目标 4.7，DEBT-15，M7 前必须有结论） |
| 分支 | `main` · 工作区 `G:\自制游戏` · 工具链 `C:\bp-toolchain` |

## 1. WIP（只能 1 条）

| ID | 一句话 | 今日判据（命令 + 期望输出） |
|---|---|---|
| — | *收工已清空；下一条从 §2 取* | — |

> 开工动作：从 §2 取第一条 → 在 DEBT/BACKLOG 里标成 `wip` → 把 verify 抄到这里 → 开始。

## 2. Top 5 队列（按"阻塞 M6 功能"排序）

| # | ID | 一句话 |
|---|---|---|
| 0 | E9 | 两人局域网真打 10 分钟（清单在验收记录 §3；自动化半边 `tools/net_soak.ps1`） |
| 1 | — | *（M6 关闭后按 M7 契约重排）* |
| 2 | — | *（同上）* |
| 3 | — | *（M6 关闭后按 M7 契约重排）* |
| 4 | — | *（同上）* |
| 5 | — | *（同上）* |

## 3. 闸门基线（**写数字，不写"全绿"**）

| 闸门 | 基线 | 采集命令 |
|---|---|---|
| smoke | 22 项，全过 | `tools\smoke.ps1` |
| 单 tick p99 / max | 2.40 / 6.00 µs（10 玩家） | `headless_sim.exe --bench 1000 --players 10` |
| 分配 | 0 allocs/tick | 同上，输出 `allocs 0.00 / tick` |
| 30% 丢包回滚 | 51 / 2000 tick | `--test-net` |
| 5% 丢包 60 s | 修正 0 次 | `--test-net` |
| 加入 / 重连偏差 | 0.00000 m | `--test-net` |
| 源文件 lint | `sources ok: 34` | `tools\check_sources.ps1` |
| 武器数据 | 2 把枪、0 警告、5 类坏值被拒、加枪只加一节 | `--test-weapons` |
| 开火链路 | 92/92 按住、52/52 松开、144/144 预测一致 | `--test-net` 的 `[fireflags]` |
| 命中判定 | 14 项全过；`[hitscan] chest 1 face 1 wide 1 edge 1 crouch 1 wall 1 range 1` | `--test-hitscan` |
| 回溯 | `[rewind] window 6 at-6 1 current-miss 1 interp 1 clamp 1` | `--test-hitscan` |
| 穿透 | `[penetrate] walls 2 one 1 two 2 scale 0.49 pistol 1 concrete 1 open 1` | `--test-hitscan` |
| 射击链路 | `[damage] body 4 rounds armour 9 head 105 wall hits 0 reload 30`；含死亡/重生/尸体/后坐力/命中记录 | `--test-damage` |
| 后坐力 | `recoil: shots 12 first z 1.278 fifth z 1.848`；沉默 24 tick 复位 | `--test-damage` |
| TTK | 胸 0.62/0.78/0.78/1.09 s（目标 0.6–1.2 ✅）；头 1 发 | `--test-damage` 的 `[ttk]` |
| 30% 丢包射击 | `[shotloss] loss 30% dropped 187 target 100->0 hitmark 5/21` | `--test-net` |
| 10 分钟链路 | `soak 600s joins=2 accepted=76538 late=0 far=0 snapshots=38268/38267` | `tools\net_soak.ps1` |
| Hitmarker | 真实开枪命中 `hitmark serial=1 amount=21 target=1 zone=1` | `godot_check.ps1` |
| 伤害进 HUD | 房主打 30 → 客户端打 `hud hp=70` | `godot_check.ps1` |
| 曳光 | 按住扳机 1 s 得到 `tracer events=14` | `godot_check.ps1` |
| 死亡/重生 | 对端 HUD 依次出现 70 → 0 → 100（`hurt@310 dead@616 back@2171`） | `godot_check.ps1` |
| 协议往返 | 10 人满编 953 B（94 B/人）；空 delta 15 B | `--test-snapshot` |
| 文档一致性 | 0 fail / 0 warn | `python tools\doc_check.py` |

> 回归判定：任一项比基线差 **>10%** 或出现非 0 分配，即视为回归，先查再继续。

## 4. 预算余量（详见 `docs/budgets.md`）

| 资源 | 已用 / 上限 | 余量 |
|---|---|---|
| delta mask 位 | 9 / 16 | 7 |
| flags 位 | 7 已用（另 4 位 v6 预留）/ 16 | 5 |
| 快照字节（10 人满编、全字段变化） | 953 / 1100 B | 147 B |
| sim tick | 0.0024 / 4 ms | 约 1600× |
| allocs/tick | 0 / 0 | 硬线 |

## 5. 阻塞（需要外部条件）

| ID | 卡在哪 | 谁/什么能解 | 影响 |
|---|---|---|---|
| DEBT-14 | 跨机 demo 比对缺第二台机器 | 借机 / 云主机 | M4 收尾项，不阻塞 M6 |
| DEBT-15 | godot-cpp 最高 4.5，计划书要 4.7 | 上游发版 / 降级接受 4.5 | **M7 前必须有结论** |

## 6. 上次会话结论（三行）

- **做成了**：S68 完成 M6.5 穿透（地图否决 + 每层 ×0.7，闸门 8 项；变异验证去掉否决后 concrete 项立刻红）；
  穿透策略收进 `PenetrationParams`，以后按材质给系数不用改调用点。
- **没做成**：还没有任何东西真的“开枪”（伤害、弹药、后坐力在 M6.6–M6.10）；
  可靠流（DEBT-20）与热重载（DEBT-22）仍是债。
- **下一件事**：M6.6 三段伤害（部位倍率 × 距离衰减 → `apply_damage`）；做完停下。

## 7. 本里程碑禁改清单（M6 范围内不碰）

- ❌ 第三人称动画、视角模型、音频定位 → M7 / M8
- ❌ 40×40 可玩图、爆破、C4、经济 → M6 后半 / M8
- ❌ tick 率 64 Hz
- ❌ `map_parse.cpp` 的单位转换点（唯一 0.0254）
- ❌ 全局重命名（已结案）
- ❌ 为性能做优化（余量 1600 倍，现在做是负收益）

---

*本文件是会话入口：开工读它 + 跑 smoke，收工改它。它过期 = 项目失忆。*

# AGENTS.md — Project Breakpoint（仓库入口 · 必读）

> 本文件在**工作区根目录**，每次新会话都会被注入，所以它承担两件事：
> **① 把计划书的核心决策与数值摘要带进上下文；② 强制你先读原文。**
> 详细约束在 docs/AGENTS.md；本文件只是摘要，**冲突时以 docs/AGENTS.md 为准**。

---

## 0. 开工前必须做的事（不能跳）

1. 读 **docs/STATE.md** 全文（≤120 行，唯一的"现在"：里程碑、WIP、闸门数字、预算余量、阻塞）。
2. 读 **docs/AGENTS.md**（铁律 / 判据清单 / 实现速查 / 环境事实）与 **docs/DEBT.md**（已知问题，先知道哪些是已知的）。
3. 跑 `tools\build.ps1`，再跑 `tools\smoke.ps1`：**红的那一项就是今天的战场**（数字抄自 STATE §3）。
4. 从 STATE §2 的 Top5 取第一条 → 在 DEBT 里标 `wip` → 写进 STATE §1 → **先写今天的判据再动代码**。
5. 只改一个关注点；越界发现的问题**记一行进 DEBT**，不顺手修；改完立刻 commit。
6. 收工：跑闸门写数字进 STATE、PROGRESS 加四段条目、DEBT 改状态、`python tools\doc_check.py` 退出码 0。

> 设计原文（`docs/计划书_v0.3.md`、`docs/网络层深化_v0.4.md`）只在**要改设计**时用 grep 定位读那 50 行，
> 不要通读：它们的价值是"被否决的方案与理由"（§2.2）和参数出处，不是日常状态。
> 数值以 `sim/data/*.toml` 为准，协议位与预算以 `docs/budgets.md` 为准——摘要写错时以它们为准。

---

## 1. 计划书摘要（原文为准）

### 1.1 一句话与主张

**5v5、有爆破目标的、Apex 式自由移动的第一人称射击游戏**，面向自己与小圈子对玩，不商业化。
核心主张：**移动自由度按 Apex，命中判定按 CS**。
差异化（v0.2 更正后）：**同一件事做得更硬——64 tick、无宽容判定、局域网零延迟。**

对外名 **IMPULSE**（缩写 **IMP**），内部代号 **Project Breakpoint / Project Bp / bp**；
三层名字各管各的读者，见 docs/IMPULSE_命名方案.md 与 decisions/0005。

### 1.2 已定的技术决策（§2.1）

| 类别 | 决策 |
|---|---|
| 宿主 | Godot 4.7 Mono（渲染/资产/编辑器） |
| 模拟层 | 自研 C++ 模拟层（GDExtension），**可脱离 Godot 独立运行** |
| tick | 固定 64 Hz |
| 网络 | Q3 net_chan 式**不可靠 UDP + delta 快照** |
| 地图 | .map 直读 + brush 平面 trace（无 BSP 编译） |
| 移动规则 | 取自 Red Eclipse 2（zlib，整套 parkour + impulse 资源） |
| 枪感 | 状态矩阵 + **查表**后坐力（与 CS 同构） |
| 判定 | hitscan 为主，狙击/投掷物用高速弹道 |
| 美术 | CC0/CC-BY 免费素材起步 |

### 1.3 关键否决（§2.2，禁止重提）

| 否决项 | 一句话理由 |
|---|---|
| Q3 服务端 + Godot 客户端 | 移动不是 Q3 的，pmove 无法逐 tick 一致 |
| Jolt / godot-jolt | .map+brush 下 Q3 clip 算法更保真；Jolt 已内置 |
| GameNetworkingSockets | 可靠有序 → head-of-line blocking |
| RmlUi / ozz / Recast / miniaudio | Godot 已覆盖或不需要 |
| 骨骼 hitbox | 动画变形导致「视觉命中但不判定」 |
| 单胶囊判定 | 打哪都一样，与「判定按 CS」冲突；改三段高度倍率 |
| 2 tick 插值 | 600 u/s 位移超出盒半宽；改 1 tick + hitbox 历史 |
| 跨机 bit-exact | 那是 lockstep RTS 的需求；改同机 bit-exact + 跨机 0.01 m 容差 |
| 打洞中继（v1.0） | 实际 2–3 周；目标锁死局域网 |
| 英雄技能系统 | 技能与精确射击是两套平衡体系 |
| 静止 8 秒拆包 | 高速移动下等于自杀 |
| Q 键一键承载墙跑/爬墙/翻越 | 400 u/s 下猜意图=送死 |
| TacZ 代码直接移植 | Java+Forge 生命周期；只取参数结构 |
| 市场空白论证 | The Finals 已在这一格；改为「做得更硬」 |

### 1.4 关键数值（§5.6 / §6 / §8）

| 项 | 值 |
|---|---|
| 单位 | 模拟层内部全米制；.map 入口 ×0.0254（唯一转换点） |
| 跑速 / 重力 / 跳跃 | 8.13 m/s / 20.32 m/s² / 6.86 m/s |
| 玩家盒 | 半径 0.405 m；站立 1.42 m；蹲 0.91 m；台阶 0.46 m |
| 滑铲 | 入场窗口 0.15 s、最低速 5.08、摩擦 ×0.35、跳 ×1.15 |
| 墙跑 | 最低入射 6.35、最长 0.9 s、重力 ×0.35 |
| 爬墙 | 4.57 m/s、最长 0.7 s |
| 翻越 | 最高 1.22 m、0.35 s |
| 冲刺 | 11.43 m/s、冷却 1.0 s |
| TTK | 有效 0.6–1.2 s（躯干） |
| 部位倍率 | 头 ×4 / 胸 ×1 / 腿 ×0.7；v0.3 爆头改 **×5** |
| 追击终结 | 受伤减速 −15% 持续 1.5 s（可刷新不叠加） |
| 垂直预算 | 每图 **2.5 层**；楼层用材质/色调区分；层间 ≥2 条路径 |
| 性能预算 | sim < 4 ms、网络 < 1 ms、渲染 < 8 ms、余量 ≥2 ms |
| 输入窗口 | 服务器 16 tick 早到窗口（不是延迟缓冲）；客户端每 tick 带最近 4 个（冗余） |
| 输入去重 | tick ≤ 已执行 → 丢弃；窗口 [last+1, last+16]；重复 0 日志，迟到 1 次计数 |
| 命令前瞻 | **lead = 4 tick**（单程 30 ms ≈ 2 + 2 tick 前瞻，v0.3 §7.0 的上限） |
| 延迟补偿 | hitbox 历史 64 tick（1 s）；回溯窗口按公式 6 tick（v0.4 §9.1） |
| 破坏（候选 B） | chunk 0.5 m 四档；每图 3 面、每队 2 次/回合、加固 1 处；C4 3.5 s |

### 1.5 里程碑（§12.2）

M0 环境验证（3 天）→ M1 .map/碰撞/哈希/度量（2 周）→ M2 基础 pmove（2 周）→
M3 parkour（3 周）→ M4 多机比对 + demo 回放（1 周）→ M5 网络（4 周）→
M6 射击（2 周）→ M7 打磨（3 周）→ M8 视角模型（1.5–2 周）。
v0.1 定义：**两个人、局域网、1 张 40×40 图、对枪 10 分钟不崩**（不含爆破/经济/bot/延迟补偿）。

---

## 2. 当前真实状态

**只在 `docs/STATE.md` 有一份**（覆盖式更新，带命令与数字）。这里不复制，只给三句定位：

- 里程碑：M0–M5 已关闭；M6 进行中（射击链路已贯通：俯仰/弹药/换弹/射速/命中/伤害）。
- 宿主实际是 **Godot 4.5**（godot-cpp 上限；计划书目标 4.7 = DEBT-15，M7 前必须有结论）。
- 闸门数量、p99、丢包回滚、协议版本一律看 STATE §3 / §0——那里的数字有命令可复现。

> 曾经在这里贴过一张"里程碑状态表"，它和 docs/AGENTS.md §0.2、HANDOFF、PROGRESS 各写一遍，
> 四处互相矛盾（"17 项闸门"出现在五个文件里）。2026-10-23 起只保留 STATE 一处。

---

## 3. 下一步（严格按计划书顺序，禁止跳步）

1. **M6 射击**（契约 `docs/contracts/M6.md`）：射击链路已贯通。下一步 M6.8 两端 HUD 断言，
   之后 死亡重生 → 后坐力 → 表现层 → 验收。**一次只做一个任务。**
2. M7/M8：音频、第三人称动画、第一人称视角模型。
3. 未解决项与债只看 `docs/DEBT.md`（33 条）；设计待定与功能缺口看 `docs/BACKLOG.md`。

---

## 4. 硬约束速记（完整版在 docs/AGENTS.md §1）

- **单位**：内部的米；转换只在 map_parse.cpp。
- **确定性**：固定步长、无迭代求解器、无带状态 RNG、整数 tick。
- **边界**：跨 GDExtension 只有状态流 + 事件通道。
- **网络**：只走不可靠通道；baseline = 最近 ack；连丢 3 个退全量；
  cmd.tick 必须对齐服务器时钟（lead 4），迟到输入**丢弃快进**，重复输入**绝不重放**。
- **MTU**：1200 B 上限，**1100 B 是分片阈值**（FragmentedTransport 已在真实链路两侧启用）；
  可靠流用 512 B 分块。
- **键位**：只在 sim/input 与 keybinds.toml；sim 只认 MoveInput。
- **路径**：工具链在 C:\bp-toolchain（ASCII）；ld 输出必须相对路径；文件用 u8path。
- **许可**：GPL/AGPL 代码不进仓库（含注释）；只读事实、自写实现，台账见 docs/assets-licenses.md（ADR 0007）。
- **命名**：对外 IMPULSE / IMP，对内 Project Bp / `bp` 前缀；代码里只有一个东西叫 impulse（移动资源），破坏叫 breach。
  **不做全局重命名（已结案）**：一直用内部代号开发，名字不影响正确性，只保证没有 bug。

---

## 5. 操作命令

~~~powershell
powershell -File tools\build.ps1        # 全量构建（-Werror）
powershell -File tools\smoke.ps1        # 22 项闸门 + bench，退出码 0 才算过
powershell -File tools\build_bridge.ps1 # GDExtension DLL（与 CMake 同一套标志）
powershell -File tools\godot_check.ps1  # 只跑桥：--hud-check + 两进程重连（3 = SKIP）
powershell -File tools\run_demo.ps1     # Godot demo
~~~

---

## 6. 文档地图

| 文件 | 作用 |
|---|---|
| docs/计划书_v0.3.md | **设计原文（必读）** |
| docs/IMPULSE_命名方案.md | 命名体系（IMPULSE / IMP / bp）、术语分层、符号；决议见 decisions/0005 |
| docs/网络层深化_v0.4.md | §7 网络专章：A1–A5 / B1–B3 / 冲突裁决 C1–C3 / N1–N8 判据 |
| docs/STATE.md | **唯一的"现在"**（≤120 行，覆盖写；开工先读它） |
| docs/contracts/M6.md | M6 里程碑契约：进入条件（已达成）、范围、退出判据、预算 |
| docs/budgets.md | 有限资源台账（协议位 / MTU / tick / 闸门 / 文档体积，80% 报警） |
| docs/AGENTS.md | 铁律、判据清单、实现速查（§8）、环境事实、文件地图 |
| docs/DEBT.md | **未解决问题与技术债的唯一入口**（33 条，状态四值 + verify 命令） |
| docs/BACKLOG.md | 设计待定（ADR 0006 之后余 5 条）与功能缺口（FEAT-01…13） |
| docs/HANDOFF.md | 这一段工作的**叙事交接**（为什么这样做、坑在哪、怎么接手）；不写状态数字 |
| docs/PROGRESS.md | 进度日志（S 短条目 + 末尾详细开发日志） |
| docs/AUDIT.md | 审计**索引**（每轮一个文件，证据在 docs/audits/） |
| docs/backup.md | GitHub / bundle / 恢复步骤 |
| docs/pitfalls/ | 事故案例（5 篇） |
| docs/decisions/ | ADR 0003 护甲 · 0004 网络时间轴 · 0005 命名 · 0006 穿透/护甲结算/热重载 · 0007 许可与净室 |
| docs/assets-licenses.md | 素材许可台账 + 「只读参考」（读过哪些 GPL 源、取了哪条事实） |
| docs/keymap.md / docs/demo-format.md | 键位 / demo 格式 |
| docs/archive/ | 只读历史（HANDOFF、GAPS、8.3 专章）；**不再维护，别当现状读** |

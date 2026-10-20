# AGENTS.md — Project Breakpoint（仓库入口 · 必读）

> 本文件在**工作区根目录**，每次新会话都会被注入，所以它承担两件事：
> **① 把计划书的核心决策与数值摘要带进上下文；② 强制你先读原文。**
> 详细约束在 docs/AGENTS.md；本文件只是摘要，**冲突时以 docs/AGENTS.md 为准**。

---

## 0. 开工前必须做的事（不能跳）

1. 完整读 **docs/计划书_v0.3.md**（2159 行，设计定稿、技术待验证）。不要只读本摘要。
2. 完整读 **docs/网络层深化_v0.4.md** 与 **docs/8.3候选B_v0.4.md**（v0.4 的两个专章，
   前者是 §7 的扩张与冲突裁决，后者是 §8.3 的选定方案）。
3. 完整读 **docs/AGENTS.md**（铁律 / 判据清单 / 未结案 / 环境事实 / 文件地图）。
4. 读 **docs/PROGRESS.md** 最后一节，再读 **docs/HANDOFF.md**，并扫一眼 **docs/DEBT.md**
   （未解决问题与技术债的单一入口；开工前先知道"哪些是已知的"）。
5. 跑 tools\build.ps1，再跑 tools\smoke.ps1，确认 **17 项行为闸门 + bench 全绿**（第 17 项是 Godot 桥）。
6. 只改一个文件集；改完立刻 commit；更新 PROGRESS。

> 为什么摘要不能替代原文：计划书里**被否决的方案与理由**（§2.2）、参数出处（§5.6/§6.2）、
> 风险登记册（§13）都不在摘要里，而它们恰恰是防止重蹈覆辙的部分。

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

## 2. 当前真实状态（摘要，细节在 docs/AGENTS.md §0.2）

| 里程碑 | 状态 |
|---|---|
| M0–M4 | ✅（M4 跨机比对缺第二台机器） |
| M5 | ✅ 不可靠通道/delta/预测回滚/时间对齐/输入去重/hitbox/full_state·加入·重连/远端插值/事件通道/真实 UDP/多 peer/两进程/**Godot --host·--connect·LAN 菜单/时钟漂移纠正/远端朝向 yaw/**掉线清理 5 s/槽位复用**已通；**A1 组局已接（信令 27115，host 占 slot 0，HELLO 22 B、协议 v5 带 client_id）、A5 票据进信令、A2 可靠流与快照共用 socket（configstring 已走可靠流）、重连顶替旧 link、**局内 HUD/impulse 条/Esc 暂停菜单/失联检测（快照年龄）/票据重连**已通；**M5 收尾完成**（A5 网关已拆） |
| M6 | 🔶 仅 HP/护甲规则（HP 100；甲 25/25 再生/50） |
| M7/M8 | ❌ |

- 宿主实际是 **Godot 4.5**（godot-cpp 最高只有 4.5；计划书目标 4.7，记为未结案）。
- 存档：GitHub 私有（SSH 443）+ 本地 bundle；远端与恢复步骤见 docs/backup.md。
- 17 项行为闸门 + bench 全绿（第 17 项 = Godot 桥加载/HUD/重连）；Godot demo 可运行（demo.bat）。
- --test-net：30% 丢包回滚 1428 → **51/2000**；5% 丢包 60 s 修正 0 次；
  中途加入/重连偏差 **0.00000 m**（加入走 FULL_STATE）。

---

## 3. 下一步（严格按计划书顺序，禁止跳步）

1. M6 射击：WeaponDef、开火输入、hitscan + hitbox 回溯、三段判定、hp/armor 进快照、
   死亡/重生、穿透/衰减。
2. M7/M8：音频、第三人称动画、第一人称视角模型。
3. 代码体检已收口（AUDIT A1–A7、AUD-01…19；只剩 AUD-16 留到 M6）；**未解决项与债看 docs/DEBT.md**。

---

## 4. 硬约束速记（完整版在 docs/AGENTS.md §1）

- **单位**：内部的米；转换只在 map_parse.cpp。
- **确定性**：固定步长、无迭代求解器、无带状态 RNG、整数 tick。
- **边界**：跨 GDExtension 只有状态流 + 事件通道。
- **网络**：只走不可靠通道；baseline = 最近 ack；连丢 3 个退全量；
  cmd.tick 必须对齐服务器时钟（lead 4），迟到输入**丢弃快进**，重复输入**绝不重放**。
- **MTU**：1200 B；快照分片丢一片整包丢弃，等下一 tick；可靠流用 512 B 分块。
- **键位**：只在 sim/input 与 keybinds.toml；sim 只认 MoveInput。
- **路径**：工具链在 C:\bp-toolchain（ASCII）；ld 输出必须相对路径；文件用 u8path。
- **命名**：对外 IMPULSE / IMP，对内 Project Bp / `bp` 前缀；代码里只有一个东西叫 impulse（移动资源），破坏叫 breach。
  **不做全局重命名（已结案）**：一直用内部代号开发，名字不影响正确性，只保证没有 bug。

---

## 5. 操作命令

~~~powershell
powershell -File tools\build.ps1        # 全量构建（-Werror）
powershell -File tools\smoke.ps1        # 17 项行为闸门 + bench，必须 ALL GREEN
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
| docs/8.3候选B_v0.4.md | §8.3 选定方案：可破坏入口、0.5 m 四档、时间预算 |
| docs/AGENTS.md | 详细约束、判据、未结案、环境、文件地图 |
| docs/HANDOFF.md | 本会话完整交接（S01–S31、每个坑的根因与证据） |
| docs/PROGRESS.md | 进度日志（S 短条目 + 末尾**详细开发日志**） |
| docs/GAPS.md | v0.1 缺口 + 技术债盘点（只分析） |
| docs/AUDIT.md | **代码体检清单**（P0–P3、证据、建议动作、处理顺序；A1–A7 已清） |
| docs/DEBT.md | **未解决问题与技术债的单一入口**（34 条 + 设计待定 + 功能缺口 + 已接受边界） |
| docs/backup.md | GitHub / bundle / 恢复步骤 |
| docs/pitfalls/ | 事故案例（4 篇） |
| docs/decisions/ | ADR（护甲 / 网络时间轴） |
| docs/keymap.md / docs/demo-format.md | 键位 / demo 格式 |

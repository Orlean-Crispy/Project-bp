# ADR 0005 — 命名与术语（IMPULSE / Project Bp）

- 日期：2026-10-20
- 状态：**已定稿**（2026-10-20 用户确认：就按内部代号开发到底，不做全局重命名，此事结案，不再讨论）
- 上游：docs/IMPULSE_命名方案.md（v1.2）；docs/计划书_v0.3.md §1.1

## 决定：三层命名，各管各的读者

| 层级 | 名称 | 用在哪 |
|---|---|---|
| 对外正式名 | **IMPULSE** | 窗口标题、标题画面、宣传、商店页 |
| 社区简称 | **IMP** | HUD、昵称、版本号 |
| 内部代号 | **Project Breakpoint / Project Bp / bp** | 仓库、文档、分支、构建、宏 |

## 代码标识符：只加前缀，不做全局重命名

- 项目级前缀保持 **bp**：`bp` 分支、`C:\bp-toolchain`、`BP_PROTOCOL_VERSION` / `BP_SIM_VERSION`。
- 模块命名空间保持 **`sim::`**，**不**改成 `bp::`。

理由三条：

1. 命名空间的价值是划分模块边界。`sim::` 说的是「这一层是什么」，`bp::` 只说了「这是我的」；
   桥接层 bridge/ 不属于 sim，压进 `bp::` 反而把边界抹平。
2. 26 个 .cpp × 上百处的机械重命名，diff 巨大、零行为收益，违反铁律 F（一个 commit 一件事）。
3. 品牌统一靠前缀（宏、产物名、分支名）已经够；标识符里再塞项目名是同义反复。

**已结案**：这条不是「暂时不改」，是**永远不改**。项目一直用内部代号开发，
名字对正确性零影响；唯一要保证的是没有 bug。真要抽公共库时再另开 ADR，不预留钩子。

## 术语分层（只有一个东西叫 impulse）

| 概念 | 用词 | 不许 |
|---|---|---|
| 移动资源 0–N | **impulse** | 不许叫 charge / stamina |
| 破坏动作 | **breach** | 不许叫 impulse |
| 破坏四档 | **breach level 1–4** | 不许叫 impulse level |
| 命中反馈 | **IMPACT** | — |
| C4 本体 | **impulse charge** | 不与移动资源混用 |

## 玩家可见

- Godot `config/name` 与启动菜单标题 → IMPULSE。
- HUD 资源条必须从 `MovementParams::max_impulses` 读格数，**不许写死 6 个格子**（见未决 1）。

## 未决

1. **impulse 上限三处不一致**：计划书 §5.2 仍标「待定（离散 6 次 / 上限由回防时间反推）」，
   sim 现值 **4**（sim/data/movement.toml 的 `max_impulses`），命名方案 §7.2 画的是 **6**。
   在 M3 手感验收时定；HUD 一律读参数。
2. ~~**仓库名**~~：**不改**。GitHub 上就叫 `unknow`；改名会让已有 clone/远端地址失效，
   对玩家与代码零收益。名字只在对外发布时才有意义，内部开发不需要。
3. **构建产物名**：静态库 target 仍叫 `sim`（`libsim.a`），可执行文件 `headless_sim.exe` 等
   是**工具名**不是产品名，按「工具名保持描述性」保留。

## 证据

- 文档：README 标题、根 AGENTS §1.1 / §4 / §6、docs/AGENTS §2 / §4 / §7 已同步；命名方案全文入 docs/。
- 玩家可见：godot/project.godot `config/name="IMPULSE"`、main.gd 菜单标题 `IMPULSE - LAN`。
- 基线：build + 16 项闸门 + bench 全绿（同一次提交）。

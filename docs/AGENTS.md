# AGENTS.md — Project Breakpoint（约束 + 交接）

> **这一份就是开工入口**。它同时承担计划书 §15 的「约束」和「上下文丢失后的交接」两个职责：
> 会话上下文被压缩后，先读本文件，再读 docs/PROGRESS.md 的最后一节。
> 硬上限 64 KB。加内容之前先问一句：**三个月后还成立吗？**

---

## 0. 项目与当前状态

**5v5、有爆破目标的 Apex 式自由移动 FPS。移动按 Apex，判定按 CS。**
Godot 宿主 + 自研 C++17 模拟层（零 Godot 依赖，可独立运行），64 tick，30 ms 以内局域网。

设计文档：docs/计划书_v0.3.md（v0.3，设计定稿、技术待验证）

### 0.1 架构（已落地）

```
Godot 4.5（窗口 / 渲染 / 输入；只做表现）
   ↑ GDScript（godot/main.gd：读输入、驱动 tick、摆相机）
   ↑ GDExtension（bridge/sim_bridge.cpp：唯一跨边界层）
sim/（纯 C++17，零 Godot 依赖；headless_sim 可独立跑全部测试）
```

### 0.2 里程碑状态（对照计划书 §12.2）

| 里程碑 | 状态 | 说明 |
|---|---|---|
| M0 环境验证 | ✅ | 工具链、Godot、godot-cpp 全部可用；tick 探针以 bench 代替 |
| M1 地图/碰撞/哈希/度量 | ✅ | .map 解析、brush 扫掠、--selftest、move_metrics |
| M2 基础 pmove | ✅ | 走/跑/跳/蹲、step-up、蹲下空间、Q3 对标数值 |
| M3 parkour | ✅ 首版 | slide / dash / wallrun / wallclimb / vault，全部有 headless 闸门 |
| M4 demo 与跨机比对 | ✅ 同机 / ⏸ 跨机 | 录制回放、逐 tick 哈希、dump 比对工具就绪；**跨机需要第二台机器** |
| M5 网络 | ✅ 完成 | 不可靠通道 + delta + 预测回滚 + 时间对齐 + 去重 + hitbox + 加入/重连 + 远端插值 + 事件通道 + 真实 UDP + B1 分片 + 多 peer 会话 + 两进程 + Godot --host/--connect + 远端渲染 + LAN 菜单 + 时钟纠正 + 远端 yaw/掉线清理 5 s/槽位复用已通；**A1 组局（信令 27115）、A5 票据/重连（resume 回原槽）、A2 可靠流共 socket、局内 HUD/暂停菜单/失联检测已通；第三人称动画未做**；**M5 收尾完成（A5 网络网关已拆出，2026-10-20）**，第三人称动画与 5v5 选边归 M7 |
| M6 射击 | 🔶 起步 | HP/护甲规则已实现；hitscan、后坐力表、三段判定、穿透未做 |
| M7 打磨 / M8 视角模型 | ❌ | 未开始 |

### 0.3 现在能跑什么

- `headless_sim` 与 Godot 桥检查合计 **17 项行为闸门** + bench 全绿（第 17 项是桥，见 §3）
- Godot demo：`demo.bat` 或 `powershell -File tools/run_demo.ps1`，可走跑跳滑铲墙跑爬墙翻越
- 手感数值可改 sim/data/movement.toml 后重跑，不用改代码

---

## 1. 铁律

> 只在**付过代价**时新增。格式：领域 / 日期 / 代价 / 判据。被推翻的直接删，不留两版。

### A — 单位制（2026-10-18，代价 =「手感不对但查不出」）

- 模拟层内部**只用米、秒、m/s、m/s²**；Z 轴向上。
- Quake unit 只在 sim/collision/map_parse.cpp 入口乘一次 0.0254，别处禁止换算。
- 判据：搜索 u/s、unit 只应命中该文件的换算注释。

### B — 确定性（2026-10-18，代价 = 回滚/重放失效、demo 报废）

- 固定步长；**禁止迭代求解器**；状态切换用整数 tick 计数，不用 float 累加。
- 实体用稠密数组；**禁止指针哈希容器**；**禁止 rand()/std::mt19937/任何带状态 RNG**。
- 随机一律 rng_at(tick, entity, index, salt)（sim/core/rng.h）。
- 不开 -ffast-math；CMake 已加 -ffp-contract=off。
- 判据：`headless_sim --selftest --ticks 600` 逐 tick 哈希一致。

### C — 边界（2026-10-18，代价 = 表现层轮询漏帧 + 回滚错乱）

- 跨 GDExtension 只有两条通道：状态流与事件通道（EventQueue，1024 条固定环）。
- 表现层不反向查询 sim；伤害数字只走快照，不走事件。
- 判据：bridge/ 内不出现轮询式反向查询。

### D — 网络（2026-10-18，代价 = head-of-line blocking）

- 快照与输入只走**不可靠通道**；delta 的 baseline = 客户端最近 ack 的快照；
  连续 3 个未 ack → 强制全量。
- 输入按 v0.4 §5.2 去重：`tick <= 已执行` 丢弃；窗口 [last+1, last+16]（早到窗口，
  不是延迟缓冲）；迟到输入**不补执行**（客户端已预测，下次快照自然回正）。
- 客户端跑在服务器 tick 轴上，命令前瞻 **lead = 4**（单程 30 ms ≈ 2 + 2 tick 前瞻）。
- 客户端可预测命中（v0.4 §9.3，`cl_predict_hit` 默认 1）；伤害数字仍等服务器；
  不一致率 > 0.5% 就关掉，回退到「等服务器」。
- 判据：`--test-net`（30% 丢包收敛 < 0.01 m、回滚 < 500；N3 5% 丢包 60 s 修正 < 3 次）。

### E — 性能（2026-10-18，代价 = tick 累积 → 人物瞬移）

- 单 tick 模拟预算 4 ms（64 Hz 总预算 15.625 ms）；稳态 0 分配。
- 判据：`headless_sim --bench 1000` 的 p99（当前 0.4 µs，离预算很远）。
- 注意：bench 用的是单玩家小图；接网络/多人后必须重测。

### F — 协作（2026-10-18，代价 = 并行改动互相覆盖）

- 一个功能一个 commit；同一个文件的多处修改**串行**（一次读、一次改、一次写回）。
- 大文件禁止整文件重写，只做精确 edit。
- 判据：git log 中单个 commit 不跨无关模块。

### G — 凭据（2026-10-18，代价 = 公开仓库泄漏）

- 密钥不进仓库（包括「先提交再删」）；只从环境变量读；文档写 <YOUR_KEY>。

### H — 内容许可（2026-10-18，代价 = 素材混入不可用授权）

- 素材进仓库前登记 docs/assets-licenses.md（尚未创建，第一个素材时建）。
- CC BY-NC-ND 的资产（如 TacZ 模型）**不得进构建产物**。

### I — 手感数值（2026-10-18，代价 = 调参要重编）

- 影响手感的数值一律在 sim/data/*.toml；运行时可重载（重载路径**尚未实现**，见未结案）。
- 参与模拟的数值必须计入 params_hash；两侧不一致要拒绝入局。
- 判据：改 movement.toml 后 params_hash 变化、selftest 仍绿。

### J — 键位隔离（2026-10-18，代价 = 改键影响确定性）

- 键位只允许出现在 sim/data/keybinds.toml 与 sim/input/；
  sim 内部**不得**出现 Key/Action，只认 MoveInput。
- 判据：`headless_sim --test-bindings`；grep sim/ 不应命中 Key 枚举。

---

## 2. 代码约定

| 项 | 规定 |
|---|---|
| 语言 | C++17；sim 层零第三方依赖（仅标准库） |
| 编译器 | MinGW-w64 GCC 16.2，固定装在 C:\bp-toolchain（见 §6） |
| 警告 | -Wall -Wextra -Werror；只有**全量构建**算数 |
| 命名 | 类型 PascalCase，函数 snake_case，成员 m_ 前缀，常量 kPascalCase |
| 命名（项目） | 对外 IMPULSE / IMP，对内 Project Bp / bp 前缀；项目级宏 BP_*；模块命名空间保持 sim::。**不做全局重命名（已结案）**（ADR 0005） |
| 头文件 | #pragma once；sim 头文件不得 include 任何 Godot 头 |
| 浮点 | float 为模拟主类型；禁止隐式 double |
| 路径 | 工程内引用用相对路径；**链接器输出必须是相对路径**；文件打开必须 std::filesystem::u8path |
| 脚本 | .ps1 只写 ASCII（PowerShell 5.1 按 GBK 读无 BOM 文件） |
| 字符串 | 磁盘格式用 UTF-8；Windows 文件 API 必须走 u8path |

---

## 3. 判据清单（tools/smoke.ps1 串起 17 项行为闸门 + bench）

| # | 判据 | 阈值 | 命令 |
|---|---|---|---|
| 1 | 地图可解析 | 实体/brush 数正确、边界为米 | --info |
| 2 | 确定性 | 同输入两次逐 tick 哈希一致 | --selftest --ticks 600 |
| 3 | 穿墙 | 8.13–30 m/s ×100 次，0 穿透 | --test-wall |
| 4 | 扫掠 | 点/盒单段跨 0.1 m 墙必命中 | --test-ray |
| 5 | 台阶/蹲下 | 0.20/0.35/0.45 可上、0.55 挡住、1.0 m 净高不能站 | --test-step |
| 6 | 跳跃语义 | 按住 300 tick 只跳 1 次；点按 3 次跳 3 次 | --test-jump |
| 7 | HP/护甲 | 规则与 TTK 落在 0.6–1.2 s | --test-health |
| 8 | 网络收敛 + 去重 + 加入 + 插值 + UDP + 分片 + 双客户端 + 信令组局 | 30% 丢包偏差 < 0.01 m、回滚 < 500；N3 修正 < 3；加入/重连 < 0.01 m；插值正确；UDP 往返；分片 0 损坏；2 客户端经 hub 加入且偏差 < 0.01 m；组局握手按槽位/票据/满员拒绝正确 | --test-net |
| 9 | 卡死恢复 | 从 brush 内部出生能自行脱困 | --test-stuck |
| 10 | 乱按压力 | 6000 tick 乱按，最长连续实心 ≤ 2 | --test-soak |
| 11 | 平台边缘 | 落到边缘后仍能继续走 | --test-edge |
| 12 | parkour | slide/dash/wallrun/wallclimb/vault 全部生效 | --test-parkour |
| 13 | 键位 | 加载/改键/保存/重载 + 冲突检测 | --test-bindings |
| 14 | demo | 同构建逐 tick 一致；改动参数必被检出 | --test-demo |
| 15 | hitbox 历史 | 64 tick 环形、帧间插值、三段 ×5/×1/×0.7、蹲下自动跟随 | --test-hitbox |
| 16 | 事件通道 | 跳跃/落地事件带 tick、可复现、环形溢出丢最旧 | --test-events |
| 17 | Godot 桥加载 | 扩展真的加载、HUD 打印 vertices>0 与 ports.h 的默认端口、两进程重连回原槽且第二次调用被拒 | tools/godot_check.ps1 |

当前全部 PASS（第 17 项在没装 Godot 或桥 DLL 未构建时打印 SKIP 并返回 3，smoke 视为跳过而不是绿）。
`--compare`（跨机 dump 比对）需要第二台机器，尚未执行。

---

## 4. 未结案（按优先级，每条必须有 lead）

1. **网络组局** — **已关闭**（M5 收尾完成）：信令组局、票据 resume 回原槽、HUD（含
   SERVER SILENT 1 s）、重连按钮、A5 网络网关拆分全部实测通过。第三人称动画与 5v5 选边
   按计划书属于 M7，不在此条。
2. **v0.4 的 M5 增量** — A1/A2/A5 全部接入并实测；configstring 带真实哈希且失配拒局。**已关闭**。
3. **M6 射击** — hitscan、后坐力表、三段部位判定（hitbox 已就绪）、穿透、伤害衰减、
   hitmarker 全未做。护甲的**吸收比例 66%** 与**再生 5 s / 10 s** 是提案值，需要确认。
4. **Godot 版本** — 计划书写 4.7 Mono，但 godot-cpp 最高只有 4.5，现用 4.5。
   lead：M7（Godot-Locomotion 绑定 4.7.2）之前必须解决。
5. **跨机 demo 比对** — 工具与流程就绪（docs/demo-format.md），缺第二台机器。
6. **游戏内改键 UI** — 键位系统已数据驱动，UI 未做。lead：M5 之后。
7. **穿透两套模型** — .map 的 _penetrable 与武器 penetration_count 谁优先未定。lead：M6。
8. **movement.toml 热重载** — 铁律 I 只做了哈希，没有 reload 路径。lead：M6 前。
9. **impulse 上限** — 计划书 §5.2 仍标「待定」，sim 现值 4（movement.toml 的 max_impulses），
   命名方案画 6 格；HUD 必须读参数、不许写死。lead：M3 手感验收。

---

## 5. 教训（编号沿用，不复用；案例在 docs/pitfalls/ 与 PROGRESS）

| # | 判据 |
|---|---|
| 1 | 盒体扫掠的支撑距离用 **min**（相交）不是 max（包含）。案例：pitfalls/minkowski-support.md |
| 2 | 工具链必须在物理 ASCII 路径；junction/subst/8.3 都绕不过。案例：pitfalls/toolchain-nonascii.md |
| 3 | 会跑一次的测试不算测试：穿墙类判据至少 100 次 × 5 个速度档。 |
| 4 | 碰撞判据必须成对：**不能穿**（100 次）与**不能卡**（贴墙静止 / 沿墙滑动）同时验。案例：pitfalls/contact-epsilon.md |
| 5 | 「面前有没有东西」的探测一律从后退 5 cm 起扫；投影/裁剪后的速度不能当探测方向。 |
| 6 | 被测输入已经进了哈希时，「能检出差异」是恒真的；物理差异要用位置 dump 证明。 |
| 7 | 路径分三种：编译器能读的、链接器能写的、C 运行库能打开的，对非 ASCII 容忍度不同。 |
| 8 | 玩家从 tick 开始就实心 = 冻结（相机能动、人不动）。必须：拒绝实心落点 + 出生时脱离。案例：PROGRESS S08 |
| 9 | 跳跃必须是按下沿触发；按住 = 连跳。案例：PROGRESS S09 |
| 10 | 客户端必须跑在服务器的 tick 轴上并提前 **4 tick** 盖戳；迟到输入只能丢弃，绝不能当新输入重放。案例：PROGRESS S11 |
| 11 | Windows 上 `bind` 成功不代表端口是你的（SO_REUSEADDR 允许抢已被 listen 的端口）。用 SO_EXCLUSIVEADDRUSE，端口冲突要让它报错。案例：pitfalls/port-hijack.md |
| 12 | 两个编译入口编**同一份源码**时，标志必须逐条一致。不一致不会报错，只会让 DLL 与 exe 的浮点结果悄悄分叉（AUD-13：桥缺 `-ffp-contract=off`）。 |
| 13 | GDExtension 报「找不到库」时，先查 `.gdextension` 的 **feature key**（editor / debug）与依赖 DLL，别信报错里的 OS/架构——release 编辑器只认 `windows.editor.*`，少了它整份 GDScript 都会「标识符未声明」（2026-10-20）。 |
| 14 | 「我机器上能跑」不是可恢复性判据：安装脚本不能依赖恰好装着的工具（7z）或文档里手抄的版本号（AUD-19）。判据是干净机器上一条命令。 |

---

## 6. 环境事实（本机，2026-10-19 实测）

| 项 | 值 |
|---|---|
| 工作区 | G:\自制游戏（**中文路径**） |
| Shell | Windows PowerShell **5.1**（不是 pwsh 7） |
| 工具链 | C:\bp-toolchain：mingw64（GCC 16.2）、cmake 4.4.4、ninja |
| Godot | C:\bp-toolchain\godot\Godot_v4.5-stable_win64.exe（4.5，非 Mono） |
| godot-cpp | C:\bp-toolchain\godot-cpp（godot-4.5-stable，SCons 构建） |
| DLL 依赖 | libwinpthread-1.dll 必须在 Godot 可执行文件旁边 |
| 磁盘 | G: 约 240 GB 空闲；.toolchain/ 是下载缓存（1.35 GB，不进 git，别用来编译） |

### 6.1 命令

```powershell
powershell -File tools\setup_toolchain.ps1   # 仅首次：下载 GCC/CMake/Ninja（无 7z 依赖，版本已锁）
powershell -File tools\build.ps1             # 配置 + 全量构建（headless_sim, move_metrics）
powershell -File tools\smoke.ps1             # 17 项行为闸门 + bench，必须全绿
powershell -File tools\build_bridge.ps1      # 重建 GDExtension DLL（与 CMake 同一套标志）
powershell -File tools\godot_check.ps1       # 只跑桥：--hud-check + 两进程重连
powershell -File tools\run_demo.ps1          # 构建桥 + 启动 Godot demo
```

godot-cpp 需要重建时：

```powershell
cd C:\bp-toolchain\godot-cpp
python -m SCons platform=windows use_mingw=yes target=template_debug -j8
```

Godot 首次导入（没有 godot/.godot 时）由 tools/godot_env.ps1 自动完成——不要在各个脚本里再抄一遍。
它也是唯一知道 Godot 可执行文件位置的地方（`$env:BP_GODOT` 优先，否则取 C:\bp-toolchain\godot 里最新的
`Godot_v*_win64.exe`），所以升版本不需要改脚本。

### 6.2 三条非 ASCII 规则（血泪）

1. **主工具链不能放在中文路径**：ld 读不了自己的 crt/lib。
2. **ld 的 -o 输出必须是相对路径**：中文输出路径直接 cannot open output file。
3. **C 运行库打开文件必须 u8path**：GDScript 传来的 UTF-8 路径，std::ifstream 会按 ANSI 解码。

---

## 7. 目录与关键文件

```
sim/core/        tick 时钟（steady_clock + checked 减法）、世界状态与哈希、无状态 RNG、版本号
sim/collision/   .map 解析（唯一单位转换点）、brush 凸体 + 面多边形、盒体扫掠、分步 slide、step-up
sim/move/        pmove：地面/空中/跳跃/蹲 + 五件套状态机（整数 tick）
sim/input/       物理键枚举、动作映射、改键/保存/冲突检测
sim/demo/        demo 二进制格式（头 + 每 tick 输入 + 哈希）
sim/net/         不可靠通道、delta 快照、服务器/客户端、劣化 loopback、ServerClock
sim/game/        combat：HP 100 + 三种护甲（数据驱动）
sim/data/        movement.toml、combat.toml、keybinds.toml、maps/*.map
bridge/          GDExtension 桥（唯一跨边界层）
godot/           project.godot / main.tscn / main.gd / breakpoint.gdextension
tools/           headless_sim（所有闸门）、move_metrics、net_loop（两进程联机）、run_net_demo（双实例）、build/smoke/build_bridge/run_demo/setup 脚本
docs/            AGENTS.md（本文件）、PROGRESS.md、HANDOFF.md、pitfalls/、decisions/、keymap.md、demo-format.md、计划书_v0.3.md、IMPULSE_命名方案.md、GAPS.md、AUDIT.md
```

### 7.1 地图

| 文件 | 用途 |
|---|---|
| sim/data/maps/test_box.map | 20×20 房间 + 0.1 m 薄墙 + 0.4 m 台；碰撞测试 |
| sim/data/maps/metrics_flat.map | 60×60 空房；move_metrics 专用，避免墙污染数据 |
| sim/data/maps/step_test.map | 五条通道：0.20 / 0.35 / 0.55 挡墙 / 三级楼梯 / 1.0 m 净高 |
| sim/data/maps/parkour_test.map | 1.0 m 矮道（滑铲）、可墙跑墙、可爬墙、1.0 m 平台（翻越） |
| sim/data/maps/playground.map | 40×30 演示图：台阶、平台、矮道、薄墙、墙跑墙、爬墙墙 |

---

## 8. 关键实现速查（改之前先读这里）

### 8.1 pmove（sim/move/pmove.cpp）

- 顺序：dash 冷却 → 地面探测 → 滑铲起手 → 墙跑/爬墙状态 → 能力起手 → 摩擦/加速/重力
  → dash 施加 → step_slide_move → 落地判定/回充。
- 跳跃：**按下沿**（jump_was_down），slide 起跳 ×1.15。
- 滑铲：地面 + 速度 ≥ 5.08 + 消耗 1 次；摩擦 ×0.35；最多 1.5 s。
- 冲刺：冷却 1 s，不耗次数，施加在摩擦之后（否则同 tick 摩擦吃掉 9%）。
- 墙跑：仅空中，≥ 6.35 m/s，需要 brush.wallrunnable；重力 ×0.35；沿墙面投影 + 0.2 m/s 内向分量。
- 爬墙：需要 brush.climbable，速度 4.57 m/s，最长 0.7 s。
- 翻越：0.46–1.22 m 台沿，0.35 s 内固定前进+上升。
- 有效速度 = base_speed × 武器系数 × 受伤减速 × 蹲下系数。
- 性能：每 tick 只算一次 yaw 的 sin/cos；蹲姿未变时，起始实心检查复用地面探测的
  start_solid，不再重复一次 hull trace。

### 8.2 碰撞（sim/collision/*）

- 扫掠 = Minkowski 展开（支撑距离取 **min**）+ 分步（step = 最小墙厚），bumps ≤ 4。
- 进入分数钳到 0（接触 epsilon 的坑）。
- 落地不接受实心点；pmove 每 tick 检查实心并向上/四向脱离。
- 基准：`--bench 1000 --players 10`；World::step 稳态 0 分配。10 人：15 brush 2.2 µs、
  201 brush 5.9 µs、801 brush 21.7 µs（预算 4000 µs）。单图 >2000 brush 再考虑 broadphase。

### 8.3 键位（sim/input/*）

- 默认瓦洛兰特布局：C 翻越 / Q 墙跑 / E 爬墙 / X 冲刺；Shift 滑铲、Ctrl 蹲。
- 改 sim/data/keybinds.toml 即可；Bindings 支持运行时 set_key/save。
- Godot 侧从 sim.get_bindings() 生成 InputMap（单一数据源）。

### 8.4 网络（sim/net/*）

- 包头：type/seq/ack/ack_bits；快照 delta 以「客户端最近 ack 的快照」为 baseline。
- 客户端：本地预测 + 128 tick 历史 + 回滚重放；命令包带最近 4 个输入。
- 时间轴：客户端 = 服务器 tick + 4（单程 30 ms ≈ 2 tick + 2 tick 前瞻）；初始化时
  预跑 4 个空 tick，复刻服务器在首个命令到达前的状态。
- 服务器：InputWindow 是“早到窗口”（16）不是延迟缓冲：接受 [last+1, last+16]；重复丢弃
  不记日志；迟到丢弃并只计一次；tick T 没有输入就沿用上一个。客户端领先 7 tick
  （lead 4 + join slack 3），窗口必须大于它，否则会丢掉下一 tick 要执行的命令。
- 加入/重连（§7.7）：新链路 pending_full_state，每 tick 发 FULL_STATE
  （join_id + player + params_hash + geometry_hash + 全量快照），收到“未来 tick”的命令
  才转正常 delta；客户端重置历史、tick = 快照 tick + 7（3 传输余量 + 4 lead）后预跑。
- 其他玩家（§7.1）：SnapshotInterpolator 存最近 32 帧（0.5 s），`sample(tick, frac)`
  在相邻两帧间插值 pos/vel；超出范围夹到最近帧、不外推；客户端每收一份快照就 push。
- 传输：LoopbackTransport 用于确定性闸门；UdpTransport（非阻塞 Winsock，客户端 connect_peer、
  服务端 recv_from→adopt_peer）用于真实局域网；一个对象一个 peer。
- 分片（v0.4 B1）：FragmentedTransport 包住任意 Transport；≤1100 B 透传，以上切成
  5 B 头（marker/id/index/total）的片；接收端按 id 重组，半包绝不交付，新 id 丢弃旧包。
- 多 peer：UdpSocket 共享 + UdpPeerHub 按源地址 demux；PeerTransport 每 peer 一个收件队列；
  未知来源进有界队列（64），HELLO（22 B，magic “BPH0” + 协议版本 + 16 B client_id）通过才 add_peer。
  协议 v4：HELLO 必须带**信令已签发**的 client_id，槽位由 matchmaking 事件给出，不按到达顺序猜；
  没走过 TCP 的 id 一律忽略（陌生人包进不了房间）。
- 判据：真 UDP 环回 2 客户端 joined 1/1、148/148 快照、偏差 0.00000 m；
  两进程 net_loop（信令组局）：joins=2、accepted=416、far=0、moved=46.87 m。
- Godot 桥：host = MatchHost(TCP 27115) + NetServer + 本机 NetClient(loopback，占 slot 0)；
  join = match_request → 拿槽位/票据 → NetClient(UDP 27116)；本地状态取 predicted()，
  远端取 SnapshotInterpolator，GDScript 画胶囊。菜单端口语义 = **信令端口**。
  判据：host 打 signal slot 1 resumed 0 / peer joined slot 1 / peers 2 / remote slot 1 visible，
  client 打 joined slot 1 / remote slot 0 visible（两个 headless Godot 进程实测）。
- 局内 HUD（godot/main.gd）：HP/护甲（桥里是占位 100/0，等 M6 联网）、impulse 条
  用 `get_max_impulses()` 画格（**不写死**），状态行显示 role/slot/peers/waiting；
  Esc 开合暂停菜单（Resume / Reconnect（仅客户端）/ Leave）。
  判据：`--hud-check` 打 `hud hp=100 armor=0 impulse=4/4 role=solo vertices=360
  build=<hash> protocol=4 port=27115`——vertices > 0 证明 mesh_builder 还在产出几何，
  port 证明默认端口来自 ports.h 而不是脚本。这条检查已并入 tools/godot_check.ps1。
- 失联检测（`NetClient::snapshot_age()`）：客户端用**最后一次收到快照的 tick 差**判断，
  > 64 tick（1 s）显示 `SERVER SILENT x.xs`。世界仍在本地推进也能看出来——
  「tick 停走」只能发现模拟卡死，发现不了服务器没了。
  判据：杀掉房主后客户端打 `breakpoint: server silent age=65`。
- 重连（bridge `reconnect()`）：用**同一个 client_id + A5 票据**重走信令 resume，回原槽。
  判据：`--reconnect-check` 客户端打 `reconnect ok`，房主打 `signal slot 1 resumed 1` 并再次
  `peer joined slot 1`。
- 回滚修复（R1）：`apply_snapshot` 在回放前把修正状态写回 `m_history[index]` 并重记该 tick 的 hitbox，
  否则重复快照会二次回滚、回溯命中判定拿到修正前的位置。判据：`[rollback] hitbox 1 dup 1 state 1`。
- 网络零分配（R6）：`NetServer::step` 用成员输入缓冲、客户端走 `World::step_single`、
  `Link::sent` 与 `m_recent` 是容量 64 环形数组。判据：`[netalloc] per_tick 0`（只统计 session 三步；
  序列化与回环传输仍分配，见 AUDIT 边界说明）。
- 联机事件（AUD-01）：`NetClient::step_local(EventQueue*)` 贯穿回放，`pump(input, events)` 传入；
  判据 `[netevents] 1`——联机里起跳事件能到桥，`drain_events()` 不再恒空。
- Godot 启动菜单：无参数且非 headless 时显示 Host/Join/Single player；--menu 可在 headless
  下构建用于脚本检查；菜单打开时暂停 tick。
- 时钟纠正（§7.0）：客户端按快照 ahead 的最小值检测漂移；超过 min+3 且冷却结束时跳过
  一次 send+step（世界冻结 ~15.6 ms），领先量永久减 1；无 tick 空洞、不改历史。
- A5 票据（sim/net/ticket.*）：32 B（client_id 16 + expiry 4 + HMAC-SHA256/12），
  密钥只在房主内存；now >= expiry 即失效。判据：valid 1 expired 1 tamper 1 wrongkey 1。
- A2 可靠流（sim/net/reliable.*）：独立 rel_seq（1 起）、窗口 32、累积 ACK + 32 位位图、
  RFC 6298 RTO（250 ms，100–1000）、5 次重传上限；configstring 512 B 分块 + CRC32。
  判据：5000 B / 10 块 / 30% 丢包 → retrans 6、complete 1、same 1、corrupt 1。
- A2 共 socket（sim/net/reliable_transport.*）：`ReliableTransport(tx, rx)` 包住任意传输；
  快照/输入原样透传，可靠包用 `PacketType::ReliableData(4)` / `ReliableAck(5)` 打标，
  接收端先 demux 再交付，两条流互不阻塞；wrapper **不**推进底层管道（调用方每 tick 推进一次）。
  判据：[dual] chunks 8/8 same 1 retrans 1 snapshot 326/400 dead 0（20% 丢包 + 20 ms）。
- A2 接入会话：`NetServer::set_configstring(blob)` 切 512 B 块，`add_client(..., rel)` 后每 tick
  在 `send()` 里补投（窗口满就等下一 tick）；`NetClient::init(..., rel)` 收到即喂 `ConfigAssembler`，
  `config_ready()/config_chunks()/config_string()` 可查。
  判据：[config] chunks 1 same 1 matched 1 refused 1 combat 1 full_state 1 ticks 18 retrans 1。
  内容语义（sim/net/configstring.h）：34 B（version + params_hash + geometry_hash）；
  客户端收齐后比对，任一不符 → `config_mismatch()` 并 `join_failed`——宁可不进局，
  也不带错规则进局。桥的 host 已按 world 的哈希生成并下发。
- **可靠流的时钟只有一个来源**：wrapper 由会话用**自己的 tick** 推进（服务器 = world.tick，
  客户端 = local.tick），调用方不得再推进同一个 wrapper；`advance()` 内部取 max，
  因为回滚会让客户端 tick 倒退，`now - last_sent` 一旦回绕，所有包都会被判超时。
- 端口只有一个来源：**sim/net/ports.h**（kDefaultSignalPort、kGateSignalPort、game_port_of）。
  信令默认 27115、游戏 = 信令 + 1、闸门用 27130（= 默认 + 15，免得和本机在跑的房主撞车）；
  Godot 侧读桥的 `get_default_signal_port()`，脚本读 net_loop 的默认值，`tools/check_sources.ps1`
  会在这四个常量出现在别的文件时直接报错。
- A1 信令（sim/net/signaling.*）：TCP **27115**（v0.4 写的 27015 已被 AppleMobileDeviceService 占用；
  Windows 上用 SO_EXCLUSIVEADDRUSE 独占，端口被占时 bind 必须失败而不是静默共享）、
  u16 长度前缀、六条消息、connect/accept 超时；
  资产只传清单。判据：真 TCP 环回六条消息逐字段一致。
  协议 v2：client_id 改为 16 B（与票据的 HMAC 输入一致），HELLO/HELLO_ACK 带 32 B 票据，
  MATCH_START 带 game_port（约定 UDP = 信令端口 + 1）。
- A1 组局（sim/net/matchmaking.*）：MatchHost 自起 TCP 线程（port 0 = 临时端口，闸门可并行）；
  握手 HELLO→ACK→MANIFEST→READY→MATCH_START；新客户端领 32 B 票据（120 s），
  同 id + 有效票据回原槽位（A5 恢复），票据被篡改或满员直接拒；sim 线程只 drain() 事件、不碰 socket。
  判据：[match] accepted 1 slot 0 ticket 1 resume 1 tamper 1 full 1 events 1。
  已接 net_loop：`--serve` 起信令（UDP = 信令端口 + 1），`--join` 先 match_request 再连 UDP，
  只放行信令通过过的 client_id。判据：tools/net_selftest.ps1 joins=2 accepted≈416 far=0 moved≈46.9。
  也已接 bridge/Godot：菜单填的是信令端口，房主留 slot 0。
- 掉线/复用：Link.last_rx_tick 记录最后收包 tick；step() 开头 320 tick（5 s）无包则 remove_client；
  add_client(..., -1) 复用第一个 inactive 槽位（满员返回 -1），重连回原槽。
  **重连早于超时**也安全：add_client 发现同槽已有 link 时用新 link 顶替（同槽不允许两个写者），
  玩家状态保持 active、不需要等 5 s。
  判据：[timeout] joined 1 dropped 1 reuse 1；[reconnect] first 0 links 1 active 1。
- 槽位激活：PlayerState.active → 快照 flags bit2；服务器 init 全 false、add_client true、
  remove_client false；桥接层跳过未激活槽位（否则 host 会看到没加入的幽灵）。
- 远端朝向：PlayerState.yaw 由 pmove 写入；PlayerSnap 的 kFieldYaw 独立字段组；协议版本 4（R9 起快照带 wall_normal/vault_dir/vault_rise/slow_ticks）。
  GDScript 用 rotation.y = atan2(-cos(yaw), sin(yaw))，插值器取前一帧 yaw。
  判据：[yaw] full 1 delta 1；host 看到 remote yaw 0.0→0.9。
- 判据：30% 丢包 2000 tick 回滚 51（曾 1428）；5% 丢包 60 s 位置修正 0 次；
  中途加入（tick 202）与重连（tick 701）偏差 0.00000 m；参数哈希不同被拒绝；
  远端缓冲 32 帧、newest == 最后一份快照。

### 8.5 战斗（sim/game/combat.*）

- HP 100；护甲 25 / 25 再生 / 50；吸收 66%（**提案**）。
- 再生甲：5 s 无伤害后开始，10 s 回满（**提案**）。
- 伤害用整数运算；apply_damage 返回实际扣血。

### 8.6 demo（sim/demo/*，docs/demo-format.md）

- 头部含 build_id / sim 版本 / params_hash / geometry_hash / 地图 / 初始状态；
  每 tick 存输入 + 世界哈希；约 28 B/tick。
- --record / --play / --dump / --compare；--test-demo 验证分叉能被定位。

### 8.7 hitbox 历史（sim/game/hitbox.*）

- 每 tick 一条 HitboxSample（脚点 / 高 / 半径 / 蹲），64 帧环形；服务器与客户端都记。
- 三段按高度比例：>0.82 头 ×5、>0.45 胸 ×1、其余腿 ×0.7；蹲下高度变，分档自动跟随。
- 回溯取相邻两帧按 frac 插值（v0.4 §9.2），不取整帧；没有下一帧就不外推。
- 判据：--test-hitbox（25 项）；--test-net 顺带断言服务器每 tick 一条、newest == tick。

### 8.8 事件通道（sim/core/event.*）

- `SimEvent`：tick + type + entity + a/b + param；**绝不承载权威数据**（伤害只走快照）。
- `EventQueue`：1024 条固定环，满了丢最旧并计数，零分配。
- 生产：`World::step(inputs, dt, &events)` 在起跳/落地沿发射；不传 sink 时零开销。
- 消费：bridge `drain_events()` 每帧取空；Godot 侧只更新 HUD/音效，不反向查询。
- 判据：--test-events（跳跃 tick 5、落地 tick 47、两次运行一致、溢出 76 条丢最旧）。

### 8.9 构建与桥（tools/build*.ps1、bridge/、godot/）

- 两个构建入口共享 **tools/sim_sources.txt**（check_sources.ps1 卡住）：CMake 出 headless_sim，
  build_bridge.ps1 出 GDExtension。加 sim .cpp 只改这一个清单。
- **标志必须逐条一致**：`-O2 -DNDEBUG -ffp-contract=off -Wall -Wextra -Werror`。
  桥用 `-isystem` 引 godot-cpp 头，第三方告警进不了我们的 -Werror；缺 `-ffp-contract=off`
  会让 DLL 与 exe 的浮点结果悄悄分叉（教训 12）。
- **版本号唯一来源是 sim/core/version.h**。CMake 只注入 BP_BUILD_ID（git short hash），
  桥用同一条命令注入同一个值；桥暴露 `get_build_id()/get_protocol_version()`，Godot 启动打
  `breakpoint: build <id> protocol 4`——两台机器的日志一比就知道是不是同一个构建。
- **breakpoint.gdextension 必须同时有 `windows.editor.x86_64`**：我们用的是 release 编辑器
  二进制跑 headless，它只认 editor key；只写 `windows.debug.*` 时扩展根本不加载，
  报错还会说是「找不到 windows.x86_64 的库」（教训 13）。
- 工具链目录用无版本名（mingw64/、cmake/、ninja/），只有 setup_toolchain.ps1 知道下载地址与
  版本；它用 Expand-Archive，不依赖 7z（教训 14）。
- Godot 可执行文件路径与首跑导入只在 tools/godot_env.ps1；桥的加载检查是第 17 项闸门
  （tools/godot_check.ps1，退出码 3 = 跳过）。

---

## 9. 开场 / 收尾协议

**开场（每次新会话）**

```
1. 读 docs/AGENTS.md（本文件）
2. 读 docs/PROGRESS.md 最后一节 + docs/HANDOFF.md（若存在）
3. 跑 tools\build.ps1，再跑 tools\smoke.ps1，确认 17 项行为闸门 + bench 全绿
4. 只改一个文件集；大文件只做精确 edit
5. 改完立刻 commit（一个功能一个 commit）
6. 更新 docs/PROGRESS.md（做了什么/结论/下一步）
```

**收尾（会话结束前）**

- 基线必须绿：smoke 全过、git 工作树干净（或明确写出未提交原因）。
- PROGRESS 追加一节；踩过的坑进 pitfalls/；架构决定进 decisions/。
- 若上下文可能丢失：更新本文件的「未结案」和「下一步」。

---

## 10. 已否决路线（不要再走）

| 方案 | 否决原因 |
|---|---|
| 自写 Win32/GDI 渲染器当 demo | 违背 §3.1「Godot 作宿主」；已删除（PROGRESS S06） |
| Godot 4.7 编辑器 + godot-cpp 4.5 绑定 | API 版本不匹配，加载失败 |
| 按住空格连跳 | 输入语义错误，服务端也会跟着错；已改为按下沿 |
| Q 键一键承载墙跑/爬墙/翻越 | 计划书 §2.2 已否决 |
| 可靠 UDP 传快照 | head-of-line blocking |
| 用结构体 memcpy 当网络格式 | padding/endian 不可控；已改显式小端读写 |
| 把缺 baseline 的 delta 当全量解 | 缺字段会解成 0；现在直接丢弃并等全量 |

---

## 11. 下一步（严格按计划书顺序，不要跳）

1. **M6 射击**：WeaponDef(toml) → 开火输入 → hitscan + hitbox 回溯 → 三段伤害
   + hp/armor 进快照 + 死亡/重生 + 穿透/衰减；TTK 用 100 HP / 三甲复算。
2. **M7 / M8**：动画、视角模型、音频垂直定位（§9.2 的楼层低通）。
3. 代码体检：A1–A7、AUD-01…19（AUD-16 除外）、R1–R19 已完成；只剩 AUD-16（hp/armor 还是常量，M6 接快照时一并解决）见 docs/AUDIT.md。

---

## 12. 上下文丢失后的第一件事

1. `git log --oneline -5` 看最后几个提交。
2. `git status` 看有没有未提交的半成品。
3. 跑 smoke；**红的那一项就是当前战场**。
4. 读 PROGRESS 末尾的**详细开发日志**（2026-10-20 起）与 HANDOFF.md。
5. 不要从头重做已完成的东西；本文件 §0.2 就是进度表。

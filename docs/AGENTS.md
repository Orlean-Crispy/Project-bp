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
| M5 网络 | 🔶 部分 | 不可靠通道 + delta 快照 + 预测回滚 + **§7.0 时间对齐** + **v0.4 §5.2 输入去重**已通；**§7.3 hitbox 历史、§7.7 重连、§7.1 插值、A1/A2/B1 未做** |
| M6 射击 | 🔶 起步 | HP/护甲规则已实现；hitscan、后坐力表、三段判定、穿透未做 |
| M7 打磨 / M8 视角模型 | ❌ | 未开始 |

### 0.3 现在能跑什么

- `headless_sim` 14 项行为闸门 + bench 全绿（见 §3）
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

- 跨 GDExtension 只有两条通道：状态流与事件通道（事件通道**尚未实现**，见未结案）。
- 表现层不反向查询 sim；伤害数字只走快照，不走事件。
- 判据：bridge/ 内不出现轮询式反向查询。

### D — 网络（2026-10-18，代价 = head-of-line blocking）

- 快照与输入只走**不可靠通道**；delta 的 baseline = 客户端最近 ack 的快照；
  连续 3 个未 ack → 强制全量。
- 输入按 v0.4 §5.2 去重：`tick <= 已执行` 丢弃；窗口 [last+1, last+8]；
  队列 >4 丢最旧；迟到输入**不补执行**（客户端已预测，下次快照自然回正）。
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
| 头文件 | #pragma once；sim 头文件不得 include 任何 Godot 头 |
| 浮点 | float 为模拟主类型；禁止隐式 double |
| 路径 | 工程内引用用相对路径；**链接器输出必须是相对路径**；文件打开必须 std::filesystem::u8path |
| 脚本 | .ps1 只写 ASCII（PowerShell 5.1 按 GBK 读无 BOM 文件） |
| 字符串 | 磁盘格式用 UTF-8；Windows 文件 API 必须走 u8path |

---

## 3. 判据清单（tools/smoke.ps1 串起 14 项行为闸门 + bench）

| # | 判据 | 阈值 | 命令 |
|---|---|---|---|
| 1 | 地图可解析 | 实体/brush 数正确、边界为米 | --info |
| 2 | 确定性 | 同输入两次逐 tick 哈希一致 | --selftest --ticks 600 |
| 3 | 穿墙 | 8.13–30 m/s ×100 次，0 穿透 | --test-wall |
| 4 | 扫掠 | 点/盒单段跨 0.1 m 墙必命中 | --test-ray |
| 5 | 台阶/蹲下 | 0.20/0.35/0.45 可上、0.55 挡住、1.0 m 净高不能站 | --test-step |
| 6 | 跳跃语义 | 按住 300 tick 只跳 1 次；点按 3 次跳 3 次 | --test-jump |
| 7 | HP/护甲 | 规则与 TTK 落在 0.6–1.2 s | --test-health |
| 8 | 网络收敛 + 去重 | 30% 丢包偏差 < 0.01 m、回滚 < 500；5% 丢包 60 s 位置修正 < 3 次（N3） | --test-net |
| 9 | 卡死恢复 | 从 brush 内部出生能自行脱困 | --test-stuck |
| 10 | 乱按压力 | 6000 tick 乱按，最长连续实心 ≤ 2 | --test-soak |
| 11 | 平台边缘 | 落到边缘后仍能继续走 | --test-edge |
| 12 | parkour | slide/dash/wallrun/wallclimb/vault 全部生效 | --test-parkour |
| 13 | 键位 | 加载/改键/保存/重载 + 冲突检测 | --test-bindings |
| 14 | demo | 同构建逐 tick 一致；改动参数必被检出 | --test-demo |

当前全部 PASS。`--compare`（跨机 dump 比对）需要第二台机器，尚未执行。

---

## 4. 未结案（按优先级，每条必须有 lead）

1. **hitbox 历史环形缓冲（§7.3）** — 计划书要求「第一天就建」，目前**没有**。
   64 tick × 1 s；v0.4 §9.2 要求命中回溯按相邻两帧插值，不能只取整帧。lead：M5 结束前。
2. **full_state / 重连 / 中途加入（§7.7）** — 未做。lead：M5。
3. **其他玩家 1 tick 插值（§7.1）** — 未做，目前只预测自己。lead：M5。
4. **v0.4 的 M5 增量** — B1 快照分片（MTU 1200）、A1 信令、A2 可靠流 + configstring
   分块、A5 恢复；v0.4 §12 估算 +6.5 天。lead：§7.7 之后。
5. **M6 射击** — hitscan、后坐力表、三段部位判定、穿透、伤害衰减、hitmarker 全未做。
   护甲的**吸收比例 66%** 与**再生 5 s / 10 s** 是提案值（不在计划书里），需要确认。
6. **Godot 版本** — 计划书写 4.7 Mono，但 godot-cpp 最高只有 4.5，现用 4.5。
   lead：M7（Godot-Locomotion 绑定 4.7.2）之前必须解决。
7. **跨机 demo 比对** — 工具与流程就绪（docs/demo-format.md），缺第二台机器。
8. **游戏内改键 UI** — 键位系统已数据驱动，UI 未做。lead：M5 之后。
9. **穿透两套模型** — .map 的 _penetrable 与武器 penetration_count 谁优先未定。lead：M6。
10. **事件通道（§3.4）** — 计划书要求的状态流 + 事件通道，事件通道未实现。lead：M5/M6。
11. **movement.toml 热重载** — 铁律 I 只做了哈希，没有 reload 路径。lead：M6 前。

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
powershell -File tools\setup_toolchain.ps1   # 仅首次：下载 GCC/CMake/Ninja
powershell -File tools\build.ps1             # 配置 + 全量构建（headless_sim, move_metrics）
powershell -File tools\smoke.ps1             # 14 项行为闸门 + bench，必须全绿
powershell -File tools\build_bridge.ps1      # 重建 GDExtension DLL
powershell -File tools\run_demo.ps1          # 构建桥 + 启动 Godot demo
```

godot-cpp 需要重建时：

```powershell
cd C:\bp-toolchain\godot-cpp
python -m SCons platform=windows use_mingw=yes target=template_debug -j8
```

Godot 首次导入（没有 godot/.godot 时）：

```powershell
C:\bp-toolchain\godot\Godot_v4.5-stable_win64.exe --headless --editor --path godot --quit
```

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
tools/           headless_sim（所有闸门）、move_metrics、build/smoke/build_bridge/run_demo/setup 脚本
docs/            AGENTS.md（本文件）、PROGRESS.md、HANDOFF.md、pitfalls/、decisions/、keymap.md、demo-format.md、计划书_v0.3.md
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

### 8.2 碰撞（sim/collision/*）

- 扫掠 = Minkowski 展开（支撑距离取 **min**）+ 分步（step = 最小墙厚），bumps ≤ 4。
- 进入分数钳到 0（接触 epsilon 的坑）。
- 落地不接受实心点；pmove 每 tick 检查实心并向上/四向脱离。

### 8.3 键位（sim/input/*）

- 默认瓦洛兰特布局：C 翻越 / Q 墙跑 / E 爬墙 / X 冲刺；Shift 滑铲、Ctrl 蹲。
- 改 sim/data/keybinds.toml 即可；Bindings 支持运行时 set_key/save。
- Godot 侧从 sim.get_bindings() 生成 InputMap（单一数据源）。

### 8.4 网络（sim/net/*）

- 包头：type/seq/ack/ack_bits；快照 delta 以「客户端最近 ack 的快照」为 baseline。
- 客户端：本地预测 + 128 tick 历史 + 回滚重放；命令包带最近 4 个输入。
- 时间轴：客户端 = 服务器 tick + 4（单程 30 ms ≈ 2 tick + 2 tick 前瞻）；初始化时
  预跑 4 个空 tick，复刻服务器在首个命令到达前的状态。
- 服务器：InputWindow 只接受 [last+1, last+8]；重复丢弃不记日志；迟到丢弃并只计一次；
  队列 >4 丢最旧；tick T 没有输入就沿用上一个。
- 判据：30% 丢包 2000 tick 回滚 185（曾 1428）；5% 丢包 60 s 位置修正 0 次。

### 8.5 战斗（sim/game/combat.*）

- HP 100；护甲 25 / 25 再生 / 50；吸收 66%（**提案**）。
- 再生甲：5 s 无伤害后开始，10 s 回满（**提案**）。
- 伤害用整数运算；apply_damage 返回实际扣血。

### 8.6 demo（sim/demo/*，docs/demo-format.md）

- 头部含 build_id / sim 版本 / params_hash / geometry_hash / 地图 / 初始状态；
  每 tick 存输入 + 世界哈希；约 28 B/tick。
- --record / --play / --dump / --compare；--test-demo 验证分叉能被定位。

---

## 9. 开场 / 收尾协议

**开场（每次新会话）**

```
1. 读 docs/AGENTS.md（本文件）
2. 读 docs/PROGRESS.md 最后一节 + docs/HANDOFF.md（若存在）
3. 跑 tools\build.ps1，再跑 tools\smoke.ps1，确认 14 项行为闸门 + bench 全绿
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

1. **§7.3 hitbox 历史**：64 tick 环形缓冲 + 相邻帧插值 + 服务器回溯接口（为 M6 铺路）。
2. **§7.7 full_state / 重连 / 中途加入**：独立消息，不走 delta；中途加入观战到下一回合。
3. **§7.1 其他玩家 1 tick 插值**：表现层，不改 sim 语义。
4. **v0.4 M5 增量**：B1 分片 → A1 信令 → A2 可靠流 → A5 恢复（按 v0.4 §12 的依赖顺序）。
5. **M6 射击**：hitscan + 后坐力表 + 三段部位判定（×4 头 / ×1 胸 / ×0.7 腿，爆头按 §6.10 取 ×5）
   + 穿透 + 伤害衰减；TTK 用 100 HP / 三甲复算。
6. **M7 / M8**：动画、视角模型、音频垂直定位（§9.2 的楼层低通）。

---

## 12. 上下文丢失后的第一件事

1. `git log --oneline -5` 看最后几个提交。
2. `git status` 看有没有未提交的半成品。
3. 跑 smoke；**红的那一项就是当前战场**。
4. 读 PROGRESS 最后一节（S10 护甲 / S11 网络时间对齐）与 HANDOFF.md。
5. 不要从头重做已完成的东西；本文件 §0.2 就是进度表。

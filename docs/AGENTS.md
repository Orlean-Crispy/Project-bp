# AGENTS.md — Project Breakpoint

> 本文件是**约束**，不是进度。进度写 `docs/PROGRESS.md`，事故写 `docs/pitfalls/`。
> 硬上限 64 KB；**加内容之前先问一句：三个月后还成立吗？**

## 0. 一句话

5v5、有爆破目标的 Apex 式自由移动 FPS。**移动按 Apex，判定按 CS。**
Godot 4.7 宿主 + 自研 C++17 模拟层（无 Godot 依赖），64 tick，目标局域网。

顶层文档：`docs/计划书_v0.3.md`（设计定稿、技术待验证）。

## 1. 铁律

> 只在**付过代价**时新增。格式：领域 / 日期 / 代价 / 判据。被推翻的直接删，不留两版。

### A — 单位制（2026-10-18 立，代价 = 「手感不对但查不出」）

- 模拟层内部**只用米、秒、m/s、m/s²**。
- Quake unit 只在 `.map` 解析入口乘一次 `0.0254`；`map_parse.cpp` 是唯一转换点。
- 禁止在任何函数参数、结构体字段、配置里出现 `u/s`、`unit`。
- 🔴 反例：v0.1 的 §5.6 用 320 u/s、§8.4 用 40 m、§6.5 用 400 m/s，三套并存且没人负责换算。一个环节漏乘就是「56 米高的玩家」，表现是手感不对，不是崩溃。
- 判据：`grep -rni "u/s\|quake unit" sim/ tools/` 只应命中 `map_parse.cpp` 的换算注释。

### B — 确定性（2026-10-18 立，代价 = 回滚/重放失效，demo 报废）

- 固定步长、固定子步数；**不用迭代求解器**；状态切换用**整数 tick 计数**，不用 float 累加。
- 实体存稠密数组 + 空闲链表；ID = 低 24 位索引 + 高 8 位世代号；序列化按 ID 升序。
- **禁止**按指针哈希的容器；**禁止** `rand()`/`std::mt19937`/任何带状态 RNG。
- 随机一律走 `rng_at(tick, entity, index, salt)`（无状态计数式）。
- 不开 `-ffast-math`。
- 🔴 反例：float 累加计时在 64 Hz 下每小时漂移数百 ms；共享 RNG 状态导致每次重放结果不同。
- 判据：`headless_sim --selftest` 同输入序列跑两次，逐 tick 世界哈希完全一致。

### C — 边界（2026-10-18 立，代价 = 表现层轮询漏帧 + 回滚错乱）

- 跨 GDExtension 只有两条通道：**状态流**（每帧扁平数组）与**事件通道**（`SimEvent` 环形缓冲，带 tick）。
- 表现层不反向查询 sim；事件不承载权威数据（伤害只走快照）。
- 🔴 反例：只做状态流时，枪口火焰/抛壳/脚步只能靠轮询，回滚时会把「被否决的预测」也播一遍。
- 判据：`grep -rn "get_state_for_ui\|sim->query" bridge/` 为空。

### D — 网络（2026-10-18 立，代价 = head-of-line blocking）

- 快照与输入只走**不可靠 UDP**；可靠流会阻塞后续快照。
- delta 的 baseline = 客户端最近一次 ack；连续丢 3 个快照强制全量。
- 客户端只预测枪口位移/后坐力/音效/抛壳，**不预测伤害与 hitmarker**。
- 判据：`netchan` 单测里「丢包 30% 下无阻塞、无解包失败」。

### E — 性能（2026-10-18 立，代价 = tick 累积 → 人物瞬移）

- 稳态每帧堆分配 = 0；容器预分配。
- 单 tick 模拟 p99 < 4 ms（64 Hz 预算 15.625 ms）。进 CI。
- 判据：`headless_sim --bench 1000` 输出 p99；Tracy 抓 sim 区间。

### F — 协作（2026-10-18 立，代价 = 并行改动互相覆盖）

- 一个功能一个 commit；同一文件的多处修改**串行**完成（一次读、一次改、一次写回）。
- 并行只允许在**文件集两两不相交**时进行。
- 大文件禁止整文件重写，只做精确 edit。
- 判据：`git log --stat` 单个 commit 不跨无关模块。

### G — 凭据（2026-10-18 立，代价 = 公开仓库泄漏）

- 密钥不进仓库（含「先提交回头再删」，历史 blob 清不掉）。只从环境变量读，文档写 `<YOUR_KEY>`。

### H — 内容许可（2026-10-18 立，代价 = 素材混入不可用授权）

- 每份素材进仓库前登记：`docs/assets-licenses.md`（名称 / 来源 URL / 许可 / 是否可商用）。
- CC BY-NC-ND 的资产（如 TacZ 模型）**不得进入构建产物**，只能本地自用。

### I — 手感数值（2026-10-18 立，代价 = 调参要重编，等于每天重开盲盒）

- 影响手感的数值一律在 `sim/data/*.toml`，运行时可重载，不重编不重启。
- **但**：参与模拟的数值集必须计入世界哈希（`params_hash`），否则客户端/服务器用不同数值静默分叉。
- 判据：改 `movement.toml` 不重启即生效；两侧 `params_hash` 不一致时日志 ERROR 并拒绝入局。

## 2. 代码约定

| 项 | 规定 |
|---|---|
| 语言 | C++17；sim 层零第三方依赖（仅标准库） |
| 编译器 | MinGW-w64 GCC 16.2，**固定装在 `C:\bp-toolchain`**（见 §7 与 pitfalls/toolchain-nonascii.md） |
| 警告 | `-Wall -Wextra -Werror`，全量构建才算数（增量缓存不可信） |
| 命名 | 类型 `PascalCase`，函数 `snake_case`，成员 `m_` 前缀，常量 `kPascalCase` |
| 头文件 | `#pragma once`；sim 头文件不得 include 任何 Godot 头 |
| 浮点 | `float` 为模拟主类型；禁止隐式 double（`-Wdouble-promotion` 复查） |
| 路径 | 工程内引用一律用相对路径，不自设绝对路径；**脚本（.ps1）只写 ASCII**（PS 5.1 按 GBK 读无 BOM 文件，中文行尾会吃掉下一行） |

## 3. 判据清单

| 判据 | 阈值 | 工具 |
|---|---|---|
| 确定性 | 同输入两次逐 tick 哈希一致 | `headless_sim --selftest` |
| 穿墙 | 20 m/s 正面撞 0.1 m 墙 ×100 次，0 次穿透 | `headless_sim --test-wall` |
| 单 tick 耗时 | p99 < 4 ms（1000 tick） | `headless_sim --bench` |
| 零警告 | 全量构建 `-Werror` 通过 | `tools/build.ps1` |
| 冒烟 | 不崩 + 能读图 + 能跑 600 tick | `tools/smoke.ps1` |
| 手感 | 单跳高/单跳远/strafe 增益/0→max tick 数 | `move_metrics` CSV |

## 4. 未结案

> 按「值不值得下一轮动手」排序。每条必须有 lead；没有 lead 的不要瞎猜。

1. **Godot 4.7 / godot-cpp 未安装** — lead：M5 之前必须解决；当前只验证纯 C++ 模拟层（M0–M4 不需要引擎）。
2. **交叉编译器**：sim 用 MinGW GCC，godot-cpp 官方支持 MSVC。lead：M5 前二选一（装 VS Build Tools，或验证 mingw 构建 godot-cpp）。
3. **HP / 护甲未定值** — 计划书只有「如 100」。lead：M6 前定，否则爆头 ×5 与 TTK 0.6–1.2 s 无法互相验证。
4. **键位表** — 已给出建议稿 `docs/keymap.md`（Q 墙跑 / E 爬墙 / V 翻越 / F 冲刺），**待试玩确认**。lead：M4 前。
5. **穿透两套模型** — `.map` 的 `_penetrable` 与 `penetration_count` 谁优先未定。lead：M6。
6. **中文本地路径** `G:\自制游戏` — 已定：编译可用，**链接不可用**，所以工具链必须在 ASCII 路径（见 §7）。任何新引入的工具（godot-cpp、TrenchBroom 的 CLI、Python 脚本）第一次接入都要先验「能不能在这个路径下跑」。
7. ~~缺少 step-up~~ — 已在 M2 完成（`--test-step`：0.20 / 0.35 / 三级 0.15 台阶可上，0.55 m 挡住，蹲下空间有效）。

## 5. 教训（编号沿用，不复用）

> 每条只留可执行判据，案例进 `docs/pitfalls/`。

| # | 判据 |
|---|---|
| 1 | 盒体扫掠的支撑距离用 **min**（相交）不是 max（包含）；`--test-wall` 出现任何 penetration 即为红。案例：`pitfalls/minkowski-support.md` |
| 2 | 工具链必须在物理 ASCII 路径；junction/subst/8.3 都不能绕过。案例：`pitfalls/toolchain-nonascii.md` |
| 3 | 会跑一次的测试不算测试：穿墙类判据至少 100 次 × 5 个速度档。 |
| 4 | 碰撞判据必须成对：**不能穿**（100 次）与**不能卡**（贴墙静止 / 沿墙滑动）同时验。案例：`pitfalls/contact-epsilon.md` |
| 5 | 「面前有没有东西」的探测一律**从后退 5 cm 处起扫**；投影/裁剪后的速度不能当探测方向。 |

## 6. 开场协议

```
1. 读 docs/AGENTS.md（本文件）
2. 读 docs/PROGRESS.md 最后一节
3. 跑 tools/build.ps1 + tools/smoke.ps1，确认基线是绿的
4. 只改一个文件集
5. 改完立刻 commit，一个功能一个 commit
6. 更新 docs/PROGRESS.md
```

> 第 3 步不能省：基线本来就是红的时候，你会把别人的 bug 当成自己改坏的。

## 7. 环境事实（本机，2026-10-18 实测）

| 项 | 值 |
|---|---|
| 工作区 | `G:\自制游戏`（**中文路径**，编译可以，链接不行） |
| 工具链 | `C:\bp-toolchain`：mingw64 / cmake-4.4.4-windows-x86_64 / ninja |
| 重建工具链 | `powershell -File tools\setup_toolchain.ps1` |
| 构建 | `powershell -File tools\build.ps1`（输出在 `build/`） |
| 冒烟 | `powershell -File tools\smoke.ps1`（info / determinism / wall / ray / step / parkour / bench） |
| Shell | Windows PowerShell **5.1**，不是 pwsh 7 |
| 运行时 | exe 静态链接 libgcc/libstdc++，不需要把 mingw 加进 PATH |

> 工作区里的 `.toolchain/` 只是下载缓存，**不要**用它来编译（ld 会失败）。

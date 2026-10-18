# PROGRESS — Project Breakpoint

> 进度、日志、交接。**不设上限**，但每节只留「做了什么 / 结论 / 下一步」。
> 约束在 `docs/AGENTS.md`。

---

## 2026-10-18 · S01 · 环境打通 + M1 骨架

### 背景

工作区 `G:\自制游戏` 原本为空。首轮会话发现命令通道整体失效（`pwsh` 每次调用
`0xC0000142`，Node 起进程 EPERM），且机器上没有任何 C++ 工具链。重启 DSH 后恢复。

### 环境事实（实测）

| 项 | 结果 |
|---|---|
| PowerShell | **5.1**（不是 7）→ .ps1 必须 ASCII |
| Git | `C:\Users\31148\scoop\apps\git\current\cmd\git.exe` |
| Python / Node / VS Code | 已装（3.14 / `C:\Program Files\nodejs` / 已装） |
| CMake / Ninja / 编译器 / MSVC | 原本全部缺失 |
| 网络 | GitHub、cmake.org 可访问 |
| 磁盘 | C: 54 GB，G: 243 GB 空闲 |

### 做了什么

1. **便携工具链**：MinGW-w64 GCC 16.2 + CMake 4.4.4 + Ninja 1.13.2，**装在 `C:\bp-toolchain`**
   （不在工作区，原因见下）。`tools/setup_toolchain.ps1` 可一键重建。
2. **M1 代码**：`.map` 解析（唯一单位转换点）、brush 凸体构建、盒体扫掠 trace、
   分步 slide_move、基础 pmove（地面/空中/跳跃/蹲）、确定性哈希、无状态 RNG。
3. **工具**：`headless_sim`（`--info / --selftest / --test-wall / --test-ray / --bench`）、
   `move_metrics`（四张 CSV 表）、`tools/build.ps1`、`tools/smoke.ps1`。
4. **测试地图**：`test_box.map`（20×20 m，含 0.1 m 薄墙与 0.4 m 台）、
   `metrics_flat.map`（60×60 m 空房，专供度量，避免墙污染数据）。

### 当前验收状态

| 判据 | 结果 |
|---|---|
| 全量构建 `-Wall -Wextra -Werror` | **通过**，零警告 |
| `--selftest` 600 tick 双跑 | **PASS**，逐 tick 哈希一致 |
| `--test-wall` 8.13 / 15.24 / 20 / 25 / 30 m/s × 100 次 | **PASS**，0 次穿透，0.1 m 墙前停住 x=-0.456 |
| `--test-ray` 点扫掠 / 7.8 m 单段 / 盒体单段 | **PASS**，薄几何不会被跨过 |
| `--bench` 1000 tick | mean 0.18 µs，p99 0.30 µs（预算 4000 µs） |
| `move_metrics` | 跳高 1.106 m，跳远 5.335 m，空中 strafe 增益 1.04 m/s，0→99% 用时 10 tick |

### 两个真实踩到的坑（已入 `docs/pitfalls/`）

1. **工具链不能在中文路径下链接**：GCC 能编译，但 ld 把 `G:\自制游戏` 按 GBK 解码后
   找不到自己的 crt/lib，报一堆 `cannot find -lstdc++`。junction 与 subst 都会被内核
   还原成真实路径，**只有物理 ASCII 路径有效**。→ 工具链放 `C:\bp-toolchain`。
2. **Minkowski 支撑方向反了**：盒体扫掠的扩张平面要用**最小**支撑距离；用了最大，
   等价于「整个盒体在凸体内侧」，结果是 100/100 次直接穿过墙。见 `pitfalls/minkowski-support.md`。

### 与计划书不一致的地方（需要你知道）

- **§5.9 的验收判据测错了对象**：玩家盒宽 0.81 m，要穿过 0.1 m 墙，中心得一次跨过
  0.91 m，而 20 m/s 每 tick 只有 0.31 m。**真正的穿墙风险在弹道/hitscan（B2）**，
  那里一 tick 走 7.8 m。已补 `--test-ray` 覆盖；玩家侧的验收仍然保留（它防的是拐角挤穿）。
- **§5.8 的「strafe 增益」不能在地面测**：地面加速的上限就是 `max_speed`，地面 strafe
  的增益恒为 0（实测 0.0000）。改成**空中 strafe（跳跃 + 前右 + 转视角）**，当前 1.04 m/s。

### 基线提交

`6b99597 M1: map parsing, brush sweep, deterministic sim, smoke gates`
提交后删除 `build/` 做了**冷构建**，再跑 `tools/smoke.ps1`，全绿。

### 下一步

- [ ] M2：stair step-up（0.46 m 台阶），现在 0.4 m 平台是走不上去的
- [ ] M2：`move_metrics` 对照 Q3 数值（跳高 1.16 m 附近、0→max 约 8 tick）调 `accel/friction`
- [ ] M2：crouch 站立时的头部空间检查（现在无条件起蹲）
- [ ] M3：parkour 五件套 + 键位表（未结案 #4）
---

## 2026-10-18 · S02 · M2 移动基础（step-up / 蹲下空间 / 基准数值）

### 做了什么

1. **step-up**（Q3 `PM_StepSlideMove` 的思路）：平走被挡 → 抬高 `step_height` → 重走 → 落下。
   只有「落下点在可站立面（法线 z > 0.7）且水平位移确实增加」才采用，所以墙不会因为
   「抬一下再走」被爬上去。
2. **蹲下空间检查**：想站起来时先用站姿盒做零长度 trace，被挡就继续蹲着。
3. **测试地图** `step_test.map`：五条平行通道（0.20 m 台阶 / 0.35 m 台阶 / 0.55 m 挡墙 /
   三级 0.15 m 楼梯 / 1.0 m 净高天花板），每条台阶通道后面有挡墙，否则被测玩家会跑过
   台阶落回地面——第一版地图就是这么骗过测试的。
4. `--test-step` 五项判据 + `--test-wall` 增加「贴墙静止」与「沿墙滑动」检查。

### 当前数值（move_metrics，对照 Q3 320 u/s）

| 指标 | 本项目 | Q3 参照 | 说明 |
|---|---|---|---|
| 跳高 | 1.106 m | 理论 1.157 m（270 u/s / 800 u/s²） | 差 4.4%，来自 64 Hz 半隐式欧拉的离散化，不是参数错 |
| 跳远 | 5.335 m | 理论 ~5.49 m（滞空 0.675 s × 8.13 m/s） | 同上 |
| 空中 strafe 增益 | 1.04 m/s | CPM 更高 | 由 `air_accel = 1.0` 决定，Apex 取向 |
| 0→99% 最高速 | 10 tick | Q3 约 10 tick | 与 Q3 一致 |

### 又一次踩坑（已入 pitfalls/contact-epsilon.md）

修完扫掠方向后，贴墙静止时每 tick 向前挤 **19 mm**、台阶前原地蹭。
根因是进入分数 `(d1 - eps)/(d1-d2)` 在 `d1 == eps` 时得到 -1e-9，被 `if (enter < 0) continue`
当成「没碰到」丢掉。钳到 0 后修复；但**钳到 0 会顺带把沿墙滑动钳死**，所以同时加了
45° 撞墙的滑动检查（实测 20 tick 沿墙 1.875 m）。

### 验收状态

| 判据 | 结果 |
|---|---|
| `--test-step` | 0.20 / 0.35 / 0.45（三级楼梯）都能上；0.55 m 挡墙能挡；1.0 m 净高下站不起来 |
| `--test-wall` | 5 个速度档 0 穿透；贴墙静止不动；沿墙滑动 1.875 m |
| `--selftest` / `--test-ray` / `--bench` | 仍全绿（bench mean 0.32 µs） |

### 下一步（M3）

- [ ] parkour 五件套：slide / wallrun / wallclimb / vault / dash + impulse 资源
- [ ] **键位表**（未结案 #4）——M3 的验收「按键分离验证」没有它无法执行
- [ ] `movement.toml` 热重载（铁律 I 目前只有哈希，还没有 reload 路径）
---

## 2026-10-18 · S03 · M3 parkour 五件套（首版）

### 做了什么

| 能力 | 触发 | 实现要点 | 验收（`--test-parkour`） |
|---|---|---|---|
| **滑铲** | SHIFT（地面、≥5.08 m/s、消耗 1 次） | 蹲姿盒 + 摩擦 ×0.35 + 入场 ×1.15，松开 8 tick 后结束 | 站着被 1.0 m 低顶挡住；滑铲通过并在下面保持蹲姿 |
| **冲刺** | F（1 s 冷却，不耗次数） | 在摩擦/加速**之后**施加，否则同 tick 摩擦吃掉 9% | 峰值 11.43 m/s vs 8.13，位移 1.44 m vs 1.07 m |
| **墙跑** | Q（空中、≥6.35 m/s、`_wallrunnable`） | 重力 ×0.35，速度投影到墙面，沿墙法线反向探测维持 | 50 tick 后 z=1.79 m（自由落体为 0） |
| **爬墙** | E（`_climbable`） | 速度 = (0,0,4.57)，最长 0.7 s，可在地面起手 | 40 tick 后 z=2.86 m |
| **翻越** | V（面前 0.46–1.22 m 台沿、消耗 1 次） | 探测「面前有立面 + 越过立面 0.6 m 处的台面高度 + 台上有站立空间」，0.35 s 内固定前进+上升 | 跳过 1.0 m 平台，落点 z=1.00 |

时间状态全部用**整数 tick**（`state_ticks`），没有 float 累加；能力用 enum 状态机表示，
可直接进 `MoveSnapshot`。

### 又一次踩坑（已记入 AGENTS 教训 5）

墙跑第一版**一帧就掉**：速度被投影到墙面之后变成与墙平行，而「是否还贴着墙」的探测
仍沿着**速度方向**做 → 永远打不中。改成沿保存的墙面法线反向探测，并加 0.2 m/s 的内向
分量维持接触。

同一类问题在翻越上又出现一次：接触瞬间的 trace 因为 epsilon 判成「没碰到」，
所以所有「面前有没有东西」的探测都必须**从稍微靠后的位置起扫**。

### 与计划书的偏差

- 计划书 §5.2 的「滑铲必须落地瞬间触发」被放宽为「地面 + 速度达标即可」。
  落地窗口（`entry_window`）已进配置但还没用上——等跳跃链稳定后再收紧。
- 二段跳（impulse jump）、Impulse Pound 仍是空白；等资源模型和地图反推数值后一起做。

### 下一步

- [ ] 键位表试玩确认（`docs/keymap.md` 是建议稿）
- [ ] `movement.toml` 热重载（铁律 I 目前只有哈希，没有 reload 路径）
- [ ] `move_metrics` 增加滑铲距离、墙跑高度曲线，让 parkour 也能 diff
- [ ] M4：多机构建比对 + demo 回放
---

## 2026-10-18 · S04 · 键位系统（数据驱动 + 可改）

### 需求

「键位一定要能改，默认先用瓦洛兰特的技能键」。所以不是换几个常量，而是把键位从代码里拿出去。

### 做了什么

| 层 | 文件 | 作用 |
|---|---|---|
| 物理键 | `sim/input/keycodes.h/.cpp` | 约 55 个键的枚举 + 名字解析（大小写不敏感、忽略空格、`SHIFT`/`CTRL`/`ESC` 别名） |
| 动作映射 | `sim/input/bindings.h/.cpp` | `Bindings`：加载 / 查询 / 改键 / 保存 / 冲突检测 / 未绑定检测 |
| 默认数据 | `sim/data/keybinds.toml` | 瓦洛兰特布局：C 翻越 / Q 墙跑 / E 爬墙 / X 冲刺 |
| 转换 | `build_move_input()` | `KeyState + Bindings + yaw` → `MoveInput`，这是 sim 唯一能看到的东西 |

**关键约束**：`sim/` 内部（pmove、collision、net）不出现任何 `Key`/`Action`，
所以改键**不可能**影响确定性，也不需要进 `params_hash`。

### 验证（`--test-bindings`，进冒烟）

1. 14 个动作全部有键、无重复绑定；
2. Q/E/C/X 确实对应墙跑/爬墙/翻越/冲刺；
3. 把跳跃改到 `V` 后：`SPACE` 立刻失效、`V` 立刻生效；
4. 存盘 → 新对象加载 → 改动还在；
5. 故意把滑铲也绑到 Q，**必须**报冲突（否则冲突检测是恒假的）。

第 5 条是照着 AGENTS 教训「恒真的断言等于没写」加的：冲突检测如果没有一次会失败的
正对照，它通过一万次也说明不了什么。

### 踩坑

序列化写的是**显示名**（`WallRun`）而解析读的是 **TOML 键名**（`wallrun`），
大小写不一致 → 存盘再读回来静默退回默认值。修好后测试立刻能抓到（第 4 条）。

### 下一步

- [ ] 游戏内改键 UI（等 Godot 侧设置菜单，M5 之后）
- [ ] 鼠标灵敏度 / ADS 缩放分开配置（M6）
---

## 2026-10-18 · S05 · M4 demo 录制 / 回放 / 跨机比对

### 做了什么

| 组件 | 文件 | 说明 |
|---|---|---|
| 格式 | `sim/demo/demo.h/.cpp` | 二进制：头部（build_id / sim 版本 / params_hash / geometry_hash / 地图 / 初始状态）+ 每 tick 输入 + 每 tick 世界哈希 |
| 构建号 | `sim/core/version.h` + CMake | configure 时取 `git rev-parse --short HEAD` 注入 `BP_BUILD_ID` |
| 录制 | `--record <file>` | 把脚本输入跑一遍并落盘（约 28 B/tick） |
| 回放 | `--play <file>` | 重建世界、逐 tick 重算、比对哈希；头部不匹配只警告不拒绝 |
| 导出 | `--dump <csv>` | 逐 tick 位置/速度，供跨机比对 |
| 比对 | `--compare a.csv b.csv --tolerance 0.01` | 最大轴向偏差 |

### 验收（`--test-demo`）

| 检查 | 结果 |
|---|---|
| 同构建回放 400 tick | 全部命中 |
| 改 `accel` 后回放 | 被检测到（头部 params 不一致 + 位置偏差 0.0662 m） |
| 两个相同 dump | 偏差 0.000000 m |
| 扰动 dump | 偏差 > 0.01 m |

### 一个容易误判的细节

`state_hash` 里包含 `params_hash`，所以改参数后**第一个 tick** 就报分叉——
这会让「改了 movement.toml 能被抓出来」这条测试**恒真**。真正的物理验证靠
`--dump` + `--compare`（0.0662 m 那个数），哈希只负责「同一构建内逐 tick 一致」。

### 还没做的

- **跨机实测**：需要第二台机器跑同一个 demo。流程写在 `docs/demo-format.md`。
- 流式录制（现在整段缓存在内存）。
- 回放时的「快进 / 逐帧」交互（等有客户端以后）。

### 下一步（M5 网络）

- [ ] netchan：不可靠 UDP + 序列号 + delta 快照
- [ ] 时间基准（§7.0）：serverTime 估计、cmd.tick、时钟漂移
- [ ] 预测与本地移动状态回滚（§7.6）
- [ ] 这些都需要先有 Godot 宿主或至少两个进程——M4 的 demo 工具正好可以用来验证回滚重放的确定性
---

## 2026-10-18 · S06 · 接回 Godot（纠正一次路线错误）

### 我做错了什么

为了让「立刻能玩」，我写了 `tools/playtest`——Win32 + GDI 的软件渲染器。
这直接违背计划书 §3.1「Godot 4.7 作宿主」与 §4.1 的技术基座选型。**已删除该工具**，
记在这里当作否决项：任何绕过宿主的「临时渲染器」都不做，调试只能走 headless 或 Godot。

### 现在的链路

```
Godot 4.5（宿主：窗口/渲染/输入）
   ↑ GDScript: main.gd（只读输入、驱动 tick、摆相机）
   ↑ GDExtension（godot-cpp 4.5）
bridge/sim_bridge.cpp  ← 唯一的跨边界层
   ↑
sim/（纯 C++17，零 Godot 依赖，headless_sim 仍然独立可跑）
```

| 组件 | 位置 | 说明 |
|---|---|---|
| 桥 | `bridge/sim_bridge.cpp` | `SimWorld` 类：`load_map / load_bindings / set_input / step / get_*`，另把 brush 面片一次性导出成顶点/法线/顶点色/索引 |
| 工程 | `godot/` | `project.godot`、`main.tscn`、`main.gd`、`breakpoint.gdextension` |
| 构建 | `tools/build_bridge.ps1` | g++ 直接产出 `godot/bin/breakpoint_bridge.windows.template_debug.x86_64.dll` |
| 地图 | `sim/data/maps/playground.map` | 40×30 m，含台阶、1 m 平台、1 m 净高矮道、墙跑墙、爬墙墙 |

启动：

```powershell
powershell -File tools\build.ps1
powershell -File tools\build_bridge.ps1
C:\bp-toolchain\godot\Godot_v4.5-stable_win64.exe --path godot
```

### 为什么是 4.5 而不是计划书的 4.7

**godot-cpp 的最高分支只到 `godot-4.5-stable`**，没有 4.6/4.7 绑定。用 4.7 编辑器配 4.5 绑定
会在 API 哈希上出问题。所以先用 4.5 把链路打通；等 godot-cpp 跟进 4.7 再升级
（已记入 AGENTS 未结案）。

### 接入时踩的两个坑

1. **GDScript 传进来的路径是 UTF-8，`std::ifstream` 在 Windows 上按 ANSI 打开** →
   地图路径变成 `G:/????????????/...`，报 `cannot open`。修法：`std::filesystem::u8path`。
2. **DLL 缺 `libwinpthread-1.dll`**（Error 126）。`-static-libgcc -static-libstdc++` 不够，
   把 `libwinpthread-1.dll` 放到 Godot 可执行文件旁边即可。
3. 附带一个：**GNU ld 连输出文件都不能落在中文路径上**（`cannot open output file G:\???...`），
   所以 `build_bridge.ps1` 里所有路径都是相对的。

### 验收

| 检查 | 结果 |
|---|---|
| `Godot --headless --path godot --quit-after 60` | 无 ERROR，地图加载成功 |
| 开窗运行 | 房间、台阶、平台、矮道、墙跑墙全部可见 |
| `--autoplay`（按住前进 128 tick） | 位置从 -16 推进到 -10.7，HUD 与相机跟随 |

### 下一步

- [ ] 输入映射目前由 GDScript 从 `sim.get_bindings()` 生成 —— 已经是单一数据源，但还没做游戏内改键界面
- [ ] Godot 侧的表现层还没接事件通道（§3.4）
- [ ] 升级到 godot-cpp 4.7（等上游）
---

## 2026-10-18 · S07 · 试玩反馈：方向反了 + 台阶边缘

### 修了什么

| 问题 | 根因 | 修法 |
|---|---|---|
| A/D 左右颠倒 | `wish_direction` 里右向量符号写反：用的是 `(-sin, +cos)`，实际应为 `(sin, -cos)` | 改符号，并用 45° 用例覆盖 |
| 鼠标右移视角左转 | sim 里 yaw 增大是**逆时针（左）**，GDScript 里写成了 `yaw += dx` | 改成 `yaw -= dx` |
| 斜着撞台阶会沿侧面滑走 | step-up 的接受条件只比**水平位移**，斜向滑动时水平收益更大，于是永远不抬脚 | 增加「落点比平地结果更高（>5 cm）就接受」，因为那才是真的上了一个台面 |

### 新判据

- `--test-edge`：① 从空中落到 1 m 平台边缘（只有 20 cm 重叠）后仍能继续走 4.6 m；② 以 11° 掠射角撞楼梯必须爬上去（`max z > 0.4`）。
- 冒烟里加了 `--test-edge`，现在共 9 项。

### 需要你确认

「卡在边缘」在我这边的两个 headless 用例里都复现不出来（落地后能继续走，掠射角也能上台阶）。
如果重试还卡，请告诉我：卡在**哪一块**（台阶/平台/矮墙）、当时是不是**斜着**按方向键、
以及松手再按能不能动。这决定是 step-up 的接受条件问题，还是碰撞盒尺寸问题。

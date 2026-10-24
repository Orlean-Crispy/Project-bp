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
---

## 2026-10-18 · S08 · 「能动视角但不能移动」

### 现象

试玩时贴墙蹭，人物卡死：鼠标能转视角，但 WASD 完全不动。

### 根因

碰撞盒被塞进了 brush 内部。此时每一 tick 的扫掠**起点就是实心**（`start_solid`），
`slide_move` 直接把速度清零并返回——位置不变、速度不变，下一 tick 重复，
于是「相机正常、人物静止」。这不是输入问题，是位置已经非法。

最可能的入口是 step-up：抬起 0.46 m → 前进 → 落下，落点若与别的 brush 重叠，
人就嵌进去了。

### 两层防御

1. **不接受实心落点**：`step_slide_move` 在采纳阶梯结果前，用零长度 trace 检查落点，
   实心就放弃这次抬脚。
2. **自动脱离**：`pmove` 每 tick 开始检查，若已实心，按固定顺序（先向上 0.1 m 递增，
   再水平四向）找第一个可用位置。顺序固定 → 确定性不变。

### 新判据

| 测试 | 内容 | 结果 |
|---|---|---|
| `--test-stuck` | 从 0.1 m 薄墙**内部**出生，60 tick 内必须自己脱困并继续移动 | 移动 6.52 m，通过 |
| `--test-soak` | 6000 tick 乱按（前进/跳/滑铲/冲刺/墙跑/爬墙/翻越），统计连续实心与静止 | 最长实心 1 tick、最长静止 43 tick，通过 |

「最长实心 1 tick」说明：即使偶尔嵌进去，下一 tick 就被推出来了。
---

## 2026-10-18 · S09 · 跳跃边沿触发 + M5 网络第一刀

### 1. 长按空格连跳（试玩反馈，已落实到 sim）

**根因**：`pmove` 里 `if (input.jump && on_ground)` 是电平触发，只要按住且落地就再跳一次。
**修法**：`PlayerState.jump_was_down`，只在按下沿起跳；状态机字段同样进快照/回放。
**判据**：`--test-jump` —— 按住 300 tick 只能起跳 **1** 次；点按 3 次必须起跳 **3** 次。

> 这条修在 sim 层而不是 Godot 脚本里，因为 M5 之后服务端也要用同一套输入语义。

### 2. M5 网络（计划书 §7.0 / §7.1 / §7.6 / §7.9 的第一层）

| 模块 | 文件 | 对应计划书 |
|---|---|---|
| 不可靠通道 | `sim/net/netchan.h/.cpp` | §7.1 序列号 + ack + ack mask，无阻塞 |
| 字节流 | `sim/net/bytes.h` | 显式小端序列化，不依赖结构体 padding |
| 快照 | `sim/net/snapshot.h/.cpp` | §7.1 delta + baseline，字段组位掩码 |
| 服务端/客户端 | `sim/net/session.h/.cpp` | §7.6 预测 + 本地回滚；§7.0 ServerClock；§7.9 输入冗余 |
| 劣化信道 | `sim/net/transport.h/.cpp` | 丢包/延迟/抖动/重复，确定性 RNG |

**实现要点**：

- **baseline = 客户端最近 ack 的快照**；连续 3 个未 ack → 强制全量（§7.1）。
- 客户端收到 delta 时，用它 **ack 过的那份快照**解码（服务器在包里写 `baseline_tick`）；
  解不开就丢弃，让服务器自然退化成全量——**绝不把缺字段解成 0**。
- 预测：本地每 tick 跑一次 `pmove` 并记录 `(tick, state, input)`；收到快照后若状态不一致，
  就回到服务器状态并重放之后的输入（§7.6，最多 1 秒 / 128 tick）。
- 输入冗余：每个命令包带最近 **4** 个输入（§7.9），单包丢失不再等于丢失一 tick。
- 服务器按 **tick 对齐**使用输入：tick T 用 tick T 的输入，没有就沿用上一个。

### 验收（`--test-net`）

30% 丢包（双向）+ 30 ms 延迟 + 10 ms 抖动 + 2% 重复，2000 tick：

| 指标 | 结果 |
|---|---|
| 客户端 vs 服务器位置偏差 | **0.00000 m** |
| 收到快照 | 1491（约 70%，与丢包率一致；无 HOL 阻塞） |
| 解码失败 | 0 |
| 全量 / delta | 80 / 2020（丢 ack 后退化全量，delta 仍是常态） |
| 回滚次数 | 1428（见下） |

### 已知不足（下一个 M5 增量）

1. **回滚次数偏高**：客户端一生成输入就立即预测，而服务器要晚 2 个 tick 才收到，
   两边 `cmd.tick` 没有对齐到“估计的服务器 tick”（§7.0 的 ServerClock 类写了但还没接进
   命令时间戳）。下一步：客户端用估计的 serverTick 盖戳，服务器按该 tick 应用，
   预测命中率会显著上升。
2. **可靠通道**只留了 AckTracker，还没有 full_state/重连消息（§7.7）。
3. **hitbox 历史环形缓冲**（§7.3）未做——属于 M6 前的必做项。
4. 客户端还没有“其他玩家”的插值渲染（§7.1），目前只预测自己。
---

## 2026-10-19 · S10 · HP / 护甲（用户拍板）

| 项 | 值 |
|---|---|
| HP | 100 |
| 小甲 | 25（不恢复） |
| 再生甲 | 25（受伤后随时间恢复） |
| 大甲 | 50（不恢复） |

### 实现

- sim/game/combat.h/.cpp：CombatParams / PlayerVitals / apply_damage / tick_vitals / equip_armor
- 数据 sim/data/combat.toml（吸收 66%、再生 5 s / 10 s 是**提案**）
- 伤害整数运算；护甲按百分比吸收，耗尽后溢出到 HP

### 验收（--test-health，进冒烟）

- 无甲 30 伤害 → 70 HP
- 小甲：30 伤害 → 吸收 19、HP 11
- 大甲：40 伤害 → 吸收 26、HP 14
- 护甲封顶：100 伤害 → 吸收 25、HP 75
- 再生甲：延迟内不回、之后回满、受伤重置延迟
- 重甲躯干 TTK = 6 发 @600RPM = 0.60 s（落在 0.6–1.2 s）

### 遗留

- 吸收比例/再生节奏待确认（只改 combat.toml）
- 爆头 ×5 与护甲的结算顺序在 M6 确定

---

## 2026-10-19 · S11 · v0.4 并入 + §7.0 时间对齐 + 输入去重

### 0. 本轮输入（两份新方案）

| 文档 | 落位 |
|---|---|
| docs/网络层深化_v0.4.md | §7 扩张：A1 信令 / A2 可靠流 / A3 打洞 / A4 去重 / A5 恢复 / B1 分片 / B2 带宽 / D 反向利用 / C1–C3 裁决 / N1–N8 判据 |
| docs/archive/8.3候选B_v0.4.md | §8.3 选定方案：可破坏入口（0.5 m 四档、每图 3 面、每队 2 次/回合、加固 1 处、C4 3.5 s）。2026-10-23 归档，只读 |

裁决（已写进 AGENTS 摘要与铁律 D）：

- C1/C2：延迟补偿 **v0.1 不做、M6 必做**；hitbox 缓冲 **M6 第一天**建。
- 回溯窗口按公式 **6 tick**（30 + 15.6 + 5 ≈ 51 ms），不用 Source 的 1 s。
- 客户端预测命中（v0.4 §9.3）：`cl_predict_hit` 默认 1，不一致率 > 0.5% 关闭；
  伤害数字仍等服务器。

### 1. §7.0 时间对齐（旧实现只是占位）

**根因**：客户端从 tick 0 与服务器同步起步，`cmd.tick = 本地 tick`。命令在路上 2 tick，
服务器执行 tick T 时还没收到 T 的输入，只能沿用上一个；等命令到达，旧代码又把它当
「最新输入」塞给下一 tick。服务端输入流整体晚 2 tick，客户端每 tick 都预测错：1428/2000。

**修法**：

- `ServerClock::align(server_tick)`：客户端跑在服务器 tick 轴上，`command_tick = server + 4`。
- `kInputLead = 4` = 单程 30 ms（≈2 tick）+ 2 tick 前瞻（v0.3 §7.0 的上限）。
- `NetClient::init` 预跑 4 个空 tick：服务器在首个命令到达前正是这样走的；不做就会在
  第一次快照上假回滚一次，之后整条历史错位。
- `apply_snapshot` 不再倒着 snap：比历史更旧的快照直接忽略。

**证据**：30% 丢包 / 2000 tick，回滚 **1428 → 185**，偏差仍 0.00000 m。

### 2. v0.4 §5.2 输入去重（InputWindow）

- 窗口 [last+1, last+8]，16 位 `seen` 位图；`executed_seen` 位图区分「重复包」与「真迟到」。
- `tick <= 已执行` → 丢弃；重复只计数不记日志；迟到丢弃并只计一次；队列 >4 丢最旧。
- **迟到输入不再当最新输入重放**（v0.4 §5.3）——旧实现正是靠它续命，代价是每 tick 都错。

### 3. 验收（--test-net，已进冒烟）

| 场景 | 结果 |
|---|---|
| 30% 丢包 + 30 ms + 10 ms 抖动，2000 tick | 回滚 185、快照 1424、全量 77 / delta 1923、队列峰值 3、偏差 0.00000 m |
| 5% 丢包 + 30 ms + 10 ms 抖动，60 s（N3） | 位置修正 **0** 次（阈值 <3）、单次最大 0 m（阈值 0.5）、偏差 0.00000 m |
| InputWindow 单测 | accept / dup / late / far / exec-dup / overflow / depth=4 全过 |

> 30% 场景仍有 180 条真迟到：单程 2 tick + 交接 1 tick，lead=4 只剩 1 tick 余量，
> 抖动到 3 tick 时最后一包会晚到。这是丢包下的正常代价，客户端用回滚收敛；lead 再加会
> 增加服务器侧输入延迟，按 §7.0 不再加。

### 4. 文档

- 两份 v0.4 原文已复制进 docs/（**不再依赖桌面副本**）。
- 新增 ADR 0004：时间轴 / 去重 / 迟到丢弃。
- 根 AGENTS 摘要、docs/AGENTS 铁律 D、判据 8、未结案、下一步同步更新。

### 5. 未做（下一步）

1. §7.3 hitbox 历史（64 tick + 相邻帧插值）。
2. §7.7 full_state / 重连 / 中途加入。
3. §7.1 其他玩家 1 tick 插值。
4. v0.4 的 B1 分片 / A1 信令 / A2 可靠流 / A5 恢复。

---

## 2026-10-19 · S12 · §7.3 hitbox 历史（M6 的前置）

### 1. 模块

`sim/game/hitbox.h/.cpp`：

| 类型/函数 | 内容 |
|---|---|
| `HitboxSample` | 脚点、高、半径、蹲标志、tick（倍率不入样本，避免两份真相） |
| `hit_zone_at` / `zone_multiplier` | >0.82 h 头 ×5、>0.45 h 胸 ×1、其余腿 ×0.7 |
| `hitbox_contains` | 径向 AABB + z 区间（M6 的命中查询直接用） |
| `HitboxHistory` | 64 帧环形；`at(tick)` 精确/最近，`sample(tick, frac)` 相邻帧插值 |

### 2. 集成

- `NetServer`：每个玩家一条历史；init 记 tick 0，之后每 step 记一条。
- `NetClient`：tick 0 + 预跑 4 个空 tick + 每次预测/回滚重放都记；为 v0.4 §9.3
  的客户端预测命中保留同一份历史。
- 回滚重放会覆盖被重算的 tick；这与“历史 = 最终预测”一致。

### 3. 验收（--test-hitbox，进冒烟）

25 项：精确帧 / 中点插值 / 不外推 / 三段边界（上下各一侧）/ 倍率 5·1·0.7 /
蹲下 0.91 m 自动重分档 / 点在内、超半径、超头顶 / 环形 64 帧 oldest=6 newest=69 /
被覆盖的 tick 夹到最旧。

--test-net 追加断言：服务器 hitbox 帧数 64、newest == 服务器 tick（2000）。

### 4. 为什么现在做而不是 M6

计划书 §7.3 要求「第一天建」，因为**回溯命中的前提是历史存在**；M6 才写会同时改
网络、判定、武器三处。现在只有 50 行，且立即被 --test-net 覆盖。

### 5. 下一步

1. §7.7 full_state / 重连 / 中途加入。
2. §7.1 其他玩家 1 tick 插值。
3. v0.4 的 B1 分片 / A1 信令 / A2 可靠流 / A5 恢复。
4. M6：hitscan（用 hitbox 历史 + 回溯窗 6 tick）、后坐力表、穿透、衰减、hitmarker。

- 护甲尚未接入 PlayerState / 快照 / 网络（M6 再挂）

---

## 2026-10-19 · S13 · 底层性能审计（语义不变）

### 0. 结论先行

- 10 人同图 **2.15 µs/tick**（预算 4000 µs，占 0.05%）；`World::step` 稳态 **0 分配**。
- 瓶颈是 brush 扫掠，不是 pmove 数学。15 / 201 / 801 brush（10 人）：
  **2.2 / 5.9 / 21.7 µs**。
- 暂不引入 broadphase：现图 15 brush；800 brush 仍只占预算 0.54%。
  等单图 >2000 brush 再做，避免为不存在的地图增加网格顺序/去重逻辑。

### 1. 改了什么（全部保持逐 tick 哈希一致）

| 优化 | 之前 | 之后 |
|---|---|---|
| pmove 三角函数 | 每 tick 最多 4 次 sin/cos | 1 次，facing / wish 复用 |
| slide_move | 每个子步 1 次 sqrt | 用外层 speed（只有 bump 会改 vel，且立即 break） |
| 起始实心检查 | 每 tick 多 1 次 hull trace | 蹲姿未变时复用地面探测的 `start_solid` |
| HitboxHistory::record | 每 tick 扫描 64 帧 | **O(1)**；`oldest_tick()` 按需扫描 |

> 没有为后续武器/破坏/多玩家加任何字段或钩子；改动都是局部算术与结果复用。

### 2. 工具

- `--bench N --players M`：多玩家基准（同出生点；玩家之间本来就没有碰撞，
  所以不引入任何游戏语义）。
- `headless_sim` 覆盖全局 `new/delete` 计数，bench 打印 `allocs / tick`；
  `World::step` 为 **0.00**，铁律 E 的“稳态 0 分配”在 sim 核心成立。

### 3. 证据

| 场景 | mean | p99 |
|---|---|---|
| 1 人 | 0.24 µs | 0.40 µs |
| 10 人 | 2.15 µs | 2.90 µs |
| 10 人 / 201 brush | 5.94 µs | 10.70 µs |
| 10 人 / 801 brush | 21.71 µs | 35.90 µs |

- 600 tick 逐 tick 哈希与优化前**逐字节一致**（build/hash_before.txt vs after）。
- 15 项闸门 + bench ALL GREEN。

### 4. 剩余（非本次）

- 网络层每 tick 仍有分配（Snapshot / 编码 / 包缓冲），离“稳态 0 分配”还有距离；
  属 M5 增量，不在 sim 核心。
- 单图 brush 数超过 ~2000 时先做 broadphase，再做压测；按当前线性外推，
  2000 brush / 10 人约 55–60 µs，仍是预算的 1.5%。

---

## 2026-10-19 · S14 · §7.7 FULL_STATE / 中途加入 / 重连

### 1. 协议

- 新链路一律 `pending_full_state`：服务器每 tick 发 `FULL_STATE`，直到收到一条
  **未来 tick** 的命令（迟到包解析成 Late/Duplicate，清不掉标记）。
- 载荷：`join_id(u32) + player_index(u8) + params_hash(u64) + geometry_hash(u64)`
  + 完整 `Snapshot`（baseline = 0）。
- `join_id` 每次 `add_client` 递增：重连是新 id；同一份重复 FS 被客户端忽略。
- 哈希不一致 → `join_failed`，拒绝入局（不允许“半套配置”）。

### 2. 客户端加入

- `init` 不再假设双方从 tick 0 起步，等 FULL_STATE。
- 收到后：清历史/最近快照，tick = 快照 tick，预跑 4 tick；
  另加 `kJoinSlack = 3`（快照到达时最多旧 3 tick），首条命令 tick = 快照 tick + 7。
- 首条命令到达服务器即清 pending，恢复正常 delta 快照。

### 3. 断线与重连

- `remove_client` 只摘链路，玩家留在世界（无输入 = 站立/落地）。
- 重新 `add_client` 走同一 FULL_STATE 路径；`join_id` 变化触发客户端重对齐。

### 4. 验收（--test-net 内，闸门 8）

| 场景 | 结果 |
|---|---|
| 比赛先跑 200 tick，客户端 tick 202 加入 | 300+100 tick 后偏差 0.00000 m |
| 摘链路 100 tick 后重连 | 第二次 FS tick 701，偏差 0.00000 m |
| 参数哈希不同的客户端 | 被拒绝（join_failed） |
| 30% 丢包 2000 tick | 回滚 **51**（曾 185/1428），迟到 42 |

> 加入路径让起始对齐更干净：客户端从真实服务器 tick 起步，不再赌“双方都从 0 开始”。

### 5. 未做

- “回合内加入观战到下一回合”：还没有回合系统，等爆破/回合落地后再接。
- 重连票据（v0.4 A5 的 HMAC / 120 s）：当前按 player_index 重新挂链路。

---

## 2026-10-19 · S15 · §7.1 远端玩家 1 tick 插值

### 1. 模块

`sim/net/interp.h/.cpp`：`SnapshotInterpolator`

| 项 | 规定 |
|---|---|
| 缓冲 | 32 帧环形（0.5 s @ 64 Hz），按 `tick % 32` 定位 |
| 采样 | `sample(tick, frac)`：精确取 tick 与 tick+1，插值 pos/vel |
| 非连续字段 | flags / impulses / state / state_ticks 取前一帧，不插值 |
| 越界 | 夹到最近帧，**不外推**；空缓冲返回 false |
| 重复 | 同 tick 再 push 直接忽略；超过 32 帧的空洞触发 reset |

### 2. 集成

- `NetClient` 每解出一份快照 push；FULL_STATE 时先 reset 再 push。
- 暴露 `remote()`；表现层用 `server_time - 1 tick` 采样（计划书 §7.1 的 1 tick 延迟）。
- 只读表现层数据：不写回 pmove，不影响命中判定（命中走 hitbox 历史）。

### 3. 验收（闸门 8 内）

- 合成快照 tick 10/11/12、两名玩家相向移动：
  `sample(11, 0.5)` → 1.5 m / 8.5 m；精确帧与越界夹取正确；重复 push 不改变帧数。
- 30% 丢包 2000 tick：远端缓冲 32 帧，`newest == client.last_snapshot_tick()`。

### 4. 下一步

1. v0.4 M5 增量：B1 分片 → A1 信令 → A2 可靠流 → A5 重连票据。
2. M6：hitscan、后坐力表、穿透、衰减、hitmarker。
3. Godot 多玩家 demo 时把 `remote()` 接到渲染（当前 demo 仍只渲染本地玩家）。

---

## 2026-10-19 · S16 · §3.4 事件通道

### 1. 架构

- `sim/core/event.h/.cpp`：`SimEvent`（tick / type / entity / a / b / param）+
  `EventQueue`（1024 条固定环，满了丢最旧并计数，**零分配**）。
- 生产端：`World::step(inputs, dt, EventQueue* events = nullptr)`。只有传 sink 才发射；
  不传时零开销，所有既有调用点不用改。
- 跳跃/落地由 World 对比 pmove 前后的 `on_ground` 得出；**pmove 完全不知道事件**，
  保持纯函数，避免把表现层概念塞进移动核心。
- 事件**不承载权威数据**：伤害/血量只走快照（计划书 §3.4 的硬规则）。

### 2. 消费端

- bridge：`step()` 传 `&m_events`；新增 `drain_events()`（Array<Dictionary>）与
  `get_tick()`。
- Godot：每帧 drain，HUD 显示 `last event`；autoplay 改为**按 sim tick 驱动**
  （不再按帧），tick 20 起跳、tick 40 后移动——帧率不再影响 demo 行为。
- 单向数据流：GDScript 只消费事件，不反向查询 sim。

### 3. 验收

| 项 | 结果 |
|---|---|
| 新闸门 `--test-events` | jump tick 5、land tick 47；两次运行序列完全一致 |
| 环形溢出 | 1100 条 → 1024 条 + 丢 76，弹出最旧 tick 76 |
| Godot headless autoplay | `event: land @13 / jump @20 / land @62`（真实跨边界） |
| 性能 | bench 0.23 µs、0.00 allocs/tick，事件分支未影响核心 |

> 调试中发现 autoplay 原按**帧**计数，headless 帧率与 sim tick 脱钩，导致起跳落在
> 空中被丢弃；改为按 `sim.get_tick()` 驱动后稳定复现。这条经验对以后所有 demo
> 自动化都成立：**自动化必须绑定 sim tick，不能绑定帧**。

### 4. 下一步

1. v0.4 M5 增量：B1 分片 → A1 信令 → A2 可靠流 → A5 重连票据。
2. M6：hitscan、后坐力表、穿透、衰减、hitmarker（事件类型再加 Muzzle/Impact）。
---

## 2026-10-19 · S17 · 真实 UDP 传输（M5 的第一块硬缺口）

### 1. 为什么现在做

在此之前 sim/net/transport.* 只有进程内 LoopbackTransport。所有网络闸门都是“同进程、可控丢包”的
模拟，**没有一个字节真的离开过进程**；M5 的验收却是“2 人局域网”。先补传输层，后面的组局才有地基。

### 2. 实现

sim/net/udp_transport.h/.cpp：UdpTransport : Transport

| 项 | 规定 |
|---|---|
| 套接字 | Winsock UDP，FIONBIO 非阻塞；open(port) 支持 0 = 临时端口 |
| 客户端 | connect_peer(host, port) |
| 服务端 | recv_from() 拿到发送者地址 → adopt_peer()，之后正常 send/recv |
| 收 | recvfrom 一次一个包；空队列立即返回 false，**不阻塞 tick** |
| advance | 空实现（真实网络没有模拟时钟） |
| 跨平台 | Windows 走 ws2_32；POSIX 分支保留（未在 CI 验证） |

CMake 只在 WIN32 链接 ws2_32，不污染 sim 的其它目标。

### 3. 验收（并入闸门 8，未新增闸门）

| 检查 | 结果 |
|---|---|
| client → server 首包 + adopt | ok |
| server → client 回包 | ok |
| 1200 B 大数据报往返 | ok |
| 空队列 poll | 立即返回 false |

--test-net 输出：[udp] hello 1 adopt 1 pong 1 1200B 1 empty-poll 1。

### 4. 已知边界

- 一个 UdpTransport 只对应一个 peer；5v5 的多客户端需要“共享 socket + 每 peer 队列”的适配层。
- 组局（Godot 里 host/join、服务端循环、远端玩家渲染）还没接；这是下一步。
- 真实 UDP 的丢包会打到 tick 上，所以游戏循环必须“每 tick 收一次、不等待”。

### 5. 下一步

1. 组局：headless server 或 Godot listen server + 客户端连接；多 peer 适配。
2. B1 分片（v0.4，MTU 1200）——大包（full_state/config）必经。
3. A2 可靠流 + configstring 分块 → A1 信令 → A5 票据。
---

## 2026-10-19 · S18 · B1 快照分片（MTU 1200）

### 1. 实现

- sim/net/fragment.*：FragmentedTransport 装饰器，可包住任意 Transport。
- 小包（≤1100 B）原样透传，零分配；大包按 1100 B 切片：
  [0xF0][id lo][id hi][index][total][chunk]。
- 接收端按 id 重组；同 id 重复片忽略；新 id 到来时未完成的旧包直接丢弃并计数，
  **绝不把半包交给上层**。
- 最大 255 片（≈280 KB）；超过直接丢弃（设计上限，不是错误路径）。

### 2. 验收（并入闸门 8）

- 3000 B → 3 片，往返逐字节一致。
- 4 B 小包透传。
- 30% 丢包 × 20 个 2500 B 包：完整送达 9 个，**0 个损坏/截断**。

### 3. 边界

- 装饰器尚未在组局路径启用；等 Godot/UDP 组局时用 FragmentedTransport(UdpTransport)
  包一层即可，NetServer/NetClient 不用改。
- “丢一片整包等下一 tick”由上层语义保证：重组不出完整包就 recv 不到数据，
  快照自然等下一份。

### 4. 下一步

1. Godot 组局（多 peer UDP + 服务端循环 + 远端渲染）。
2. A1 信令 → A2 可靠流 → A5 票据。
3. M6。
---

## 2026-10-19 · S19 · 多 peer UDP 与会话（两个客户端真连）

### 1. 拆分

- sim/net/udp_socket.*：非阻塞 UDP socket（open / send_to / recv_from）。
- UdpTransport 改为 “socket + peer 地址”，仍是一个 peer 的客户端端点。
- sim/net/udp_hub.*：PeerTransport（共享 socket 的每 peer 收件队列）+ UdpPeerHub
  （按源地址 demux；未知来源进有界队列，最多 64 条）。
- sim/net/handshake.*：6 字节 HELLO（magic “BPH0” + 协议版本），版本不符直接忽略。

### 2. 会话测试（并入闸门 8）

- 真 UDP 环回、两个客户端、同一个 hub、服务器 2 名玩家。
- 客户端先后 HELLO（间隔 20 ms），服务器按到达顺序分配槽位。
- 结果：joined 1/1，snapshots 148/148，dev **0.00000 / 0.00000 m**。

### 3. 意义

- 这已经是“2 人局域网”的会话层：真 UDP、FULL_STATE 加入、多 peer demux、预测回滚。
- 还差：独立进程/Godot 组局（UI、循环、远端渲染），以及 A1/A2/A5。

### 4. 下一步

1. 两进程 net_loop（--serve/--join）验证跨进程时钟。
2. Godot 组局：--host/--connect、远端玩家用 SnapshotInterpolator 渲染。
3. A1 信令 → A2 可靠流 → A5 票据。
---

## 2026-10-19 · S21 · Godot 组局：--host / --connect + 远端渲染

### 1. 桥接层

- SimWorld 增加 host(port, players) / join(ip, port) / step_network()。
- host = listen server + 本机 NetClient 走 LoopbackTransport（复用完整预测/回滚路径）；
  远端客户端走 UdpPeerHub + UdpTransport。
- 本地玩家状态一律取 NetClient::predicted()，远端玩家取 SnapshotInterpolator
  （tick-1 采样）；不再为联机写第二套移动逻辑。
- get_remote_players() 返回 slot/pos/crouched/state；GDScript 生成胶囊体并按身高缩放。

### 2. Godot 侧

- CLI：--host、--connect[=ip]、--port=N（同时读 get_cmdline_args 与 user args）。
- step 循环按 is_networked() 分流：单机 step()，联机 step_network()。
- HUD 显示 role/slot/peers；加入与远端可见时打印日志（供 headless 验证）。
- tools/run_net_demo.ps1 一键起 host + client 两个窗口。

### 3. 验收（headless，两实例）

- host：hosting on 27161 → joined slot 0 → peers 1 → remote slot 1 visible → peers 2。
- client：connecting → joined slot 1 → peers 1 → remote slot 0 visible。
- 两端 stderr 均为空。

### 4. 未做

- 连接 UI（当前是命令行参数）。
- 客户端时钟漂移纠正（两进程已量化 ~1 tick/s）。
- 远端玩家的朝向/动画（快照暂无 yaw；胶囊体足够验证同步）。
- 5v5 槽位管理、断线重连 UI。

### 5. 下一步

1. 连接 UI（host/join 按钮 + IP 输入）。
2. 客户端时钟漂移纠正（§7.0 的减速/丢 cmd）。
3. A1 信令 → A2 可靠流 → A5 票据。
---

## 2026-10-19 · S20 · 两进程 net_loop + 输入窗口修复

### 1. 工具

- tools/net_loop（--serve/--join）：真 UDP 服务端 + 两个客户端进程，64 Hz。
- tools/net_selftest.ps1：起 1 服务器 + 2 客户端，校验 join、输入应用、快照流。
- 结果：joins=2 accepted=417 far=0 moved=46.86m clients_ok=True。

### 2. 抓到的真 bug

- 现象：两进程下服务器玩家不动，客户端贴着出生点。
- 诊断：acc 停在 8、cmd0 停在 52，dup 每 tick 涨；seen 却一直跟上客户端。
- 根因：客户端领先 7 tick（lead 4 + join slack 3），输入窗口只有 8、队列只有 4；
  每来一个新 tick 就“丢最旧”，丢掉的正是服务器下一 tick 要执行的命令。
  进程内测试两条循环严格同步，掩盖了它（服务器一直没执行输入，偏差仍是 0）。
- 修复：InputWindow 的窗口与队列都改为 16；窗口是“早到窗口”不是延迟缓冲，
  命令仍按自己的 tick 执行，延迟不变。
- 证据：修复后 acc 持续增长、depth 7–8、far=0；服务器两名玩家都跑起来。

### 3. 已知

- 客户端时钟相对服务器仍有 ~1 tick/s 漂移（两进程 sleep_until 节奏不同）；
  16 tick 窗口能扛十几秒，长局需要 §7.0 的漂移纠正（减速/丢 cmd）。
- net_selftest 不进 smoke：依赖墙钟调度。

### 4. 下一步

1. Godot --host/--connect + 远端渲染（复用 SnapshotInterpolator）。
2. 客户端时钟漂移纠正。
3. A1/A2/A5 → M6。
---

## 2026-10-19 · S22 · 连接 UI（启动菜单）

### 1. 实现

- 没有 --host / --connect 且不是 headless 时，启动后显示 LAN 菜单：
  Host / Join（IP + 端口）/ Single player。
- --menu 强制在 headless 下也构建 UI（用于脚本检查，不依赖窗口）。
- 菜单打开时暂停 tick（accumulator 清零），单机世界不会在后台空转。
- CLI 仍然有效：--host、--connect=IP、--port=N；run_net_demo.ps1 直接双实例。

### 2. 验收

- headless `--menu`：脚本无错误，输出 "breakpoint: net menu open"。
- 带 --host/--connect 的 headless 双实例仍按 S21 的日志正常加入。

### 3. 未做

- 局内菜单（Esc 呼出、断线重连按钮）。
- 客户端时钟漂移纠正（两进程已量化 ~1 tick/s）。
- 远端朝向/动画（快照暂无 yaw）。

### 4. 下一步

1. 客户端时钟漂移纠正（§7.0 的减速追赶，最多丢 2 cmd）。
2. 远端朝向/动画。
3. A1 信令 → A2 可靠流 → A5 票据。
---

## 2026-10-19 · S23 · 客户端时钟漂移纠正（有界减速）

### 1. 机制

- 客户端每收一份快照记录 `ahead = local.tick - snap.tick`，维护历史最小值 `ahead_min`。
- 当 `ahead > ahead_min + 3` 且冷却结束（32 tick）时，标记跳过下一次循环。
- 跳过 = `send_input` 不发包 + `step_local` 不推进；世界冻结一个循环（约 15.6 ms），
  服务器的 tick 继续前进，于是领先量永久减 1。
- **不制造 tick 空洞、不改历史、不瞬移**：命令仍按自己的 tick 执行，只是晚一个循环发出，
  而它本来就有 7 tick 余量。

### 2. 验收

- 新测试 `[clock]`：正常跑 120 tick 后强制客户端多走 6 tick（模拟快循环），
  再跑 240 tick + 60 tick 静止。结果：**skips=3、far=0、dev 0.00000 m**。
- 12 秒两进程实跑：far=0，最终服务器 / 客户端位置完全一致（29.594, 29.594）。
- 实测该环境 12 秒内漂移未超过阈值（偏移 9 → 5），所以自然运行 skips=0；
  强制测试证明纠正路径有效。

### 3. 代价

- 每个快照多两次整数比较；bench 不变（0.23 µs、0 allocs/tick）。
- 每次纠正冻结约 15.6 ms，冷却 0.5 s，不会连续卡顿。

### 4. 下一步

1. 远端朝向/动画、局内/断线重连 UI。
2. A1 信令 → A2 可靠流 → A5 票据。
3. M6 射击。
---

## 2026-10-19 · S24 · 远端朝向（yaw 进快照）

### 1. 数据

- PlayerState 增加 yaw；pmove 每 tick 从 input.yaw 写入（纯输入派生，确定性不变）。
- PlayerSnap 增加 yaw，快照 delta 新增独立字段组 kFieldYaw（1<<4，mask 0x1F）。
- 协议版本 1 → 2：HELLO 会拒绝旧版本构建（headless 与 bridge 都重新定义为 2）。

### 2. 表现

- get_remote_players() 返回 yaw；GDScript 用
  `rotation.y = atan2(-cos(yaw), sin(yaw))`（与相机同一套映射）旋转胶囊体。
- 胶囊体加一个前方小方块，朝向可见；蹲下缩放仍然生效。
- 插值器取相邻帧的前一帧 yaw（64 Hz 下足够；角度最短弧插值留待动画阶段）。

### 3. 验收

- 快照 roundtrip：全量 / delta 的 yaw 都逐位还原（[yaw] full 1 delta 1）。
- headless 双实例：host 看到 remote slot 1 yaw 0.0 → 0.9；
  client 看到 remote slot 0 yaw 0.9；两端 stderr 为空。

### 4. 代价

- 每玩家每 tick 多 4 B（仅在变化时）；mask 多一位。
- bench 不变（0.23 µs、0 allocs/tick）。

### 5. 下一步

1. 局内/断线重连 UI。
2. A1 信令 → A2 可靠流 → A5 票据。
3. M6 射击。
---

## 2026-10-19 · S25 · A1 信令（TCP 控制面）

### 1. 协议

- sim/net/signaling.h/.cpp：TCP 27015，u16 长度前缀帧 + 二进制消息，不用 JSON。
- 六条消息：HELLO / HELLO_ACK / ASSET_MANIFEST / ASSET_READY / MATCH_START / FULL_STATE。
- TcpListener / TcpStream：非阻塞 connect + select 超时、accept 超时、SO_RCVTIMEO、TCP_NODELAY。
- 资产只传清单（hash / size / name），正文走 HTTP + hash 缓存（尚未接）。

### 2. 验收

- 真 TCP 环回（端口 0 自动分配）：六条消息逐字段往返一致。
- 输出：[signal] hello 1 ack 1 manifest 1 ready 1 match 1 full 1。

### 3. 边界

- 还未接进 Godot / NetServer：菜单仍走 UDP HELLO；信令线程、MATCH_START → UDP 会话、
  资产缓存是下一步。
- TcpStream 是阻塞 + 超时，尚未起线程；接入时必须放信令线程，sim 线程禁止阻塞 IO。

### 4. 下一步

1. 信令接入组局（线程 + 事件队列 + MATCH_START 触发 UDP 加入）。
2. A2 可靠流 + configstring 512 B 分块 + CRC32。
3. A5 票据。
---

## 2026-10-19 · S26 · A2 可靠流 + configstring 分块

### 1. 实现

- sim/net/reliable.*：独立 rel_seq（从 1 开始，0 表示“未确认任何包”），窗口 32。
- 累积 ACK + 32 位位图（bit i 覆盖 ack+1+i）；RFC 6298 RTO（初始 250 ms，钳 100–1000 ms）。
- 重传上限 5 次 → dead；configstring 按 512 B 分块，8 B 头（index/total/CRC32）+ 负载。
- ConfigAssembler 逐块校验 CRC，缺块不 complete，assemble 按 index 拼接。

### 2. 验收（30% 丢包）

- 5000 B → 10 块，60 次重传预算内完成：chunks 10 retrans 6 complete 1 same 1 corrupt 1 dead 0。
- 篡改任意字节的块被 CRC 拒绝。

### 3. 抓到的 bug

- 第一版 complete() 只看 m_chunks.size()==total，而 resize 会补空槽，导致 7 块时就“完成”，
  拼接结果缺 3 块。修复：complete 要求每块非空。

### 4. 边界

- 尚未与快照共用同一个 socket：A2 目前是独立层 + 闸门，NetServer 的单 socket 双队列是下一步。
- 重传上限后的断线流程与 A5 票据联动未接。

### 5. 下一步

1. 把 A2 接进 NetServer 的单 socket（高优先快照队列 + 低优先可靠队列）。
2. A5 票据。
3. A1 信令线程接入组局。
---

## 2026-10-19 · S27 · A5 重连票据

### 1. 实现

- sim/net/ticket.*：32 B = client_id(16) + expiry(4, 秒) + HMAC-SHA256 截断 12 B。
- 房主内存持有随机密钥（TicketIssuer 默认构造用 random_device，不落盘）。
- verify：常量时间比较 + 过期边界（now >= expiry 即无效）。

### 2. 验收

- [ticket] valid 1 expired 1 tamper 1 wrongkey 1。
- 覆盖：到期前 1 秒有效、到期时刻失效、篡改 client_id、换密钥。

### 3. 抓到的边界

- 第一版用 expiry < now，导致到期那一秒仍有效；改为 now >= expiry。

### 4. 边界

- 尚未接进信令/重连流程：HELLO 已带 ticket 字段，服务端校验与重连状态机是下一步。

### 5. 下一步

1. A1/A2 接入组局（信令线程、单 socket 双队列）。
2. 票据接 HELLO / 断线重连。
3. M6 射击。
---

## 2026-10-19 · S28 · 全量复查（功能 + 质量）

### 1. 复查范围

- 全量构建（-Werror）、16 项闸门 + bench（10 玩家）、两进程 net_selftest、
  Godot headless 双实例、静态检索（TODO/FIXME、rand、源文件清单）。

### 2. 发现并修复

- **幽灵远端**：服务器为未加入的槽位也发快照，host 在客户端加入前就渲染出 slot 1 胶囊。
  修复：PlayerState/PlayerSnap 增加 active 位（flags bit2），服务器 init 全部置 false、
  add_client 置 true、remove_client 置 false；桥接层跳过未激活槽位。
  证据：host 日志变为 peers 2 → remote slot 1 visible（此前是先 remote）。
- **票据密钥熵**：MinGW 的 std::random_device 可能确定性；Windows 改用 CRT 的 rand_s，
  失败时退到 steady_clock，不再用 std::rand。
- 顺带修掉：A2 组装器“空槽也算完成”（S26）、票据到期边界（S27）。

### 3. 结果

- 闸门：16 项 + bench ALL GREEN（10 玩家 mean 2.04 µs、p99 3.40 µs、max 4.30 µs、
  0 allocs/tick；预算 4000 µs）。
- 两进程：joins=2、accepted=420、far=0、moved=47.22 m。
- Godot：host joined slot 0 → peers 1 → peers 2 → remote slot 1；client 对称；stderr 空。
- 静态检索：sim/ 内无 TODO/FIXME、无 std::rand。

### 4. 已知技术债（未改，记录）

- CMake 与 build_bridge.ps1 各维护一份源文件清单，靠人同步；应改成单一清单。
- PROGRESS 中 S21 被插在 S20 之前（时间顺序错乱），内容正确但不便阅读。
- InputWindow 的 Overflow 分支在窗口=16 后不可达（防御性保留）。
- net_loop 的诊断输出偏多，仅工具使用，不影响游戏。

### 5. 下一步

1. A1/A2/A5 接入组局（信令线程、单 socket 双队列、重连状态机）。
2. 局内/断线重连 UI、第三人称动画。
3. M6 射击。
---

## 2026-10-19 · S29 · 源文件清单单一化（清架构债）

### 1. 改动

- 新增 tools/sim_sources.txt：26 个 sim .cpp 的唯一清单。
- CMake：`file(STRINGS ... SIM_SOURCES)` + `add_library(sim STATIC ${SIM_SOURCES})`。
- build_bridge.ps1：读同一份清单，不再维护第二份数组。
- 新增 tools/check_sources.ps1：比对清单与 `git ls-files sim/**/*.cpp`，不一致即红。

### 2. 验收

- check_sources：`sources ok: 26 sim .cpp files`。
- 全量构建通过；bridge 重建通过（现在编译全部 sim 源，符号不会再漏）。
- 16 项闸门 + bench 仍全绿。

### 3. 代价

- bridge 多编译 demo/combat/reliable/ticket 等当前未调用的文件，DLL 略大；
  换来的是“再也不会因为漏一个 .cpp 而链接失败”。

### 4. 下一步

1. A1/A2/A5 接入组局（信令线程、单 socket 双队列、重连状态机）。
2. 局内/断线重连 UI、第三人称动画。
3. M6 射击。
---

## 2026-10-19 · S30 · 静默客户端超时清理

- NetServer 每条 Link 记录 last_rx_tick；任何可解包都会刷新。
- step() 开头扫描：ever_seen 且 320 tick（5 s）没收到包 → remove_client（槽位失活）。
- 验收：[timeout] joined 1 dropped 1；其余网络闸门不变。
---

## 2026-10-19 · S31 · 易解技术债（第一批）

- 槽位复用：`add_client(..., -1)` 选第一个 inactive 槽位；bridge/net_loop 不再维护 next_slot。
  超时释放后重连回到原槽（[timeout] joined 1 dropped 1 reuse 1）。
- `check_sources.ps1` 接入 smoke：源清单与 git 不一致时闸门直接红。
- README 已在 S30 后更新到 M5，并链接 GAPS.md。

### 未动（明天）

- PROGRESS S21/S20 顺序；InputWindow Overflow 死分支；Winsock 初始化重复；
  A2 单 socket 双队列；A1 信令线程；DLL dead-strip；远端插值 frac；10 分钟 soak。

### 下一步

1. A1/A2/A5 接入组局。
2. 局内/断线重连 UI。
3. M6 射击。
---

## 2026-10-20 · S32 · IMPULSE 命名落地

- 命名方案 docs/IMPULSE_命名方案.md 全文入库（675 行，与桌面原件逐字节一致）；
  ADR 0005 记三层命名（IMPULSE / IMP / Project Bp·bp）与术语分层（impulse / breach / IMPACT / impulse charge）。
- 决定**不做** `sim::` → `bp::` 全局重命名：模块命名空间说的是「这一层是什么」，
  26 个 .cpp 的机械改动零行为收益；项目级前缀仍用 bp（BP_* 宏、bp 分支、C:\bp-toolchain）。
- 玩家可见：godot/project.godot `config/name` 与 LAN 菜单标题 → IMPULSE。
- 新未结案：impulse 上限三处不一致（sim 4 / 命名方案 6 / 计划书 §5.2 待定），HUD 必须读参数。
- 判据：build + 16 项闸门 + bench ALL GREEN；Godot `--headless --quit` 无脚本错误。

---

## 2026-10-20 · S33 · A1 组局握手 + A5 票据（M5 收尾第一步）

- 新增 sim/net/matchmaking.*：MatchHost（自起 TCP 线程、事件队列、槽位/票据表）+ `match_request`（客户端）。
- 信令协议升到 v2：`client_id` 改 16 B（与票据 HMAC 输入一致）、HELLO/HELLO_ACK 带 32 B 票据、
  MATCH_START 带 `game_port`（约定 UDP = 信令端口 + 1）。
- 握手六消息走完才发 MATCH_START；新客户端领 120 s 票据；同 id + 有效票据回原槽（A5）；
  票据被篡改或房间满直接拒；sim 线程只 `drain()` 事件，不碰 socket。
- 判据（并入 --test-net）：`[match] accepted 1 slot 0 ticket 1 resume 1 tamper 1 full 1 events 1`。
- 未接 bridge/Godot：菜单仍走 UDP HELLO，下一步把 host/join 换成信令。

---

## 2026-10-20 · S34 · 组局接进 net_loop：HELLO 带 client_id（协议 v3）

- UDP HELLO 从 6 B 变 22 B：magic + 协议版本 + **16 B client_id**；`BP_PROTOCOL_VERSION` 升到 3。
- `net_loop --serve` 起 MatchHost（TCP 信令），UDP = 信令端口 + 1；`--join` 先 `match_request` 拿槽位/票据再连 UDP。
- 服务器只放行**信令通过过**的 id，槽位来自 matchmaking 事件；不再按「第一条 HELLO 给 0」猜。
  没走过 TCP 的 id 直接忽略（陌生人包进不了房间）。
- 判据：tools/net_selftest.ps1 → joins=2 accepted=416 far=0 moved=46.87 clients_ok=True；
  16 项闸门 + bench 仍全绿（其中 --test-net 的 UDP 会话改为 id 映射槽位）。
- 未接 bridge/Godot：菜单仍走旧路径，下一步把 host/join 换成信令（bridge 已带 client_id 发 HELLO）。

---

## 2026-10-20 · S35 · Godot 菜单改走信令；端口劫持坑

- bridge：`host()` 起 MatchHost（信令端口），UDP 自动 = 信令端口 + 1；listen server 自己占 slot 0
  （MatchHost 新增 `reserved` 参数），客人从 slot 1 起；`join()` 先 `match_request` 拿槽位再连 UDP。
- Godot 菜单端口语义改为**信令端口**，默认 `27115`（UDP 27116）。
- 踩坑：27015/tcp 被 `AppleMobileDeviceService` 占用；Windows 的 `SO_REUSEADDR` 允许第二个 socket
  绑上已被 listen 的端口，于是 `bind` 假成功、客户端被别人的服务接走，报 `receive failed`。
  修法：`SO_EXCLUSIVEADDRUSE`（绑不上就报错）+ 默认端口换 27115。案例 docs/pitfalls/port-hijack.md。
- 判据（两个 headless Godot 进程）：
  host  → `signal slot 1 resumed 0` / `peer joined slot 1` / `peers 2` / `remote slot 1 visible`；
  client → `joined slot 1` / `remote slot 0 visible`。
- `net_selftest.ps1`：joins=2 accepted=417 far=0 moved=46.69；16 项闸门 + bench 全绿。

---

## 2026-10-20 · S36 · A2 可靠流与快照共用 socket

- 新增 sim/net/reliable_transport.*：`ReliableTransport(tx, rx)` 包住任意 Transport；
  快照/输入原样透传，可靠包用 `PacketType::ReliableData(4)` / `ReliableAck(5)` 打标。
- 接收端先 demux（数据包交给 ReliableReceiver，ACK 交给 ReliableSender），两条流互不阻塞；
  wrapper 不推进底层管道——调用方每 tick 推进一次，避免双倍时钟。
- 判据（并入 --test-net）：`[dual] chunks 8/8 same 1 retrans 1 snapshot 326/400 dead 0`，
  20% 丢包 + 20 ms 延迟下 4 KB configstring 分 8 块全部按序到达且内容一致，38% 快照仍照常流过。
- 尚未接进 NetServer/NetClient：configstring 还没走这条流，下一步做重连状态机时一起接。

---

## 2026-10-20 · S37 · configstring 走可靠流；修掉「两个时钟」

- 会话接入 A2：`NetServer::set_configstring(blob)` 切 512 B 块；`add_client(..., rel)` 后
  每 tick 在 `send()` 里补投（窗口满就等下一 tick，不阻塞快照）。
- 客户端：`NetClient::init(..., rel)`，收到的块直接喂 `ConfigAssembler`；
  暴露 `config_ready()/config_chunks()/config_string()`。
- 判据（并入 --test-net）：`[config] chunks 8/8 same 1 full_state 1 ticks 50 retrans 17`，
  50% 丢包 + 20 ms 下 4 KB 配置分 8 块全部按序到达、内容一致，同时 FULL_STATE 照常到达。

### 修掉的 bug：一个 RTO 计时器被两个时钟推进

第一次跑出来 `retrans 8 @ tick 0`：一个包都没丢，却报 8 次重传。加打印才看清：

```
[sender] retrans seq=1 now=0 last=15 rto=250
```

`now` 倒退了——`NetServer::send()` 用 `world.tick` 推进 wrapper，`而闸门又用循环 tick 推进同一个
wrapper，两个时钟不同步；`uint32` 减法回绕成几十亿，于是每个包都被判定超时。

修法两层：
1. `ReliableTransport::advance()` 内部取 max，时钟永不后退（回滚也会让客户端 tick 倒退）；
2. **只有会话推进 wrapper**（服务器 = world.tick，客户端 = local.tick），调用方不得重复推进。

教训：RTO 这种「现在 - 上次」的算法，**只允许一个单调时钟**；第二个时钟不会报错，只会让所有包看起来过期。

### 债

- configstring 现在只验证了**传输**，内容还是任意 blob；真实内容（params/几何哈希/键位表）等重连状态机时一起接。

---

## 2026-10-20 · S38 · 重连早于超时：同槽只能有一个写者

- 问题：掉线清理是 5 s，但玩家（或自动重连）往往 1 s 内就回来了。旧实现直接 `m_links.push_back`，
  同一个 player 槽位会有**两个 link 同时写**——输出对着两个 transport，输入两份互相覆盖。
- 修法：`NetServer::add_client` 在显式槽位下先扫一遍 `m_links`，同槽的旧 link 先删掉，
  再挂新的；玩家状态保持 active，不必等 5 s 超时。
- 判据（并入 --test-net）：`[reconnect] first 0 links 1 active 1`——重连后链路数仍是 1，
  槽位仍 active。
- 16 项闸门 + bench 全绿（p99 0.40 µs，0 分配）。

### 债

- 全链路重连演练还没做：现在验证的是「会话层顶替旧 link」，信令 resume → 回原槽 → FULL_STATE
  这一段只在 `[match]` 和 `[join/reconnect]` 里分别验过，没串成一次完整演练。
- configstring 内容仍是任意 blob（传输已验证，语义未接）。

---

## 2026-10-20 · S39 · 局内 HUD + 暂停菜单

- 桥新增 `get_max_impulses()/get_hp()/get_armor()`；HUD 的格数从参数读，
  再没人能在 UI 里写死 6 格（ADR 0005 未决 1）。
- `godot/main.gd`：HP/护甲 + impulse 条（`J` 实心 / `.` 空）+ 状态行（role/slot/peers/waiting）
  + Esc 暂停菜单（Resume / Leave）。暂停时 tick 停走（和 LAN 菜单同一处理）。
- 判据：`--hud-check` 打 `hud hp=100 armor=0 impulse=4/4 role=solo`；
  两个 headless Godot 进程联机时无脚本错误、无假 LINK LOST。
- 已知局限：断线检测只看「世界 tick 是否 1.5 s 不动」，这是**世界冻结**检测，
  不是「服务器失联」检测；真检测要快照年龄（客户端最后一次收到快照的 tick），未做。
- 16 项闸门 + bench 全绿。

---

## 2026-10-20 · S40 · 失联检测（快照年龄）+ A5 票据重连

- `NetClient::snapshot_age()`：本地 tick − 最后一份快照的 tick；桥暴露 `get_snapshot_age()`。
  之前 HUD 用「tick 1.5 s 不动」判断，那只抓得到模拟卡死——服务器死了客户端照样自己走。
- HUD：客户端 `age > 64`（1 s）显示 `SERVER SILENT x.xs`；暂停菜单加 Reconnect。
- 桥 `reconnect()`：同 client_id + A5 票据重走信令 resume，回原槽，不重新排队。
- 判据一（失联）：起 host+client，杀掉 host，4 s 后客户端打 `breakpoint: server silent age=65`。
- 判据二（重连）：`--reconnect-check` → 客户端 `reconnect ok`；房主
  `signal slot 1 resumed 1` 并再次 `peer joined slot 1`（同一槽，不是新号）。
- 16 项闸门 + bench 全绿。

### 债

- 重连后客户端会短暂 `peers 0`（旧 UDP 已关、新 HELLO 未到），HUD 会闪一下；可接受，未打磨。
- 全链路的自动化闸门仍是分段的（[match] 握手 / [reconnect] 顶替 / --reconnect-check 端到端），
  没有一条命令一次跑完全部。

---

## 2026-10-20 · S41 · 代码体检第一轮（只读，出清单）

- 新增 docs/AUDIT.md：8 条可疑点 + 3 条「明确不动」+ 处理顺序。方法写了：全仓 TODO 扫描、
  「定义了但没人调用」、可静态推出恒假的分支、文件体积。
- 结论：**P0 没有**。P1 三条：Winsock 初始化两份、桥职责过载、configstring 语义未接。
  P2 四条：InputWindow::Overflow 不可达、AckTracker 死类、闸门巨文件 2916 行、加入逻辑两份。
- 两个新证据：`AckTracker` 全仓只有定义与实现（真死代码）；
  `Overflow` 因「窗口 16 = 深度 16 + 位图去重」，`m_queue.size() >= 16` 恒假。
- 本轮**不改代码**，先把清单交给用户点单；下一轮从 A1+A2（纯删除）开始。

---

## 2026-10-20 · S42 · 体检清理：A1 + A2（纯删除）

- **A1**：删掉 `InputWindow::Result::Overflow`、`m_overflow_cmds`、
  `NetRunStats::overflow` 与打印字段。窗口（16）= 队列深度（16）+ 位图去重，
  `m_queue.size() >= kWindowDepth` 在数学上恒假——留着只会让「溢出 0」被误读成「从不溢出」。
  注释写清了：窗口 = 深度是**设计**，不是巧合。
- **A2**：删掉 `AckTracker`（class + 两个方法，约 30 行）。全仓只有定义与实现，
  真正的 ack 处理在 `Link.sent` / `last_cmd_seq` 里。
- 判据：全量构建无警告（-Werror）；16 项闸门 + bench 全绿；`--test-net` 少打印一个字段，
  其余数字不变（回滚 51、far=0）。
- 行为零变化：删的全是「永远走不到」与「没人调用」。

---

## 2026-10-20 · S43 · 体检清理：A3（Winsock 一份）

- 新增 `sim/net/winsock.h`：header-only 的 `ensure_winsock(std::string&)`（POSIX 恒真），
  `WSAStartup` 只在这里出现一次。
- `signaling.cpp` 与 `udp_socket.cpp` 各删掉一份本地实现，改为 include 这个头。
- 判据：全量构建 -Werror 无警告；16 项闸门 + bench 全绿（`[signal]` 走 TCP、` [udp]` 走 UDP，
  两条路径都覆盖到同一处初始化）。
- 行为零变化：只是把「两份可能分叉的代码」变成「一份」。

---

## 2026-10-20 · S44 · 体检：A6 configstring 语义 + 校验

- 新增 `sim/net/configstring.h`：34 字节 `ConfigBlob`（version + params_hash + geometry_hash），
  手写小端编解码，不依赖任何库。
- `NetClient`：配置块收齐后解码比对；版本/参数哈希/几何哈希任一不符 → `config_mismatch()`，
  并且置 `join_failed`。**宁可不进局，也不带错规则进局**（v0.4 3.5）。
- 桥的 host 现在真的发这个 blob（参数哈希 + 几何哈希取自 world）。
- 判据（并入 --test-net）：`[config] chunks 1 same 1 matched 1 refused 1 full_state 1 ticks 18 retrans 1`——
  正确配置接受；把 params_hash 翻一位后客户端拒局。
- 16 项闸门 + bench 全绿；桥编译通过。

### 顺带

- `headless_sim` 的自定义 `operator delete` 与 `std::free` 被 GCC 的 `-Wmismatched-new-delete` 误报
  （计数器本来就是 malloc/free 配对），在定义处加 pragma 抑制并写明原因。

---

## 2026-10-20 · S45 · 体检：A7 admission 只有一份

- 新增 `sim/net/admission.h`：`approve(id, slot)` / `take(id)`（取走即消费，未批过返回 -1）。
- `tools/net_loop` 与 `bridge/sim_bridge.cpp` 都删掉各自的 approved 容器与 find_if，改用这一个类。
  两份实现最容易出现「工具里能重连、游戏里不能」这种不对称 bug。
- 判据：`[match] ... admit 1`（A 两次批准、B 一次、陌生 id 取不到、取空后 pending=0）；
  16 项闸门 + bench 全绿；`net_selftest.ps1` joins=2 accepted=418 far=0 moved=47.04。

---

## 2026-10-20 · S46 · 体检：A4 第一批（网络闸门搬家）

- 把 14 个网络闸门（join/udp/fragment/session/timeout/ticket/reliable/match/reconnect/config/
  dual/signal/clock/net）从 `main.cpp` 搬到 `tests_net.cpp`，连同伴生的
  `NetRunStats`、`net_test_input`、`run_net_scenario`。
- `Options` 提到 `options.h`（main 与测试 TU 共用）；`tests_net.h` 只放声明，
  `main.cpp` 负责解析与分发。include 放在匿名 namespace **之前**，否则声明会被内部链接化。
- 结果：main.cpp 2916 → 1722 行；tests_net.cpp 1243 行。
- 判据：全量构建 -Werror 无警告；16 项闸门 + bench ALL GREEN；
  `net_selftest.ps1` joins=2 accepted=418 far=0 moved=46.68——行为与搬家前一致。

### 教训

- 第一次用 PowerShell 一行流写文件，静默失败（什么也没写），改用 Node 的 `fs` 一次成功。
  文件搬运这种事，用「能报错的语言」而不是「猜它跑没跑的一行流」。

---

## 2026-10-20 · S47 · 体检：A4 第二批（演示闸门搬家）

- 演示组（record/play/compare/demo，含 `PlayOutcome`、play_file、compare_dumps）搬到 `tests_demo.cpp`。
- 共用件抽到 `test_util.h/.cpp`：load_world_from / load_world / make_input。
  它们原本在 main 的匿名 namespace 里，跨 TU 用不了——这也说明「搬家」会顺手暴露隐式耦合。
- 结果：main.cpp 2916 → **1355** 行；tests_net 1257、tests_demo 375、test_util 小文件。
- 判据：全量构建 -Werror 无警告；16 项闸门 + bench ALL GREEN（demo 回放、分叉检测都在里面）。

### 顺带

- 两次缺少 `using sim::X` 与 `kDt` 的编译错误，说明搬函数的正确姿势是：
  连同它依赖的 `using` 与常量一起看，而不是只搬函数体。

---

## 2026-10-20 · S48 · 体检：A4 收口（main.cpp 2916 → 272）

- 移动/杂项 16 个闸门搬到 `tests_move.cpp`；main.cpp 只剩 includes、分配计数、usage、main。
- 三处隐式耦合被搬家暴露：匿名 namespace 的 `using` 别名、`kDt` 常量、
  `g_allocations`（bench 要读）——后者抽成 `allocation_count()`。
- 还踩到一个：前两次拆分让 `}  // namespace` 的行号漂移，搬第三组时被一起搬走，
  `main` 就落进了匿名 namespace。**按行号搬代码，必须重新确认结构行在哪**。
- 判据：全量构建 -Werror 无警告；16 项闸门 + bench ALL GREEN。
- 现状：main.cpp 272 行、tests_net 1257、tests_move 1132、tests_demo 375、test_util 小文件。

---

## 2026-10-20 · S49 · 体检 A5 第一步：抽出纯展示件

- 新增 `bridge/mesh_common.h`：to_godot（Z-up → Y-up）与 material_color（材质→颜色），
  都是纯函数，header-only，不碰 socket/world。
- `sim_bridge.cpp` 删掉自己那两份，改成 `using bridge::...`，8 处调用一字未改。
- 为什么先做这一步：下一步要把 `build_mesh()` 搬进独立的 mesh_builder 时，
  builder 只依赖这个头就够，不用把整个桥类拖过去。
- 判据：桥编译通过；`--hud-check` 打 hud hp=100 armor=0 impulse=4/4 role=solo；
  两个 headless Godot 进程联机 —— host 见 peer joined slot 1 / remote slot 1 visible，
  client 见 joined slot 1 / remote slot 0 visible。

---

## 2026-10-20 · S50 · A5 第二步：build_mesh 独立成 mesh_builder

- 新增 `bridge/mesh_builder.h/.cpp`：`build_map_mesh(world.collision, verts, normals, colors, indices)`，
  纯输入输出，读 brush、写 godot 数组，不碰网络与 SimWorld。
- `SimWorld::build_mesh()` 现在只剩一行调用；`tools/build_bridge.ps1` 的源列表加了 mesh_builder.cpp。
- 判据：桥编译通过；`--hud-check` 正常；两个 headless Godot 进程联机无错误、
  client 打 `joined slot 1 / remote slot 0 visible`（地图与远端都还在）。
- 剩余：网络网关（host/join/reconnect/drain）仍在 SimWorld 里，下一步才动。

---

## 2026-10-20 · S51 · 给 HUD 检查加一条「网格非空」判据

- `--hud-check` 现在多打一个 `vertices`：拆走 mesh_builder 之后，
  没有任何东西能证明它还在生成几何——编译通过、地图也可能是空的（全灰）。
- 判据：`hud hp=100 armor=0 impulse=4/4 role=solo vertices=360`（playground 360 个顶点）。
- 这条补上 A5 第一/二步缺的「功能完好」证据：不是「没报错」，是「产出了东西」。

---

## 2026-10-20 · S52 · M5 收尾（A5 第 3 步：NetGateway）

- `bridge::NetGateway`（net_gateway.h/.cpp）接手全部网络状态：server / match / admission /
  client / hub / udp / 两条 loopback / hosting / client_id / ticket / hello 计时器。
  方法：`start_host / join / reconnect / pump(input) / active / hosting / joined / slot / peers / snapshot_age / client()`。
- `SimWorld` 478 → **313 行**，host/join/reconnect/step_network 只做转发，成员全删，无死代码。
  日志字符串逐字未改（`peer joined slot` 等）。
- 验收（我自己重跑，不采信代理自述）：
  `build_bridge.ps1` 通过；`--hud-check` → `hud ... vertices=360`；
  `smoke.ps1` ALL GREEN；`net_selftest.ps1` two processes joined；
  代理另跑 `--reconnect-check`：client `reconnect ok`、host `signal slot 1 resumed 1`。
- **M5 关闭**：docs/AGENTS 与根 AGENTS 的 M5 状态改为 ✅；未结案第 1、2 条关闭。
  第三人称动画与 5v5 选边按计划书归 M7，不计入 M5。

---

## 2026-10-20 · S53 · 第二轮体检：合并清单 + 第一批修复

- 两个只读代理分审 sim/ 与 bridge/胶水，合并出 19 条（AUD-01…19）。**没有 P0**，5 条 P1。
- 本轮修掉 5 条：
  `AUD-04` HUD 被同帧覆盖（HP/IMP 从来没显示过——之前只验了 getter，没验渲染）；
  `AUD-03` ESC 在 LAN 菜单上叠加暂停菜单、关闭后鼠标被捕获；
  `AUD-07` 不可达的 link_lost（本地 tick 每帧都变）；
  `AUD-09/18/11` 死常量、未读字段、未用 using。
- 判据：桥编译；`--hud-check` 正常；两进程 client 打 `joined slot 1 / remote slot 0 visible`，stderr 0 行。
- 剩下 P1：AUD-01（联机事件恒空）、AUD-02（暂停踢人）、AUD-05（远端插值退化）。

### 教训

- 「验证」和「验收」不是一回事。我此前用 `--hud-check` 打了 getter 就算过，
  但真正显示的那一行在同帧被覆盖。**判据要盯用户看到的东西，不是接口返回值。**

---

## 2026-10-20 · S54 · 体检前两批（R3–R9）收口

七条 P1 全部修复，一条一 commit、每条自带闸门：

| 项 | 改法 | 闸门 |
|---|---|---|
| R3 | `Link::sent` 改按发送序 deque，seq 回绕不再打乱 ack | `[seqwrap] ok 1 delta +300 full +0` |
| R5 | UDP 端口加 `SO_EXCLUSIVEADDRUSE` | `[udp] exclusive 1` |
| R4 | `MatchHost::release()`，握手失败/超时/stop 都归还槽位 | `[match] ... release 1` |
| R2 | vault 起手后重算 busy，爬墙不能同 tick 覆盖 | `[parkour] vault_climb_single 1` |
| R9 | state_hash/same_state 补齐 state/timer/yaw/边沿/墙面/翻越量，快照协议 v3→v4 | `state_fields 8/8`、`yaw_hash 1` |
| R7 | 删两个假参数、读 `wallrun_normal_dot`、`+`实现受伤减速 −15%/1.5 s、读 `headshot_mult` | `slowed 6.91 restored 8.13 m/s` |
| R8 | combat 哈希并入 params_hash，ConfigBlob 升 v2 | `[config] ... refused 1 combat 1` |

- 验证（我自己重跑）：全量构建无警告；`smoke.ps1` ALL GREEN；
  `--test-net` 上述判据齐；`--selftest` `state_fields 8/8`；
  `--test-parkour`、`--test-demo` PASS；`net_selftest.ps1` joins=2 accepted=415。
- 副作用：快照线格式变化 → `BP_PROTOCOL_VERSION` 3→4（AGENTS 已同步）。

### 剩余

- P1：R1（回滚未改写该 tick 的 history/hitbox，M6 才用到）、R6（网络热路径每 tick 分配）。
- P2：R10–R19（死分支、无效判据、重复代码、硬编码 64 Hz、未用字段/API、未校验索引、u8path、map 遍历）。

---

## 2026-10-20 · S55 · 体检 P2 批量（R10–R19）

- 10 条一条一 commit。验证（我自己重跑）：`smoke.ps1` ALL GREEN；
  `--test-wall` 打 `max_step=0.1172 m (limit 0.150) ok`（R11 现在真的会红）；
  `--test-health` 打 `invalid armour type … ok / absorption above 100% … ok`（R17）；
  `--test-net` 的 `window: accept 1 dup 1 late 1 far 1 exec-dup 1 full-window 1 depth 16`（R19 换成定长数组后语义不变）；
  工作树干净。
- 值得记的三点：
  1. **改判据本身就是修 bug**：R11 的`max_step` 只打印不比对、R12 的「不外推」断言的是入参 tick——
     这类「恒真判据」比没有判据更危险，它给人已经在保护的错觉。
  2. **R14 用 A/B 证明零行为变化**：改完把旧版 stash，比对 `--test-net` 输出逐字节相同，
     而不是「我觉得等价」。
  3. **R19 把两个热路径 map 换成定长数组**（窗口 16 / 32），API 与语义不变，同样用逐字节 A/B 证明。
- 剩余：R1（回滚未改写该 tick 的 history/hitbox）、R6（网络热路径每 tick 分配）。

---

## 2026-10-20 · S56 · 体检 R1 / R6（sim 侧收口）

- **R1**：`apply_snapshot` 在回放前把修正后的状态写回 `m_history[index]` 并重记该 tick 的 hitbox。
  新闸门手工喂两份同 tick 快照：`[rollback] hitbox 1 dup 1 state 1`——
  重复快照不再二次回滚、被修正 tick 的 hitbox 等于修正后位置（1e-4）。
  把两行修复删掉，闸门打 `hitbox 1 dup 0 state 0` 并失败，证明它真能红。
- **R6**：服务端 inputs 改成员缓冲；新增 `World::step_single`（客户端每 tick 不再造单元素 vector）；
  `Link::sent` 与 `m_recent` 改成容量 64 的环形数组，稳态不再每 tick 分配。
  新闸门：`[netalloc] per_tick 0`（200 tick 预热后统计 100 tick 的 session step）。
  行为证明：stash 前后 `--test-net` 输出只差这一行新判据，其它统计逐字节相同。
- 我复核：smoke ALL GREEN；`[rollback]` 与 `[netalloc]` 均在场；工作树干净。

### 边界（不装作做完了）

- R6 只覆盖 R6 列出的三处 session 热路径。回环测试里的 `ByteWriter`/快照编解码/LoopbackTransport` 队列
  仍有约 40 次/tick 分配——那是序列化与测试传输的 API，不是本条的范畴，需要单独立项。
- 桥侧审计仍有 3 条 P1（AUD-01 联机事件恒空、AUD-02 暂停停网泵、AUD-05 远端插值退化）与若干 P2 未处理。

---

## 2026-10-20 · S57 · 桥侧体检：三条 P1 + 四条 P2

- **AUD-01**：`NetClient::step_local(EventQueue*)` 贯穿（含回放），`pump(input, events)` 传入；
  闸门 `[netevents] 1`——联机里起跳事件能到桥，drain_events 不再是空。
- **AUD-02**：菜单暂停时仍泵网络（零输入），世界本地停走；`--force-paused` 下房主 8 s 后仍 peers 2。
- **AUD-05**：远端采样改到客户端快照轴并带真实 frac，插值不再是 nearest。
- **AUD-06**：信令成功但 UDP 失败时保持「已断开」，清远端胶囊，不回落到地图世界。
- **AUD-08**：胶囊半径/身高从桥的参数读，改 toml 不再与碰撞不同步。
- **AUD-10**：键名表补全 NUM/ENTER/方向键/鼠标中右键，未知键改为告警而非静默跳过。
- **AUD-12**：删掉 template_release 这条永远不存在的 DLL 声明。
- 复核：全量构建无警告；`--test-net` 里 `[netevents]` / `[rollback]` / `[netalloc]` 全在；
  `smoke.ps1` ALL GREEN；桥重建通过。

### 未做完（明确留档）

- **AUD-13**：桥编译仍不带 -Werror / BP_BUILD_ID（godot-cpp 头自带告警，需先做范围隔离）。
- **AUD-14**：端口字面量仍分散（27115/27016/27130）。
- **AUD-15**：脚本公共块（Godot 路径、首跑导入）仍复制多份。
- **AUD-17**：重连按钮双击保护、joined_printed 复位。
- **AUD-19**：setup 仍依赖 7z、Ninja 未锁版本。
- **AUD-16**：留到 M6（hp/护甲进快照后才有真实值）。

---

# 开发日志（详细）

> 从 2026-10-20 起，在 PROGRESS 末尾追加详细日志；上面的 S 编号短条目保留，作为索引。
> 每篇固定六段：目标 / 做了什么 / 为什么这样做 / 证据 / 失败与修正 / 债与下一步。
> 详细日志是给“三个月后的自己”看的：要能复现判断，而不只是知道结论。

## 2026-10-20（凌晨）· 把 M5 收成可交付基线

### 0. 目标与起点

这一晚的目标不是加新玩法，而是把已经能跑的东西变成“丢了也能恢复、别人也能接手”的基线。
起点是 S28 全量复查之后：M0–M4 全绿；M5 的不可靠通道、delta、预测回滚、FULL_STATE、
远端插值、真实 UDP、两进程、Godot host/connect 都已通；A1/A2/A5 有独立实现和闸门，
但没有接进组局；仓库只在 G:\自制游戏 一块盘上，没有远端。

### 1. 先存档，再动代码

判断：不等 v0.1。v0.1 还要几周，仓库只有约 2 MB，一次磁盘故障就会丢掉全部历史。
做法是 3-2-1 的简化版：

1. 版本历史：已有的 commit；打 checkpoint-2026-10-19 tag。
2. 本地离线副本：git bundle --all 生成单文件，放到仓库外的 G:\BreakpointBackups。
   第一版 314 KB，git bundle verify 通过；它自带全部 refs，换台机器直接 clone。
3. 异地副本：GitHub 私有仓库。

同时写了 tools/backup.ps1（时间戳 + 自动 verify）和 docs/backup.md（步骤、恢复、节奏）。
这一步的价值不在“多了一个文件”，而在于把“怎么恢复”写成了可执行步骤。

### 2. GitHub：连接问题和历史问题各翻一次车

第一次是连接。git ls-remote 报 Failed to connect to github.com port 443 via 127.0.0.1。
查下来 git 全局 http.proxy 指向 127.0.0.1:7897，但端口没有监听；进程里只有
clash-verge-service（后台服务），主程序没启动。直连 443 的 TCP 能建立，但 TLS 阶段被 reset。

绕法是 SSH over 443：ssh.github.com:443，本机 SSH key 已在 GitHub。探针返回
Hi Orlean-Crispy! You have successfully authenticated。remote 设为
ssh://git@ssh.github.com:443/Orlean-Crispy/unknow.git，SSH 不经过 HTTP 代理。

第二次是历史。首次 push 被拒：远端有 Initial commit（LICENSE + README），与本地
30 多个 commit 没有共同祖先。先试 git rebase origin/main，在第一个 M1 commit 就
AA README.md 冲突——本地也有一份 README。没有硬解，直接 git rebase --abort，
改用 git merge --allow-unrelated-histories；README 保留本项目版本（远端只有一行
# unknow），LICENSE 一并合入。合并提交 59fa04a，之后 push 成功。

这里的选择值得记录：rebase 适合“我的提交叠在别人后面”，不适合“两条独立历史的第一次握手”。
第一次合并用 merge 更诚实，也更容易解释。SSH 443 到 GitHub 仍会间歇 reset，所以 push 写成
“失败重试 2–3 次”的循环，而不是假设一次成功。

### 3. 构建基建：把“两份清单”合成一份

S28 审计把“CMake 与 build_bridge.ps1 各维护一份源文件清单”列为第一技术债。
它不是洁癖：之前桥接构建就因为漏了 interp.cpp / hitbox.cpp 直接链接失败。

做法：新增 tools/sim_sources.txt（26 个 sim .cpp 的唯一清单）；CMake 用 file(STRINGS)
读它，add_library(sim STATIC ${SIM_SOURCES})；build_bridge.ps1 读同一份文件，删掉手写数组；
新增 tools/check_sources.ps1 比对清单与 git ls-files，并接到 smoke.ps1 第一项。
以后新增 .cpp 忘了登记，闸门直接红。

代价是 bridge 现在会编译 demo/combat/reliable/ticket 等暂时没调用的文件，DLL 略大；
换来的是“结构上不可能再漏文件”。这个交换在 v0.1 之前是划算的。

### 4. 联机健壮性：幽灵、掉线、槽位

这一节是今晚唯一改到网络行为的部分，三个问题串在一起：

4.1 幽灵远端。服务器为所有预留槽位都发快照，host 在客户端加入前就渲染出 slot 1 的胶囊。
修法：PlayerState.active → 快照 flags bit2；服务器 init 全 false、add_client true、
remove_client false；桥接层跳过未激活槽位。修复后 host 日志顺序从 remote slot 1 先出现，
变成 peers 2 之后才出现。

4.2 掉线不释放。客户端静默后链路一直留在 m_links，槽位永远 active。修法：每条 Link
记录 last_rx_tick（收到任何可解包的服务器 tick 都刷新），step() 开头扫描，连续 320 tick
（5 秒）没有包就 remove_client。闸门 [timeout] joined 1 dropped 1。

4.3 槽位只增不减。这是 4.2 暴露出来的真 bug：超时释放了 slot 1，但 bridge 的 m_next_slot
和 net_loop 的 next_slot 只 ++，新客户端拿 slot 2 → 人满，重连永远进不来。修法：
add_client(..., -1) 选第一个 inactive 槽位；bridge/net_loop 不再维护计数器；满员返回 -1。
闸门扩展为 [timeout] joined 1 dropped 1 reuse 1。

这三条合起来才闭环：掉线要释放、释放的槽要能复用、复用后要能重新激活。
只做前两条，重连仍然进不来；只做第三条，幽灵仍然在。

### 5. 协议件的验收（已有，但今晚重新跑过）

| 件 | 输出 | 说明 |
|---|---|---|
| A1 信令 | [signal] hello 1 ack 1 manifest 1 ready 1 match 1 full 1 | 真 TCP 环回、六条消息逐字段 |
| A2 可靠流 | [reliable] chunks 10 retrans 6 complete 1 same 1 corrupt 1 dead 0 | 5000 B / 30% 丢包 |
| A5 票据 | [ticket] valid 1 expired 1 tamper 1 wrongkey 1 | HMAC-SHA256 截断 12 B |
| 时钟 | [clock] skips 3 far 0 dev 0.00000 m | 强制漂移 6 tick |
| 双客户端 | [udp session] joined 1/1 snapshots 148/148 dev 0.00000 m | 真 UDP + hub |
| 掉线/复用 | [timeout] joined 1 dropped 1 reuse 1 | 5 s 超时 + 槽位复用 |

bench（10 玩家）：mean 2.04 µs、p99 3.40 µs、max 4.30 µs、0 allocs/tick；单玩家 p99 0.40 µs。
预算 4000 µs，余量三个数量级。

### 6. 失败与修正（不写结论，写判断）

1. 把“连接失败”误认成“认证失败”：ls-remote 报错先看 via 127.0.0.1，那是代理，不是凭据。
   先查代理端口和进程，再谈 token。
2. rebase 处理无关历史：第一次合并用 rebase，冲突在 README；正确做法是 abort + merge。
   教训：历史关系不清楚时，merge 比 rebase 安全。
3. A2 组装器“空槽也算完成”：resize(total) 会补空槽，size()==total 恒真；改成每块非空才算完成。
   又一次证明“看起来完成”比“报错”更危险。
4. 票据到期边界：expiry < now 让到期那一秒仍有效；改 now >= expiry。
   时间边界要写成“有效区间”，不要写“过期条件”再取反。
5. 输入窗口（更早）：客户端领先 7 tick，窗口 8 / 队列 4，丢的正是下一 tick 要执行的命令；
   进程内测试因为两条循环同步而完全没发现。两进程测试是必要手段。

### 7. v0.1 缺口（原 GAPS 文档，2026-10-23 归档到 docs/archive/GAPS.md）

P0：射击（无武器/hitscan/开火输入/准星）、伤害联网（快照无 hp/armor）、死亡与重生、
开火键位、40×40 地图、A1/A2/A5 接入组局。P1：局内/断线 UI、5v5 槽位、音频、第三人称动画。
P2：第一人称枪模、跨机 demo 比对、Godot 4.7。

建议顺序：射击最小闭环 → A1/A2/A5 接入 → 地图/UI/音频。

### 8. 债与下一步

今晚清掉：源清单双份、幽灵远端、掉线不释放、槽位不复用、check_sources 手动化。
仍欠：PROGRESS S21/S20 顺序、InputWindow::Overflow 死分支、signaling.cpp 与 udp_socket.cpp
重复 Winsock 初始化、A2 单 socket 双队列、A1 信令线程、DLL dead-strip、远端插值 frac、
10 分钟联机 soak。

下一步不是继续造协议，而是做 M6 射击最小闭环：WeaponDef(toml) → 开火输入 →
hitscan（复用 hitbox 历史）→ 三段伤害 → hp/armor 进快照 → 死亡/重生。
判据先写：固定靶命中/未命中、三段倍率、TTK 0.6–1.2 s。

### 9. 当前基线（S31 当天）

- main = 732a8c6（已推 GitHub）；tag checkpoint-2026-10-19 指向合并提交 59fa04a。
- 远端：ssh://git@ssh.github.com:443/Orlean-Crispy/unknow.git。
- bundle：G:\BreakpointBackups\breakpoint-20261019.bundle（已 verify）。
- 16 项闸门 + bench ALL GREEN；工作树干净。
- 下一件事：开 sim/game/weapon.* 与开火输入，先写闸门再接表现层。
---

## 2026-10-20（下午）· 命名方案落地（S32）

### 0. 目标

不是加玩法，是把「这个名字」从一份桌面文档变成仓库里的事实。三件事必须一起做，
否则名字只落在 README 上：**文档（谁说了算）/ 玩家可见（窗口标题）/ 代码前缀（bp）**。

### 1. 做了什么

| 动作 | 结果 |
|---|---|
| 命名方案入库 | docs/IMPULSE_命名方案.md，675 行，与桌面原件 SHA-256 一致 |
| ADR 0005 | 三层命名 + 术语分层 + **拒绝**全局重命名 + 三条未决 |
| 文档地图 | 根 AGENTS §1.1/§4/§6、docs/AGENTS §2/§4/§7、README 标题与链接 |
| 玩家可见 | project.godot `config/name="IMPULSE"`、LAN 菜单 `IMPULSE - LAN` |
| 新未结案 | docs/AGENTS §4 第 9 条：impulse 上限三处不一致 |

### 2. 为什么这么做

**① 为什么不全量重命名 `sim::` → `bp::`。**
命名空间划的是模块边界。`sim::` 回答「这一层是什么」，`bp::` 只回答「这是我的」；
桥接层本来就不属于 sim，压进 `bp::` 会把唯一一条真边界抹平。再加上 26 个 .cpp 的机械 diff、
零行为收益——那不是统一品牌，是把 git 历史搅浑。品牌统一交给前缀（BP_* 宏、bp 分支、bp-toolchain）。

**② 为什么先把 4 vs 6 记成未结案，而不是就地改成 6。**
计划书 §5.2 至今写着「待定」；sim 的 `max_impulses=4` 是当时的占位；命名方案画 6 格是
Red Eclipse 的参考值。三个数字谁都没被验收过——手感验收（M3）才有资格拍板。
现在改成 6 会让「文档一致」掩盖「数值没验」；记成未结案、并要求 HUD 从参数读，才是诚实的做法。

**③ 为什么文档先行、代码只动玩家可见的两处。**
命名是低频、高影响的决定；代码里的标识符是高频、低影响的载体。先把「谁说了算」写死（ADR），
后面的改动才有依据，否则下次又会出现「桌面一份、仓库一份」。

### 3. 证据

- 文档：根 AGENTS §1.1 新增对外名/代号一段；§4 新增「命名」硬约束；§6 文档地图新增一行。
- docs/AGENTS：§2 新增「命名（项目）」行（模块命名空间保持 sim::）；§4 新增第 9 条未结案；§7 文件地图更新。
- 玩家可见：Godot 窗口标题与 LAN 菜单标题已改；`--headless --quit` 无脚本错误。
- 基线：build 成功，16 项行为闸门 + bench **ALL GREEN**（p99 0.40 µs，0 分配）。

### 4. 失败与修正

1. 写 ADR 时在模板字符串里转义反引号，落盘成了 `\``（反斜杠 + 反引号）。
   症状是 Markdown 里所有行内代码都带一个反斜杠——肉眼扫一遍才发现。
   教训：**生成含反引号的 Markdown，写完必须读回来比对**，不能相信「写成功了」。
2. 一开始想把 `docs/AGENTS.md` 的未结案写成「M6 前解决」，写完发现它和 M3 手感验收绑定，
   改成 lead：M3。lead 写错等于没写。

### 5. 债与下一步

- **已定（用户拍板）**：不做全局重命名，项目一直用内部代号开发；仓库也保持 `unknow`。
  ADR 0005 已把这条从「未决」改成「结案」：名字不影响正确性，剩下的唯一目标是没有 bug。
- 欠：impulse 上限定值（M3）；
  构建产物名保持工具名（ADR 已记录理由，不再当债）。
- 名字的下一步不是继续改文档，而是 M5 收尾后进 M6；HUD 做资源条时按 ADR 读参数、画 J 图标。


## 2026-10-20（下午–晚间）· M5 收尾 + 两轮代码体检（S39–S57 完整记录）

### 0. 这一段的起点与目标

起点：M5 的主干（不可靠 UDP / delta / 预测回滚 / FULL_STATE / 远端插值 / 事件通道 / 两进程）都已通，
但 A1 信令、A2 可靠流、A5 票据还只是"协议件"，没有接进组局；局内没有 HUD；代码里积了 26 项
可疑点（后来两轮体检实际找出 45 条）。

目标按计划书顺序：**先把 M5 收口成可交付的基线，再做全量体检**，每一步都要「闸门先绿再提交」。

### 1. M5 收尾：从「协议件」到「能两个人开局」

| 步 | 做了什么 | 判据 |
|---|---|---|
| A1 组局 | `MatchHost` 自起 TCP 线程，握手 HELLO→ACK→MANIFEST→READY→MATCH_START，签发 32 B 票据 | `[match] accepted 1 slot 0 ticket 1 resume 1 tamper 1 full 1` |
| 信令进 UDP | HELLO 从 6 B 变 22 B，带 16 B client_id；槽位来自信令而非到达顺序 | 两进程 net_selftest joins=2 |
| 桥接 | host 起 MatchHost + NetServer（自占 slot 0），join 先 match_request 再连 UDP | host 见 `peer joined slot 1`，client 见 `joined slot 1` |
| A2 共 socket | `ReliableTransport(tx,rx)` 打标 demux，configstring 走可靠流 | `[dual] 8/8 same 1`、`[config] matched 1` |
| A5 重连 | 同 id + 票据 resume 回原槽；同槽 link 顶替而非新增 | `[reconnect] first 0 links 1 active 1`、客户端 `reconnect ok` |
| 局内 UI | HUD（HP/ARMOR/IMP 点阵/速度/坐标/事件）、Esc 暂停菜单、失联提示 | `--hud-check` 与两进程联机 |
| 失联检测 | `NetClient::snapshot_age()` > 1 s 显示 SERVER SILENT | 杀房主后 `server silent age=65` |

踩过的两个真坑：

1. **端口劫持**：27015/tcp 被 AppleMobileDeviceService 占着，而 Windows 的 `SO_REUSEADDR` 允许
   第二个 socket 绑上已被 listen 的端口——`bind` 假成功，客户端连到别人的服务，报 `receive failed`。
   修法：Windows 改 `SO_EXCLUSIVEADDRUSE`，默认信令端口换 27115。案例 docs/pitfalls/port-hijack.md。
2. **一个 RTO 计时器被两个时钟推进**：闸门和 `NetServer::send()` 各自推进同一个 wrapper，
   时间倒退后 `uint32` 回绕，全部包被判超时。修法：advance 取 max + 只由会话推进。

### 2. 第一轮体检（A1–A7，结构）

| 项 | 内容 | 结果 |
|---|---|---|
| A1 | `InputWindow::Overflow` 恒假（窗口 16 = 深度 16 + 位图去重） | 删 |
| A2 | `AckTracker` 全仓零调用 | 删 |
| A3 | Winsock 初始化写了两份 | 抽 `sim/net/winsock.h` |
| A4 | 闸门文件 2916 行 / 34 个 `run_*` | 拆成 tests_net/tests_move/tests_demo + test_util，main 272 行 |
| A5 | 桥 519 行塞了网络、渲染、输入、HUD | mesh_common.h、mesh_builder.*、net_gateway.*；SimWorld 478→313 |
| A6 | configstring 只是任意 blob | 34 B 格式（version + params/geometry hash），失配拒局 |
| A7 | 加入逻辑两份（net_loop 与桥） | 抽 `sim/net/admission.h` |

### 3. 第二轮体检（AUD 19 条 + R 19 条）

两个只读代理分审 sim 与 bridge/胶水，合并 38 条，**0 个 P0**。修复分四批：

- **批一（UI）**：HUD 文本同帧被覆盖（HP/IMP 从未显示）、ESC 在 LAN 菜单上叠加暂停菜单导致鼠标捕获、
  删掉不可达的 `link_lost` 与未用常量/字段。
- **批二（P1 sim）**：R3 序号回绕（`Link::sent` 改按发送序 deque，闸门 `[seqwrap] ok`）、
  R5 UDP 端口独占（`[udp] exclusive 1`）、R4 槽位释放（`[match] release 1`）、
  R2 翻越/爬墙不能同 tick 双花 impulse（`[parkour] vault_climb_single 1`）、
  R9 哈希补齐（`state_fields 8/8`、快照协议 3→4）、R7 六个假参数（删两个、读两个、实现受伤减速）、
  R8 combat 哈希并入（`[config] refused 1 combat 1`）。
- **批三（P2 sim）**：R10–R19——死分支、恒真判据（R11 `max_step` 只打印、R12 断言入参 tick）、
  TTK 重算伤害、64 Hz 硬编码、未用字段/API、索引校验、u8path、两个热路径 map 换定长数组。
- **批四（P1 sim）**：R1 回滚改写该 tick 的 history/hitbox（`[rollback] hitbox 1 dup 1 state 1`）、
  R6 网络热路径零分配（`[netalloc] per_tick 0`，仅限 session 三步，序列化仍分配，已写明边界）。
- **批五（桥）**：AUD-01 联机事件（`[netevents] 1`）、AUD-02 暂停不停网泵、AUD-05 远端采样改快照轴、
  AUD-06/08/10/12。

### 4. 证据（最终基线）

- 16 项行为闸门 + bench：**ALL GREEN**；p99 0.40 µs，0.00 allocs/tick；
- 两进程 LAN：`joins=2 accepted=414 far=0 moved=46.69`；
- 协议版本：CMake / build_bridge / version.h 全为 **4**，文档无残留 v3；
- 工作树干净，无 stash，bundle 已刷新。

### 5. 失败与修正（这一段最值钱）

1. **「验证」不等于「验收」**：我用 `--hud-check` 打印 getter 就宣布 HUD 完成，实际
   渲染路径被同帧覆盖，HP 从没显示过。判据必须盯**用户看到的东西**。
2. **恒真判据比没有判据更危险**：R11/R12 的检查永远不会失败，却让人以为有保护。修判据本身是一次 bug fix。
3. **按行号搬代码，结构行会漂移**：前两次拆分让 `}  // namespace` 行号变了，第三次按旧坐标搬，
   把 `main` 搬进了匿名 namespace。每次都要重新确认结构行。
4. **文件操作别用一行流**：PowerShell 一行流写文件静默失败（什么都没写），换 Node 的 fs 一次成功。
5. **代理会死在半路**：两次执行代理因余额中断，留下已提交和未提交的改动。处理方式固定为：
   `git status` → 逐条复核 → 重跑闸门 → 收下能绿的、回滚不能的；不重做、不盲信自述。

### 6. 明确未完成（不许含糊）

- AUD-13 桥编译不带 `-Werror`/`BP_BUILD_ID`（godot-cpp 头自带告警，需范围隔离）；
- AUD-14 端口字面量四处；AUD-15 脚本公共块五份；
- AUD-17 重连按钮双击保护；AUD-19 setup 依赖 7z、Ninja 未锁版本；
- AUD-16 留到 M6（hp/护甲未进快照）；
- R6 只覆盖 session 三步，ByteWriter/编解码/回环队列仍约 40 次/tick 分配——需单独立项。

### 7. 下一步

先清 AUD-19（换机即炸）与 AUD-13（告警盲目），再按计划书进 **M6 射击**：
WeaponDef(toml) → 开火输入 → hitscan + hitbox 回溯 → 三段伤害 → hp/armor 进快照 → 死亡/重生 → 穿透/衰减。

---

## 2026-10-20（傍晚）· 清债：AUD-19/13/17/14/15（S58）

### 0. 目标

五条 P2 债，按「炸得最响」排序，一次清完：**换机即炸的安装脚本**（AUD-19）→
**桥的编译标志不一致**（AUD-13）→ **重连双击**（AUD-17）→ **端口四处写死**（AUD-14）→
**脚本公共块复制**（AUD-15）。不加新功能。验收标准不是「改了」，而是「换台机器能装、
桥坏了闸门会红、端口改了只改一处」。

### 1. 做了什么

| 编号 | 症状（改之前） | 改法 | 判据 |
|---|---|---|---|
| AUD-19 | setup 调 `7z`（本机没装）；ninja 取 `releases/latest` | `Expand-Archive`；三个工具全锁确切版本；ZIP magic 校验；目录统一 `mingw64/ cmake/ ninja/`；老 `cmake-4.4.4-windows-x86_64/` 改名复用 | 本机 adopt 成功并打印 g++ 16.2.0 / cmake 4.4.4 / ninja 1.13.2；真实 zip 与假 zip 各跑一次 |
| AUD-13 | 桥缺 `-Werror`/`-ffp-contract=off`/`-DNDEBUG`/BP_BUILD_ID | 标志与 CMake 逐条对齐；godot-cpp 用 `-isystem`；build id 来自 `git rev-parse`；版本号收进 version.h | 桥零告警；Godot 打 `build <hash>` 且 hash == HEAD |
| AUD-17 | 双击重连叠两次 join；`joined_printed` 不复位 | `_do_reconnect()` + 成功上锁；复位三个一次性日志 | `reconnect double=0`；重连后又打 `joined slot 1` |
| AUD-14 | 27115/27016/27130/27116 四处，游戏端口一处 `+1` 一处写死 | `sim/net/ports.h` 唯一定义；桥暴露 `get_default_signal_port()`；lint 禁止别处出现 | `--hud-check` 打 `port=27115`；两个 Godot 进程不带 `--port` 也能连上 |
| AUD-15 | Godot 路径与首跑导入复制多份 | `tools/godot_env.ps1`（唯一知道路径与导入的地方）+ `tools/godot_check.ps1`（第 17 项闸门） | smoke 第 17 项 PASS；没装 Godot 时 SKIP 而不是假绿 |

提交：`adbaf84`（AUD-13）→ `efacb11`（AUD-19）→ `b3a37f5`（AUD-17）→ `13b88e2`（AUD-14）→
`08334fd`（AUD-15）。一次一条债，互相不缠绕。

### 2. 顺手挖出来的两个真问题（这一段最值钱）

**① 扩展从来就没加载过。** 做 AUD-13 时我加了 `get_build_id()`，第一次跑 `--hud-check`
准备「看它打印 build id」，结果什么都没有。真实症状链是：

```
ERROR: No GDExtension library found for current OS and architecture (windows.x86_64)
  at: parse_gdextension_file (core/extension/gdextension_library_loader.cpp:360)
ERROR: GDExtension dynamic library not found: 'res://breakpoint.gdextension'
SCRIPT ERROR: Parse Error: Identifier "SimWorld" not declared in the current scope
```

DLL 就在 `godot/bin/` 里，导出表里 `breakpoint_library_init` 也在，但
`breakpoint.gdextension` 只写了 `windows.debug.x86_64`，而我们用的是 **release 编辑器二进制**
跑 headless——它只认 `windows.editor.x86_64`。报错里的 `(windows.x86_64)` 是 os.arch，
不是被查找的键，所以第一眼会往「文件不存在」上查，越查越远。修法是两个键都指向 template_debug DLL。

更糟的是它的**失败方式**：main.gd 解析失败 → `--hud-check` 那行永远执行不到 →
进程不退出 → 我三次尝试留下三个僵尸 Godot，把 DLL 锁住，下一次 `build_bridge.ps1` 报
`cannot open output file: Permission denied`。所以 `godot_check.ps1` 里所有 headless 调用都带超时
（单个 60 s、重连 20 s），超时即失败并打印日志路径——「静默挂住」必须变成可读的失败。

**② 桥缺 `-ffp-contract=off`。** DLL 与 `headless_sim` 编的是同一份 sim 源码，一个允许 FMA 收缩、
一个不允许：浮点结果可能差最后一位，影响 demo 哈希回放、回滚一致性、两端预测。没有任何闸门会报，
因为闸门只跑 exe。这条被写成教训 12：**两个入口编同一份源码，标志必须逐条一致**。

### 3. 为什么这么做（架构判断）

- **端口的所有者只能是 C++ 头。** 端口要同时被 C++（信令/闸门）、GDScript（菜单）、
  PowerShell（两进程脚本）读到。跨语言时「抄一份常量」必然漂移（已经漂了：27016 是上古默认）。
  所以 `sim/net/ports.h` 是唯一所有者，其余人**读**：桥转成方法、脚本解析头文件、
  net_loop 用常量做默认值。再加一条 lint 把「抄」这件事变成红色。
- **闸门的边界画错了，比少一条闸门更危险。** 16 项闸门全绿而桥根本没加载——这不是缺测试，
  是测试只覆盖了 sim 进程。所以第 17 项不是「再检查一遍 HUD」，而是「把桥拉进闸门体系」。
- **版本号只该出现在下载 URL 那一行。** 目录名带版本（`cmake-4.4.4-…`）会把版本号复制到
  `build.ps1`、文档、别人的记忆里。改成无版本名 + 安装脚本负责适配，升版本就只改一行 URL。
- **重连的锁要跟着事实走，不是跟着按钮走。** 按钮 `disabled` 是表现层的延迟，真正的事实是
  「这张票据已经被用过、这条 link 已经被替换」。锁放在 `_do_reconnect()` 里、成功后保持到菜单重开，
  才挡得住同一帧的第二次点击。
- **失败要能失败。** 端口 lint 写完后第一次运行就红在自己的注释上；`--reconnect-check` 故意
  再调一次 `_do_reconnect()` 并断言 `double=0`。判据的价值在于它曾经红过。

### 4. 证据（最终基线）

- `tools/smoke.ps1`：**17 项行为闸门 + bench ALL GREEN**（p99 0.40 µs，0.00 allocs/tick），
  其中第 17 项自带两条：`hud hp=100 armor=0 impulse=4/4 role=solo vertices=360
  build=<hash> protocol=4 port=27115` 与两进程 `reconnect ok / double=0 / signal slot 1 resumed 1`；
  （S59 之后协议升到 5、hud 行多了 protocol/port 之外的快照字段说明，见下一节。）
- `tools/net_selftest.ps1`：`joins=2 accepted=420 far=0 moved=46.69 ports_ok=True clients_ok=True`；
- 两个 Godot 进程**一个 `--port` 都不带**：host 打 `hosting on port 27115`、client 打
  `connecting to 127.0.0.1:27115`，双方互相 `remote slot N visible`；
- `tools/check_sources.ps1`：`sources ok: 27` + `ports ok: literals only in sim/net/ports.h`；
- `build_bridge.ps1`：`-Werror` 下零告警，`built … (build id 08334fd)`。

### 5. 失败与修正

1. **`$Host` 是只读自动变量**：两进程脚本第一版直接崩在 `$host = Start-Process …`
   （PowerShell 内置 `$Host`）。改名 `$hostProcess`。
2. **僵尸 Godot 锁住 DLL**：扩展加载失败时 `--hud-check` 不会退出，我先后留下的三个进程让
   `ld` 报 `cannot open output file … Permission denied`。教训：headless 调用一律带超时，
   改 DLL 前先确认没有 Godot 在跑。
3. **`-DBP_BUILD_ID="abc"` 会被 PowerShell 吃掉引号**：宏最后变成裸标识符
   （`#define BP_BUILD_ID a4cd724`），只有 `-DBP_BUILD_ID=\"abc\"` 才对。用 `g++ -E -dM`
   两种写法各测一遍才定下来——这种细节不能靠「应该可以」。
4. **端口 lint 先红了自己**：注释里写了 27115。把规则文件本身排除，注释改写。
5. **中文路径第四次咬人**：`objdump -p` 对 DLL 报 `No such file or directory`（路径被按 ANSI 解码），
   只能先复制到 ASCII 目录再查导出表。

### 6. 明确未完成（不许含糊）

- **AUD-16**：`get_hp()/get_armor()` 仍是常量 100/0，M6 把 hp/armor 放进快照时一并解决；
- **R6 边界**：只做到 session 三步零分配，序列化/回环传输仍约 40 次/tick；
- **第 17 项在没装 Godot 的机器上是 SKIP**：这是有意的（工具链脚本不下载 Godot），
  但也意味着「换机后忘装 Godot」不会让 smoke 变红，只有 `godot_check.ps1` 会明确说 SKIP；
- **跨机 demo 比对**仍缺第二台机器。

### 7. 下一步

债清了，按计划书进 **M6 射击最小闭环**：WeaponDef(toml) → 开火输入 → hitscan（复用 hitbox 历史）
→ 三段伤害 → hp/armor 进快照 → 死亡/重生。判据先写：固定靶命中/未命中、三段倍率、TTK 0.6–1.2 s。

## 2026-10-20（夜）· M1–M5 全量审计与修复（S59）

### 0. 目标

用户要求「M1 到 M5 完整检查一遍，确定功能完整、代码正常、无无用代码、无屎山，架构合规」。
做法：四个**只读**代理（sim 核心/移动、sim 网络、bridge+Godot、tools+闸门）分别通读，
我再逐条复核、逐条决定「修 / 记 / 待定」。不给"整体良好"这种结论，只给能验证的条目。

### 1. 结论先说

1. **闸门大面积"能打印不能失败"**：`--info`、`--bench`、land 事件、分片 delivered、dup 帧、
   selftest 的 8/21 字段、胸倍率、墙"是否真的到达"、光线 fraction、join 文件写失败静默跳过——
   smoke 只看退出码，所以它们全绿却从不证明任何事。这是本轮最大的一类问题。
2. **两处真数据缺陷**：impulse 着地**每 tick** 回满（4 个 62 ms 到手）；
   回滚快照缺 slide/dash/vault 三个按下沿与 speed_coefficient（按住会被当成"刚按下"重放）。
3. **三处"文档说了、代码没做"**：`--force-paused` 从没被解析（AUD-02 的验证是假的）、
   `entry["state"]` 没删（AUD-18 写着已删）、桥从未接可靠流 configstring（AGENTS 写着已下发）。
   分别改成：真解析、删除、把文档说成真话（真实链路靠 FULL_STATE 两个哈希拒局，功能已覆盖）。
4. 死代码清掉 11 处：rng_at64、clampf、dropped_commands、decode_failures、stale_skips、
   sent_packets、delivered_packets、PlayerState::tick、last_acked_snapshot、Link::ever_seen、
   DemoWriter::m_file。

### 1b. 按里程碑看的检查表

| 里程碑 | 查了什么 | 代表性问题 | 修法与判据 | 仍留 |
|---|---|---|---|---|
| M1 地图/碰撞/哈希 | .map 解析（entity/brush/平面/材质）、单位换算、扫掠与分步、geometry_hash、--info | --info 只打印；origin 在桥里换算；geometry_hash 不含 _climbable；0 brush 地图能加载；bumps 多给一次 | 断言 test_box 形状（2 entities / 8 brushes / 21.0 m）；entity_origin_metres()；哈希含标志并缓存；拒绝空 brush；严格 < | 分步长度与防穿注释/判据表述（AUD-33） |
| M2 基础 pmove | 走跑跳蹲、step-up、蹲下空间、摩擦/加速、数值与计划书 §5.6 | 空中也 step-up；impulse 每 tick 回满；NaN 输入能污染整局 | 仅着地 step；recharge_ticks=32；入口 isfinite/dt 守卫 | 手感常量仍是字面量（AUD-28） |
| M3 parkour | slide/dash/wallrun/wallclimb/vault 状态机、enter/exit 对称、计时、代价 | 滑铲跳那一 tick 仍走 slide 摩擦；爬墙 keep 不查 climbable；翻越/爬墙被当 Jump | 重算 sliding；keep 查标志；Jump 要求 jump 沿 | 空中 mantle 的定位（设计待定 2） |
| M4 demo/比对 | 格式、头部、录制回放、--dump/--compare、分叉定位 | Reader 泄漏句柄；截断当 EOF；build_id 从不比较；compare 一方 EOF 也 PASS | 析构/close；feof 区分；build_id 进 mismatch 且返回 1；同 EOF + rows>0 | demo 初始状态不完整（AUD-30） |
| M5 网络 | 线格式、delta、回滚、输入窗口、时钟、可靠流、票据、组局、分片、UDP、桥 | 快照缺三个沿与 speed_coefficient；幽灵链路永不超时；全零 id 可占房主槽；ack_bits 恒 0 却宣称有；闸门多处只打印 | 协议 v5 + [snapfields]；超时从 add_client 起算；拒全零 id；netchan 注明；9 处判据改成能失败 | AUD-20/21/22/23/24/26/27 |

### 2. 做了什么（按域）

**模拟/碰撞**
- impulse 充能改整数间隔 `recharge_ticks`（toml + params_hash）；`--test-parkour` 断言
  「31 tick 不回、第 32 tick 回 1」。
- 空中不再 step-up（只有 on_ground 才 step_slide_move）；跳跃撞台沿不再免费上台阶。
- Jump 事件要求 jump 按下沿 + 起跳前着地；翻越/爬墙不再被报成跳跃。
- `geometry_hash` 纳入 `_climbable/_wallrunnable` 并缓存；bumps 上限改严格 `<`。
- pmove 入口拒绝 NaN/dt≤0，forward/right 钳到 [-1,1]；爬墙 keep 检查 climbable。
- map_parse 拒绝 0 brush 的图；新增 `entity_origin_metres()`，桥不再做第二次 0.0254 换算。
- 删除 `PlayerState.tick`（不进快照却进哈希 → 回滚后假分叉）。

**网络**
- 协议 **v5**：PlayerSnap 增加三个按下沿（flags bit4-6）、`recharge_progress`、
  `speed_coefficient`；`[snapfields]` 逐字段往返断言。
- 链路超时从 add_client 起算（原来"没收到过包"的 link 永不超时 → 幽灵槽 + 43 KB/s）。
- HELLO 拒绝全零 client_id（原来会被当成"空闲槽"占住，可顶掉房主 slot 0）。
- MatchHost::start 先 stop 旧线程；PeerTransport 收件队列封顶 64；暴露 `bad_snapshots()` 供 net_loop 打印。
- `ack_bits` 在 v0.1 恒 0，netchan.h 写明，不再假装"每包带 32 位 mask"。

**桥/Godot**
- solo 步进改 `step_single`；params_hash 改 combined（含 combat.toml）。
- 眼高 0.9×当前姿态高度（原来恒 1.55，比站姿头顶还高、蹲下不动）。
- join 失败回滚 disconnected；start_host 失败清 TCP/端口；join_impl 先建后换。
- `--force-paused` 真解析；暂停时同步相机并 drain 事件；Esc 过滤 echo；autoplay 跳跃改边沿。
- 删除未读的 `entry["state"]`。

**demo/键位/工具**
- DemoReader 加析构/close、区分短读与 EOF；build_id 进 mismatch，`--play` 遇 mismatch 返回 1。
- `--compare` 要求两边同时 EOF 且 rows>0；`key_from_name("NONE")` 往返。
- CLI：strtol 全量校验、未知 --script 报错、--record/--play 互斥、--dump 需 --play、usage 补 --tolerance。
- `run_net_alloc_check` 从 main.cpp 移进 tests_net.cpp；CMake 源清单过滤空行/注释。
- check_sources 收 untracked，新增三条铁律 lint（sim 无 Godot / 0.0254 只在 map_parse / 无状态 RNG），
  匹配前先剥注释。

### 3. 为什么（架构判断）

- **闸门必须能失败**：只会打印的判据给人"已验证"的错觉，比没有判据更危险（教训 3/13）。
- **状态哈希的字段集 = 快照的字段集**：两者唯一用途都是"两边是不是同一个世界"；
  只要有一个字段只在一侧，回滚与跨机比对就会给出相反结论。v5 就是补齐这个集合。
- **单位转换只能有一个地方**：桥里那次 0.0254 不是"顺手"，是让规矩失效的第一步；
  现在由 map_parse 提供米制接口、lint 兜底。
- **文档里"已完成"的每一句都要有代码/闸门支撑**：本轮三处都是文档跑在代码前面。

### 4. 证据（本轮最终基线）

- 全量构建 `-Werror` 零告警；`smoke.ps1` **17 项 + bench ALL GREEN**，新增判定：
  `p99 within 4 ms / 0 allocs`、`state_fields 21/21`、`[snapfields] 1`、
  `impulse recharge interval … ok`、`[fragment] delivered 9`、`[netalloc] per_tick 0`、
  `[match] … second 1 … slots 1`、`--info` 断言 2 entities/8 brushes/21.0 m。
- `godot_check.ps1`：`hud … protocol=5 port=27115` + 重连 `double=0` + **暂停 7 s 不掉线**。
- `net_selftest.ps1`：`joins=2 accepted≈420 far=0 moved≈46.7 ports_ok=True`。
- `check_sources.ps1`：`sources ok: 27` / `ports ok` / `rules ok`。

### 5. 失败与修正

1. 快照往返断言第一版把 `slow_factor` 算进去——它是 pmove 每 tick 从 slow_ticks 派生的，
   不是线上状态。改成只比真状态，并在注释里写明"派生量不算"。
2. soak 阈值收到 30 tick 立刻红：45 tick 的"不动"是**贴墙顶着走**，不是冻结。最终按
   "solid 才是真冻结"保留 2 tick，frozen 放到 64（1 s）并写清理由。
3. `[fragment]` 一开始要求 delivered≥10，30% 丢包下实测 9。阈值要卡在"能证明重组发生过"
   的最低线（改 5），否则闸门变成运气测试。
4. 改 main.cpp 的 include 后忘了 `tick_seconds`/`load_combat_beside` 的头文件，-Werror 立刻抓住。
5. `probe` 这个变量名与已有的 `World probe` 撞名，编译报 "conflicting declaration"——改名 `probe_field`。

### 6. 明确未完成（都在 AUDIT 6.2/6.3，带 lead）

- AUD-20 客户端落后不补 tick（>110 ms 卡顿后输入永久 Late）；
- AUD-21 序列化/接收热路径仍每次 4-8 次分配（`[netalloc]` 只覆盖 session 三步）；
- AUD-22 可靠流 u16 回绕不安全；AUD-23 分片传输未接真实链路；
- AUD-24 admission 无 TTL；AUD-26 UDP 不校验源地址；AUD-27 正常离开不释放组局槽位；
- AUD-28 手感常量仍是字面量（铁律 I）；AUD-29 combat 诊断与 max_health；
- AUD-31 桥的信令在主线程同步（最坏 ~11 s 冻结）；AUD-32..35 表现层/主循环项；
- 设计待定：impulse 充能速率、空中 step-up 是否做成 mantle、护甲 66%、configstring 的定位。

### 7. 下一步

M6 射击，顺序不变：WeaponDef(toml) → 开火输入 → hitscan + hitbox 回溯 → 三段伤害 →
hp/armor 进快照（顺带 AUD-16/AUD-29）→ 死亡/重生 → 穿透/衰减。
AUD-20/26/27 在 M6 前清（都影响"两个人真打一局"）。

---




---

## S60 · 2026-10-23 · M6 入口条件（审计 7 条）+ 治理迁移

### 1. 做了什么

**代码**（一条 checklist，七个独立缺口）：
1. **协议 v6**：mask/flags 8→16 位，`kFieldAll=0x01FF`，新增 Vitals 组（hp/armor，接 PlayerState 真值）
   与 Weapon 组（预留，恒 0）；四条 static_assert；demo 头、session 的 same_state、桥的 get_hp 全部跟上。
2. **AUD-23**：分片接进真实链路——`UdpPeerHub::add_peer` 返回 fragmenting view，客户端 join 也包一层。
3. **AUD-26**：`UdpTransport::connect_peer` 规范化地址，`recv` 只收对端 (host,port)，其余丢弃计数。
4. **AUD-27**：`NetServer::add_client(..., ClientId*)` → `remove_client` 进 departure 队列 →
   网关 drain 后 `MatchHost::release`。
5. **AUD-20**：`NetClient::catch_up()`（落后 >4 tick 重锚时间轴）；`main.gd` 不再清零累加器。
6. **本地渲染插值**：桥暴露 `get_prev_position/get_prev_eye_height`，相机在两 tick 间混合。
7. **E 键让位交互**：WallClimb → V，`Action::Use` 改名 `Interact` 绑 E；腿倍率进 `movement.toml`。

**治理**：`docs/STATE.md`（≤120 行，唯一"现在"）、`docs/budgets.md`（资源台账）、
`docs/contracts/M6.md`（契约，入口条件已勾）、`docs/BACKLOG.md`（设计待定/功能缺口拆出）、
`tools/doc_check.py`（8 条检查 + `--selftest`，已进 smoke）；HANDOFF/GAPS/AUDIT/8.3 归档到
`docs/archive/` 与 `docs/audits/`，AGENTS 只留指针（"17 项闸门"从五个文件收到一处）。

### 2. 判据（命令 + 实际输出）

- `--test-snapshot`：`[wire] full 1 vitals 1 flags 0x7FF 1 idle delta 15 B；10 players 833 B, 82 B/player`
- `--test-net`：`[udpsrc] rejected 1 injection-drop 1 legit-after 1`；
  `[frag] real-link 10p 833 B ten 1 decode 1 nofrag 1 3000B 1 split 1 small 1`；
  `[slots] first 1 full 1 reported 1 freed 1 cycles 3/1 anonymous 1`；
  `[catchup] freeze 300 behind 293 catchups 1 accepted 196->203->259 late 0->0 settled 1`
- `--test-bindings`：`wall climb on V` / `interact on E` / `E is reserved for interact` 全 ok
- `--test-hitbox`：`leg multiplier reads movement.toml` ok（`hitbox.cpp` 里没有 0.7 了）
- `godot_check.ps1`：`hud … protocol=6 port=27115` + `interp prev=-16.0000 cur=-15.9877 gap=0.0123`
- `--bench 1000 --players 10`：p99 **2.40 µs** / max 4.20 µs / 0 allocs
- `python tools\doc_check.py`：9 ok / 2 warn（都是"契约里还没建的文件"）/ 0 fail；`--selftest` 6 fail

### 3. 失败与修正

1. 协议 v6 第一次跑闸门就红：encode 里 `w.u8(now.flags)` 忘了改成 u16，flags 0x7FF 被截成 0xFF；
   idle delta 的期望长度也多算了一个 count 字节。两条都是新闸门当场抓的。
2. `[slots]` 第一版 `cycles 0/0`：测试自己忘了释放"第二个被拒绝后又被放进来"的那个 id，
   不是产品 bug；把每轮释放写全才过。
3. `[catchup]` 第一版 `behind 5`：冻住客户端时 LoopbackTransport 把 300 份快照都排队了，
   恢复时逐份解完就追上了。真实 UDP 缓冲区不会留 4.7 秒——补了"丢积压"这一步才复现出 `behind 293`。
4. E 键改到 V 之后 `--test-bindings` 失败：测试把 jump 重绑到 V 与新的爬墙键冲突。
   换到 Z，并顺手加了"E 不许被任何移动动作占用"的检查。
5. doc_check 抓到六处真实漂移：AGENTS 指向已归档的 HANDOFF/GAPS、README 的 4.7/4.5 自相矛盾、
   STATE 的 `2.40 / 4.20` 被当成闸门计数（正则改成认 `smoke … N 项`）。

### 4. 下一步

拍 D1（穿透模型）/ D2（护甲 66% 与再生）/ D3（movement.toml 热重载）→ 签 M6 契约 →
W1 `sim/data/weapons.toml`（待建）+ WeaponDef。射击功能本身一行没写，不要把它算成"M6 已完成"。

## S61 · 2026-10-23 · 开 M6 前的代码体检（去冗余 + 架构）

### 1. 做了什么

- **测试去重与拆分**：删掉 `--test-net` 里的 `[snapfields]`（和 `--test-snapshot` 重复）；
  `tests_net.cpp` 81 KB → 拆成 `tests_net`（会话）/ `tests_transport`（UDP+分片）/ `tests_lobby`（准入+票据+组局），
  全部 ≤ 56 KB；5 份重复的 `poll` lambda 收成一个 `wait_for`。
- **槽位释放改成回调**：`drain_departed` 队列要求每个调用方都记得 drain，`net_loop` 第一天就漏了；
  改成 `set_departure_handler`，桥与 net_loop 各装一次，漏不掉。网关在 `abort_host` 里先清 handler。
- **补 tick 与加入共用一段循环**：`advance_to_clock()`；`kJoinSlack` 从两处局部常量收成一处。
- **收紧 API**：`UdpTransport` 删掉没有生产调用者的 `recv_from/adopt_peer`（只剩"已连接端点"一个职责）；
  `FragmentedTransport` 删掉没人读的 `kMaxDatagramBytes`；`ClientId` 移进 `sim/net/client_id.h`
  （`session.h` 不再为了一个 id 去 include 票据格式）。
- **让诊断量有读者**：30% 丢包闸门现在断言"确实丢了包"（`dropped 1205`）；真实链路分片闸门断言
  `dropped_packets()==0`（`abandon 1` 表示干净链路上没有丢弃半包）。原来这两个计数器写了没人看。
- **首帧相机**：`load_map` 末尾先 `remember_previous()`，插值从出生点开始，不再从世界原点滑入。

### 2. 判据

`tools\smoke.ps1`：19 项 + bench 全绿；`[30% loss] … dropped 1205`；`[frag] … abandon 1`；
`tests_*.cpp` 单文件最大 56 KB（原 81 KB）；`python tools\doc_check.py` 0 fail。

### 3. 失败与修正

1. 拆分后 `tests_lobby.cpp` 少了 `ports.h/session.h/transport.h`，-Werror 一次报出全部 15 个未声明符号。
2. 把 UDP 闸门改成"双端 connect"后忘了删 `from_host/from_port`，编译期报未使用变量才想起来。
3. `[frag] real-link` 的 printf 里一度写成 `ten_bytes.size()`（变量其实叫 `bytes`），编译立刻抓住。

### 4. 下一步

拍 D1/D2/D3 → 签 M6 契约 → W1 `weapons.toml`（待建）。射击功能仍然一行没写。

## S62 · 2026-10-24 · M6.0 规则落地（ADR 0006）

### 1. 做了什么

- **D1 穿透定稿**：武器决定能不能穿、穿几层；地图 `_penetrable 0` 一票否决。写进 ADR 0006。
- **D2 护甲改成按部位结算**（本次唯一改代码的部分）：
  `HitZone::Head` = 真伤，直接扣 HP、护甲不掉；胸/腿先扣护甲池（`absorb_percent = 100`），
  扣完溢出才扣血。`HitZone` 从 `hitbox.h` 移到 `combat.h`（它是伤害概念），
  `apply_damage(vitals, damage, zone, params)` 强制调用方想清楚打的是哪。
- **D3 热重载**：决定不做，文档改成"启动时读"，记 `DEBT-22`（lead M7）。
- 契约 draft → **active**，§6 改成 M6.0–M6.12 的单任务表，做完一个停一次。

### 2. 判据

`--test-health`（13 项）：`light armour: 30 body damage -> 25 armour, 5 HP`、
`headshot ignores armour`、`leg damage uses the armour pool`、`body TTK with heavy armour: 6 shots = 0.60 s`。
smoke 19 项 + bench 全绿；`doc_check` 0 fail。

### 3. 失败与修正

- 换成"护甲全吃"之后，旧测试里"light armour: 30 → 19 absorbed, 11 HP"就错了（那是 66% 模型的数）。
  没有改实现去迁就测试，而是按 ADR 重写了三条断言，并补了爆头真伤 / 腿部走护甲两条。
- `HitZone` 原来在 `hitbox.h`，而 `combat.h` 不能 include 它（`hitbox → pmove → combat` 会成环）。
  移到 `combat.h` 后依赖方向变成 `hitbox → combat`，环消失。

### 4. 下一步

M6.1 武器数据层：`sim/data/weapons.toml`（待建）+ `WeaponDef` 加载 + 独立哈希，
闸门 `--test-weapons`。**做完停下**，等下一句"继续"再开 M6.2。

## S63 · 2026-10-24 · 对照原文复核 M6（只读，不改代码）

### 1. 做了什么

把计划书里所有与 M6 有关的段落重新读了一遍（§4.1–4.2 来源与许可、§6.1–6.10 枪感与判定、
§7.8 确定性随机、§12.2 里程碑），并按"开源来源 → 我们取哪一层"整理成表。结论：
M6 的**设计**是拼装的（Red Eclipse 2 的参数字段 + TacZ 的字段补全 + Q3/OpenArena 的 hitscan 思路 +
CS 的弹道形态），**代码全是自己写的**，数值一个都不许照抄。

### 2. 判据

这次只动文档、没有改代码，所以没有新增闸门；收工仍然跑了 smoke（19 项 + bench，ALL GREEN）
与 doc_check（0 fail）。核对结果以行号引用给出：
`weapons.h/weapdef.h` 结构（L810–848）、TacZ 的 9 个字段（L872–892）、
"弹道必须线段扫掠，写进 M6 验收"（L1006）、三段判定（L1037–1060）、
ADS 灵敏度（L1018–1032）、追击终结（L1080–1113）、`rng_at` 使用清单（L1345–1350）。

### 3. 失败与修正（发现的问题，尚未修）

对照后发现 **3 处缺口 + 1 处范围风险**，写进了提案但没动代码，等拍板：

1. **高速弹道（线段扫掠 vs 胶囊）不在 M6.1–M6.12 里**，而原文把它写进了 M6 验收标准。
   但 v0.1 若只有 hitscan 武器，它没有使用者（验证不了）。倾向：写进 out + DEBT，随 M8 狙击枪做。
2. **`rng_at` 没进 M6 任务**：后坐力随机分量、霰弹、弹孔都要它（§7.8），现在是"实现了但没人用"。
3. **ADS 灵敏度缩放**（§6.7）没有归属：M6 只做 `ads` 状态位，缩放属于表现层，应随 M8 视角模型。
4. **`WeaponDef` 40+ 字段**若在 M6.1 一次全做，就是"没有使用者的代码"；应按任务分批长。

### 4. 下一步

拍板上面 1–4 → 改契约 §6 与 §2 out → M6.1（`weapons.toml` 待建 + WeaponDef + `--test-weapons`）。

## S64 · 2026-10-24 · M6.1 武器数据层

### 1. 做了什么

- **D4/D5 定稿**（按契约 §4 的建议默认执行）：高速弹道不进 M6（v0.1 全是 hitscan，没有使用者）→
  `BACKLOG FEAT-12`；`WeaponDef` 按任务分批，没有使用者的字段不先写。
- **`sim/data/weapons.toml`**：rifle / pistol 两把，11 个字段——伤害近/远、衰减起止、rpm、射程、
  弹匣、备弹、换弹秒数、穿透层数、每层伤害系数。pistol 的用途是给 M6.5 做对照（步枪能穿、手枪被挡）。
- **`sim/game/weapon.{h,cpp}`**：`WeaponDef` / `WeaponParams`（`find`、`hash`）/ `load_weapon_params`。
  加载规则只写在一处：缺键警告并用编译默认值、拼错报 unknown key、不能模拟的值直接报错。
- **`TomlBinder` 提出来共用**：原来是 `load_movement_params` 里的匿名结构，现在在 `sim/data/config.h`，
  两个加载器共用一份"读一个数、类型不对就警告"的逻辑——两份就会漂。
- 数据层硬化：TOML 解析拒绝重复键、容忍 UTF-8 BOM（原因见 §3）。
- 新闸门 `--test-weapons`（18 项检查），smoke 第 20 项；`tools/sim_sources.txt` 28 个 sim 源文件。

### 2. 判据

`[weapons] weapons 2 clean 1 stable 1 independent 1 defaults 1 rejects 5`；
smoke 20 项 + bench ALL GREEN；`sources ok: 28`；`doc_check` 0 fail。
反证（闸门能红）：把 `damage_near` 拼成 `damgae_near` 的一份数据放进 `--data` 同目录，
闸门打印 `clean 0` 并退出 1——不是靠"我看它绿了"。

### 3. 失败与修正

- 首次构建红在 `-Wmissing-field-initializers`（`TomlBinder{t, w}` 少给 `used`）：加构造函数，不改警告等级。
- "缺键保留默认值"一开始用 `damage_near = 10`，撞上编译默认的 `damage_far = 18`，被校验拒了。
  不是 bug，是校验对了——改成 30，并在测试里写清为什么。
- 用 PowerShell 改 toml 做反证时，`Set-Content -Encoding UTF8` 在文件头写了 BOM，
  解析器在第 1 行报 `expected key = value`（真正的病根看不见）。顺手让 `load_toml_file` 容忍 BOM，
  并加一条闸门钉住；这不是放水：编辑器存 BOM 是常态，报错应该指向真问题。
- 顺带发现重复 `[section]` 会静默只生效一半（`find` 取第一个），现在解析器直接报 `duplicate key`。

### 4. 边界 / 下一步

- 武器表还没进 `World`，也还没进 join 哈希：M6.2 做（两台机器枪数据不一致必须拒绝入局）。
- `headshot_mult` / `legshot_mult` 仍在 `movement.toml`，M6.6 定唯一归属，不许两份都活。
- 下一步 **M6.2 开火输入**（三个按下沿 + flags 7–9）；做完停下，等下一句"继续"。
## S65 · 2026-10-24 · M6.2 开火输入 + 许可边界（ADR 0007）

### 1. 做了什么

- **按钮位掩码**：`sim/input/buttons.*` 定义 10 个按钮位（jump/crouch/slide/dash/wallrun/
  wallclimb/vault/fire/reload/ads）。命令包、桥的 `set_input(f, r, yaw, buttons)`、
  GDScript 的 `get_action_bits()` 共用这一份——加按钮不再加一个位置参数。
- **开火链路**：`MoveInput` 加 fire/reload/ads；`PlayerState` 加同名按住状态（不是一 tick 脉冲：
  丢一个包不该把“还在扣扳机”读成“松手”）；进 `state_hash` 与快照 flags 7–9（v6 预留位，未升版本）。
  左键开火 / 右键开镜 / R 换弹进 keybinds.toml 与 Bindings 默认值。
- **枪表进 join 哈希**：`World::weapons` + `load_weapons_beside`；`combined_params_hash(m, c, w)`
  在服务器、客户端、桥、headless 四处一致。两侧枪数据不同 → 拒绝入局。
- **许可边界**（ADR 0007）：GPL 只读事实、实现自写；台账 `docs/assets-licenses.md`（含
  「只读参考」表：RE2/TacZ/OpenArena/Xonotic 各取了哪条事实）；铁律 K；`check_sources.ps1`
  扫到 GPL/AGPL 头就红。
- weapons.toml 头部写清“加一把枪 = 加一段”；闸门新增两条：加第三把枪不改代码、加枪会动哈希。

### 2. 判据

`[fireflags] held 92/92 released 52/52 agree 144/144`；`[config] … weapons 1`（枪表不同被拒）；
`--test-snapshot` 的 trigger 位往返 + `--test-bindings` 的 fire/aim + 掩码往返；
`[weapons] … rejects 5` 且“a new gun is one new section”为 ok。smoke 20 项 + bench ALL GREEN。

### 3. 失败与修正

- `[fireflags]` 第一版直接逐 tick 比服务器与客户端：`held 94/100`、`agree 148/160`。
  不是 bug——客户端把命令提前 4 tick 盖戳，切换后的前几 tick 服务器还在执行旧输入。
  改成跳过 8 tick 的暖机段再采样，数字变成 92/92、52/52、144/144；暖机原因写进注释。
- 桥的 `set_input` 原本要把 10 个 bool 变 13 个；改成传掩码，GDScript 建一次表。
  这条不是顺手：再加两个动作，参数顺序就没人记得住了。

### 4. 边界 / 下一步

- 数字 id 进快照（Weapon 组）留给 M6.7，届时 `weapons.toml` 加显式 `slot`（现在按 id 字符串认）。
- 后坐力、扩散矩阵、ADS 仍按任务分批；仿 TacZ 只仿结构，数值自己推导（ADR 0007）。
- 下一步 **M6.3 静态 hitscan**（射线 vs 玩家盒 + 世界，E1 命中/不中）；做完停下。
## S66 · 2026-10-24 · M6.3 静态 hitscan

### 1. 做了什么

- **`sim/game/hitscan.*`**：`hitscan(world, boxes, count, origin, dir, max_distance, ignore_entity)`。
  先扫世界（`trace_box` 零尺寸 = 点射线），再逐玩家 AABB 求交，**最近者胜**——墙挡人。
  方向在函数内部归一化：传 (10,0,0) 和 (1,0,0) 得到同一个距离；零向量算未命中，不是 NaN。
- **玩家体积复用 `HitboxSample`**：所以 M6.4 的回溯只是把“当前帧”换成“回溯帧”，签名不变；
  M6.6 用 `hit_zone_at(sample, point.z)` 就得到部位。
- 判定形状是 **AABB**，和 `hitbox_contains` 完全一致：判定与可视体积不能各说一套。
- 第 21 项闸门 `--test-hitscan`，14 条检查（E1 的“必中/必不中”在里面）。

### 2. 判据

`[hitscan] chest 1 face 1 wide 1 edge 1 crouch 1 wall 1 range 1`；smoke 21 项 + bench ALL GREEN。
**闸门能红**（这次真做了变异验证）：把世界检测短路掉，`a wall between them stops the shot`
立刻 FAIL、退出码 1；改回来再绿。

### 3. 失败与修正

- 读 `.map` 才发现薄墙只覆盖 y ∈ [-2, 2]（不是整张图）。第一版差点把“瞄准点偏一个盒宽”的测试
  放在 y=0 的车道上——那条射线会先打到墙，测试会“通过”但它证明的是墙，不是盒宽。
  现在玩家测试全在 y=5 的开阔道，墙测试单独在 y=0。
- 0.40 中 / 0.41 不中这一对是故意加的：只测“偏 0.81 不中”无法区分“盒宽正确”和“判定偏小”。

### 4. 边界 / 下一步

- **还没有俯仰**：`MoveInput` 只有 yaw，`hitscan` 接的是显式方向。谁生成方向（以及 pitch 进命令包
  要不要升协议 v7）在 M6.6 决定——这条已经写进契约那一行，不会漏。
- M6.5 的穿透需要知道“打到了哪个 brush”，届时给 `ShotHit` 加 `const Brush*`（现在加了没人用）。
- 下一步 **M6.4 hitbox 回溯**（6 tick 窗口 + 帧间插值）；做完停下。
## S67 · 2026-10-24 · M6.4 hitbox 回溯（公式 + 插值）

### 1. 做了什么

- **`sim/game/rewind.*`**：
  `rewind_ticks(one_way_ms)` = 单程延迟 × 64拍 + **1 tick 插值**，夹到 `kMaxRewindTicks = 6`。
  按 v0.4 §9.1 的推导：30 ms RTT + 15.6 ms 插值 + 5 ms 抖动 ≈ 51 ms → 4 tick，取 6 作裕度。
  这行是公式不是经验值：改输入（延迟/插值）而不是改常数。
- `rewind_frame(histories, count, shot_tick, rewind, out, cap)`：从 64 tick 历史里取
  `shot_tick - rewind` 那一帧，**小数部分用 `HitboxHistory::sample` 插值**（不取整帧）。
  比最旧帧更老 → 夹到最旧，**绝不外推**。零分配：缓冲区由调用方持有。
- 闸门加 8 项到 `--test-hitscan` 的 `[rewind]` 段：靶子以 8 m/s 横穿火线，6 tick 走 0.75 m，
  超过半个盒宽——同一个瞄准点，打回溯帧命中、打当前帧落空。

### 2. 判据

`[rewind] window 6 at-6 1 current-miss 1 interp 1 clamp 1`；smoke 21 项 + bench ALL GREEN。
**闸门能红**：把 `shot_tick - rewind` 改成 `+`，4 项立刻 FAIL（帧位置、命中、插值、夹取）。

### 3. 失败与修正

- 第一版 clamp 项期望 `y = -7.5`（tick 0），实际失败：hitbox 环只有 **64 tick**，
  我录了 101 帧，tick 0 早被覆盖。改成录 61 帧（一个整秒）并把用例改成“早于最旧帧”，
  期望值随之改成 tick 40 的位置——测试因此还顺手钉住了“历史只有 1 秒”这件事。
- 体感上“晚 6 tick 仍命中”靠的是靶子**横穿**火线（沿 y 走）。第一版让靶子沿射线方向走，
  那样不管回不回都命中，测试等于没测。

### 4. 边界 / 下一步

- 回溯的**生产消费者**还没出现：真正开枪（用输入方向 + `rewind_ticks(实测 RTT)`）在 M6.6/M6.7。
- 客户端预测命中（v0.4 §9.3）需要“同一个函数 + 同一份历史”，也就是这套 API；但要等伤害落地。
- 下一步 **M6.5 穿透**（地图 `_penetrable` + 每层 ×0.7）；做完停下。
## S68 · 2026-10-24 · M6.5 穿透（地图否决 + 每层 ×0.7）

### 1. 做了什么

- **地图侧**：`Brush::penetrable`，来自实体的 `_penetrable`（和 `_climbable` 同一个解析点）。
  ADR 0006 D1 的默认值是**挡**：没写这个键的墙就是混凝土。它同时进 `geometry_hash`——
  两张只差 `_penetrable` 的图不能互通，否则一发子弹的结果取决于谁当房主。
- **判定侧**：`hitscan_penetrating`。先取射线上最近的世界表面与最近的玩家：玩家在前就是命中；
  世界在前就先问地图（不可穿 → `stopped_by_map`），再问武器预算（层数用完 → `out_of_layers`）；
  都放行就穿过这一层、`damage_scale *= 0.7`，从**凸体出口**（六个半空间里沿射线方向最近的那个面）
  再推进 1 mm 继续。原来的 `hitscan` 现在是它的 `layers = 0` 包装——只有一条射线实现。
- **接口**：`PenetrationParams { layers, damage_scale }` 代替两个位置参数。以后要按材质给不同系数，
  改的是这个结构体怎么填（或让它带一个 per-brush 乘数），不是 `hitscan_penetrating` 的签名。
- 新地图 `sim/data/maps/penetrate_test.map`：x=0 与 x=2 两堵可穿墙、x=5 一堵混凝土；
  专供闸门，不污染 movement 基准。

### 2. 判据

`[penetrate] walls 2 one 1 two 2 scale 0.49 pistol 1 concrete 1 open 1`；smoke 21 项 + bench ALL GREEN。
**闸门能红**：把地图否决短路掉，`concrete vetoes even five layers` 立刻 FAIL（步枪穿过了混凝土）。

### 3. 失败与修正

- 生成测试地图时把 worldspawn 的闭括号切掉了（12 个房间 brush 其实都在同一个实体里），
  解析器报 `line 59: expected a plane`。改成显式拼实体块，`--info` 校验 9 个 brush 才算过。
- `dot` 与 `types.h` 里的同名函数撞了（匿名命名空间遮蔽），编译报 ambiguous；删掉自造的那份，
  直接用现成的 `dot` / `length`——同一类工具不该有第二份。

### 4. 边界 / 下一步

- 穿透**还没有伤害**：`damage_scale` 只是结果里的一个数，M6.6 才把它乘进 `apply_damage`。
- 每层目前一个系数（来自武器）；按材质分系数时，在循环里再乘一个 per-brush 数即可，
  调用点与 `ShotResult` 都不用动。
- 下一步 **M6.6 三段伤害**（部位倍率 × 距离衰减；同一定下“谁提供射线方向”与倍率的唯一归属）；做完停下。
## S69 · 2026-10-24 · 射击链路贯通（M6.6/M6.7 框架，协议 v7）

### 1. 做了什么

- **俯仰**：`MoveInput.pitch`，命令包加 f32 → **协议 v7**。`view_forward(yaw, pitch)` 与
  `eye_position()` 成为“眼睛在哪、朝哪看”的唯一实现；桥的 `get_eye_height()` 改用同一个
  `kEyeHeightScale`。没有 pitch，子弹永远只能平着飞。
- **武器身份**：`WeaponDef.slot` 是显式字段（0..63、唯一），不是文件下标——在文件中间插一把枪
  不会改别人的 id。`find_by_slot` + `equip_weapon`。
- **运行时状态**：`PlayerState` 加 `weapon_id / ammo / reserve / fire_cooldown / reload_left`，
  全部进 `state_hash` 与 Weapon 快照组（教训 16）。满编快照 833 → **873 B**（227 B 余量）。
- **`sim/game/shoot.*`**：`update_weapon` 先走计时器再开火；服务器与客户端都调它（弹药/冷却一致），
  但客户端世界里没有受害者，所以伤害仍只有服务器算（A4）。开火 = `eye_position` +
  `view_forward` + `rewind_frame` + `hitscan_penetrating` + `hit_zone_at` + `damage_at_range`
  → `apply_damage`。射速 `round(60×64/rpm)`（600 rpm → 6 tick），换弹 `round(秒×64)`。
- **第 22 项闸门 `--test-damage`**：胸 25、护甲多吃两发、爆头 125 一枪、墙全挡、换弹填满。

### 2. 判据

`[damage] body 4 rounds armour 10 head 125 wall hits 0 reload 30`；smoke 22 项 + bench ALL GREEN；
`--test-snapshot`：`protocol v7 … 873 B, 86 B/player`；`[fireflags] 92/92 52/52 144/144` 仍绿。

### 3. 失败与修正（以及一个必须记下的发现）

- 眼高 0.9×身高 = 1.278 m，而头段起点是 0.82×身高 = 1.164 m → **平射同身高对手必中头**。
  这不是 bug 而是参数冲突，但会让 TTK 直接崩（×5 一枪）。记成 `BACKLOG DESIGN-09`，
  等拍板（抬头段起点 / 压眼高 / 接受）；闸门里打胸时显式压了 -0.05 rad。
- 服务器还没有测 RTT，回溯只能用 1 tick（`rewind_ticks(0)`）。记成 `DEBT-23`，
  不在本轮顺手造一个假的延迟估计。
- 编译期两次小错：`near()` 未使用（-Werror）、`tests_shoot.cpp` 少 include；都按规矩修，没降警告。

### 4. 边界 / 下一步

- 半自动 vs 自动、开镜扩散、后坐力、命中提示、死亡重生都还没有；**M6.8 的两端 HUD 断言**是下一件。
- 现在能从扣扳机一路走到“对方掉血”，但画面上还看不出来——这是下一步要补的。
## S70 · 2026-10-24 · M6.8 与表现层（曳光、敌方红轮廓）

### 1. 做了什么

- **M6.8**：桥加了一个**只给闸门用**的 `apply_test_damage(slot, amount)`（走真实 `apply_damage`），
  房主在客户端加入后打 slot 1 三十点；`godot_check.ps1` 新开一段双进程检查，客户端 HUD 打出
  `hud hp=70` 即通过。伤害来源→快照→对端 HUD 这条链第一次有证据（AUD-16 关闭）。
- **曳光**：`EventType::Shot`（a = 枪口、b = 命中点或射程尽头、param = 武器 slot），
  `update_weapon(..., events)` 每次开火发一个；桥 `drain_events()` 本来就带 a/b；
  GDScript 生成一根细盒子、`look_at` 对齐、0.09 s 淡出。
- **单机也能开火**：之前 solo 的 `step()` 只走 `step_single`，扣扳机什么都不会发生。
  现在 solo 也调同一个 `update_weapon`，所以弹药/冷却/曳光在离线时同样工作。
- **敌方红轮廓**：远端玩家节点加一个略大的胶囊壳（`CULL_FRONT` + unshaded 红），
  只画背面得到红边；蹲下随节点缩放。

### 2. 判据

- `godot_check.ps1`：`== tracers ==` → `tracer events=14`；`== damage reaches the other HUD ==` →
  房主 `damage slot 1 hp=70`、客户端 `hud hp=70`。
- `--test-damage` 加两条：每次开火恰好一个 Shot 事件、事件从眼睛出发且沿枪口方向。
- smoke 仍是 22 项 + bench 全绿（没有新增顶层闸门，检查落在第 18 项里）。

### 3. 失败与修正

- `NetServer::world()` 只有 const 版，桥的伤害钩子拿不到可变世界；加了非 const 重载。
  （这是测试钩子，但重载本身是对的：服务器本来就拥有这个世界。）
- 第一版把 solo 开火漏了：`--fire-check` 会打印 `tracer events=0`。不是猜测，是闸门先红。

### 4. 边界

- 别人的枪口火光/曳光看不到（事件还没上网络；M7 事件通道）。
- 没有队伍，所以“远端 = 敌方”；FEAT-14 记下分队后要按队伍着色。
- hitmarker、伤害数字、枪口火光仍未做（M6.11 剩余）。
## S71 · 2026-10-24 · M6.9 死亡与重生

### 1. 做了什么

- **死亡不是新字段**：`hp > 0` 就是活着，flags bit10 继续预留。理由写在 AGENTS §8.14——
  派生值进两份真值，迟早有一份是错的（教训 16 的同一类问题）。
- 死后输入进不了 pmove（换成空 `MoveInput`），但**照样走重力和落地**：冻结整个 step 会让尸体
  悬在半空，看起来像 bug。`update_weapon` 第一行拦掉扳机。
- **尸体不挡枪**：`HitboxSample` 加 `alive`，`hitscan` 跳过 dead 帧；插值取两帧的 and。
  这条让 `--test-damage` 的老断言“7 发打 7 次命中”变成 4——不是回归，是行为变了，断言跟着改。
- 重生：`sim/game/respawn.*` 的 `tick_respawn` 计 320 tick（5 s），到点用 `make_spawn_player()`
  （与入局同一份定义）重造玩家，并 reset 该槽的 hitbox 历史。服务器侧独有，客户端只从快照看到。
- HUD 在 hp<=0 时显示 `DEAD`。

### 2. 判据

- `--test-damage` 新增 6 项：杀死、死后不能开火、计时器带回、重生是全新玩家（满血/满弹/回出生点）、
  尸体不挡射线、活人挡射线。
- `godot_check.ps1` 的伤害段扩成三段：房主 30 → 70，再 100 → 死，5 s 后 HUD 回 100；
  断言是**顺序**（`hurt@310 dead@616 back@2171`），不是三个独立的 -match。

### 3. 失败与修正

- 第一次写闸门时用绝对 tick 做阶段判断（`now > 160`），但房主在客户端加入前已经跑到 800+ tick，
  三个阶段在同一帧连发，客户端只看到 0 与 100，`hud hp=70` 一次都没打出来。改成相对 `damage_hurt_tick`。
- 排查时以为快照没带 Vitals，加了 `get_snapshot_hp()` 诊断才看清：客户端日志尾部全是重生后的 100，
  死亡窗口在更早的位置。**先看全量日志再改代码**。

### 4. 边界

- 没有比分/击杀播报（E5 的另一半），M8。
- 重生点固定（`spawn_point_for(i)` 按槽位偏移），没有出生点选择与保护。
- HUD 没有重生倒计时（客户端不知道服务器计时器）。
## S72 · 2026-10-24 · M6.10 查表后坐力（协议 v8）

### 1. 做了什么

- **表在数据里**：`recoil_pitch` / `recoil_yaw` 是 weapons.toml 里的数字数组（每发一步，弧度），
  `recoil_recovery_ticks` 是沉默复位时间。第 1 发是直的，之后逐发累积；到表尾不再涨。
  TOML 子集为此加了一个类型（`[1.0, 2.0]`），只支持数字。
- **应用在方向**：`view_forward(yaw + kick.yaw, pitch + kick.pitch)`，命中和相机用同一个偏移；
  相机通过 `get_recoil_offset()` 读，所以准星和弹着点不会分家。
- **进快照的是索引**（`recoil_index`/`recoil_recovery`，协议 v8）：它们决定命中方向，必须回滚一致。
  punch 本身不进快照——它是索引的纯函数，客户端自己算（A4）。
- 沉默 24 tick（0.375 s）复位，不是松扳机就复位：点射能恢复准度，按住不放一路爬到表尾。

### 2. 判据

`recoil: shots 12 first z 1.278 fifth z 1.848` —— 12 发打 15 m 外的墙，第 5 发比第 1 发高 0.57 m；
`--test-weapons` 的“一个后坐力步进也进哈希”；`--test-snapshot` 打 `protocol v8 … 903 B, 89 B/player`。
smoke 22 项 + bench ALL GREEN；godot_check 全过（protocol 8）。

### 3. 失败与修正

- 第一次把 `recoil_recovery_ticks` 当必填键，`--test-weapons` 的“每个缺失键都报警”从 10 变成 11 而红。
  想清楚后改成可选（没有表就不需要复位计时），并加“有表则必须 > 0”的校验——比硬凑计数正确。
- 打印行 `reload 30` 一度变成 `reload 18`：后坐力测试排在换弹后面又打空了半个弹匣，
  汇总行读的是“当前 ammo”。改成在换弹断言处存快照值，汇总读那个值。

### 4. 边界

- 开镜（ADS）降低后坐力还没做：M6.11 的 `ads_*` 字段要乘到 `recoil_offset` 上。
- 半自动/自动的区分仍未做（手枪现在也是按住连发）。
- 没有后坐力恢复的视觉平滑（相机直接跟索引走，一格一格），等 M7 的视角层。
## S73 · 2026-10-24 · M6.11 表现层（hitmarker / 伤害数字 / 枪口火光 / ADS）

### 1. 做了什么

- **hitmarker 走快照，不走事件**：铁律 C 的原话是「伤害数字只走快照」。`update_weapon` 命中时把
  `hit_target/amount/zone/serial` 写在射手身上，进新的 Hit 组（5 B/人，协议 **v9**）；客户端比较
  serial 变没变判断“是不是新命中”，显示 0.3 s。打空、被墙挡、打尸体都不亮。
- **枪口火光**：同一份 `Shot` 事件在枪口生成一个 0.06 m 发光球，0.05 s 回收。
- **ADS**：`ads_recoil_scale` 乘在 `recoil_offset` 上（步枪 0.55），FOV 75→55 是纯表现。
- **闸门**：`godot_check` 新开 `== hitmarker ==`——房主**真的瞄准并开枪**（不是测试钩子），
  命中后打出 `hitmark serial=1 amount=25 target=1 zone=1`。

### 2. 判据

- `--test-damage` 4 条：命中记录字段、第二发 serial+1、打空不动 serial、ADS 弹着点更低
  （`plain fifth z 1.848` → `ads fifth z 1.592`）。
- `--test-snapshot`：`protocol v9 … 953 B, 94 B/player`；`godot_check` 全过（hitmark 一段）。

### 3. 失败与修正

- 房主的 hitmarker 永远不亮：`local_player()` 对房主返回**预测**世界里的自己，而那个世界只有一个玩家，
  伤害发生在权威世界。改成：有权威世界（host/solo）时读它，客户端才读预测。
  ——这类“两个世界”的坑值得记：房主的 HUD 数值可以来自预测（会被快照修正），但**命中记录**在预测里
  永远是 0，因为它没打中过任何人。
- 打空的测试最初一直“命中”：靶子挪走后 hitbox 历史还留着旧位置（回溯正好读到）。先空跑 8 tick 再打。

### 4. 边界

- 没有命中音效（M7 音频）。
- 伤害数字是命中者自己看到的；没有“被打的人看到的受伤方向提示”。
- 枪口火光只有自己看得到（事件还没上网络，M7）。
## S74 · 2026-10-24 · M6.12 验收：E3 没达标

### 1. 做了什么

- **TTK 表**：把伤害公式提成 `shot_damage()`（shoot.cpp 与表共用一份，防止各算一套），
  `--test-damage` 用真实 `apply_damage` 循环算出 4 甲 × 3 部位的表，钉住上界 1.2 s 与结构关系。
- **E6**：`--test-net` 新增 `[shotloss]` —— 30% 丢包、20 ms 延迟，客户端朝 2 m 外的目标开火，
  断言「服务器掉血 + 客户端拿到 hitmark」，并且**丢包数必须 > 0**（否则这个检查什么都没证明）。
- **验收记录** `docs/audits/2026-10-M6-acceptance.md`：E1–E9 逐条证据 + E9 手测清单。

### 2. 判据

`[shotloss] loss 30% dropped 187 target 100->0 hitmark 4/25`；
`ttk rifle chest none 0.28s/4 light 0.38s/5 regen 0.38s/5 heavy 0.47s/6`；
bench `p99 2.40 µs / max 6.00 µs / allocs 0`。

### 3. 失败与修正（这一节是本轮的重点）

- **E3 未达标**：目标躯干 0.6–1.2 s，实测裸胸 0.28 s、重甲 0.47 s。25 伤害 × 600 rpm 两处叠加，
  再被 `DESIGN-09`（平射必中头 → 一枪 125）放大。**没有偷偷调数据让它“过”**，记成 `DESIGN-10`，
  验收记录里写了差距和三种调法；闸门只钉上界与结构关系。
- hitmarker 在 30% 丢包下最初永远不亮：`apply_snapshot` 发现“模拟状态相同”就直接丢弃快照，
  而命中记录不算模拟状态，于是被一起丢了。改成“状态相同也要把表现字段抄过去”。
  这类 bug 只有把整条链路跑一遍才会露头，单元测试永远测不到。
- `printf` 里的 `30%` 被当成格式符（`-Werror=format`），改成 `30%%`。

### 4. 边界 / 下一步

- M6 **不宣布关闭**：E3 等调参、E9 等手测。机制层面没有已知缺口。
- 下一步优先级：先拍 DESIGN-09/10（它决定 M7 的手感基线），再手测 E9。

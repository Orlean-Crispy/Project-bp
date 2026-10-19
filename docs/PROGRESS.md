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
| docs/8.3候选B_v0.4.md | §8.3 选定方案：可破坏入口（0.5 m 四档、每图 3 面、每队 2 次/回合、加固 1 处、C4 3.5 s） |

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

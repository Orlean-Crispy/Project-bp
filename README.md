# Project Breakpoint

5v5、有爆破目标的 Apex 式自由移动 FPS。**移动按 Apex，判定按 CS。**
Godot 4.7 宿主 + 自研 C++17 模拟层（零 Godot 依赖），64 tick，目标局域网。

> 设计文档：`docs/计划书_v0.3.md`
> 约束：`docs/AGENTS.md`　进度：`docs/PROGRESS.md`　事故：`docs/pitfalls/`

## 现在能做什么（M1–M4 + Godot 宿主已接）

- **Godot 4.5 宿主 + GDExtension 桥**：窗口、渲染、输入都走 Godot；模拟层仍然是零依赖的 C++
- 读 TrenchBroom 的 `.map`（Standard / Quake 2 四点面），**入口一次**把 Quake unit 转成米
- brush 平面求交成凸体，盒体扫掠 trace（Minkowski 扩张，不是点采样）
- 分步 `slide_move`：按最小墙厚切步，20 m/s 撞 0.1 m 墙 100 次 0 穿透
- 基础 pmove：地面加速/摩擦、空中控制、跳跃、蹲、**step-up 上台阶**、蹲下空间检查
- parkour 五件套：**滑铲 / 冲刺 / 墙跑 / 爬墙 / 翻越**，全部走整数 tick 状态机
- **键位数据驱动**：`sim/data/keybinds.toml`，默认瓦洛兰特布局（C 翻越 / Q 墙跑 / E 爬墙 / X 冲刺），
  运行时可改、存盘即生效；sim 内部看不到键名
- 无状态 RNG、逐 tick 世界哈希
- **demo 录制 / 回放**：存输入 + 种子，回放时逐 tick 重算并比对哈希，分叉能定位到具体 tick
- **跨机构建比对**：两台机器各导一份位置 CSV，`--compare` 报最大偏差（容差 0.01 m）
- `move_metrics` 四张表（CSV），可与 Q3 的数值对比

## 快速开始

```powershell
powershell -File tools\setup_toolchain.ps1   # 只需一次，装到 C:\bp-toolchain
powershell -File tools\run_demo.ps1          # 构建桥 + 启动 Godot demo
powershell -File tools\build.ps1             # 配置 + 全量构建（-Werror）
powershell -File tools\smoke.ps1             # info / 确定性 / 穿墙 / 扫掠 / 性能
```

单项运行：

```powershell
build\headless_sim.exe --info                       # 地图统计
build\headless_sim.exe --selftest --ticks 600       # 同输入双跑，逐 tick 哈希
build\headless_sim.exe --test-wall                  # 5 个速度档 × 100 次穿墙测试
build\headless_sim.exe --test-ray                   # 点/盒的单段扫掠
build\headless_sim.exe --test-step                  # 台阶 / 挡墙 / 蹲下空间
build\headless_sim.exe --test-parkour               # 滑铲 / 冲刺 / 墙跑 / 爬墙 / 翻越
build\headless_sim.exe --keys                       # 打印当前键位 + 冲突
build\headless_sim.exe --test-bindings              # 改键 / 存盘 / 重载 全链路
build\headless_sim.exe --record build\run.bpd       # 录一段 demo
build\headless_sim.exe --play build\run.bpd         # 回放并逐 tick 比对哈希
build\headless_sim.exe --play build\run.bpd --dump a.csv
build\headless_sim.exe --compare a.csv b.csv --tolerance 0.01
build\headless_sim.exe --test-demo                  # 录制/回放/分叉检测 全链路
build\headless_sim.exe --bench 1000                 # 单 tick 耗时 p99
build\move_metrics.exe                              # 四张手感表
```

## 目录

```
sim/core/        tick 时钟、世界状态与哈希、无状态 RNG、向量
sim/collision/   .map 解析、brush 凸体、盒体扫掠、分步 slide
sim/move/        pmove（地面/空中/跳跃/蹲 + parkour 五件套）
sim/input/       键位表：物理键枚举、动作映射、改键/存盘/冲突检测
sim/demo/        demo 二进制格式：录制、回放、头部校验
bridge/          GDExtension 桥（唯一的跨边界层）
godot/           Godot 工程：project.godot / main.tscn / main.gd
sim/data/        movement.toml（手感数值）、keybinds.toml（键位）、maps/*.map
tools/           headless_sim、move_metrics、build/smoke/setup 脚本
docs/            AGENTS / PROGRESS / pitfalls / keymap / 计划书
```

## 两条别踩的线

1. 工具链必须在 **ASCII 路径**（`C:\bp-toolchain`）。中文路径下 GCC 能编译、**ld 不能链接**。
2. 盒体扫掠的支撑距离用 **min**。用 max 会让 100% 的盒子直接穿过墙。
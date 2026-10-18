# Project Breakpoint

5v5、有爆破目标的 Apex 式自由移动 FPS。**移动按 Apex，判定按 CS。**
Godot 4.7 宿主 + 自研 C++17 模拟层（零 Godot 依赖），64 tick，目标局域网。

> 设计文档：`docs/计划书_v0.3.md`
> 约束：`docs/AGENTS.md`　进度：`docs/PROGRESS.md`　事故：`docs/pitfalls/`

## 现在能做什么（M1 已完成）

- 读 TrenchBroom 的 `.map`（Standard / Quake 2 四点面），**入口一次**把 Quake unit 转成米
- brush 平面求交成凸体，盒体扫掠 trace（Minkowski 扩张，不是点采样）
- 分步 `slide_move`：按最小墙厚切步，20 m/s 撞 0.1 m 墙 100 次 0 穿透
- 基础 pmove：地面加速/摩擦、空中控制、跳跃、蹲、**step-up 上台阶**、蹲下空间检查
- 无状态 RNG、逐 tick 世界哈希
- `move_metrics` 四张表（CSV），可与 Q3 的数值对比

## 快速开始

```powershell
powershell -File tools\setup_toolchain.ps1   # 只需一次，装到 C:\bp-toolchain
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
build\headless_sim.exe --bench 1000                 # 单 tick 耗时 p99
build\move_metrics.exe                              # 四张手感表
```

## 目录

```
sim/core/        tick 时钟、世界状态与哈希、无状态 RNG、向量
sim/collision/   .map 解析、brush 凸体、盒体扫掠、分步 slide
sim/move/        pmove（地面/空中/跳跃/蹲；parkour 待 M3）
sim/data/        movement.toml（唯一手感数值来源）、maps/*.map
tools/           headless_sim、move_metrics、build/smoke/setup 脚本
docs/            AGENTS / PROGRESS / pitfalls / 计划书
```

## 两条别踩的线

1. 工具链必须在 **ASCII 路径**（`C:\bp-toolchain`）。中文路径下 GCC 能编译、**ld 不能链接**。
2. 盒体扫掠的支撑距离用 **min**。用 max 会让 100% 的盒子直接穿过墙。

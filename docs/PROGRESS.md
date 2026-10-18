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

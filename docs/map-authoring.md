# 地图制作（Blender → .map）

> 模拟层**直接读 `.map` 的 brush**（没有 BSP、没有网格碰撞）。所以地图不是“在 Godot 里摆模型”，
> 而是“写一组凸体”。这份文档写清楚从 Blender 到可玩地图的那条路。

## 1. 为什么不是 Godot 插件

查过了，结论要写在前面，免得下次又找一遍：

| 方案 | 状态 | 结论 |
|---|---|---|
| Godot 导入 Blender 的 glTF | 内置，能用 | 只能拿**外观**，碰撞是 mesh，不是 brush —— 与本项目判定模型冲突 |
| `io_scene_map`（Blender contrib 的 .map 导入导出） | 存在，停在 Blender 2.66 时代 | GPL 代码，且老到在 5.0 上不可靠；**不进仓库**（ADR 0007） |
| TrenchBroom | Quake 系标准 .map 编辑器，成熟 | 它才是“原生工具”；但所有者想用 Blender 的建模手感 |
| **本项目自带导出器** | `tools/blender/bp_export_map.py`（约 250 行，自写） | ✅ 唯一能同时满足“用 Blender”“输出 brush”“许可干净”的方案 |

导出器不是插件市场的现成货，是**按我们的约定写的一支脚本**：Blender 里每个物体 → 一个 brush。

## 2. 三条不可协商的约定

1. **Z 轴向上，单位是米，但 `.map` 里存的是 Quake 单位**（1 unit = 0.0254 m）。
   导出器负责乘 `1/0.0254`；解析器在入口乘回来。**只在这两处换算**（铁律 A）。
2. **每个物体必须是凸体**。box / 凸棱柱 / 切角盒都可以；L 形、带洞、合并的重叠体不行。
   导出器会逐顶点验证（所有顶点都在每个面平面之内），不凸就报错并停下——**不产出坏图**。
3. **面朝向**：Quake 约定三点从外面看是顺时针，`make_plane()` 用 `cross(a-b, c-b)` 求法线，
   必须朝外。导出器用 Blender 的面法线校验，反了就换点序。

## 3. 材质与标志位

材质名（同时也是渲染颜色）只有 9 个，写在 `bridge/mesh_common.h`：

`FLOOR / CEILING / WALL / THINWALL / PLATFORM / STEP / LOWCEIL / METAL / CONCRETE`

对象自定义属性 → 导出成实体键：

| 属性 | 键 | 默认 |
|---|---|---|
| `surface`（或材质名） | 直接写进每个面的纹理名 | `CONCRETE` |
| `climbable = 1` | `_climbable 1` | 0（**默认不能爬**） |
| `wallrunnable = 1` | `_wallrunnable 1` | 0 |
| `penetrable = 1` | `_penetrable 1` | 0（**默认挡弹**） |

默认关闭是设计（计划书 §5.7）：地图作者必须显式开启，否则“不可达区域”没有意义。

> 标志位是**实体级**的（解析器按实体给 brush 打标），所以导出器把同标志的物体归进一个实体块。

## 4. 用法

```powershell
# 无头导出（批处理 / CI 用）
& 'C:\Program Files\Blender Foundation\Blender 5.0\blender.exe' --background --factory-startup `
  --python tools\blender\bp_export_map.py -- build\my_map.map

# 自检：造一个 40x40 盒子房 + 平台 + 可穿薄墙 + 可爬墙，导出并验证
& '...\blender.exe' --background --factory-startup `
  --python tools\blender\bp_export_map.py -- --selftest build\blender_test.map
.\build\headless_sim.exe --map build\blender_test.map --info
```

在 Blender 里：物体放进集合 **`BP_MAP`**（没有该集合就导出全场景的 mesh），
然后 `File > Export > Project Bp Map`（装成插件时）或 Text Editor 里跑脚本。

## 5. 设计约束（计划书 §8.4 / §8.5 / §5.7）

| 项 | 值 | 出处 |
|---|---|---|
| v0.1 尺寸 | **40×40 m**，1 个目标点 | §8.4 |
| 最大层数 | **2.5 层**（地面 + 一层 + 少量高点） | §8.5 |
| 楼层区分 | **必须**用材质/色调，不能只靠高度 | §8.5 |
| 层间通道 | 每两层至少 **2 条路径** | §8.5 |
| 最小墙厚 | **0.15 m**（与 pmove 的分步上限同一个数） | §5.6 / 铁律 |
| 表面属性 | `surface / climbable / wallrunnable / penetrable`，前三个默认关 | §5.7 |

## 6. 已知边界

- **没有贴图**：渲染是顶点色 + 假 lambert。贴图/天空盒是 M8 之后的事。
- **没有光照烘焙**：只有一盏方向光。
- **闸门是地图专用的**：`--test-wall` / `--test-step` / `--test-parkour` 等绑定在
  `test_box.map` / `step_test.map` / `parkour_test.map` 的几何上；拿 `--map` 指到别的图，
  它们会给出**误导性的 FAIL**（走得根本不是那堵墙）。见 DEBT-24。
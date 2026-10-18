# 键位系统

> **原则：键位是数据，不是代码。** 任何一次改键都不需要重编译，也不会影响模拟层——
> 模拟层只接收 `MoveInput`，永远看不到键名。

## 默认布局（瓦洛兰特式，技能键 C / Q / E / X）

| 动作 | 键 | 对应瓦洛兰特 |
|---|---|---|
| 前进 / 后退 / 左 / 右 | W / S / A / D | 相同 |
| 跳跃 | `SPACE` | 相同 |
| 蹲 | `LEFT_CTRL` | 相同 |
| 滑铲 | `LEFT_SHIFT` | Shift（瓦是走路，我们改成滑铲） |
| 墙跑 | `Q` | 技能 2 |
| 爬墙 | `E` | 技能 3（招牌技） |
| 翻越 | `C` | 技能 1 |
| 冲刺 | `X` | 终极技 |
| 换弹 / 交互 / 计分板 | `R` / `F` / `TAB` | 相同 |

## 怎么改

编辑 `sim/data/keybinds.toml`，保存即生效（重新加载配置时）：

```toml
[actions]
jump      = "SPACE"
wallrun   = "Q"
wallclimb = "E"
vault     = "C"
dash      = "X"
```

规则：

| 项 | 说明 |
|---|---|
| 大小写 | 不敏感（`space` / `SPACE` 都行） |
| 空格 | 名字里的空格会被忽略（`left shift` 有效） |
| 别名 | `SHIFT`=`LEFT_SHIFT`、`CTRL`=`LEFT_CTRL`、`ESC`=`ESCAPE` |
| 冲突 | 同一个键绑两个动作会在加载时报告，`--keys` 会列出来 |
| 缺失 | 没写的项保留代码里的默认值，并在 stderr 警告 |

## 运行时改键（给以后的设置界面留的口子）

```cpp
sim::Bindings bindings;
bindings.load("sim/data/keybinds.toml", error, warnings);

bindings.set_key(sim::Action::Jump, sim::Key::V);   // 立即生效
bindings.save("keybinds.toml", error);              // 持久化
```

## 验证

```powershell
build\headless_sim.exe --keys            # 打印当前解析结果 + 冲突
build\headless_sim.exe --test-bindings   # 加载 / 改键 / 保存 / 重新加载 全链路
```

`--test-bindings` 会验证四件事：

1. 每个动作都有键、且没有重复绑定；
2. **Q/E/C/X** 确实是墙跑/爬墙/翻越/冲刺；
3. 把跳跃改到 `V` 之后，**`SPACE` 立刻失效、`V` 立刻生效**；
4. 存盘再读回来，改的键还在（并且故意制造一次重复绑定，证明冲突检测不是恒假）。

## 还没做的

- 游戏内的改键界面（需要 Godot 侧先有设置菜单，M5 之后）
- 每个玩家独立配置 / 云同步（本项目不需要）
- 鼠标灵敏度与 ADS 缩放分开配置（到 M6 一起做）

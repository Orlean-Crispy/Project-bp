# Demo 格式与跨机比对

> 目标（计划书 §10.3 / B3）：**记录输入 + 种子，逐 tick 回放**；demo 头里写明
> 构建号，跨机构建不一致时**显式警告**而不是静默播错。

## 为什么存输入而不是存状态

存状态等于存视频：回放只能"看起来像"。存输入 + 初始状态 + 每 tick 的世界哈希，
回放时**重新算一遍**，哈希对不上就说明两个构建的行为真的不同——这才叫可复现。

## 文件布局（小端，无对齐填充）

```
magic            "BPDM"
u16              format_version = 1
u16              reserved
u32              sim_version
u32              protocol_version
u32              tick_rate
u64              params_hash          # movement.toml 的哈希
u64              geometry_hash        # 地图几何的哈希
u16 + bytes      build_id             # git short hash，configure 时写入
u16 + bytes      map                  # 地图相对路径
u16              player_count
  每个玩家:
    f32 x3       pos
    f32 x3       vel
    f32 x2       speed_coefficient / slow_factor
    u8           crouched
    u8           impulses
    u16          padding
u32              tick_count
每个 tick:
  u32            tick
  u64            state_hash           # 该 tick 结束后的世界哈希
  u16            input_count
    每个输入:
      f32 x3     forward / right / yaw
      u16        flags（jump/crouch/slide/dash/wallrun/wallclimb/vault）
```

实测体积：**约 28 B / tick**（600 tick ≈ 17 KB），一局 30 分钟 ≈ 2.8 MB。

## 回放

```powershell
build\headless_sim.exe --play build\demo_manual.bpd
```

输出会先报头部信息；如果 `params_hash` / `geometry_hash` / `sim_version` 对不上，
打印 `WARNING header mismatch: ...`，然后**继续尽力播放**（计划书要求的行为），
最后报告第几个 tick 开始分叉。

## 跨机构建比对（M4 验收）

两台机器跑同一份 demo，各自导出位置序列，再比对最大偏差：

```powershell
# 机器 A
build\headless_sim.exe --play demo.bpd --dump dump_a.csv
# 机器 B（同一个 demo 文件）
build\headless_sim.exe --play demo.bpd --dump dump_b.csv
# 任一台机器
build\headless_sim.exe --compare dump_a.csv dump_b.csv --tolerance 0.01
```

判定：`max dev <= 0.01 m` 为通过（计划书 §10.3 的跨机容差）。

### 本机已经验证的部分

| 检查 | 结果 |
|---|---|
| 同构建回放 600 tick | 全对，`max dev = 0.000000` |
| 改 `accel` 10 → 12 后回放 | 头部即报 params 不一致；位置最大偏差 0.0662 m > 0.01 |
| 两个相同 dump 比对 | 0.000000 m |

**跨机本身还没做**——需要第二台机器。工具链和流程已经就位，
把 `dump_a.csv` 拷过去即可；这一条要等你那台 8250U 笔记本能跑起来才能打勾。

## 已知限制

- 录制时把 tick 全部缓存在内存里再写盘（一局 30 分钟约 11 万个 tick，内存占用可接受）。
  真要录整场比赛再改成流式写 + 回填 tick_count。
- `build_id` 是 **configure 时**的 git hash；`tools/build.ps1` 每次都重新 configure，
  所以正常构建流程下它是准的。

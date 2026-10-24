# budgets — 有限资源台账

> **为什么有这个文件**：M5→M6 审计的 P0（协议 mask 只剩 1 位、flags 只剩 1 位）不是代码 bug，
> 是**没人记账**。位域、MTU、tick、seq、队列都是有上限的东西，用完就卡住，而"快用完"从不报警。
>
> **规矩（顺序不能反）**：
> ```
> 想加字段/加包/加闸门 → ① 先改本表 → ② 再写 static_assert → ③ 最后写代码
> ```
> 到 **80%** 报警线：该条目自动升级为 P1，写进 DEBT，不许"下个里程碑再说"。

---

## 1. 协议（`sim/net/snapshot.*`）

| 资源 | 上限 | v5 已用 | v6 已用 | 余量 | 断言位置 | 报警线 |
|---|---|---|---|---|---|---|
| delta mask 位 | 16（uint16） | 7 | **9** | 7 | `snapshot.cpp` static_assert | 12 |
| flags 位 | 16（uint16） | 7 | **7**（另 4 位预留） | 5 | 同上 | 12 |
| 每玩家快照字节（全字段） | — | ~40 | **82** | — | `--test-snapshot` 打印 | 1100 B/包 |
| 单包（10 人满编、全字段） | 1100 B（安全线） | ~420 | **833** | 267 B | `--test-snapshot` | 1100 |
| 空 delta（1 人，无变化） | — | 15 | **15** | — | `--test-snapshot` | — |
| 协议版本 | — | 5 | 6 | — | `version.h` | 每次升版必须改 demo 头 |

**已用位的明细（v6）**

```
mask : 0 Pos | 1 Vel | 2 Status | 3 Timers | 4 Yaw | 5 Extra | 6 Charge
       7 Vitals（已接 hp/armor） | 8 Weapon（预留，恒 0，M6 W5 填值）
       9–15 空
flags: 0 on_ground | 1 crouched | 2 active | 3 jump | 4 slide | 5 dash | 6 vault
       7 firing | 8 reloading | 9 ads | 10 alive      ← 已定义、无生产者，恒 0
       11–15 空
```

**必须存在的四条断言**（`snapshot.cpp`，改位域先看这里）

```cpp
constexpr uint16_t kFieldsUsed = kFieldPos|...|kFieldVitals|kFieldWeapon;
constexpr uint16_t kFlagsUsed  = kFlagOnGround|...|kFlagVaultWasDown;
static_assert((kFieldsUsed & ~kFieldAll) == 0, "kFieldAll 未覆盖全部字段组");
static_assert(kFieldAll != 0xFFFF, "位域已满，必须扩到 uint32");
static_assert((kFlagsUsed & ~kFlagsAll) == 0, "kFlagsAll 未覆盖全部 flags");
static_assert(kFlagsAll != 0xFFFF, "flags 已满，必须扩到 uint32");
```

> 注意：**加 group 会改线格式**（旧解码器会读到未知字节），加 flag 位不会（u16 宽度不变）。
> 前者必须升协议版本，后者不用。

## 2. 时间 / 性能

| 资源 | 上限 | 实测 | 余量 | 采集命令 | 报警线 |
|---|---|---|---|---|---|
| 单 tick 模拟 | 4 ms | 0.0024 ms（p99，10 玩家） | 约 1600× | `--bench 1000 --players 10` | 3.2 ms |
| 网络单 tick | 1 ms | 未单独测（AUD-21） | — | `[netalloc]` / 计时 | 0.8 ms |
| 渲染帧 | 8 ms | 未测 | — | Godot profiler | 6.4 ms |
| 堆分配 | **0 / tick** | 0 | 0 | `--bench` 的 allocs 行 | 任何非 0 → P1 |
| 主循环追帧上限 | 5 tick/帧 | 硬编码 5，丢弃数已计数打印 | — | `godot/main.gd` | 每秒丢 > 1 tick 即查 |

> 余量 1600 倍意味着：**现在做性能优化是负收益**（加复杂度、降可读性）。
> 唯一要守的指标是 `0 allocs/tick`。

## 3. 网络与槽位

| 资源 | 上限 | 已用 | 余量 | 断言 / 观测 | 报警线 |
|---|---|---|---|---|---|
| 可靠流 seq | 65536（u16） | 未测（AUD-22） | — | 70000 包闸门（待做） | 需先做差值比较 |
| admission 队列 | **无上限**（AUD-24） | 无 TTL | — | 无 | 加 TTL 120 s + 容量上限 |
| 组局槽位 | 2（v0.1） | 0 | 2 | `[slots] cycles 3` | — |
| 玩家上限 | 10（满编测试） | — | — | `--test-snapshot` 10 人 / `--bench --players 10` | — |
| MTU | 1200 B（分片阈值 1100） | 833（满编全字段） | 267 | `[frag] sent/reassembled` | 1100 |
| 每 peer 收件队列 | 64 包 | — | — | `udp_hub.h` kMaxInbox | — |

## 4. 测试与闸门

| 资源 | 值 | 说明 |
|---|---|---|
| smoke 闸门数 | **21** | 加一项必须同步：`STATE §3`、`budgets`（本行）、`AGENTS.md`、`docs/AGENTS.md` |
| `tests_*.cpp` 体量 | 单文件 ≤ 70 KB | 2026-10-23 把 `tests_net.cpp`（81 KB）按主题拆成 transport / lobby / net 三份 |
| 文档体积 | — | `PROGRESS` ≤150 KB / 单文件；`STATE` ≤120 行；`DEBT` ≤400 行 |
| 会话读入 | 60 KB | 超了 = 任务该切，不是该硬读 |

## 5. 变更记录

| 日期 | 变更 | 谁 | 关联 |
|---|---|---|---|
| 2026-10-22 | 建立本表；补 v6 目标值 | — | M5→M6 审计 P0-1 |
| 2026-10-23 | v6 落地：mask/flags 16 位，Vitals 接真实值，Weapon 预留；实测 82 B/玩家、833 B 满编 | S60 | P0-1 |
| 2026-10-23 | 闸门 17 → 19（协议往返 + 文档一致性）；tick 实测 2.40 µs | S60 | M6 入口条件 |
| 2026-10-24 | 闸门 19 → 20（武器数据）；sim 源文件 27 → 28 | S64 | M6.1 |
| 2026-10-24 | 闸门 20 → 21（命中判定）；sim 源文件 28 → 30 | S66 | M6.3 |

---

*每加一个"有限的东西"，先在这里开一行。没有行 = 没有上限 = 早晚撞墙。*

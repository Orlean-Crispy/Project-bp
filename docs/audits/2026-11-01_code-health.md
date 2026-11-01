# 代码健康检查 · 2026-11-01

> 触发：所有者要求「先检查代码，确认无 bug、无无用代码、无屎山、架构化」。
> 本文件记录**这次实际做了什么检查、发现了什么、修了什么、还留着什么**。
> 结论分三档：**已修**（带判据）、**已知未修**（DEBT 编号）、**结构性评价**（不给假结论）。

## 1. 先说清楚：外部审查的条目**没有**全部做完

截至 S89，外部审查（`audits/2026-11-01_external-review.md`）的处理状态：

| 条目 | 状态 |
|---|---|
| 3 条 P1（换弹假绿 / NaN 穿越 / 幽灵 target） | ✅ S82 修复 |
| AUD-45 闸门不言谎 | ✅ S82 |
| AUD-36 唯一 `game_step` | ✅ S83 |
| AUD-37 事件合同（回滚去重 + overflow） | ✅ S84 |
| AUD-39 字段清单（hash/快照半边） | ✅ S85 |
| AUD-40 入口硬化 | ✅ S86 |
| AUD-41 座位 ghost | ✅ S87 |
| AUD-42 端口独占 | ✅ S88 |
| AUD-47 hub 清理 + ACK 回绕 | ✅ S89 |
| **AUD-38 连接终态机** | ❌ 未做（P1） |
| **AUD-39 demo 战斗阶段 / EOF** | ❌ 未做（P1） |
| **AUD-43 catch-up 每帧预算** | ❌ 未做 |
| **AUD-44 配置/地图校验、`max_health`、`tick_vitals`** | ❌ 未做 |
| **AUD-46 远端权威事件（协议 v10）** | ❌ 未做（P1） |

所以答案是：**没有全做完**。M7.0 的正确性硬化收了 9 条，还剩 5 条，其中 3 条是 P1。

## 2. 这次检查跑的东西

| 检查 | 方法 | 结果 |
|---|---|---|
| 死代码 | 脚本扫 `sim/ bridge/ tools/ godot/` 所有 `.h` 里的方法声明，统计全仓库引用数 | 1 处真死代码（见 §3.2），已删 |
| 脚手架残留 | grep `TODO/FIXME/XXX/HACK` | sim/ 与 bridge/ **0 命中** |
| 空转换/哑参数 | grep `(void)` | 9 处，全部是「接口要求但本实现不用」的显式标注，合理 |
| 调试钩子 | grep `debug_` | 4 处，全部只在闸门里用，且注释写明用途 |
| 全量闸门 | `smoke.ps1` | 23 项 + bench，见 §4 |

## 3. 发现并修掉的问题

### 3.1 `ReliableReceiver` 的回绕判断仍然是错的（**真 bug**，AUD-22 的后半）

S89 只修了发送侧（`e.seq <= ack`），接收侧还留着同一类错误：

```cpp
if (seq > m_next && (uint16_t)(seq - m_next) < m_window)   // 回绕后：m_next=0xFFFF, seq=0 → 直接丢弃
```

后果：**跨过回绕的第一个包被静默丢掉**，然后等发送端重传 —— 而发送端已经认为它确认了。

现在只用序列距离：`ahead = (uint16_t)(seq - m_next)`；`ahead == 0` 交付，`0 < ahead < window` 缓冲，
`ahead > 0x8000` 是旧包。判据：

```
[reliable] chunks 10 retrans 6 complete 1 same 1 corrupt 1 dead 0 wrap 1 recvwrap 1
```

**变异验证**：把条件改回 `seq > m_next` → `recvwrap 0`，测试红。

### 3.2 死代码：`NetClient::last_armor()` 与 `m_last_armor`

一个没有调用者的访问器，和一个只有它读的成员（外加每份快照都写一次）。删掉三者。
编译通过、`--test-net` 仍绿，证明确实没人依赖它。

### 3.3 我上一轮在 `reliable.h` 里留下的访问控制翻转

为了加测试钩子，我把 `public:` 插在类中间，变成 public/private/public/private 四段。
钩子移到公共区顶部，恢复成 public/private 两段。**这类东西单看编译毫无问题，读起来才知道疼。**

## 4. 闸门基线

```
ALL GREEN (23 gates + bench, no skips)
```

## 5. 结构性评价（诚实版）

**好的部分**

- 边界只有两层：`sim/` 零 Godot 依赖，`bridge/` 是唯一跨界层，GDScript 只做表现。这条一年来没有被破坏。
- 一个功能一个入口：`game_step` 收拢了移动/武器/hitbox 的顺序（ADR 0008），`update_weapon` 在生产代码里只有一个调用者。
- 数据与代码分离：武器/移动/战斗/键位全在 toml，加一把枪不改 C++。
- 判据可执行：23 项闸门 + `mutants.ps1`，且刚证明了三条闸门真的会红。

**还不够好的部分（按优先级）**

| 位置 | 体积 | 问题 |
|---|---|---|
| `tools/headless_sim/tests_net.cpp` | 57 KB | 一个文件塞了收敛/去重/加入/插值/UDP/分片/源校验/槽位/补 tick/双客户端，**该按主题再拆**（`tests_transport.cpp` 已经尝到甜头） |
| `sim/net/session.cpp` | 33 KB | 服务器与客户端共处一文件，`apply_snapshot`/`catch_up`/`receive` 三条长路径；AUD-43 动它之前应拆 `server`/`client` 两部分 |
| `godot/main.gd` | 31 KB | HUD/菜单/tracer/插值/网络全在一份脚本里；AUD-32 做脏标记时可顺手按职责拆节点 |
| `tools/headless_sim/tests_move.cpp` | 59 KB | 同上，是历史最老的一份测试，拆分收益低于上面三处 |

**明确不算屎山的地方**：没有全局状态机、没有隐式单例、没有「到处 include 一切」的万能头；
`sim/` 内部每个模块只依赖 `core/` 与自己的邻居。

## 6. 没有做的事（不要当成做了）

- 没有跑静态分析器（clang-tidy / cppcheck），只有 `-Wall -Wextra -Werror` 全量构建。
- 没有模糊测试、没有 sanitizer（审查里「现在就上 CI/sanitizer」被我否决过，理由仍成立：先在 Linux 上把测试跑通）。
- 没有逐行重读全部 3 万行；死代码是脚本扫的，结构是读文件与数体积得出的。
## 7. 第二遍（S96，M7.0 收尾之后）

对象是 S89–S95 新写的那批代码（`join_state`、`event_wire`、`catch_up` 预算、demo combat）。

### 7.1 修掉

| 项 | 说明 |
|---|---|
| `JoinTracker::waited_ms()` / `name()` | 声明 + 定义，**零调用者**，删。 |
| `NetClient::budget_hits()` + `m_budget_hits` | 只写不读的计数器，删（收包上限本身保留）。留着就是“看起来有人管”的假象。 |
| `kEventBytes` | 常量没人用；数字写在注释里就够了，删。 |

### 7.2 记录下来的行为（不是 bug，但必须写清楚）

- 快照包尾的事件块**每条包最多 4 条**，多出来的直接丢弃。这是分片预算的决定（4×36 B 让满编 954→1098 B，
  仍不进分片），不是疏忽；丢弃的是曳光与火光，不承载权威数据。注释已写在 `session.h` 的 `m_drained` 上。
- `EventDedup` 是 32 格的环：同一 `(tick, entity, type)` 若隔了 32 条以上事件才重复到达，会被当成新的。
  现在的发包路径不会重发旧事件，所以这只是兜底，不是正确性依赖。

### 7.3 扫描结果

```
sim/ bridge/ tools/ 里的 TODO/FIXME/MUTANT/DBG：0
未引用方法：0（第一遍 1 处已删，本遍 3 处已删）
smoke：ALL GREEN (23 gates + bench, no skips)
```

### 7.4 仍未做（和第一遍一样，不重复粉饰）

静态分析器、sanitizer、fuzz 都没有；`tests_net.cpp` 仍然是最大的文件（58 KB）。
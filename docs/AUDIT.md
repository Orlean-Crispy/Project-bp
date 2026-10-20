# 代码体检（2026-10-20，M5 收尾后第一轮）

> 只读不写：这一份是**可疑点清单**，不是改动记录。每条都要能被证据支持，
> 改之前先确认「如果这里错了会有什么症状」。

## 0. 方法

1. 全仓扫描 TODO / FIXME / XXX / HACK —— **0 条**（这一阶段没留口头债）。
2. 找「定义了但没人调用」的符号（AckTracker 就是这样抓到的）。
3. 找「条件恒假」的分支（窗口/深度关系可以静态推出来）。
4. 数文件与函数体积（god file 候选）。
5. 对照 AGENTS §4 的未结案逐条核状态。

## 1. 结论摘要

| # | 严重度 | 模块 | 一句话 |
|---|---|---|---|
| A1 | ✅ 已删 | sim/net/session.* | `InputWindow::Overflow` 不可达，`m_overflow_cmds` 永远是 0 |
| A2 | ✅ 已删 | sim/net/netchan.* | `AckTracker` 全仓无调用（约 30 行死代码） |
| A3 | ✅ 已抽 | sim/net/winsock.h | Winsock 初始化曾写两份，现在只剩一处 inline 实现 |
| A4 | ✅ 已拆 | tools/headless_sim/ | 2916 → 272 行；网络/演示/移动三组各自成文件，main 只剩参数与分发 |
| A5 | ✅ 已拆 | bridge/ | mesh_common.h + mesh_builder.* + net_gateway.*；SimWorld 478→313 行 |
| A6 | ✅ 已接 | sim/net/configstring.h | configstring 现在有格式与校验：哈希不符客户端拒局 |
| A7 | ✅ 已抽 | sim/net/admission.h | 加入逻辑只有一份：approve/take，两个 UDP 宿主共用 |
| A8 | P3 | godot/main.gd | 日志前缀仍是 `breakpoint:`（内部代号，已结案，不改） |

P0（可能算出错结果）：**本轮没有发现**。

## 2. 逐条

### A1 · `InputWindow::Overflow` 不可达（P2）

- 证据：`kWindowTicks = 16`、`kWindowDepth = 16`；进入该分支前已按 `offset < 16` 过滤，
  `m_seen` 位图又把重复 tick 挡掉，所以 `m_queue.size()` 最大 15，`>= 16` 恒假。
- 症状：`overflow_commands()` 永远报 0，看起来像「从不溢出」，实际是**测不到**。
- 建议：删掉 `Overflow` 枚举值、`m_overflow_cmds` 与对应的 `case`，并在注释里写死
  「窗口 = 深度，故意不留溢出路径」。
- 风险：低。删完 `--test-net` 的 `[dedup]` 项应保持全绿。

### A2 · `AckTracker` 无人调用（P2）

- 证据：`netchan.h` 定义、`netchan.cpp` 实现，`grep -r AckTracker` 只有这两处。
  实际的 ack 处理在 `Link.last_cmd_seq` / `snapshots` map 里另写了一套。
- 症状：读者会以为 ack 走这个类，改网络时找错地方。
- 建议：整类删除。若将来需要「判断某 seq 是否被确认」，再按当时的语义重写。

### A3 · Winsock 初始化两份（P1）

- 证据：`signaling.cpp:31-41` 与 `udp_socket.cpp:27+` 各有一份 `WSAStartup` 与错误处理。
- 症状：加后端（比如 IPv6 或代理）时只改一处，另一处漏掉；错误文案也会分叉。
- 建议：抽 `sim/net/winsock.h`（`ensure_winsock(std::string&)`，POSIX 下恒真），
  两处都 include 它。判据：`--test-net` 的 `[signal]`/`[udp]` 全绿。

### A4 · 闸门巨文件（P2）

- 证据：`tools/headless_sim/main.cpp` 2916 行，34 个 `run_*` 函数 + `main` 分发。
- 症状：任何一条闸门改动都要在 3000 行里找位置；冲突概率高（铁律 F）。
- 建议：按域拆成 `tests_move.cpp` / `tests_net.cpp` / `tests_demo.cpp`，共用一个
  `TestRegistry`（`{name, fn}` 数组），`main.cpp` 只留参数解析与分发。**一次只搬一组**。
- 风险：中（搬运期间容易漏注册），所以每搬一组就跑一次 smoke。

### A5 · 桥职责过载（P1）

- 证据：`sim_bridge.cpp` 519 行，同一个 `SimWorld` 同时管 map 网格、输入映射、host/join、
  远端玩家渲染数据、HUD 数值。
- 症状：改动一处网络字段要碰渲染代码；Godot 绑定层和表现逻辑耦合。
- 建议：拆成 `bridge/SimWorld`（状态与绑定）、`bridge/net_gateway`（host/join/reconnect/事件）、
  `bridge/mesh_builder`（顶点/颜色生成）。**不动 sim/**。
- 风险：中高，放最后做，先在 M6 前只拆 `mesh_builder`（纯函数，最好验证）。

### A6 · configstring 语义未接（P1，功能性）

- 证据：`[config]` 闸门里 blob 是随机字节；真实内容（params/几何哈希/键位表）没打包。
- 症状：客户端即使收不到配置也照样进局——配置流现在只是「管道演示」。
- 建议：定义 `ConfigBlob`（version + params_hash + geometry_hash + movement 参数），
  服务端 `set_configstring(build_config_blob(...))`，客户端比对哈希，不一致就拒绝入局。
  判据：改 movement.toml 后客户端被拒（新增闸门项）。

### A7 · 加入逻辑两份（P2）

- 证据：`tools/net_loop` 与 `bridge/sim_bridge.cpp` 各写一遍「HELLO → 查 approved → add_client」。
- 症状：修好一边忘了另一边；重连行为可能不一致。
- 建议：把「approved 队列 + HELLO 匹配」抽成 `sim/net/admission.h`（纯 C++，不碰 Godot），
  两边都调它。判据：`net_selftest.ps1` 与 `--reconnect-check` 都过。

### A8 · 日志前缀（P3，不改）

- `breakpoint:` 是内部代号，ADR 0005 已结案不改名。留档只为避免下次又提。

## 3. 明确不动的

| 对象 | 理由 |
|---|---|
| `sim/` 命名空间 `sim::` | ADR 0005 已结案 |
| 快照/输入线格式 | 有闸门 + 协议版本，动它等于重测全部网络 |
| `LoopbackTransport` 的 tick 计数 | 确定性依赖它，不是「多余参数」 |

## 4. 建议顺序

1. ~~A1 + A2（纯删除，一个 commit）~~ —— 已完成（S42）
2. ~~A3（抽 winsock.h）~~ —— 已完成（S43）
3. ~~A6（configstring 语义）~~ —— 已完成（S44）
4. ~~A7（admission 抽取）~~ —— 已完成（S45）
5. ~~A4（拆闸门文件）~~ —— 完成（S46–S48），main.cpp 只剩参数解析与分发
6. A5（拆桥，最后做）

每一步都必须 `tools/smoke.ps1` 全绿、更新 PROGRESS、单独 commit。

## 5. 第二轮（2026-10-20，M5 关闭后）

> 两个只读代理分别审 `sim/` 与 `bridge/`+Godot 胶水；下表是合并后的结论。
> 编号接 A 系列，用 AUD-nn；「已修」条目都有提交与验证记录。

| id | sev | 位置 | 问题 | 状态 |
|---|---|---|---|---|
| AUD-01 | P1 | net_gateway pump | 联机时 `step_local` 不传 EventQueue，局内事件恒为空 | ✅ 已修 |
| AUD-02 | P1 | main.gd 暂停 | 暂停停止网络泵，>5 s 会被服务器踢 | ✅ 已修 |
| AUD-03 | P1 | main.gd 菜单 | ESC 在 LAN 菜单上叠加暂停菜单，关闭后鼠标被捕获 | ✅ 已修 |
| AUD-04 | P1 | main.gd HUD | `_update_hud` 写的 HP/IMP 同帧被 `_process` 覆盖，从未显示 | ✅ 已修 |
| AUD-05 | P1 | bridge 远端采样 | 按预测 tick−1、frac=0 取样，插值实际退化为 nearest | ✅ 已修 |
| AUD-06 | P2 | net_gateway | 信令成功但 UDP 失败时回落到地图世界，远端胶囊残留 | ✅ 已修 |
| AUD-07 | P2 | main.gd | `link_lost` 永不触发（本地 tick 每帧都变） | ✅ 已删（由 snapshot_age 覆盖） |
| AUD-08 | P2 | main.gd 胶囊 | 半径/身高写死，改 toml 会与碰撞不同步 | ✅ 已修 |
| AUD-09 | P2 | main.gd | `EYE_FALLBACK` 未使用 | ✅ 已删 |
| AUD-10 | P2 | main.gd 键位表 | `KEYCODES` 缺 NUM/ENTER/方向键等，改键静默跳过 | ✅ 已修 |
| AUD-11 | P2 | 头文件 | bridge/net_loop 有无用 include，mesh_builder 缺 `<algorithm>` | ✅ 已部分修（using 删除） |
| AUD-12 | P2 | .gdextension | 声明了 release DLL 路径但从不产出 | ✅ 已修 |
| AUD-13 | P2 | build_bridge.ps1 | 桥编译不带 `-Werror` 与 `BP_BUILD_ID` | 待修 |
| AUD-14 | P2 | 端口字面量 | 27115/27016/27130 散落四处 | 待修 |
| AUD-15 | P2 | tools/*.ps1 | Godot 路径/首跑导入块复制了五份 | 待修 |
| AUD-16 | P2 | bridge | `get_hp/get_armor` 是常量 100/0 | ⏸ 留到 M6 |
| AUD-17 | P2 | main.gd 重连 | 重连按钮可能双击双触发；`joined_printed` 未复位 | 待修 |
| AUD-18 | P2 | bridge 远端字典 | 每帧构造 `state` 字段但 GDScript 不读 | ✅ 已删 |
| AUD-19 | P2 | setup_toolchain | 依赖未安装的 `7z`，Ninja 未锁版本 | 待修 |

### 已修批次（S53）

- AUD-03：ESC 在 LAN 菜单上先关 LAN 菜单；两个菜单各自只在对方关闭时捕获鼠标。
- AUD-04：HUD 合并成一条字符串（HP / ARMOR / IMP 点阵 / speed / pos / event），只写一次。
- AUD-07/AUD-09/AUD-18/AUD-11（部分）：删掉不可达的 `link_lost`、未用常量、未读字典字段、未用 using。
- 验证：桥编译通过；`--hud-check` 正常；两进程联机 client `joined slot 1 / remote slot 0 visible`，stderr 0 行。

### 第二轮 · sim 侧（R 系列，只读代理）

> 62 个 sim 源文件全读；死分支由常量推出（窗口 16、substep 上限 4、64 Hz 等）；未跑构建，纯静态。

| id | sev | 位置 | 问题 | 状态 |
|---|---|---|---|---|
| R1 | P1 | session.cpp 回滚 | 回滚后未改写该 tick 的 history/hitbox，M6 读 hitbox 时会用到旧预测 | ✅ 已修 |
| R2 | P1 | pmove.cpp vault/climb | `busy` 只算一次，vault 起手后 wallclimb 仍可覆盖，白花一次 impulse | ✅ 已修 |
| R3 | P1 | session.h Link::sent | 以 `out_seq`（u16）为键，17 分钟后回绕，ack 永远对不上→永久全量 | ✅ 已修 |
| R4 | P1 | matchmaking.cpp 槽位 | 只 reserve 不 release，N 个不同 id 后房间永久 RoomFull | ✅ 已修 |
| R5 | P1 | udp_socket.cpp | 游戏端口没设 `SO_EXCLUSIVEADDRUSE`，与教训 #11 矛盾 | ✅ 已修 |
| R6 | P1 | session 热路径 | 每 tick 建 vector、`m_local.step({..})`、把快照拷进两个 map | ✅ 已修 |
| R7 | P1 | config.h/config.cpp | 6 个参数被加载/哈希但 sim 从不读取（用字面量代替） | ✅ 已修 |
| R8 | P1 | combat.cpp | `CombatParams::hash()` 无人调用，两套 combat.toml 也能互通 | ✅ 已修 |
| R9 | P1 | world.cpp 哈希 | `state_hash` 漏掉 state/timer/yaw 等参与后续 tick 的字段 | ✅ 已修 |
| R10 | P2 | trace.cpp | 内层 `live_speed < kMinSpeed` 恒假（外层已排除） | ✅ 已修 |
| R11 | P2 | tests_move.cpp | `max_step_length` 只打印不比对；>38.4 m/s 时 substep 超过墙厚 | ✅ 已修 |
| R12 | P2 | tests_move.cpp | 「不外推」判据断言的是入参 tick，恒真；一处三元两边都是 'slide' | ✅ 已修 |
| R13 | P2 | tests_move/tests_demo | TTK 闸门重算一遍伤害公式；录像块重复两处 | ✅ 已修 |
| R14 | P2 | session/reliable | 64 Hz 又硬编码成 kDt、320 tick、`*1000/64` | ✅ 已修 |
| R15 | P2 | pmove.h/brush/trace | 写了从不读的字段（wallrun_was_down、landed_solid、brush_index…） | ✅ 已修 |
| R16 | P2 | 多处 | 零调用 API：TickClock、has/oldest_tick/empty、reload_pending… | ✅ 已修 |
| R17 | P2 | combat/config | armor_type 未校验、absorb>100 会变成治疗；访问器无边界检查 | ✅ 已修 |
| R18 | P2 | demo/bindings | `fopen(path.c_str())` 绕过 u8path（AGENTS 2） | ✅ 已修 |
| R19 | P2 | reliable/session | 每包遍历 std::map 算 ack；InputWindow 用 map 存 tick | ✅ 已修 |

**建议顺序**：R3/R4/R5（小、后果重）→ R2 → R9 → R7/R8 → R1/R6 → 其余 P2 分批。

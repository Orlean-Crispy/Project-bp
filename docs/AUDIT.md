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
| A5 | 🔶 第 1 步 | bridge/ | 纯展示件已抽到 mesh_common.h；build_mesh 与网络网关待拆 |
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

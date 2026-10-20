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
| AUD-13 | P2 | build_bridge.ps1 | 桥编译不带 `-Werror` 与 `BP_BUILD_ID` | ✅ 已修 |
| AUD-14 | P2 | 端口字面量 | 27115/27016/27130 散落四处 | ✅ 已修（ports.h + lint） |
| AUD-15 | P2 | tools/*.ps1 | Godot 路径/首跑导入块复制了五份 | ✅ 已修（godot_env + godot_check） |
| AUD-16 | P2 | bridge | `get_hp/get_armor` 是常量 100/0 | ⏸ 留到 M6 |
| AUD-17 | P2 | main.gd 重连 | 重连按钮可能双击双触发；`joined_printed` 未复位 | ✅ 已修 |
| AUD-18 | P2 | bridge 远端字典 | 每帧构造 `state` 字段但 GDScript 不读 | ✅ 已删 |
| AUD-19 | P2 | setup_toolchain | 依赖未安装的 `7z`，Ninja 未锁版本 | ✅ 已修 |

### 已修批次（S53）

- AUD-03：ESC 在 LAN 菜单上先关 LAN 菜单；两个菜单各自只在对方关闭时捕获鼠标。
- AUD-04：HUD 合并成一条字符串（HP / ARMOR / IMP 点阵 / speed / pos / event），只写一次。
- AUD-07/AUD-09/AUD-18/AUD-11（部分）：删掉不可达的 `link_lost`、未用常量、未读字典字段、未用 using。
- 验证：桥编译通过；`--hud-check` 正常；两进程联机 client `joined slot 1 / remote slot 0 visible`，stderr 0 行。

### 已修批次（S58，AUD-13/14/15/17/19）

> 2026-10-20，M6 之前最后一次清债。每条都带「怎么证明」；两个顺手挖出来的真问题写在最后。

- **AUD-19 · 换机即炸的安装脚本**：原来调 `7z`（本机没装），ninja 取 `releases/latest`。
  现在只用 `Expand-Archive`（PowerShell 5.1 自带）、ninja 锁 v1.13.2、另外两个也锁到确切版本；
  下载后先验 ZIP magic（错误页/半截文件会指名道姓地报错）。安装目录统一成无版本名
  `mingw64/ cmake/ ninja/`，老机器上的 `cmake-4.4.4-windows-x86_64/` 直接改名复用而不是重下 54 MB；
  `build.ps1` 只认稳定名，缺哪个工具就报哪个。
  验证：脚本在本机 adopt 后打印 g++ 16.2.0 / cmake 4.4.4 / ninja 1.13.2；全量构建干净；
  抽两个函数名从源码里取出、对真实 ninja.zip 跑一遍（成功）和一个假 zip 跑一遍（拒绝）。

- **AUD-13 · 桥的编译标志与 CMake 不一致**：桥编的是同一份 sim 源码，但少了 `-Werror`、
  `-ffp-contract=off`、`-DNDEBUG` 和 `BP_BUILD_ID`。现在标志逐条对齐，godot-cpp 头用
  `-isystem` 引入（第三方告警进不了我们的 `-Werror`）；`BP_BUILD_ID` 用与 CMake 相同的
  `git rev-parse --short HEAD` 注入，桥暴露 `get_build_id()/get_protocol_version()`。
  顺带把版本号收成单一来源 `sim/core/version.h`（CMake 不再定义 protocol/sim 版本）。
  验证：桥在 `-Werror` 下零告警；Godot 打 `breakpoint: build <hash> protocol 5` 且 hash == HEAD。

- **AUD-14 · 端口四处写死且已经漂移**：菜单/run_net_demo 27115、net_loop 默认 27016、
  闸门脚本 27130，游戏端口一处写 `+1`、一处写 27116。现在只在 `sim/net/ports.h` 出现
  （默认 27115、闸门 = 默认 + 15、`game_port_of()` 派生 +1），Godot 读桥的
  `get_default_signal_port()`，`check_sources.ps1` 新增端口 lint（写别的文件就红）。
  验证：lint 在写它自己的注释时就红了一次（证明能失败）；`--hud-check` 打 `port=27115`；
  net_selftest `ports_ok=True`；两个 Godot 进程**一个 --port 都不带**也能 host/join 并互相渲染。

- **AUD-17 · 重连按钮双击**：按钮到帧末才释放，连点两下会叠两次 join（新票据 + 新 link）。
  现在 `_do_reconnect()` 成功后上锁（失败解锁可重试，菜单重开时解锁），并复位
  `joined_printed/silent_printed/last_peer_count`——否则重连后再也不会打 "joined slot"。
  `--reconnect-check` 故意再调一次，判据是 `reconnect double=0`。
  验证：两个 headless Godot 进程，client 打 `reconnect ok / double=0 / joined slot 1 / peers 1 /
  remote slot 0 visible`，host 打 `signal slot 1 resumed 1 / peer joined slot 1`。

- **AUD-15 · Godot 路径与首跑导入复制多份**：现在只有 `tools/godot_env.ps1` 知道 Godot 在哪
  （`$env:BP_GODOT` 优先，否则取工具链目录里最新的 `Godot_v*_win64.exe`）以及怎么导入工程；
  `run_demo.ps1`/`run_net_demo.ps1` 变回三行。新增 `tools/godot_check.ps1`：把原来手工敲的
  `--hud-check` 与两进程重连固化成闸门（没 Godot 或没 DLL 时打印 SKIP 并返回 3），
  **接进 smoke 成为第 17 项**——headless_sim 的闸门一个都看不到桥，这正是下面第一个真问题潜伏的原因。

#### 顺手挖出来的两个真问题

1. **扩展根本没被加载**：`breakpoint.gdextension` 只有 `windows.debug.x86_64`，而我们是拿
   release 编辑器二进制跑 headless，它只认 `windows.editor.x86_64`。结果 DLL 明明在、
   报错却说「找不到 windows.x86_64 的库」，随后 `SimWorld` 未声明、main.gd 解析失败，
   `--hud-check` 永远不退出（进程挂着）。修法：两个 key 都指向 template_debug DLL。
   教训 13；`godot_check.ps1` 带 60 s 超时，就是为了把这类「静默挂住」变成可读的失败。
2. **桥缺 `-ffp-contract=off`**：DLL 与 exe 编同一份 sim，一个允许 FMA 收缩、一个不允许，
   浮点结果可能不同——demo 哈希回放、回滚一致性都会受影响，而且没有任何闸门会报。
   这条属于 AUD-13 的「标志一致性」，现在被写进教训 12。

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
## 6. 第三轮（2026-10-20 晚，M1–M5 全量只读审计）

> 四个只读代理（sim 核心/移动、sim 网络、bridge+Godot、tools+闸门）分别通读，我再逐条复核。
> 结论：**闸门大面积"能打印但不能失败"**，另有两处真数据缺陷。下面按处理结果分三块：
> 已修（有闸门证明）、记录在案的未修（有严重度与 lead）、以及设计待定项。

### 6.1 已修（本轮改动，全部有闸门）

| 域 | 问题 | 修法与判据 |
|---|---|---|
| 模拟 | impulse 着地**每 tick** +1：4 个 impulse 62 ms 回满 | 新参数 `recharge_ticks=32`（进 toml/params_hash）；`--test-parkour` 断言 31 tick 不回、第 32 tick 回 1 |
| 模拟 | 空中也跑 step-up：跳跃撞台沿=免费上台阶 | 仅 on_ground 走 step_slide_move；空中走 slide_move |
| 模拟 | 翻越/爬墙被当成跳跃事件 | Jump 事件要求 `input.jump` 按下沿 + 起跳前着地 |
| 模拟 | `state_hash` 含 `PlayerState.tick`，但它不进快照 → 回滚后假分叉 | 删除该字段（无人读），哈希改为 `recharge_progress` |
| 模拟 | `geometry_hash` 不含 `_climbable/_wallrunnable` → 两份不同规则地图同哈希 | 折进哈希；并缓存（原来每 tick 重算全图） |
| 模拟 | bumps 上限 `<=` 实际给 5 次而非 4 | 改严格 `<` |
| 模拟 | pmove 无 NaN/dt 守卫，一条坏命令污染整局 | 入口拒绝非有限值与 dt<=0；forward/right 钳到 [-1,1] |
| 模拟 | 爬墙 keep 只查 wall.hit，不查 climbable | keep 补 brush 标志 |
| 模拟 | map_parse 接受 0 brush 的图（玩家永远下落） | 明确报错 |
| 模拟 | entity origin 在桥里乘 0.0254（违反铁律 A） | 新增 `entity_origin_metres()`，桥不再换算；check_sources 新增 0.0254 规则 |
| 网络 | PlayerSnap 缺 slide/dash/vault 按下沿与 speed_coefficient → 回滚把"按住"当"刚按下" | 协议 v5：flags bit4-6 + `recharge_progress` + `speed_coefficient`；`[snapfields]` 逐字段往返 |
| 网络 | 从没发过包的 link 永不超时（幽灵槽位 + 43 KB/s） | `last_rx_tick` 从 add_client 起算；`[timeout]` 仍绿 |
| 网络 | 全零 client_id 被当成"空闲槽"占位，可顶掉房主 slot 0 | HELLO 直接拒（`[match] tamper` 路径同一出口） |
| 网络 | MatchHost::start 对已 join 线程再赋值 → terminate | 先 stop() |
| 网络 | PeerTransport 收件队列无上限（孤儿 peer 长局 OOM） | 队列封顶 64 |
| 网络 | 客户端错误快照计数无读者 | 暴露 `bad_snapshots()`，net_loop 打印 `bad=` |
| demo | DemoReader 无析构：每次 --play 泄漏句柄并锁文件 | 加析构/close/禁用拷贝 |
| demo | 截断的 demo 当干净 EOF → 回放 PASS | `feof` 区分短读，报 truncated |
| demo | 头里 build_id 从不比较，异构建 demo 静默 PASS | 比较 build_id，`--play` 遇 header mismatch 返回 1 |
| demo | `--compare` 一方 EOF 也 PASS，空 dump 也 PASS | 要求同时 EOF 且 rows>0 |
| 桥 | solo 每 tick 构造 vector（`step({input})`） | 改 `step_single`；铁律 E |
| 桥 | params_hash 只含 movement，与联机不同源 | 改 `combined_params_hash`（读 combat.toml） |
| 桥 | 相机眼高恒 1.55（高于站姿头顶、蹲下不动） | 0.9×当前姿态高度 |
| 桥 | join 信令失败后 m_disconnected 卡死，solo 冻结 | 失败且未建链路时回滚标志 |
| 桥 | start_host 失败遗留 TCP 线程/端口/僵尸房主 | `abort_host()` 全清 |
| 桥 | join_impl 先销毁旧链路再建新的 | 先建后换，失败保留旧会话 |
| 桥 | 每帧未读的 `entry["state"]`（AUD-18 声称已删） | 删除 |
| Godot | `--force-paused` 只声明不解析（AUD-02 从没被真正验证） | 解析该参数；godot_check 新增"暂停 7 s 不掉线" |
| Godot | 暂停分支不更新相机/不 drain 事件 | 两者都做 |
| Godot | `tick == 20` 用帧采样等值，跳帧则永不跳 | 改一次性边沿 |
| Godot | Esc 无 `echo` 过滤，长按反复开关菜单 | 加 `not event.echo` |
| 工具 | `--ticks/-1`、`--bench 0/abc` 崩溃或静默 0 | strtol 全量校验；`--script` 未知报错；`--record+--play` 互斥；`--dump` 需 `--play` |
| 闸门 | `--info`/`--bench` 只打印，smoke 只看退出码 | 断言 brush/实体/bounds；p99≤4 ms 且 allocs==0 才算过 |
| 闸门 | `--test-net` 的 land 事件、分片 delivered、dup 帧、selftest 字段覆盖、胸倍率、墙"是否真到达"、光线 fraction、大厅 slot1 打印、join 文件写失败静默跳过 | 全部补成可失败断言；netalloc 阈值由 `>1.0` 改 `!=0` |
| 工具 | `run_net_alloc_check` 定义在 main.cpp（与 AUDIT A4 声明矛盾） | 移进 tests_net.cpp |
| 工具 | CMake 的 `file(STRINGS)` 不过滤注释，与 build_bridge 解释不一致 | 过滤空行/注释 |
| 工具 | check_sources 只看已跟踪文件、规则全靠文档 | 收 untracked；新增 sim 无 Godot、0.0254 唯一、无状态 RNG 三条规则（先剥注释再匹配） |

### 6.2 记录在案、本轮**未修**（有严重度与 lead）

> 这一节保留审计当天的证据叙述；**"现在还有哪些问题"以 docs/DEBT.md 为准**（同样的 AUD 编号，
> 加了关闭判据与状态）。这里说"未修"的条目若已关闭，DEBT 的 §9 会有记录。

| id | sev | 问题 | lead |
|---|---|---|---|
| AUD-20 | P1 | 客户端落后没有补 tick 路径：一次 >110 ms 卡顿吃掉 lead+slack 后，命令永久 Late，只能重连 | M6 前 |
| AUD-21 | P1 | 序列化/接收热路径每次约 4-8 次堆分配（`[netalloc]` 只统计 session 三步，边界已写明） | M6 |
| AUD-22 | P1 | 可靠流 `seq <= ack` / `seq > m_next` 不是回绕安全的（第 65536 个包后流静默死） | M6 |
| AUD-23 | P1 | `FragmentedTransport` 只在闸门里组装，bridge/net_loop 的真实 UDP 没套（M6 加 hp 后满编快照会走 IP 分片） | M6 |
| AUD-24 | P1 | admission 无 TTL/上限：TCP 通过但没发 HELLO 的 id 永久占一条 deque | M6 |
| AUD-26 | P1 | UDP recv 丢弃源地址，同网段任意主机可注入快照/命令 | M6 前 |
| AUD-27 | P1 | MatcHost 槽位在"正常离开"时不释放（bridge 从不 release）：N 个 id 后房间永久 RoomFull | M6 前 |
| AUD-31 | P2 | 桥的 `match_request` 在主线程同步执行（最坏约 11 s 窗口冻结） | M7 |
| AUD-25 | P2 | `ReliableTransport` 的 now_ms 只在 advance 更新，采样少算一 tick | 顺手 |
| AUD-28 | P2 | 仍有一批手感常量是字面量（0.02 / -0.05 / +0.15 / 滑铲结束 0.5× / 攀爬 0.05 / 墙跑内推 0.2 / 翻越 0.55 / 1e-3 / 可行走 0.7），违反铁律 I | M6 前 |
| AUD-29 | P2 | combat.toml 加载无未知键/类型诊断；`max_health` 无消费者（M6 接 vitals） | M6 |
| AUD-30 | P2 | demo 初始状态不完整（只能从 spawn 录制）；DemoWriter 不是流式写 | M6 |
| AUD-32 | P2 | GDScript HUD 每帧重拼；左右 Ctrl/Shift 未区分侧别；tick 秒数三处写死 | M7 |
| AUD-33 | P2 | trace 的注释与判据仍把 step_len 当"防穿墙"依据（真正防穿是连续扫掠） | M7 |
| AUD-35 | P2 | 主循环每帧最多 5 tick，超出部分直接丢弃（持续 <13 fps 时 sim 时钟落后） | M7 |

### 6.3 设计待定（不是 bug，需要拍板）

1. impulse 充能速率：计划书只写"触地开始充能"，现值 `recharge_ticks=32`（0.5 s/个）是提案。
2. 空中 step-up：本轮按"跳跃撞台沿不该免费上台阶"关掉；若要做 Apex 式自动 mantle 应显式实现。
3. 护甲吸收 66% 与再生 5 s/10 s 仍是提案值（改 1% 就会破 TTK 0.6–1.2 s 判据）。
4. 可靠流 configstring 只在闸门启用：真实链路靠 FULL_STATE 的两个哈希拒局（功能已覆盖），
   可靠通道留给 M6 之后的资产；AGENTS 已改成这个说法，不再宣称"桥已下发 configstring"。
5. 快照 `acked_cmd` 与包头 `ack_bits` 暂未消费（后者恒 0，已在 netchan.h 注明），M6 接输入 ack 时再定。


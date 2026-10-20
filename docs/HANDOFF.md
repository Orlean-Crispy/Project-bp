# HANDOFF — 会话完整交接（2026-10-18 ~ 10-20）

> 用途：上下文被压缩或换会话后，除了 docs/AGENTS.md 的约束，还需要知道**这一天到底做过什么、
> 为什么这么做、证据是什么**。约束与未结案以 AGENTS.md 为准；本文件只记录过程与结论。

---

## 0. 一句话总结

从零搭起 M0–M4 全部、M5 的网络骨架、M6 的 HP/护甲规则；17 项行为闸门 + bench 全绿
（第 17 项是 Godot 桥，含暂停不掉线）；Godot 4.5 宿主 + GDExtension 桥可运行。
所有试玩反馈都已落回 sim 层并带回归测试。

**最新一次是 2026-10-20 夜的 M1–M5 全量审计（S59），看 §9。** 它改了协议（v5）、
把一批"只打印不断言"的闸门改成会失败，并留下 12 条带 lead 的未修项。

---

## 1. 时间线

### S01 环境打通 + M1

- 首轮 pwsh 全部 0xC0000142（DLL 初始化失败），Node spawn EPERM；重启 DSH 后恢复。
- 工具链先放工作区，**失败**：ld 读不了中文路径。改到 C:\bp-toolchain。
- 产物：.map 解析（唯一单位转换点）、brush 凸体、盒体扫掠、分步 slide、哈希、RNG、
  headless_sim、move_metrics。
- 坑：Minkowski 支撑方向用 max → 100/100 穿透（pitfalls/minkowski-support.md）。
- 证据：selftest 600 tick 一致；5 速度档 ×100 次 0 穿透；bench p99 0.3 µs。

### S02 M2（step-up / 蹲下空间）

- step-up：抬高 → 前进 → 落下；落点必须可站立且有收益。
- 坑：接触 epsilon 把「已贴上」判成「没碰到」→ 贴墙每 tick 挤 19 mm
  （pitfalls/contact-epsilon.md）。修法：进入分数钳到 0，并加沿墙滑动的正对照。

### S03 M3（parkour 五件套，首版）

- slide / dash / wallrun / wallclimb / vault，全部整数 tick 状态机。
- 坑：墙跑一帧就掉——速度投影到墙面后变平行，探测仍沿速度方向；改为沿法线反向探测。
- 坑：翻越探测不到——同类 epsilon 问题；所有「面前有没有东西」改为后退 5 cm 起扫。

### S04 键位（数据驱动）

- sim/input：键码枚举、Bindings（加载/改键/保存/冲突检测）、build_move_input。
- 默认瓦洛兰特布局：C 翻越 / Q 墙跑 / E 爬墙 / X 冲刺。
- 坑：序列化用显示名（WallRun）而解析用 TOML 键名（wallrun）→ 存盘重载静默回默认。

### S05 M4（demo 录制/回放/跨机比对）

- 二进制 demo：头（build_id / sim 版本 / params_hash / geometry_hash / 地图 / 初始状态）
  + 每 tick 输入 + 世界哈希；约 28 B/tick。
- 坑（认知）：params_hash 进了世界哈希，所以「改参数能检出」是恒真的；
  物理差异必须用位置 dump 证明（教训 6）。

### S06 接回 Godot（纠正路线错误）

- 先写了 Win32+GDI 自绘 demo，违背 §3.1；用户指出后删除，改用 Godot。
- 栈：Godot 4.5（godot-cpp 最高 4.5）+ SCons 构建的 godot-cpp + bridge/sim_bridge.cpp。
- 坑：GDScript 路径是 UTF-8，std::ifstream 按 ANSI 打开 → 地图打不开；改 u8path。
- 坑：DLL 缺 libwinpthread-1.dll（Error 126）；复制到 Godot 可执行文件旁。
- 坑：ld 不能写中文输出路径 → build_bridge.ps1 全用相对路径。

### S07 试玩反馈：方向反了

- A/D 反：wish_direction 右向量符号错（(-sin,+cos) 应为 (sin,-cos)）。
- 鼠标反：sim 里 yaw 增大是左转，脚本写成 yaw += dx；改 yaw -= dx。
- 斜撞台阶滑走：step-up 只比水平收益；改为「落点更高 5 cm 也接受」。

### S08 「能动视角但不能移动」

- 根因：碰撞盒卡进 brush 内部，每 tick 扫掠起点实心 → 速度清零。
- 修：step-up 拒绝实心落点；pmove 每 tick 检测实心并向上/四向脱离。
- 证据：--test-stuck（薄墙内部出生，60 tick 后移动 6.52 m）；
  --test-soak（6000 tick 乱按，最长连续实心 1 tick）。

### S09 跳跃边沿 + M5 网络第一刀

- 连跳：jump 改为按下沿（jump_was_down），字段进快照。
  证据：--test-jump（按住 300 tick 只跳 1 次；点按 3 次跳 3 次）。
- 网络：netchan（seq/ack/ack_bits）、delta 快照（baseline=最近 ack，3 个未 ack 退全量）、
  客户端预测 + 128 tick 历史 + 回滚、输入冗余 4、服务器按 tick 对齐输入。
- 坑：客户端用最新快照解 delta（baseline 不匹配）→ 缺字段解成 0；
  改为按包内 baseline_tick 查历史快照。
- 坑：服务器把带 tick 的输入排队延后应用 → 人为 2 tick 偏移；改为按 tick 对齐。
- 证据：30% 双向丢包 + 30 ms，偏差 0.00000 m；1491 快照；全量 80 / delta 2020；
  回滚 1428（已知：cmd.tick 未对齐服务器时钟）。

### S10 HP / 护甲（用户拍板）

- HP 100；小甲 25；再生甲 25（随时间恢复）；大甲 50。
- 实现 sim/game/combat.*，数据 sim/data/combat.toml，整数伤害运算。
- 提案（待确认）：吸收 66%；再生甲 5 s 无伤害后开始、10 s 回满。
- 证据：--test-health 8 项全过；重甲躯干 TTK = 6 发 @600RPM = 0.60 s。

### S11 v0.4 并入 + §7.0 时间对齐（2026-10-19）

- 用户给了两份 v0.4：网络专章（docs/网络层深化_v0.4.md）与 §8.3 候选 B（docs/8.3候选B_v0.4.md）。
- 裁决 C1/C2：延迟补偿 v0.1 不做、M6 必做；回溯窗口按公式 6 tick（不是 Source 的 1 s）；
  客户端预测命中（§9.3）默认开，不一致率 > 0.5% 关闭。
- §7.0 根因：客户端与服务器同 tick 起步、用本地 tick 盖戳 → 服务器执行 tick T 时
  命令还在路上，只能沿用上一 tick；旧代码又把迟到命令当“最新输入”重放 → 每 tick 都错。
- 修法：客户端 tick 轴 = 服务器 tick + 4（单程 2 + 前瞻 2）；init 预跑 4 个空 tick；
  InputWindow 丢弃迟到/重复；比历史更旧的快照直接忽略。
- 证据：--test-net 回滚 1428 → 185；N3（5% 丢包 60 s）位置修正 0 次；偏差 0.00000 m。

### S12 §7.3 hitbox 历史（2026-10-19）

- sim/game/hitbox.*：HitboxSample（脚点 / 高 / 半径 / 蹲）+ 64 帧环形；
  服务器与客户端每 tick 各记一条。
- 三段按高度比例（0.82 / 0.45），倍率 **5 / 1 / 0.7**；蹲下高度变，分档自动跟随。
- 回溯 `sample(tick, frac)` 在相邻两帧间插值（v0.4 §9.2）；缺下一帧不外推。
- 判据：--test-hitbox（25 项）；--test-net 顺带断言服务器 newest == tick。
- 成本：10 人 × 64 帧 × ~40 B ≈ 25 KB；bench p99 0.5 µs 不受影响。

### S13 底层性能审计（2026-10-19）

- 优化（逐 tick 哈希不变）：pmove 每 tick 只算一次 sin/cos；slide 子步去掉重复 sqrt；
  蹲姿未变时复用地面探测的 start_solid，省一次 hull trace；HitboxHistory 记录改 O(1)。
- 工具：`--bench N --players M`；全局 new/delete 计数，World::step 稳态 **0 分配**。
- 数据（10 人）：15 brush **2.15 µs**、201 brush 5.94 µs、801 brush 21.71 µs；
  p99 分别 2.9 / 10.7 / 35.9 µs（预算 4000 µs）。
- 结论：不做 broadphase（800 brush 仍 0.54% 预算）；等单图 >2000 brush 再评估。
  没有为未来的武器/破坏加任何字段或钩子。

### S14 §7.7 FULL_STATE / 中途加入 / 重连（2026-10-19）

- 新链路 pending_full_state：每 tick 发 FULL_STATE（join_id、player、params/geometry hash、
  全量快照），收到“未来 tick”的命令才转 delta；迟到/重复命令清不掉标记。
- 客户端不再假设双方从 tick 0 起步：拿 FS 后重置历史，tick = FS tick + 7 预跑，
  首条命令到达即完成握手。
- 断线只摘链路，玩家留在世界；重连走同一 FS 路径，join_id 变化触发重对齐。
- 参数哈希不一致的客户端被拒绝（join_failed）。
- 证据：tick 202 中途加入、tick 701 重连，偏差均 0.00000 m；30% 丢包回滚 51（曾 185）。

### S15 §7.1 远端玩家 1 tick 插值（2026-10-19）

- sim/net/interp.*：SnapshotInterpolator，32 帧环形（0.5 s），按 tick+frac 插值 pos/vel；
  flags/state 取前一帧；越界夹到最近帧、不外推；重复 push 忽略。
- NetClient 每份快照 push，FULL_STATE 时 reset；暴露 remote() 给表现层。
- 只读：不参与 pmove / 命中判定；命中用 hitbox 历史。
- 证据：合成 10/11/12 两玩家中点 1.5/8.5 m；30% 丢包下缓冲 32 帧且 newest == 最后快照。
- 待办：Godot demo 接入远端渲染（目前仍只画本地玩家）。

### S16 §3.4 事件通道（2026-10-19）

- sim/core/event.*：SimEvent + 1024 条固定环；World::step 可选 sink，pmove 不知道事件。
- 跳跃/落地由 World 比较前后 on_ground 发射；事件带 tick，不承载伤害等权威数据。
- bridge：drain_events() + get_tick()；Godot 每帧消费，HUD 显示 last event。
- autoplay 改为按 sim tick 驱动（按帧会因 headless 帧率与 tick 脱钩而把起跳丢在空中）。
- 证据：--test-events（jump 5 / land 47、两次一致、溢出丢 76）；Godot 输出
  land @13 / jump @20 / land @62；bench 0.23 µs、0 allocs/tick。
### S17 真实 UDP 传输（2026-10-19）

- sim/net/udp_transport.*：非阻塞 Winsock UDP；客户端 connect_peer，服务端 recv_from→adopt_peer。
- CMake 仅 WIN32 链接 ws2_32；Loopback 保留给确定性闸门。
- 证据：--test-net 的 [udp] hello/adopt/pong/1200B/empty-poll 全过。
- 边界：一个 transport 一个 peer；Godot 组局与多 peer 适配是下一步。
### S18 B1 快照分片（2026-10-19）

- sim/net/fragment.*：FragmentedTransport 装饰器；小包零开销透传，大包按 1100 B 切片，
  5 B 头（marker/id/index/total），接收端按 id 重组，半包绝不交付。
- 3000 B 往返一致、小包透传；30% 丢包 20 个 2500 B 包：送达 9、损坏 0。
- 尚未接组局路径；届时 FragmentedTransport(UdpTransport) 包一层即可。
### S19 多 peer UDP 与会话（2026-10-19）

- 拆分 UdpSocket / UdpTransport；新增 PeerTransport + UdpPeerHub（按源地址 demux，
  未知来源有界队列）+ 6 字节 HELLO（magic + 协议版本）。
- 闸门 8 新增：真 UDP 环回、两个客户端、同一 hub、2 名服务器玩家。
- 结果：joined 1/1、snapshots 148/148、dev 0.00000 / 0.00000 m。
- 仍在一个进程内；下一步是两进程 net_loop 与 Godot --host/--connect。
### S20 两进程 net_loop + 输入窗口修复（2026-10-19）

- tools/net_loop（--serve/--join）+ tools/net_selftest.ps1：真 UDP、64Hz、两客户端进程。
- 抓到：服务器不执行输入。acc 卡在 8、cmd0 卡在 52；根因是窗口 8 / 队列 4 小于
  客户端领先的 7 tick，每次插入新 tick 都丢掉下一 tick 要执行的命令。
- 修复：InputWindow 窗口与队列都改为 16（早到窗口，不是延迟缓冲）。
- 证据：joins=2、accepted=417、far=0、moved=46.86m、clients_ok。
### S21 Godot 组局（2026-10-19）

- SimWorld::host（listen server + 本机 NetClient/loopback）/ join（UDP NetClient）/ step_network。
- 本地状态统一走 NetClient::predicted()；远端走 SnapshotInterpolator；GDScript 画胶囊体。
- CLI：--host / --connect[=ip] / --port=N；tools/run_net_demo.ps1 一键双实例。
- headless 证据：host joined slot 0、peers 2、remote slot 1 visible；client joined slot 1、
  remote slot 0 visible；stderr 空。
- 未做：连接 UI、时钟漂移纠正、远端朝向/动画、断线重连 UI。
### S22 连接 UI（2026-10-19）

- 启动 LAN 菜单：Host / Join（IP+端口）/ Single player；--menu 可在 headless 下构建。
- 菜单打开时暂停 tick；CLI 与 run_net_demo.ps1 保持可用。
- headless --menu 输出 "net menu open"，无脚本错误。
### S23 客户端时钟漂移纠正（2026-10-19）

- 客户端按快照 ahead 的最小值检测漂移；超过 min+3 且冷却结束时跳过下一次循环
  （不发命令、不推进），领先量永久减 1，无 tick 空洞、无历史修复。
- 强制漂移测试：skips=3、far=0、dev 0.00000 m；12 秒两进程 far=0，最终位置一致。
- 实测环境 12 秒内自然漂移未越阈值（skips=0）；纠正作为长局保险。
### S24 远端朝向（2026-10-19）

- PlayerState/PlayerSnap 增加 yaw；快照 delta 新增 kFieldYaw 字段组；协议版本 2。
- 远端胶囊按 yaw 旋转并加前方标记；插值器取前一帧 yaw（64 Hz 足够）。
- 证据：[yaw] full 1 delta 1；host 看到 remote yaw 0.0→0.9，client 看到 remote 0.9。
### S25 A1 信令协议（2026-10-19）

- sim/net/signaling：TCP 27015、u16 长度前缀帧、六条消息（HELLO/ACK/MANIFEST/READY/
  MATCH_START/FULL_STATE）+ TcpListener/TcpStream（connect/accept 超时）。
- 真 TCP 环回逐字段验证：[signal] hello 1 ack 1 manifest 1 ready 1 match 1 full 1。
- 尚未接入 Godot/NetServer；下一步是信令线程 + MATCH_START 触发 UDP 会话，再做 A2。
### S26 A2 可靠流（2026-10-19）

- sim/net/reliable：独立 rel_seq、窗口 32、累积 ACK + 32 位位图、RFC 6298 RTO、5 次重传上限。
- configstring 512 B 分块 + CRC32；组装器逐块校验，缺块不 complete。
- 30% 丢包：5000 B / 10 块 → retrans 6、complete 1、same 1、corrupt 1、dead 0。
- 尚未与快照共用 socket；单 socket 双队列是下一步。
### S27 A5 重连票据（2026-10-19）

- 32 B：client_id(16) + expiry(4) + HMAC-SHA256 截断 12；密钥只在房主内存。
- 验收：valid 1 / expired 1 / tamper 1 / wrongkey 1；到期时刻即失效。
- 未接信令与重连状态机；HELLO 已预留 ticket 字段。
### S28 全量复查（2026-10-19）

- 修复幽灵远端：快照 flags 增加 active 位，未加入槽位不渲染；host 日志 peers 2 → remote。
- 票据密钥改用 rand_s（失败退 steady_clock），去掉 std::rand。
- 复查：16 闸门 + bench（10 玩家 2.04/3.40/4.30 µs、0 alloc）、两进程 joins=2/420/47.22 m、
  Godot 双实例正常；sim 无 TODO/FIXME、无 std::rand。
- 技术债：CMake 与 build_bridge 源清单重复；PROGRESS S21/S20 顺序错乱；Overflow 不可达。

---

## 2. 当前基线数字（2026-10-19）

| 指标 | 值 |
|---|---|
| 跳高 / 跳远 | 1.106 m / 5.335 m |
| 空中 strafe 增益 | 2.11 m/s |
| 0→99% 最高速 | 10 tick |
| 撞墙穿透 | 8.13–30 m/s ×100 次，0 次 |
| 接触位置 | x = -0.456（理论 -0.455） |
| bench p99 | 0.4 µs（单玩家）/ 2.9 µs（10 人） |
| World::step 分配 | 0.00 / tick |
| 网络偏差 | 0.00000 m（30% loss / 30 ms） |
| 网络回滚 | 51 / 2000 tick（30% loss；曾 1428） |
| N3 位置修正 | 0 次 / 60 s（5% loss + 10 ms jitter；阈值 <3） |
| 中途加入 / 重连 | 偏差 0.00000 m（FS tick 202 / 701） |
| 远端插值 | 32 帧缓冲（0.5 s），中点插值 1.5 m / 8.5 m |
| 事件通道 | 1024 环，溢出丢 76；Godot 收到 land/jump/land |
| 真实 UDP | 回环 hello/pong/1200 B 往返全过 |
| 分片 | 3000 B 往返一致；30% 丢包下 0 损坏 |
| UDP 双人会话 | 2 客户端 joined、148/148 快照、偏差 0.00000 m |
| 两进程 | joins=2、accepted=417、far=0、moved=46.86 m |
| Godot 组局 | host slot 0 / client slot 1，双方 remote visible |
| 护甲 | 25 / 25 再生 / 50；吸收 66% |
| demo | ~28 B/tick；600 tick ≈ 17 KB |

---

## 3. 关键决定（含否决）

| 决定 | 理由 |
|---|---|
| 工具链放 C:\bp-toolchain | ld 不能读中文路径（junction/subst/8.3 都试过） |
| Godot 4.5（不是 4.7） | godot-cpp 最高 4.5；4.7 是目标，等上游 |
| sim 层不引入 Godot 类型 | 计划书 §3.2；headless 测试依赖它 |
| demo 存输入 + 每 tick 哈希 | 回滚与 replay 需要精确定位 |
| 键位数据驱动 | 用户要求可改；且保证 sim 确定性 |
| 跳跃按下沿 | 用户试玩反馈；服务端语义必须一致 |
| 删除 Win32 自绘 demo | 违背宿主选型 |

---

## 4. 没做完的事（按计划书顺序）

1. §7.7 full_state / 重连 / 中途加入
2. §7.1 其他玩家 1 tick 插值
3. 事件通道（§3.4）
4. v0.4 M5 增量：B1 分片、A1 信令、A2 可靠流、A5 恢复
5. M6：hitscan、后坐力表、三段判定、穿透、伤害衰减、hitmarker
6. 跨机 demo 比对（缺第二台机器）
7. 游戏内改键 UI、movement.toml 热重载

---

## 5. 复现命令

~~~powershell
powershell -File tools\build.ps1
powershell -File tools\smoke.ps1                 # 17 项行为闸门 + bench，必须 ALL GREEN

build\headless_sim.exe --test-net                 # 网络收敛/回滚数
build\headless_sim.exe --test-health              # 护甲规则与 TTK
build\headless_sim.exe --test-stuck --test-soak  # 卡死恢复与压力
build\headless_sim.exe --test-jump                # 跳跃边沿
build\headless_sim.exe --record build\run.bpd --ticks 600
build\headless_sim.exe --play build\run.bpd
~~~

Godot demo：

~~~powershell
powershell -File tools\run_demo.ps1
~~~

---

## 6. 给下一个会话的提醒

- 不要重写已经绿的东西；smoke 是唯一真相。
- 所有试玩问题必须先落到 sim + 一个 headless 判据，再谈表现层。
- 严格按 AGENTS §11 的顺序推进；跳步会重演「demo 先行」的错误。
- 用户在意两件事：严格遵循计划书、不要遗漏。每次收尾更新 AGENTS 的未结案。
### S29 源文件清单单一化（2026-10-19）

- tools/sim_sources.txt 成为唯一清单；CMake 与 build_bridge.ps1 都读它。
- tools/check_sources.ps1 校验清单 == git ls-files sim/**/*.cpp（26 个）。
- 全量构建 + bridge + smoke 仍绿；修掉“桥接漏 .cpp 导致链接失败”的架构债。

## 7. 2026-10-20 收束（S30–S31 + 详细日志）

### 7.1 这一晚补的三件事

1. **掉线清理**：Link 记录 last_rx_tick；连续 320 tick（5 s）无包 → remove_client，槽位失活。
   判据：`[timeout] joined 1 dropped 1`。
2. **槽位复用**：add_client(..., -1) 选第一个 inactive 槽位；bridge/net_loop 删除 next_slot。
   判据：`[timeout] joined 1 dropped 1 reuse 1`。
3. **源清单单一化 + 闸门化**：tools/sim_sources.txt 是唯一清单；check_sources 接入 smoke 首项。

### 7.2 存档与远端

- GitHub 私有仓库：https://github.com/Orlean-Crispy/unknow
- 本机 remote：`ssh://git@ssh.github.com:443/Orlean-Crispy/unknow.git`（SSH 443，绕开失效的 HTTP 代理）。
- 首次合并：远端有 Initial commit（LICENSE + README），用 `--allow-unrelated-histories` 合并，
  README 保留本项目版本；合并提交 59fa04a。
- 本地 bundle：`G:\BreakpointBackups\breakpoint-20261019.bundle`；恢复步骤见 docs/backup.md。

### 7.3 当前基线

- 17 项闸门（第 17 项 = Godot 桥）+ bench ALL GREEN；工作树干净。
- main 以 `git log --oneline -1` 为准（本轮收尾会再提交一次文档）。
- 详细开发日志：docs/PROGRESS.md 末尾「开发日志（详细）」，2026-10-20 起按六段模板追加。

### 7.4 下一步

1. M5 收尾：A1/A2/A5 接入组局（信令线程、单 socket 双队列、票据/重连状态机）+ 局内 UI。
2. M6 射击：WeaponDef、开火输入、hitscan、三段伤害、hp/armor 进快照、死亡/重生。
3. 缺口与顺序：docs/GAPS.md。

### 7.5 命名落地（S32，同日）

- 对外名 **IMPULSE**、缩写 **IMP**、内部代号 **Project Breakpoint / Project Bp / bp**；
  方案全文 `docs/IMPULSE_命名方案.md`，决议 `docs/decisions/0005-naming-impulse.md`。
- **没有**做 `sim::` → `bp::` 全局重命名（ADR 0005 写了三条理由）；项目级前缀继续用 bp。
- 玩家可见：`godot/project.godot` 的 `config/name` 与 LAN 菜单标题已改为 IMPULSE。
- 未决：impulse 上限 sim 4 / 方案 6 / 计划书 §5.2 待定（lead：M3 手感验收）；
  GitHub 仓库名仍是 `unknow`，改名需在 GitHub 侧操作。

## 8. 2026-10-20（下午–晚间）· M5 收尾 + 两轮代码体检

### 8.1 M5 现在意味着什么

M5 已关闭（docs/AGENTS.md §0.2 标 ✅）。它包含：不可靠 UDP + delta 快照 + 预测回滚 + 时间对齐 +
输入去重 + hitbox 历史 + FULL_STATE 加入/重连 + 远端插值 + 事件通道 + 真实 UDP + B1 分片 +
多 peer + 两进程 + Godot --host/--connect + LAN 菜单 + 时钟纠正 + 远端 yaw + 5 s 掉线清理 + 槽位复用 +
**A1 信令组局（27115）、A5 票据/重连、A2 可靠流共 socket（configstring）、局内 HUD、暂停菜单、失联检测**。

不属于 M5（按计划书归 M7）：第三人称动画、5v5 选边。

### 8.2 架构（这段是新拆的，找代码先看这里）

```
bridge/sim_bridge.cpp    SimWorld：状态/输入/事件/HUD 数值/Godot 绑定（313 行）
bridge/net_gateway.*     NetGateway：全部 socket 与每 tick 网络泵（host/join/reconnect/pump）
bridge/mesh_builder.*    build_map_mesh(collision) → godot 数组（纯函数）
bridge/mesh_common.h     to_godot（Z-up→Y-up）、material_color（纯函数）
sim/net/matchmaking.*    MatchHost/MatchClient：TCP 握手、票据、槽位（含 release）
sim/net/admission.h      approve/take：UDP 谁可以被放进来（两个宿主共用）
sim/net/reliable_transport.*  一条 socket 两种语义，打标 demux
sim/net/configstring.h   18 B 配置契约（version u16 + params_hash + geometry_hash）
tools/headless_sim/      main.cpp（272 行，只分发）+ tests_net/move/demo + test_util
```

### 8.3 协议与数据格式（v4）

- `BP_PROTOCOL_VERSION = 4`：HELLO 22 B（magic + 版本 + 16 B client_id）；快照新增
  wall_normal/vault_dir/vault_rise（kFieldExtra）、jump_was_down（flags bit3）、slow_ticks。
- 信令协议 v2：client_id 16 B、HELLO/ACK 带 32 B 票据、MATCH_START 带 game_port（= 信令端口 + 1）。
- configstring v2：18 B payload，params_hash 已并入 combat 规则哈希。
- 旧 DLL 与新 sim 不可混用（协议不兼容，有意为之）。

### 8.4 本轮新增的闸门（都在 --test-net / --selftest 里）

| 判据 | 说明 |
|---|---|
| `[signal] / [match] / [ticket]` | TCP 六消息、握手/票据/满员/恢复 |
| `[match] … release 1` | 握手中断归还槽位 |
| `[udp] exclusive 1` | 同端口第二个 socket 必须打不开 |
| `[seqwrap] ok 1 delta +300 full +0` | u16 快照序号回绕后 ack 仍有效 |
| `[dual] chunks 8/8` | 可靠流与快照共 socket 互不阻塞 |
| `[config] matched 1 refused 1 combat 1` | 配置/战斗规则失配拒局 |
| `[rollback] hitbox 1 dup 1 state 1` | 回滚改写该 tick 历史、重复快照不二次回滚 |
| `[netalloc] per_tick 0` | session 三步稳态零分配 |
| `[netevents] 1` | 联机时事件通道不再为空 |
| `state_fields 8/8 / yaw_hash 1` | 世界哈希覆盖参与后续 tick 的字段 |
| `[parkour] vault_climb_single 1` | 翻越与爬墙同 tick 只花一次 impulse |

### 8.5 怎么验（照抄即可）

```powershell
powershell -File tools\build.ps1
powershell -File tools\smoke.ps1          # 17 闸门（含 Godot 桥）+ bench，必须 ALL GREEN
powershell -File tools\net_selftest.ps1   # 两进程真实 UDP：joins=2 far=0 ports_ok=True
powershell -File tools\build_bridge.ps1   # 改过 sim/ 或 bridge/ 之后必须重跑
powershell -File tools\godot_check.ps1    # 只跑桥：--hud-check + 重连 + 暂停 7 s（退出码 3 = 跳过）
```

第 17 项已经在 smoke 里，正常不用手敲。想手工看两个窗口：`tools\run_net_demo.ps1`（两边都走
ports.h 的默认端口，不用填数字），或 host `--host`、client `--connect=127.0.0.1`；
host 应打 `peer joined slot 1 / remote slot 1 visible`，client 打 `joined slot 1 / remote slot 0 visible`，
两个 stderr 必须为空。重连验证加 `--reconnect-check`（host 应打 `signal slot 1 resumed 1`，
client 应打 `reconnect ok` 与 `reconnect double=0`）。
暂停验证：client 加 `--force-paused`，7 s 后仍应打 `peers 1` 且不出现 `SERVER SILENT`（AUD-02 真闸门）。

**注意**：`--hud-check` 在扩展加载失败时**不会退出**，会一直挂着。所以别手敲裸命令等它，
用 `godot_check.ps1`（带 60 s 超时）；改 DLL 前先确认没有 Godot 进程在跑，否则 ld 报 Permission denied。

### 8.6 明确未完成（交接必读）

**协议 v5**：PlayerSnap 现在带四个按下沿、recharge_progress、speed_coefficient；
旧 DLL/旧 EXE 不能混用（HELLO 版本号会拒）。改快照字段必须同时改 version.h 与本文。

- **AUD-16**：`get_hp()/get_armor()` 仍是常量 100/0，M6 把 hp/armor 放进快照时一并解决。
- **AUD-20**：客户端落后不补 tick；**AUD-26**：UDP 不校验源地址；**AUD-27**：正常离开不释放组局槽位
  —— M6 前清（都影响"两个人真打一局"）。
- **AUD-21/22/23/24**：序列化分配、可靠流 u16 回绕、分片未接真实链路、admission 无 TTL。
- **AUD-28/29**：手感常量仍是字面量（铁律 I）、combat 诊断与 max_health（M6 一起）。
- **AUD-31..35**：信令主线程同步、HUD 每帧重拼、左右键侧别、trace 注释、主循环 5 tick 上限。
- 第 17 项在没装 Godot（或没建 DLL）的机器上打印 SKIP 并返回 3——smoke 视为跳过；
  跨机 demo 比对仍缺第二台机器。

AUD-19/13/17/14/15 已清（S58）；S59 又修了三十余条（impulse 充能、空中 step-up、Jump 事件、geometry_hash 标志、快照 v5、
幽灵链路超时、全零 id、demo 句柄/截断、桥的 solo 分配/眼高/失败清理、CLI 校验，
以及一大批"只打印不断言"的闸门）。细节见 PROGRESS 最后一节与 AUDIT 第 6 节。

### 8.7 下一步

债已清，直接进 M6：WeaponDef(toml) → 开火输入 → hitscan + hitbox 回溯
→ 三段伤害 → hp/armor 进快照（顺带 AUD-16）→ 死亡/重生 → 穿透/衰减。每一小步先写闸门。

### 8.8 本轮新增/改动的脚本（别再造第二份）

| 脚本 | 职责 |
|---|---|
| tools/setup_toolchain.ps1 | 唯一知道下载地址与版本；Expand-Archive；无 7z |
| tools/build.ps1 | 只认无版本目录名，缺工具就报名字 |
| tools/build_bridge.ps1 | 与 CMake 同一套标志 + BP_BUILD_ID |
| tools/godot_env.ps1 | 唯一知道 Godot 路径与首跑导入（`$env:BP_GODOT` 优先） |
| tools/godot_check.ps1 | 第 17 项闸门：--hud-check + 两进程重连，带超时，退出码 3 = SKIP |
| tools/check_sources.ps1 | sim 源码清单 + 端口字面量 + 三条铁律 lint（含 untracked） |

---

## 9. 2026-10-20（夜）· M1–M5 全量审计交接（S59）

> 这一节是给"下一个打开仓库的人"的：**先读 §9.2 的三件事**，再决定要不要动代码。

### 9.1 一句话

四个**只读**代理分别通读 sim 核心/移动、sim 网络、bridge+Godot、tools+闸门；我逐条复核后
修了 **三十余条**（§9.4 逐条列出；其中 2 条真数据缺陷、1 大类"闸门根本不会失败"），协议升到 **v5**，
17 项闸门现在**真的会红**。剩 **12 条 + 5 个设计待定**，其中 AUD-20/26/27 排在 M6 前。

### 9.2 先看这三件事（否则一定会改错）

1. **协议 v5，快照字段变了。**
   PlayerSnap 现在带四个 *_was_down（flags bit0-6）、recharge_progress、speed_coefficient。
   旧 DLL 与旧 EXE **不能混用**（HELLO 版本号会拒）。要动快照，必须同时改：
   sim/core/version.h、sim/net/snapshot.h/.cpp、session.cpp 的 same_state、
   tests_net.cpp 的 [snapfields]、本文件 §8.6、docs/AGENTS §8.4。
   **判据**：--test-net 里 [snapfields] 1。
2. **17 项闸门现在会真的失败。** --bench 超过 4 ms 或有任何分配就返回 1；--info 会断言
   test_box 的形状；光线命中要求 fraction 落在区间；墙测试要求"真的到达墙"；
   --play 遇到不同 build_id 直接失败。看到红先怀疑实现，不要先怀疑环境。
3. **文档曾经跑在代码前面。** 本轮发现三处：--force-paused 从未解析（AUD-02 的验证是假的）、
   entry["state"] 没删（AUD-18 写着已删）、"桥已下发 configstring"（真实链路没接可靠流）。
   以后写"已通"之前先找到会失败的闸门；找不到就先写进 docs/AUDIT.md §6.2。

### 9.3 这五类问题，如果没修会发生什么（按后果排序）

| 类 | 具体 | 没修的后果 |
|---|---|---|
| A | impulse 着地**每 tick** +1 | 4 个 impulse **62 ms** 回满，M3 的 parkour 资源与冷却全部失真，"省着用"这件事不存在 |
| B | 回滚快照缺 slide/dash/vault 沿与 speed_coefficient | 按住滑铲/冲刺每个快照都被当"刚按下"重放：白扣 impulse、修正数暴涨、位置永远追不上 |
| C | 从没收到包的链路永不超时 | 掉线者永久占槽 + 每 tick 继续 FULL_STATE（约 43 KB/s）；越玩越卡、新玩家进不来，还能看到"立正幽灵" |
| D | 全零 client_id 被当成"空闲槽" | 局域网里任何人构造一个全零 id 就能占走房主 slot 0，把房主顶掉 |
| E | 闸门只打印不 return | 所有"证据"都可能是假的：9 处判据（info/bench/land/分片/dup/字段/胸倍率/墙到达/光线 fraction）从未失败过 |

### 9.4 已修（按域逐条；每条都给出"现在怎么证明"）

| 域 | 修了什么 | 证明 |
|---|---|---|
| 模拟 | impulse 充能改 recharge_ticks（toml + hash） | --test-parkour：31 tick 不回、第 32 tick 回 1 |
| 模拟 | 空中不再 step-up | --test-step 仍绿；跳跃撞台沿不再抬升 |
| 模拟 | Jump 事件要求按下沿 + 起跳前着地 | --test-events（jump tick 5 / land tick 47） |
| 模拟 | 删 PlayerState.tick，hash 用 recharge_progress | state_fields 21/21 |
| 模拟 | geometry_hash 含 _climbable/_wallrunnable 且缓存 | [config] 与两进程联机 |
| 模拟 | bumps 严格 < max_bumps | --test-wall 100 次 × 5 档 |
| 模拟 | pmove 拒绝 NaN/dt≤0，输入钳 [-1,1] | --selftest 600 tick 一致 |
| 模拟 | 爬墙 keep 检查 climbable | --test-parkour wall climb |
| 模拟 | 0 brush 地图报错 | --info 现在断言 brush>0 |
| 模拟 | entity origin 由 map_parse 换算 | check_sources 的 0.0254 规则 |
| 网络 | 协议 v5：四个沿 + recharge + speed_coefficient | [snapfields] 1 |
| 网络 | 超时从 add_client 起算 | [timeout] joined 1 dropped 1 reuse 1 |
| 网络 | 拒绝全零 client_id | [match] 拒绝路径（与 tamper 同出口） |
| 网络 | MatchHost::start 先 stop | 重复 host 不再 terminate |
| 网络 | peer 收件队列封顶 64 | 长局内存不随孤儿 peer 增长 |
| 网络 | bad_snapshots() 接到 net_loop | 手工跑 net_loop 可见 bad= |
| demo | DemoReader 析构/close；短读≠EOF | --test-demo 截断检测 |
| demo | build_id 进 mismatch，--play 失败 | --test-demo 改参数/改构建都红 |
| demo | --compare 要求同 EOF 且 rows>0 | 空/截断 dump 会失败 |
| demo | 删除死字段 m_file | 编译期 |
| 桥 | solo 用 step_single | 铁律 E（0 分配） |
| 桥 | params_hash 用 combined | [config] combat 1 同源 |
| 桥 | 眼高 0.9×姿态高度（蹲下会降） | --hud-check 手动可见，逻辑单点 |
| 桥 | join 失败回滚 disconnected | 失败后 solo 仍能走 |
| 桥 | start_host 失败 abort_host() | 不再留 TCP 线程/端口 |
| 桥 | join_impl 先建后换 | 瞬时 UDP 失败不打断现有会话 |
| 桥 | 删除未读 entry["state"] | 每帧少一个 Variant |
| Godot | --force-paused 真解析 | godot_check「暂停 7 s 不掉线」 |
| Godot | 暂停时同步相机 + drain 事件 | 恢复不瞬移、旧事件不集中吐出 |
| Godot | Esc 过滤 echo；autoplay 改边沿 | 长按不反复开关；跳帧也能跳 |
| 工具 | CLI 全量校验、record/play 互斥、dump 需 play | --bench 0 / --ticks -1 报错退出 2 |
| 工具 | 闸门改成能失败（9 处） | 见 §9.3 E |
| 工具 | CMake 与 build_bridge 同解释源清单 | 在 sim_sources.txt 加注释不再只坏一边 |
| 工具 | check_sources 收 untracked + 三条铁律 lint | sources ok / ports ok / rules ok |
| 工具 | run_net_alloc_check 移回 tests_net | AUDIT A4 名副其实；[netalloc] 0 |
| 清理 | 死代码 11 处 | 全量 -Werror + 频率扫描 |

### 9.5 未修（12 条）与设计待定（5 条）

| id | sev | 一句话 | lead |
|---|---|---|---|
| AUD-20 | P1 | 客户端落后不补 tick，>110 ms 卡顿后输入永久 Late | **M6 前** |
| AUD-26 | P1 | UDP recv 不校验源地址（同网段可注入） | **M6 前** |
| AUD-27 | P1 | 正常离开不释放组局槽位（N 个 id 后 RoomFull） | **M6 前** |
| AUD-21 | P1 | 序列化/接收每 tick 约 4-8 次分配（[netalloc] 只覆盖 session 三步） | M6 |
| AUD-22 | P1 | 可靠流 u16 回绕不安全（第 65536 个包后静默死） | M6 |
| AUD-23 | P1 | FragmentedTransport 未接真实链路（M6 加 hp 后满编快照会走 IP 分片） | M6 |
| AUD-24 | P1 | admission 无 TTL/上限 | M6 |
| AUD-25 | P2 | ReliableTransport 的 now_ms 只在 advance 更新（采样少一 tick） | 顺手 |
| AUD-28 | P2 | 手感常量仍是字面量（0.02/-0.05/+0.15/0.5×/0.05/0.2/0.55/1e-3/0.7），违反铁律 I | M6 前 |
| AUD-29 | P2 | combat.toml 无未知键诊断；max_health 无消费者 | M6 |
| AUD-30 | P2 | demo 初始状态不完整（只能从 spawn 录）；DemoWriter 非流式 | M6 |
| AUD-31..35 | P2 | 信令主线程同步 / HUD 每帧重拼 / 左右键侧别 / trace 注释 / 5 tick 上限 | M7 |

**设计待定（需要拍板，不是 bug）**
1. impulse 充能速率（现 recharge_ticks=32 = 0.5 s/个，是提案）；
2. 空中 step-up 已按"不该免费上台阶"关掉；要做 Apex 式 mantle 应显式实现；
3. 护甲吸收 66% 与再生 5 s/10 s 仍是提案（改 1% 就破 TTK 判据）；
4. 可靠流 configstring 目前只在闸门启用（真实链路靠 FULL_STATE 两个哈希拒局）；
5. acked_cmd 与 ack_bits 暂未消费（后者恒 0，已在 netchan.h 注明），M6 接输入 ack 时再定。

### 9.6 怎么验（照抄，含预期输出）

~~~powershell
powershell -File tools\build.ps1          # -Werror，零告警
powershell -File tools\smoke.ps1          # 17 项 + bench；结尾 ALL GREEN
powershell -File tools\build_bridge.ps1   # 与 CMake 同一套标志
powershell -File tools\godot_check.ps1    # hud + 重连 double=0 + 暂停 7 s
powershell -File tools\net_selftest.ps1   # joins=2 accepted≈420 far=0 ports_ok=True
~~~

关键行（数字可小幅浮动，语义不能变）：

~~~
PASS: 2 entities, 8 brushes, 21.0 m across
state_fields  21/21
  [snapfields] 1
  impulse recharge interval          top-up after 32 ticks=1  ok
  [fragment] 3000B 1 small 1 lossy delivered 9 corrupted 0
  [netalloc] per_tick 0
PASS: p99 within the 4 ms budget, zero allocations per tick
hud hp=100 armor=0 impulse=4/4 role=solo vertices=360 build=<hash> protocol=5 port=27115
PASS: the link survived 7 s with the menu open
~~~

**别手敲裸 --hud-check**：扩展加载失败时它不退出，会挂着并把 DLL 锁住（ld 报 Permission denied）。
用 godot_check.ps1（带超时）。

### 9.7 下一个人的前 30 分钟

1. 读 docs/AGENTS.md §0.2 / §3 / §5 / §8，再读本文件 §8 与 §9。
2. git log --oneline -8：f6f9e29 是审计收尾，前面 5 条是 AUD-19/13/17/14/15。
3. build.ps1 → smoke.ps1：**不绿不要往下走**，红的那个就是战场。
4. build_bridge.ps1 → godot_check.ps1：确认扩展真的加载、端口来自 ports.h。
5. 要动快照/协议：先读 §9.2 第 1 条（六个地方必须一起改）。
6. 要动移动：先跑 --test-parkour，再按 pmove.cpp 顶部的顺序注释读。
7. 要动网络：先跑 --test-net，再读 session.cpp 的输入窗口、回滚、时钟三段。
8. 接手 M6：从 AUD-16（hp/armor 进快照）开始，它同时解决 AUD-29。

### 9.8 本轮踩的坑（过程记录，不是设计决策）

1. $Host 是 PowerShell 只读自动变量，两进程脚本第一版直接崩；改名 $hostProcess。
2. 扩展加载失败时 --hud-check 不退出 → 留下 3 个僵尸 Godot → 下一次链接报
   cannot open output file … Permission denied。**改 DLL 前先确认没有 Godot 在跑。**
3. 中文路径又咬人：objdump/nm 对 DLL 直接 No such file or directory，只能复制到 ASCII 目录再看导出表。
4. probe 与已有的 World probe 撞名 → conflicting declaration；改名 probe_field。
5. 快照往返断言把 slow_factor 算进去——它是 pmove 每 tick 从 slow_ticks 派生的，不是线上状态。
6. soak 阈值收到 30 tick 立刻红：45 tick 的"不动"是**贴墙顶着走**，合法。最终按"solid 才是真冻结"
   （≤2）判，frozen 放到 64（1 s）。
7. [fragment] 一开始要求 30% 丢包下 delivered≥10，实测 9。阈值要卡在"能证明重组发生过"的最低线
   （改 5），否则闸门变成运气测试。

### 9.9 文件地图变化（本轮）

- **新增**：sim/net/ports.h（唯一端口来源）、map_parse.h 的 entity_origin_metres()、
  tools/godot_env.ps1、tools/godot_check.ps1；Snapshot 新增 kFieldCharge 字段组。
- **删除**：rng_at64、clampf、Session 的三个死 accessor、LoopbackTransport 的两个计数器、
  PlayerState::tick、Link::ever_seen、last_acked_snapshot、DemoWriter::m_file。
- **移动**：run_net_alloc_check 从 main.cpp 移到 tests_net.cpp（main.cpp 只剩参数与分发）。
- **协议**：BP_PROTOCOL_VERSION=5（version.h 是唯一来源）。

### 9.10 存档

- commit f6f9e29（审计修复 + 文档），已推 main。
- bundle：G:\BreakpointBackups\breakpoint-20261020-161319.bundle（verify 通过）。
- 工作树干净；无残留 Godot 进程。

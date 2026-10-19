# HANDOFF — 会话完整交接（2026-10-18 ~ 10-19）

> 用途：上下文被压缩或换会话后，除了 docs/AGENTS.md 的约束，还需要知道**这一天到底做过什么、
> 为什么这么做、证据是什么**。约束与未结案以 AGENTS.md 为准；本文件只记录过程与结论。

---

## 0. 一句话总结

从零搭起 M0–M4 全部、M5 的网络骨架、M6 的 HP/护甲规则；16 项 headless 行为闸门 + bench 全绿；
Godot 4.5 宿主 + GDExtension 桥可运行。所有试玩反馈都已落回 sim 层并带回归测试。

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
powershell -File tools\smoke.ps1                 # 16 项行为闸门 + bench，必须 ALL GREEN

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

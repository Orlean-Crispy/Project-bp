# 未解决问题与技术债（单一入口）

> 2026-10-20 建立。**以后"已知问题"只认这一份**：AGENTS §4 只留需要拍板的条目，
> AUDIT §6.2 保留审计当时的证据叙述，GAPS.md 降级为历史对照（v0.1 缺口盘点，已过期）。
> 关闭一条 = 改这里的「状态」+ 写上能复现的证据（命令与关键输出），同一个 commit 完成。

## 0. 使用方式

- **级别**：P0 会算错/崩/泄漏；P1 会误判、卡住、不同步、可被利用；P2 死代码/性能/可维护性；
  P3 文档与口味。级别只看"如果发生，后果多大"，不看"多难修"。
- **状态**：`open` 未动；`blocked` 被外部条件挡住（写在备注）；`needs-decision` 等拍板；
  `closed` 已修（移到最后一张表，保留 ID 不复用）。
- **关闭判据**必须是可失败的检查（命令 + 期望输出），不写"改好了"。
- **ID 规则**：审计编号（AUD-xx）沿用 docs/AUDIT.md；这里新增用 DEBT-xx / DESIGN-xx / FEAT-xx。
  永不复用；删除的条目进"关闭记录"。

**当前计数**：未解决 **34** 条（P1 7 / P2 20 / P3 7）；设计待定 **8**；功能缺口 **11**；
已接受边界 **5**。

---

## 1. 网络（sim/net、bridge 的联机部分）

| ID | 级别 | 症状 | 证据 / 位置 | 关闭判据 | 状态 / lead |
|---|---|---|---|---|---|
| AUD-20 | P1 | 客户端落后不补 tick：一次 >110 ms 卡顿吃掉 lead+slack 后，命令永久 Late，只能重连 | session.cpp 输入窗口 + main.gd 5 tick 上限 | 新增闸门：客户端停 300 tick 再恢复，late 停止增长且位置继续前进；无需重连 | open · M6 前 |
| AUD-21 | P2 | 序列化/接收热路径每 tick 约 4-8 次堆分配；`[netalloc]` 只统计 session 三步，所以永远 0 | session.cpp encode/wrap/recv；main.cpp 的统计边界 | 把 encode/decode/recv 计入 netalloc，仍要求 per_tick == 0 | open · M6 |
| AUD-22 | P2 | 可靠流 `seq <= ack` / `seq > m_next` 不是 u16 回绕安全的；第 65536 个包后静默死 | reliable.cpp:67,99 | 用差值比较；闸门推 70000 个 rel 包仍完整交付 | open · M6（现在只传 configstring，打不到） |
| AUD-23 | P1 | `FragmentedTransport` 只在闸门里组装，bridge/net_loop 的真实 UDP 没套；M6 加 hp/护甲后满编快照会走 IP 分片 | bridge/net_gateway.cpp 的 add_client；net_loop/main.cpp | 真实链路包 >1100 B 时 `[frag] sent/reassembled` 计数增长且快照完整；10 人场景跑通 | open · **M6 前** |
| AUD-24 | P2 | admission 的 approve 队列无 TTL/上限：TCP 通过但不发 HELLO 的 id 永久留在 deque（内存 + 调试噪音，不占槽位） | admission.h:17-29 | 加过期（如 120 s）或容量上限；闸门断言过期后被清理 | open · M6 |
| AUD-25 | P3 | `ReliableTransport` 的 now_ms 只在 advance 更新，recv 在 advance 之前时 RTT 采样少一 tick | reliable_transport.cpp:46,56-60 | recv 里也推进 now_ms；loopback RTT 采样等于 1-2 tick | open · 顺手 |
| AUD-26 | P1 | UDP recv 丢弃源地址，客户端不校验包来自 connect_peer 的主机：同网段可注入快照/命令 | udp_transport.cpp:26-30 | 源地址不符即丢并计数；闸门用第二个 socket 注入应被拒 | open · **M6 前** |
| AUD-27 | P1 | MatcHost 槽位只在 MATCH_START 前的中断路径释放；正常离开后 bridge 从不 release → 2 个 id 各进一次后房间永久 RoomFull | matchmaking.cpp:74-92,174-176；net_gateway 无 release 调用 | NetServer::remove_client → MatchHost::release(slot) 回调；闸门：2 槽房连续 join/leave 3 次仍能进 | open · **M6 前** |
| DEBT-01 | P2 | 包头 `ack_bits` 恒 0、`Snapshot::acked_cmd` 解出无人读：第二套 ack 的残骸 | netchan.h（已注明）、snapshot.cpp:128 | 要么删字段（协议 v6）要么真填并让基线选择用它，二者都要闸门 | open · M6 |

## 2. 模拟 / 移动（sim/move、collision、data）

| ID | 级别 | 症状 | 证据 / 位置 | 关闭判据 | 状态 / lead |
|---|---|---|---|---|---|
| AUD-28 | P2 | 仍有一批影响手感的常量是字面量：地面探测 0.02、墙探测 -0.05/+0.15、滑铲结束 0.5×、攀爬接受 0.05、墙跑内推 0.2、翻越前进 0.55、kClipEpsilon 1e-3、可行走法线 0.7 | pmove.cpp:9-11,137,242,383,389；trace.cpp:11,235 | 全部进 MovementParams 并计入 params_hash；grep 这些字面量只出现在 config 默认值 | open · M6 前（违反铁律 I） |
| AUD-33 | P2 | trace 的注释与闸门仍把 step_len 当"防穿墙"依据；真正防穿是连续扫掠，>38.4 m/s 时 step_len 会超墙厚 | trace.h:43-47；tests_move.cpp:212-219 | 注释改对；墙闸门要么覆盖 45 m/s 并仍 0 穿透，要么明确写"上限由 max_slide_steps 定义" | open · M7 |
| DEBT-02 | P2 | `move_metrics` 用 `world.params.hash()`（只 movement）且不读 combat.toml；headless_sim 用 combined，同一份数据两个 params_hash | tools/move_metrics/main.cpp | 复用 load_world、用 combined_params_hash；输出与 `--selftest` 的 params_hash 一致 | open · 顺手 |
| AUD-30 | P2 | demo 初始状态不完整（缺 state/timers/沿），只能从 spawn 录；从局中录会第 0 tick 分叉 | demo.h DemoPlayerInit；demo.cpp:199 | 补齐初始字段并纳入 header 哈希，或 record 拒绝非 spawn 状态并报错 | open · M6 |

## 3. 桥 / Godot 表现层

| ID | 级别 | 症状 | 证据 / 位置 | 关闭判据 | 状态 / lead |
|---|---|---|---|---|---|
| AUD-16 | P1 | `get_hp()/get_armor()` 是常量 100/0，HUD 永远满血；伤害无法验证 | bridge/sim_bridge.cpp:257 | hp/armor 进 PlayerState+快照，HUD 读真实值；闸门：打 30 伤害后 HUD 显示 70 | open · M6（与 AUD-29 同批） |
| AUD-31 | P2 | 桥的 `match_request` 在 Godot 主线程同步执行（connect 2 s + 3×recv 3 s，最坏约 11 s 冻结） | net_gateway.cpp:113；matchmaking.h:102 的注释 | 信令移到工作线程/异步，或超时缩到 ≤2 s 且按钮禁用 + 显示进度 | open · M7 |
| AUD-32 | P2 | HUD 每帧重建 `Array[String]`+join，`_process` 每帧拼字符串；低端机帧时间上升 | godot/main.gd:153-174,517-533 | 脏标记（hp/armor/impulse/pos 变化才重拼）；states/pips 预生成 | open · M7 |
| AUD-35 | P2 | 主循环每帧最多 5 tick，超出部分直接丢弃：持续 <13 fps 时 sim 时钟永久落后 | godot/main.gd:453-454,496-497 | 记录并报告丢弃 tick，或固定预算追赶；闸门：10 fps 跑 10 s 后 tick 落后 < 1 tick/s | open · M7 |
| DEBT-03 | P2 | 左右 Ctrl/Shift 在 KEYCODES 里映射到同一个键，且 InputEventKey 没设 location：绑 Right Ctrl 时按 Left 也触发 | godot/main.gd:21-24；keycodes.cpp:26-28 区分左右 | 用侧别键码/event.location，或加载时拒绝侧别绑定并给出警告 | open · M7 |
| DEBT-04 | P3 | tick 秒数写死 `1.0/64` 三处：main.gd、sim_bridge.cpp、remote_sampling.h；改 tick 率会漂移 | 三处常量；tick.h 已有 tick_seconds() | 桥暴露 get_tick_seconds()，GDScript 读它；C++ 侧统一用 sim::tick_seconds() | open · 顺手 |
| DEBT-05 | P2 | movement.toml/keybinds.toml 的 warnings 收集后不打印：拼错键名静默用默认值 | sim_bridge.cpp:60-61,94-95 | 逐条 WARN_PRINT；闸门：故意写错键名，启动日志出现一条警告 | open · M6 |
| DEBT-06 | P2 | 桥每帧新建 `std::vector<PlayerSnap>` + Dictionary；`get_remote_players` 是每帧热点 | sim_bridge.cpp:181-197 | 复用成员 vector（newest_tick 未变则不重建）；测量每帧 0 分配 | open · M7 |
| DEBT-07 | P2 | 只有 template_debug DLL 与 editor/debug 键；没有 release 导出路径（导出包里 sim.* 全失效） | breakpoint.gdextension；build_bridge.ps1 | 决定"v0.1 只跑编辑器"并写进文档，或补 release 构建 + windows.release 键 + 导出验证 | open · M7（需拍板发布目标） |
| DEBT-08 | P3 | `godot/main.tscn` 的 uid 是 12 字符，其余 .uid 是 13 位：是否非法未实测 | main.tscn:1 | 在 Godot 4.5 打开工程无 invalid uid 警告，或重写 uid | open · 待核实 |

## 4. 工具 / 构建 / 可恢复性

| ID | 级别 | 症状 | 证据 / 位置 | 关闭判据 | 状态 / lead |
|---|---|---|---|---|---|
| DEBT-09 | P1 | `setup_toolchain.ps1` 只装 GCC/CMake/Ninja；Godot 与 godot-cpp 仍要手工（换机不可一键） | tools/setup_toolchain.ps1 头部注释；AGENTS §6 | 脚本一键装齐四项（或明确分开的第二步脚本），干净机器跑 smoke 绿 | open · M6 前 |
| DEBT-10 | P2 | Godot 未安装时第 17 项打 SKIP 返回 3，smoke 仍打 ALL GREEN：可以"绿着"没有桥 | smoke.ps1 的 skip 分支；godot_check.ps1 | 把 SKIP 视为非绿（exit 1 或单独 "17/17" 才算过），或 setup 保证 Godot 存在 | open · 与 DEBT-09 同批 |
| DEBT-11 | P1 | 没有 CI：全部闸门靠本机手工跑；"上次绿"无法证明当前提交 | 仓库无 .github/workflows | 一条 CI：build + smoke（headless 部分）+ check_sources 在 push 上跑 | open · v0.1 前 |
| DEBT-12 | P2 | 编译标志在两处各写一遍（CMakeLists.txt / build_bridge.ps1）：改一处忘一处就回到教训 12 | CMakeLists.txt:14；build_bridge.ps1:42 | 单一 flags 清单（脚本生成 CMake 参数，或 CMake 导出 compile_commands 校验） | open · M6 |
| DEBT-13 | P3 | check_sources 只扫 *.cpp/*.h/*.gd/*.ps1；端口规则只认 27xxx；.bat/project.godot/.gdextension 不在范围 | tools/check_sources.ps1 | 扩展扫描范围；新增一条故意违规的自测（脚本 --selftest） | open · 顺手 |
| DEBT-14 | P2 | 跨机 demo 比对缺第二台机器：`--compare` 的跨机路径从未真跑 | docs/demo-format.md；HANDOFF | 两台机器各录一份同图 demo，--dump 后 `--compare` 偏差 < 0.01 m | blocked · 需要硬件 |
| DEBT-15 | P2 | 计划书目标 Godot 4.7 Mono，实际 4.5（godot-cpp 上限）；4.7 的 API/绑定未验证 | AGENTS §4 第 4 条 | 升级到 4.7 并跑全绿，或正式把 4.5 写进计划书修订 | blocked · 等 godot-cpp 4.7 |

## 5. 文档 / 流程

| ID | 级别 | 症状 | 证据 / 位置 | 关闭判据 | 状态 / lead |
|---|---|---|---|---|---|
| DEBT-16 | P3 | `docs/网络层深化_v0.4.md` 写包头 10 B（含 flags），实现是 9 B 无 flags；两套线格式说法 | v0.4 §7 表格；netchan.cpp:7 | 文档改成"实现为 9 B，flags/位图预留"，或代码补字段（需协议 v6） | open · 顺手 |
| DEBT-17 | P2 | GAPS.md（10-19）已过期且与 AUDIT/DEBT 重叠：仍写着"A1/A2/A5 未接"等已修项 | docs/GAPS.md | 已加"已被 DEBT.md 取代"头（本轮），后续删除或只留历史注记 | open · 本轮已加头，待删 |
| DEBT-18 | P3 | PROGRESS.md 单文件 110 KB；查找靠搜索，历史段落挤在一起 | docs/PROGRESS.md | 按月拆分（PROGRESS-2026-10.md）或加索引 | open · M7 |
| DEBT-19 | P3 | 计划书 v0.3 与 v0.4 的冲突只显式裁决了 C1–C3；其余"以谁为准"散落在各处 | 网络层深化 §冲突裁决 | 列一张"v0.4 覆盖 v0.3 的完整清单"并进 AGENTS §6 | open · M7 |

## 6. 设计待定（需要拍板，不是 bug）

| ID | 议题 | 现状 | 需要决定 | 影响 |
|---|---|---|---|---|
| DESIGN-01 | impulse 充能速率 | 本轮新增 `recharge_ticks=32`（0.5 s/个），是提案 | 每 N tick 回 1 个，还是"落地立刻回 1 次 + 冷却" | 手感与 parkour 节奏 |
| DESIGN-02 | 空中 step-up / mantle | 本轮关掉（跳跃撞台沿不再自动上台阶） | 是否要做 Apex 式自动 mantle（显式状态 + 动画） | 移动手感、关闸门方式 |
| DESIGN-03 | 护甲吸收 66% / 再生 5 s/10 s | combat.toml 实现为 66% 吸收，5 s 后 10 s 回满 | 数值是否定稿（改 1% 就破 TTK 0.6–1.2 s 判据） | M6 平衡 |
| DESIGN-04 | configstring 的定位 | 真实链路靠 FULL_STATE 两个哈希拒局；可靠流只在闸门接 | 只做"规则哈希"，还是将来装资产清单 | M6 后是否接可靠流 |
| DESIGN-05 | 穿透两套模型 | .map 的 `_penetrable` 与武器 `penetration_count` 谁优先未定 | 选一套或明确组合规则 | M6 穿透/衰减 |
| DESIGN-06 | impulse 上限 | sim=4，命名方案画 6，计划书 §5.2 待定 | 定 4 还是 6（HUD 已读参数，不写死） | 手感与 HUD |
| DESIGN-07 | movement.toml 热重载 | 文档写"可重载"，实际没有 reload 路径，只改哈希 | 实现 reload（并重算 params_hash），或删掉"reloadable"字样 | 调参流程 |
| DESIGN-08 | v0.1 发布形态 | 只有编辑器 + template_debug DLL | 只跑编辑器，还是要导出 release 包（见 DEBT-07） | 发布/CI 范围 |

## 7. 功能缺口（按里程碑；不是 bug，是还没做）

| ID | 里程碑 | 缺口 | 现状 | 关闭判据 |
|---|---|---|---|---|
| FEAT-01 | M6 | 武器层：WeaponDef(toml)、开火输入、hitscan + hitbox 回溯、三段伤害、穿透/衰减、hitmarker | 只有 HP/护甲规则与 hitbox 历史 | 固定靶命中/未命中、三段倍率、TTK 0.6–1.2 s 闸门 |
| FEAT-02 | M6 | 死亡/重生/回合流程 | 无死亡状态 | 死亡后不可移动、重生回出生点、比分变化 |
| FEAT-03 | M6 | hp/armor 进快照（AUD-16） | 桥里是常量 | 打 30 伤害后两端 HUD 同步显示 70 |
| FEAT-04 | M7 | 40×40 地图 | playground 40×30 且只有 6 类测试几何 | 一张 40×40 可玩图 + geometry_hash 稳定 |
| FEAT-05 | M7 | 音频（脚步/落地/枪声/楼层低通） | 无 | 声源方位与楼层过滤可听辨；事件驱动 |
| FEAT-06 | M7 | 第三人称动画 | 胶囊 + yaw + 朝向块 | 至少 3 段动作（idle/run/air）+ 远端可见 |
| FEAT-07 | M8 | 第一人称视角模型 | 无 | 枪模随移动/后坐力摆动，不参与判定 |
| FEAT-08 | M7 | 5v5 队伍/选边/记分板 | 服务器支持 N 人，无队伍 | 两队槽位、队伍色、记分板 |
| FEAT-09 | v1.0 | 破坏（v0.4 候选 B） | 未实现 | 每图 3 面可破坏 + 时间预算 |
| FEAT-10 | v1.0 | 经济/回合/C4 拆包 | 未实现（v0.1 明确不含） | 回合流程 + C4 3.5 s + 经济结算 |
| FEAT-11 | — | 跨机 demo 比对（DEBT-14） | 缺第二台机器 | 见 DEBT-14 |

## 8. 已接受边界（不修，写明原因）

| 边界 | 原因 | 若将来要改 |
|---|---|---|
| 跨机不做 bit-exact | 同机 bit-exact + 跨机 0.01 m 容差已足够；lockstep 不是需求 | 需要重做时间轴与浮点策略 |
| 只做局域网，不做 NAT 穿透/中继 | v1.0 目标锁死 LAN；打洞要 2–3 周 | 见计划书 §7.5 |
| 分片只在闸门里启用（AUD-23） | 当前满编快照 673 B < 1100 B；先不引入真实链路复杂度 | M6 前必须接（AUD-23） |
| `ack_bits` 恒 0 | 累积 ack 足够 v0.1；位图没有消费者 | M6 接输入 ack 时一起做（DEBT-01） |
| HUD hp/armor 占位 100/0 | 等 M6 把 hp/armor 放进快照（AUD-16） | 与 FEAT-03 同批 |

## 9. 关闭记录（保留 ID，不复用）

| 日期 | ID | 怎么关的 | 证据 |
|---|---|---|---|
| 2026-10-20 | AUD-19/13/17/14/15 | 安装脚本去 7z + 锁版本；桥标志对齐 + build id；重连单次；端口单一来源；Godot 环境/检查脚本各一份 | PROGRESS S58；AUDIT §5 |
| 2026-10-20 | S59 三十余条 | 见 docs/PROGRESS.md 的 S59 §1b/§2 与 AUDIT §6.1 | 17 项闸门 + bench 全绿；godot_check 三项全过 |
| 2026-10-20 | GAPS §3 的 #7/#10/#12 | 槽位复用（add_client(-1)）、check_sources 入 smoke、README 对齐 M5 | GAPS.md 末尾 |

---

## 10. 维护约定

1. **只在这里加"已知"**：新发现的问题先加一行（哪怕还没排期），再决定修不修；不写在聊天里就算丢失。
2. **一条一个 commit**：修完把状态改 `closed` 并挪到 §9，附命令与输出；同时更新 PROGRESS 一句话。
3. **每月核对一次**：AUDIT/GAPS/HANDOFF 里出现的"未完成"必须在这里有对应行；找不到就是漏登记。
4. **不要在这里写"大概"**：症状要能被第三方复现，关闭判据要能被闸门执行。

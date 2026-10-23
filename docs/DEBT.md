# 未解决问题与技术债（唯一入口）

> 2026-10-20 建立，2026-10-23 按"账实相符制"改造：**状态只有四个值**（open / wip / blocked / closed），
> **每条 open 都必须带一条能跑的 verify 命令**。设计待定与功能缺口搬到了 `docs/BACKLOG.md`——
> 那两类的"关闭"不是"修好"，混在一张表里会让 open 失去意义。
>
> 规则：新发现的问题**先在这里加一行**，再决定修不修；关闭 = 改状态 + 写证据（命令与关键输出），同一个 commit。

## 0. 使用方式

- **级别**：P0 会算错/崩/泄漏；P1 会误判、卡住、不同步、可被利用；P2 死代码/性能/可维护性；P3 文档与口味。
- **状态**：`open` 未动；`wip` 正在做（同时只能一条）；`blocked` 卡在外部条件；`closed` 已修（移进 §6）。
- **lead**：M6 / M7 / 顺手，只表示排期，不是状态。状态与排期分两列，是为了能统计。
- **verify**：一条命令 + 期望输出。**没有 verify 的条目不许进 open**（想法去 BACKLOG）。

**当前计数**：未解决 **31** 条（P1 3 / P2 20 / P3 8）；关闭记录 **7** 条；设计待定与功能缺口见 BACKLOG。
改表就要改这个数字——`tools/doc_check.py` 会核对它和实际行数。

---

## 1. 网络（sim/net、bridge 的联机部分）

| ID | 级别 | 症状 | 证据 / 位置 | verify | status | lead |
|---|---|---|---|---|---|---|
| AUD-21 | P2 | 序列化/接收热路径每 tick 约 4-8 次堆分配；`[netalloc]` 只统计 session 三步，所以永远 0 | session.cpp encode/wrap/recv | `headless_sim --test-net` 输出 `[netalloc] per_tick 0` 且统计覆盖 encode/decode | open | M6 |
| AUD-22 | P2 | 可靠流 `seq <= ack` / `seq > m_next` 不是 u16 回绕安全的；第 65536 个包后静默死 | reliable.cpp:67,99 | 推 70000 个 rel 包仍完整交付（新闸门） | open | M6 |
| AUD-24 | P2 | admission 的 approve 队列无 TTL/上限：走过 TCP 但不发 HELLO 的 id 永久留在 deque | admission.h:17-29 | 过期（120 s 或容量上限）后 `admission.pending()==0` | open | M6 |
| AUD-25 | P3 | `ReliableTransport` 的 now_ms 只在 advance 更新，recv 在 advance 之前时 RTT 采样少一 tick | reliable_transport.cpp:46,56-60 | loopback RTT 采样等于 1-2 tick | open | 顺手 |
| DEBT-01 | P2 | 包头 `ack_bits` 恒 0、`Snapshot::acked_cmd` 解出无人读：第二套 ack 的残骸 | netchan.h；snapshot.cpp | 要么删字段（需再升协议）要么真填，二者都要闸门 | open | M6 |
| DEBT-20 | P2 | 真实链路的可靠流没接：bridge 只 `set_configstring`，从不 `add_client(..., rel)`，所以 configstring 从没真正下发（docs 曾写"已走可靠流"，本轮改正） | net_gateway.cpp:70,186 | 真实链路 `[config] chunks>=1` 且 `configstring 已下发` 出现在 host 日志 | open | M6 |

## 2. 模拟 / 移动（sim/move、collision、data）

| ID | 级别 | 症状 | 证据 / 位置 | verify | status | lead |
|---|---|---|---|---|---|---|
| AUD-28 | P2 | 仍有一批影响手感的常量是字面量：地面探测 0.02、墙探测 -0.05/+0.15、滑铲结束 0.5×、攀爬接受 0.05、墙跑内推 0.2、翻越前进 0.55、kClipEpsilon 1e-3、可行走法线 0.7（腿倍率已在 S60 挪进 toml） | pmove.cpp:9-11,137,242,383,389；trace.cpp:11,235 | `grep` 这些字面量只出现在 config.h 默认值 | open | M6 |
| AUD-33 | P2 | trace 注释与闸门仍把 step_len 当"防穿墙"依据；真正防穿是连续扫掠，>38.4 m/s 时 step_len 会超墙厚 | trace.h:43-47；tests_move.cpp:212-219 | 注释改对，或闸门覆盖 45 m/s 仍 0 穿透 | open | M7 |
| DEBT-02 | P2 | `move_metrics` 用 `world.params.hash()`（只 movement）且不读 combat.toml，与 headless_sim 的 combined hash 不一致 | tools/move_metrics/main.cpp | 输出 hash 与 `--selftest` 的 params_hash 相同 | open | 顺手 |
| AUD-30 | P2 | demo 初始状态不完整（缺 state/timers/沿），只能从 spawn 录；从局中录第 0 tick 分叉 | demo.h DemoPlayerInit | 补齐初始字段或 record 拒绝非 spawn 状态 | open | M6 |

## 3. 桥 / Godot 表现层

| ID | 级别 | 症状 | 证据 / 位置 | verify | status | lead |
|---|---|---|---|---|---|---|
| AUD-16 | P1 | hp/armor 已进 PlayerState 与快照（v6 kFieldVitals），但还没有任何东西造成伤害，所以 HUD 永远满血 | bridge/sim_bridge.cpp get_hp | `godot_check.ps1` 打 30 伤害后 HUD 显示 70 | open | M6 |
| AUD-31 | P2 | 桥的 `match_request` 在 Godot 主线程同步执行（最坏约 11 s 冻结） | net_gateway.cpp join_impl | 信令移到工作线程，或超时 ≤2 s 且按钮禁用 | open | M7 |
| AUD-32 | P2 | HUD 每帧重建 `Array[String]`+join；低端机帧时间上升 | godot/main.gd _update_hud | 脏标记：hp/armor/impulse/pos 不变则不重拼 | open | M7 |
| AUD-35 | P2 | 主循环每帧最多 5 tick：持续 <13 fps 时丢弃的 tick 现在计数并打印（S60），但没有独立闸门 | godot/main.gd | 10 fps 跑 10 s 后 tick 落后 < 1 tick/s | open | M7 |
| DEBT-03 | P2 | 左右 Ctrl/Shift 在 KEYCODES 里映射到同一个键，且 InputEventKey 没设 location | godot/main.gd KEYCODES | 用侧别键码，或加载时拒绝侧别绑定 | open | M7 |
| DEBT-04 | P3 | tick 秒数写死 `1.0/64` 三处：main.gd、sim_bridge.cpp、remote_sampling.h | 三处常量 | 桥暴露 get_tick_seconds()，C++ 统一用 sim::tick_seconds() | open | 顺手 |
| DEBT-05 | P2 | movement.toml/keybinds.toml 的 warnings 收集后不打印：拼错键名静默用默认值 | sim_bridge.cpp:98-101 | 故意写错键名，启动日志出现一条警告 | open | M6 |
| DEBT-06 | P2 | 桥每帧新建 `std::vector<PlayerSnap>` + Dictionary | sim_bridge.cpp get_remote_players | 复用成员 vector；测量每帧 0 分配 | open | M7 |
| DEBT-07 | P2 | 只有 template_debug DLL 与 editor/debug 键；没有 release 导出路径 | breakpoint.gdextension | 决定"v0.1 只跑编辑器"，或补 release 构建 + 导出验证 | open | M7 |
| DEBT-08 | P3 | `godot/main.tscn` 的 uid 是 12 字符，其余 .uid 是 13 位 | main.tscn:1 | 打开工程无 invalid uid 警告，或重写 uid | open | 待核实 |

## 4. 工具 / 构建 / 可恢复性

| ID | 级别 | 症状 | 证据 / 位置 | verify | status | lead |
|---|---|---|---|---|---|---|
| DEBT-09 | P1 | `setup_toolchain.ps1` 只装 GCC/CMake/Ninja；Godot、godot-cpp 仍要手工，而 smoke 第 19 项还需要 `python`（doc_check） | tools/setup_toolchain.ps1 头部；smoke.ps1 的 docs 步 | 干净机器一条命令后 smoke 全绿（含 python 与 Godot） | open | M6 |
| DEBT-10 | P2 | Godot 未安装时第 19 项 SKIP 返回 3，smoke 仍打 ALL GREEN | smoke.ps1 的 skip 分支 | SKIP 视为非绿，或 setup 保证 Godot 存在 | open | 与 DEBT-09 同批 |
| DEBT-11 | P1 | 没有 CI：全部闸门靠本机手工跑 | 仓库无 .github/workflows | push 上跑 build + smoke（headless 部分） | open | v0.1 前 |
| DEBT-12 | P2 | 编译标志在两处各写一遍（CMakeLists.txt / build_bridge.ps1） | CMakeLists.txt:14；build_bridge.ps1 | 单一 flags 清单 | open | M6 |
| DEBT-13 | P3 | check_sources 只扫 *.cpp/*.h/*.gd/*.ps1；.bat/project.godot/.gdextension 不在范围 | tools/check_sources.ps1 | 扩展扫描范围 + 一条故意违规的自测 | open | 顺手 |
| DEBT-14 | P2 | 跨机 demo 比对缺第二台机器：`--compare` 的跨机路径从未真跑 | docs/demo-format.md | 两台机器各录一份，偏差 < 0.01 m | blocked | 需要硬件 |
| DEBT-15 | P2 | 计划书目标 Godot 4.7 Mono，实际 4.5（godot-cpp 上限） | AGENTS §4 | 升级到 4.7 跑全绿，或修订计划书 | blocked | 等 godot-cpp |

## 5. 文档 / 流程

| ID | 级别 | 症状 | 证据 / 位置 | verify | status | lead |
|---|---|---|---|---|---|---|
| DEBT-16 | P3 | `网络层深化_v0.4.md` 写包头 10 B（含 flags），实现是 9 B 无 flags | v0.4 §7；netchan.cpp | 文档改成"实现为 9 B，flags 预留" | open | 顺手 |
| DEBT-18 | P3 | PROGRESS.md 单文件 113 KB；查找靠搜索 | docs/PROGRESS.md | 按月拆分并留索引 | open | M7 |
| DEBT-19 | P3 | 计划书 v0.3 与 v0.4 的冲突只显式裁决了 C1–C3 | 网络层深化 §冲突裁决 | 列一张"v0.4 覆盖 v0.3"的清单 | open | M7 |
| DEBT-21 | P3 | `NetClient::catch_up` 在一次 `receive()` 里连跑最大约 300 tick（4.7 s 冻结）；更长的冻结会想跑几千 tick，理论上能吃掉一帧预算 | session.cpp catch_up | `--bench` 不覆盖它；补一条"冻结 5 s 后 catch_up 耗时 < 4 ms"的测量 | open | M7 |

## 6. 关闭记录（保留 ID，不复用）

| 日期 | ID | 怎么关的 | 证据 |
|---|---|---|---|
| 2026-10-20 | AUD-19/13/17/14/15 | 安装脚本去 7z + 锁版本；桥标志对齐 + build id；重连单次；端口单一来源；Godot 环境/检查脚本各一份 | PROGRESS S58；AUDIT §5 |
| 2026-10-20 | S59 三十余条 | 见 docs/PROGRESS.md 的 S59 §1b/§2 与 docs/audits/2026-10-M5.md §6.1 | 17 项闸门 + bench 全绿 |
| 2026-10-23 | AUD-20 | 客户端落后补 tick：`catch_up()` 在快照领先 4 tick 以上时重锚时间轴；main.gd 不再把累加器清零 | `[catchup] freeze 300 behind 293 catchups 1 accepted 196->203->259 late 0->0` |
| 2026-10-23 | AUD-23 | 分片接进真实链路：hub 每个 peer 一个 FragmentedTransport，bridge/net_loop 客户端同 | `[frag] real-link 10p 833 B ten 1 decode 1 nofrag 1 3000B 1 split 1 small 1` |
| 2026-10-23 | AUD-26 | UDP 源地址校验：`connect_peer` 规范化地址，`recv` 不匹配即丢并计数 | `[udpsrc] rejected 1 injection-drop 1 legit-after 1` |
| 2026-10-23 | AUD-27 | 槽位释放：`NetServer::remove_client` 上报 client_id，网关 `MatchHost::release` | `[slots] first 1 full 1 reported 1 freed 1 cycles 3/1 anonymous 1` |
| 2026-10-23 | DEBT-17 | GAPS.md 归档到 docs/archive/，主目录不再有会误导的过期文档 | `docs/archive/GAPS.md` 存在；doc_check 路径检查 0 失败 |

## 7. 已接受边界（不修，写明原因）

| 边界 | 原因 | 若将来要改 |
|---|---|---|
| 跨机不做 bit-exact | 同机 bit-exact + 跨机 0.01 m 容差已足够 | 需要重做时间轴与浮点策略 |
| 只做局域网，不做 NAT 穿透/中继 | v1.0 目标锁死 LAN | 见计划书 §7.5 |
| `ack_bits` 恒 0 | 累积 ack 足够 v0.1 | 与 DEBT-01 同批 |
| 伤害不在客户端预测（A4） | 预测伤害会导致"打中了又没了" | 需要服务器结算 + 客户端校正表现 |
| 快照不做压缩 | 10 人满编 833 B，余量够；加密/压缩等真有需求再说 | 见 documents/budgets |

---

## 8. 维护约定

1. **只在这里加"已知"**：新问题先加一行（哪怕还没排期）。写在聊天里就算丢失。
2. **一条一个 commit**：修完把状态改 `closed` 并挪到 §6，附命令与输出。
3. **每月核对**：docs/AUDIT.md 与 docs/PROGRESS.md 里出现的"未完成"必须在这里有对应行。
4. **不要写"大概"**：症状要能被第三方复现，verify 要能被闸门执行。
5. `tools/doc_check.py` 每次 smoke 都核对：计数守恒、状态枚举、open 行必须有 verify。

# PROGRESS — Project Breakpoint

> 进度、日志、交接。**不设上限**，但每节只留「做了什么 / 结论 / 下一步」。
> 约束在 `docs/AGENTS.md`。

---


> **S01–S59 的短条目已归档**：`docs/archive/PROGRESS_S01-S59.md`（2026-10-18 ~ 10-23，一字未改）。
> 详细开发日志仍在文件末尾；这里只保留 S60 之后的条目。

# 开发日志（详细）

> 从 2026-10-20 起，在 PROGRESS 末尾追加详细日志；上面的 S 编号短条目保留，作为索引。
> 每篇固定六段：目标 / 做了什么 / 为什么这样做 / 证据 / 失败与修正 / 债与下一步。
> 详细日志是给“三个月后的自己”看的：要能复现判断，而不只是知道结论。

## 2026-10-20（凌晨）· 把 M5 收成可交付基线

### 0. 目标与起点

这一晚的目标不是加新玩法，而是把已经能跑的东西变成“丢了也能恢复、别人也能接手”的基线。
起点是 S28 全量复查之后：M0–M4 全绿；M5 的不可靠通道、delta、预测回滚、FULL_STATE、
远端插值、真实 UDP、两进程、Godot host/connect 都已通；A1/A2/A5 有独立实现和闸门，
但没有接进组局；仓库只在 G:\自制游戏 一块盘上，没有远端。

### 1. 先存档，再动代码

判断：不等 v0.1。v0.1 还要几周，仓库只有约 2 MB，一次磁盘故障就会丢掉全部历史。
做法是 3-2-1 的简化版：

1. 版本历史：已有的 commit；打 checkpoint-2026-10-19 tag。
2. 本地离线副本：git bundle --all 生成单文件，放到仓库外的 G:\BreakpointBackups。
   第一版 314 KB，git bundle verify 通过；它自带全部 refs，换台机器直接 clone。
3. 异地副本：GitHub 私有仓库。

同时写了 tools/backup.ps1（时间戳 + 自动 verify）和 docs/backup.md（步骤、恢复、节奏）。
这一步的价值不在“多了一个文件”，而在于把“怎么恢复”写成了可执行步骤。

### 2. GitHub：连接问题和历史问题各翻一次车

第一次是连接。git ls-remote 报 Failed to connect to github.com port 443 via 127.0.0.1。
查下来 git 全局 http.proxy 指向 127.0.0.1:7897，但端口没有监听；进程里只有
clash-verge-service（后台服务），主程序没启动。直连 443 的 TCP 能建立，但 TLS 阶段被 reset。

绕法是 SSH over 443：ssh.github.com:443，本机 SSH key 已在 GitHub。探针返回
Hi Orlean-Crispy! You have successfully authenticated。remote 设为
ssh://git@ssh.github.com:443/Orlean-Crispy/unknow.git，SSH 不经过 HTTP 代理。

第二次是历史。首次 push 被拒：远端有 Initial commit（LICENSE + README），与本地
30 多个 commit 没有共同祖先。先试 git rebase origin/main，在第一个 M1 commit 就
AA README.md 冲突——本地也有一份 README。没有硬解，直接 git rebase --abort，
改用 git merge --allow-unrelated-histories；README 保留本项目版本（远端只有一行
# unknow），LICENSE 一并合入。合并提交 59fa04a，之后 push 成功。

这里的选择值得记录：rebase 适合“我的提交叠在别人后面”，不适合“两条独立历史的第一次握手”。
第一次合并用 merge 更诚实，也更容易解释。SSH 443 到 GitHub 仍会间歇 reset，所以 push 写成
“失败重试 2–3 次”的循环，而不是假设一次成功。

### 3. 构建基建：把“两份清单”合成一份

S28 审计把“CMake 与 build_bridge.ps1 各维护一份源文件清单”列为第一技术债。
它不是洁癖：之前桥接构建就因为漏了 interp.cpp / hitbox.cpp 直接链接失败。

做法：新增 tools/sim_sources.txt（26 个 sim .cpp 的唯一清单）；CMake 用 file(STRINGS)
读它，add_library(sim STATIC ${SIM_SOURCES})；build_bridge.ps1 读同一份文件，删掉手写数组；
新增 tools/check_sources.ps1 比对清单与 git ls-files，并接到 smoke.ps1 第一项。
以后新增 .cpp 忘了登记，闸门直接红。

代价是 bridge 现在会编译 demo/combat/reliable/ticket 等暂时没调用的文件，DLL 略大；
换来的是“结构上不可能再漏文件”。这个交换在 v0.1 之前是划算的。

### 4. 联机健壮性：幽灵、掉线、槽位

这一节是今晚唯一改到网络行为的部分，三个问题串在一起：

4.1 幽灵远端。服务器为所有预留槽位都发快照，host 在客户端加入前就渲染出 slot 1 的胶囊。
修法：PlayerState.active → 快照 flags bit2；服务器 init 全 false、add_client true、
remove_client false；桥接层跳过未激活槽位。修复后 host 日志顺序从 remote slot 1 先出现，
变成 peers 2 之后才出现。

4.2 掉线不释放。客户端静默后链路一直留在 m_links，槽位永远 active。修法：每条 Link
记录 last_rx_tick（收到任何可解包的服务器 tick 都刷新），step() 开头扫描，连续 320 tick
（5 秒）没有包就 remove_client。闸门 [timeout] joined 1 dropped 1。

4.3 槽位只增不减。这是 4.2 暴露出来的真 bug：超时释放了 slot 1，但 bridge 的 m_next_slot
和 net_loop 的 next_slot 只 ++，新客户端拿 slot 2 → 人满，重连永远进不来。修法：
add_client(..., -1) 选第一个 inactive 槽位；bridge/net_loop 不再维护计数器；满员返回 -1。
闸门扩展为 [timeout] joined 1 dropped 1 reuse 1。

这三条合起来才闭环：掉线要释放、释放的槽要能复用、复用后要能重新激活。
只做前两条，重连仍然进不来；只做第三条，幽灵仍然在。

### 5. 协议件的验收（已有，但今晚重新跑过）

| 件 | 输出 | 说明 |
|---|---|---|
| A1 信令 | [signal] hello 1 ack 1 manifest 1 ready 1 match 1 full 1 | 真 TCP 环回、六条消息逐字段 |
| A2 可靠流 | [reliable] chunks 10 retrans 6 complete 1 same 1 corrupt 1 dead 0 | 5000 B / 30% 丢包 |
| A5 票据 | [ticket] valid 1 expired 1 tamper 1 wrongkey 1 | HMAC-SHA256 截断 12 B |
| 时钟 | [clock] skips 3 far 0 dev 0.00000 m | 强制漂移 6 tick |
| 双客户端 | [udp session] joined 1/1 snapshots 148/148 dev 0.00000 m | 真 UDP + hub |
| 掉线/复用 | [timeout] joined 1 dropped 1 reuse 1 | 5 s 超时 + 槽位复用 |

bench（10 玩家）：mean 2.04 µs、p99 3.40 µs、max 4.30 µs、0 allocs/tick；单玩家 p99 0.40 µs。
预算 4000 µs，余量三个数量级。

### 6. 失败与修正（不写结论，写判断）

1. 把“连接失败”误认成“认证失败”：ls-remote 报错先看 via 127.0.0.1，那是代理，不是凭据。
   先查代理端口和进程，再谈 token。
2. rebase 处理无关历史：第一次合并用 rebase，冲突在 README；正确做法是 abort + merge。
   教训：历史关系不清楚时，merge 比 rebase 安全。
3. A2 组装器“空槽也算完成”：resize(total) 会补空槽，size()==total 恒真；改成每块非空才算完成。
   又一次证明“看起来完成”比“报错”更危险。
4. 票据到期边界：expiry < now 让到期那一秒仍有效；改 now >= expiry。
   时间边界要写成“有效区间”，不要写“过期条件”再取反。
5. 输入窗口（更早）：客户端领先 7 tick，窗口 8 / 队列 4，丢的正是下一 tick 要执行的命令；
   进程内测试因为两条循环同步而完全没发现。两进程测试是必要手段。

### 7. v0.1 缺口（原 GAPS 文档，2026-10-23 归档到 docs/archive/GAPS.md）

P0：射击（无武器/hitscan/开火输入/准星）、伤害联网（快照无 hp/armor）、死亡与重生、
开火键位、40×40 地图、A1/A2/A5 接入组局。P1：局内/断线 UI、5v5 槽位、音频、第三人称动画。
P2：第一人称枪模、跨机 demo 比对、Godot 4.7。

建议顺序：射击最小闭环 → A1/A2/A5 接入 → 地图/UI/音频。

### 8. 债与下一步

今晚清掉：源清单双份、幽灵远端、掉线不释放、槽位不复用、check_sources 手动化。
仍欠：PROGRESS S21/S20 顺序、InputWindow::Overflow 死分支、signaling.cpp 与 udp_socket.cpp
重复 Winsock 初始化、A2 单 socket 双队列、A1 信令线程、DLL dead-strip、远端插值 frac、
10 分钟联机 soak。

下一步不是继续造协议，而是做 M6 射击最小闭环：WeaponDef(toml) → 开火输入 →
hitscan（复用 hitbox 历史）→ 三段伤害 → hp/armor 进快照 → 死亡/重生。
判据先写：固定靶命中/未命中、三段倍率、TTK 0.6–1.2 s。

### 9. 当前基线（S31 当天）

- main = 732a8c6（已推 GitHub）；tag checkpoint-2026-10-19 指向合并提交 59fa04a。
- 远端：ssh://git@ssh.github.com:443/Orlean-Crispy/unknow.git。
- bundle：G:\BreakpointBackups\breakpoint-20261019.bundle（已 verify）。
- 16 项闸门 + bench ALL GREEN；工作树干净。
- 下一件事：开 sim/game/weapon.* 与开火输入，先写闸门再接表现层。
---

## 2026-10-20（下午）· 命名方案落地（S32）

### 0. 目标

不是加玩法，是把「这个名字」从一份桌面文档变成仓库里的事实。三件事必须一起做，
否则名字只落在 README 上：**文档（谁说了算）/ 玩家可见（窗口标题）/ 代码前缀（bp）**。

### 1. 做了什么

| 动作 | 结果 |
|---|---|
| 命名方案入库 | docs/IMPULSE_命名方案.md，675 行，与桌面原件 SHA-256 一致 |
| ADR 0005 | 三层命名 + 术语分层 + **拒绝**全局重命名 + 三条未决 |
| 文档地图 | 根 AGENTS §1.1/§4/§6、docs/AGENTS §2/§4/§7、README 标题与链接 |
| 玩家可见 | project.godot `config/name="IMPULSE"`、LAN 菜单 `IMPULSE - LAN` |
| 新未结案 | docs/AGENTS §4 第 9 条：impulse 上限三处不一致 |

### 2. 为什么这么做

**① 为什么不全量重命名 `sim::` → `bp::`。**
命名空间划的是模块边界。`sim::` 回答「这一层是什么」，`bp::` 只回答「这是我的」；
桥接层本来就不属于 sim，压进 `bp::` 会把唯一一条真边界抹平。再加上 26 个 .cpp 的机械 diff、
零行为收益——那不是统一品牌，是把 git 历史搅浑。品牌统一交给前缀（BP_* 宏、bp 分支、bp-toolchain）。

**② 为什么先把 4 vs 6 记成未结案，而不是就地改成 6。**
计划书 §5.2 至今写着「待定」；sim 的 `max_impulses=4` 是当时的占位；命名方案画 6 格是
Red Eclipse 的参考值。三个数字谁都没被验收过——手感验收（M3）才有资格拍板。
现在改成 6 会让「文档一致」掩盖「数值没验」；记成未结案、并要求 HUD 从参数读，才是诚实的做法。

**③ 为什么文档先行、代码只动玩家可见的两处。**
命名是低频、高影响的决定；代码里的标识符是高频、低影响的载体。先把「谁说了算」写死（ADR），
后面的改动才有依据，否则下次又会出现「桌面一份、仓库一份」。

### 3. 证据

- 文档：根 AGENTS §1.1 新增对外名/代号一段；§4 新增「命名」硬约束；§6 文档地图新增一行。
- docs/AGENTS：§2 新增「命名（项目）」行（模块命名空间保持 sim::）；§4 新增第 9 条未结案；§7 文件地图更新。
- 玩家可见：Godot 窗口标题与 LAN 菜单标题已改；`--headless --quit` 无脚本错误。
- 基线：build 成功，16 项行为闸门 + bench **ALL GREEN**（p99 0.40 µs，0 分配）。

### 4. 失败与修正

1. 写 ADR 时在模板字符串里转义反引号，落盘成了 `\``（反斜杠 + 反引号）。
   症状是 Markdown 里所有行内代码都带一个反斜杠——肉眼扫一遍才发现。
   教训：**生成含反引号的 Markdown，写完必须读回来比对**，不能相信「写成功了」。
2. 一开始想把 `docs/AGENTS.md` 的未结案写成「M6 前解决」，写完发现它和 M3 手感验收绑定，
   改成 lead：M3。lead 写错等于没写。

### 5. 债与下一步

- **已定（用户拍板）**：不做全局重命名，项目一直用内部代号开发；仓库也保持 `unknow`。
  ADR 0005 已把这条从「未决」改成「结案」：名字不影响正确性，剩下的唯一目标是没有 bug。
- 欠：impulse 上限定值（M3）；
  构建产物名保持工具名（ADR 已记录理由，不再当债）。
- 名字的下一步不是继续改文档，而是 M5 收尾后进 M6；HUD 做资源条时按 ADR 读参数、画 J 图标。


## 2026-10-20（下午–晚间）· M5 收尾 + 两轮代码体检（S39–S57 完整记录）

### 0. 这一段的起点与目标

起点：M5 的主干（不可靠 UDP / delta / 预测回滚 / FULL_STATE / 远端插值 / 事件通道 / 两进程）都已通，
但 A1 信令、A2 可靠流、A5 票据还只是"协议件"，没有接进组局；局内没有 HUD；代码里积了 26 项
可疑点（后来两轮体检实际找出 45 条）。

目标按计划书顺序：**先把 M5 收口成可交付的基线，再做全量体检**，每一步都要「闸门先绿再提交」。

### 1. M5 收尾：从「协议件」到「能两个人开局」

| 步 | 做了什么 | 判据 |
|---|---|---|
| A1 组局 | `MatchHost` 自起 TCP 线程，握手 HELLO→ACK→MANIFEST→READY→MATCH_START，签发 32 B 票据 | `[match] accepted 1 slot 0 ticket 1 resume 1 tamper 1 full 1` |
| 信令进 UDP | HELLO 从 6 B 变 22 B，带 16 B client_id；槽位来自信令而非到达顺序 | 两进程 net_selftest joins=2 |
| 桥接 | host 起 MatchHost + NetServer（自占 slot 0），join 先 match_request 再连 UDP | host 见 `peer joined slot 1`，client 见 `joined slot 1` |
| A2 共 socket | `ReliableTransport(tx,rx)` 打标 demux，configstring 走可靠流 | `[dual] 8/8 same 1`、`[config] matched 1` |
| A5 重连 | 同 id + 票据 resume 回原槽；同槽 link 顶替而非新增 | `[reconnect] first 0 links 1 active 1`、客户端 `reconnect ok` |
| 局内 UI | HUD（HP/ARMOR/IMP 点阵/速度/坐标/事件）、Esc 暂停菜单、失联提示 | `--hud-check` 与两进程联机 |
| 失联检测 | `NetClient::snapshot_age()` > 1 s 显示 SERVER SILENT | 杀房主后 `server silent age=65` |

踩过的两个真坑：

1. **端口劫持**：27015/tcp 被 AppleMobileDeviceService 占着，而 Windows 的 `SO_REUSEADDR` 允许
   第二个 socket 绑上已被 listen 的端口——`bind` 假成功，客户端连到别人的服务，报 `receive failed`。
   修法：Windows 改 `SO_EXCLUSIVEADDRUSE`，默认信令端口换 27115。案例 docs/pitfalls/port-hijack.md。
2. **一个 RTO 计时器被两个时钟推进**：闸门和 `NetServer::send()` 各自推进同一个 wrapper，
   时间倒退后 `uint32` 回绕，全部包被判超时。修法：advance 取 max + 只由会话推进。

### 2. 第一轮体检（A1–A7，结构）

| 项 | 内容 | 结果 |
|---|---|---|
| A1 | `InputWindow::Overflow` 恒假（窗口 16 = 深度 16 + 位图去重） | 删 |
| A2 | `AckTracker` 全仓零调用 | 删 |
| A3 | Winsock 初始化写了两份 | 抽 `sim/net/winsock.h` |
| A4 | 闸门文件 2916 行 / 34 个 `run_*` | 拆成 tests_net/tests_move/tests_demo + test_util，main 272 行 |
| A5 | 桥 519 行塞了网络、渲染、输入、HUD | mesh_common.h、mesh_builder.*、net_gateway.*；SimWorld 478→313 |
| A6 | configstring 只是任意 blob | 34 B 格式（version + params/geometry hash），失配拒局 |
| A7 | 加入逻辑两份（net_loop 与桥） | 抽 `sim/net/admission.h` |

### 3. 第二轮体检（AUD 19 条 + R 19 条）

两个只读代理分审 sim 与 bridge/胶水，合并 38 条，**0 个 P0**。修复分四批：

- **批一（UI）**：HUD 文本同帧被覆盖（HP/IMP 从未显示）、ESC 在 LAN 菜单上叠加暂停菜单导致鼠标捕获、
  删掉不可达的 `link_lost` 与未用常量/字段。
- **批二（P1 sim）**：R3 序号回绕（`Link::sent` 改按发送序 deque，闸门 `[seqwrap] ok`）、
  R5 UDP 端口独占（`[udp] exclusive 1`）、R4 槽位释放（`[match] release 1`）、
  R2 翻越/爬墙不能同 tick 双花 impulse（`[parkour] vault_climb_single 1`）、
  R9 哈希补齐（`state_fields 8/8`、快照协议 3→4）、R7 六个假参数（删两个、读两个、实现受伤减速）、
  R8 combat 哈希并入（`[config] refused 1 combat 1`）。
- **批三（P2 sim）**：R10–R19——死分支、恒真判据（R11 `max_step` 只打印、R12 断言入参 tick）、
  TTK 重算伤害、64 Hz 硬编码、未用字段/API、索引校验、u8path、两个热路径 map 换定长数组。
- **批四（P1 sim）**：R1 回滚改写该 tick 的 history/hitbox（`[rollback] hitbox 1 dup 1 state 1`）、
  R6 网络热路径零分配（`[netalloc] per_tick 0`，仅限 session 三步，序列化仍分配，已写明边界）。
- **批五（桥）**：AUD-01 联机事件（`[netevents] 1`）、AUD-02 暂停不停网泵、AUD-05 远端采样改快照轴、
  AUD-06/08/10/12。

### 4. 证据（最终基线）

- 16 项行为闸门 + bench：**ALL GREEN**；p99 0.40 µs，0.00 allocs/tick；
- 两进程 LAN：`joins=2 accepted=414 far=0 moved=46.69`；
- 协议版本：CMake / build_bridge / version.h 全为 **4**，文档无残留 v3；
- 工作树干净，无 stash，bundle 已刷新。

### 5. 失败与修正（这一段最值钱）

1. **「验证」不等于「验收」**：我用 `--hud-check` 打印 getter 就宣布 HUD 完成，实际
   渲染路径被同帧覆盖，HP 从没显示过。判据必须盯**用户看到的东西**。
2. **恒真判据比没有判据更危险**：R11/R12 的检查永远不会失败，却让人以为有保护。修判据本身是一次 bug fix。
3. **按行号搬代码，结构行会漂移**：前两次拆分让 `}  // namespace` 行号变了，第三次按旧坐标搬，
   把 `main` 搬进了匿名 namespace。每次都要重新确认结构行。
4. **文件操作别用一行流**：PowerShell 一行流写文件静默失败（什么都没写），换 Node 的 fs 一次成功。
5. **代理会死在半路**：两次执行代理因余额中断，留下已提交和未提交的改动。处理方式固定为：
   `git status` → 逐条复核 → 重跑闸门 → 收下能绿的、回滚不能的；不重做、不盲信自述。

### 6. 明确未完成（不许含糊）

- AUD-13 桥编译不带 `-Werror`/`BP_BUILD_ID`（godot-cpp 头自带告警，需范围隔离）；
- AUD-14 端口字面量四处；AUD-15 脚本公共块五份；
- AUD-17 重连按钮双击保护；AUD-19 setup 依赖 7z、Ninja 未锁版本；
- AUD-16 留到 M6（hp/护甲未进快照）；
- R6 只覆盖 session 三步，ByteWriter/编解码/回环队列仍约 40 次/tick 分配——需单独立项。

### 7. 下一步

先清 AUD-19（换机即炸）与 AUD-13（告警盲目），再按计划书进 **M6 射击**：
WeaponDef(toml) → 开火输入 → hitscan + hitbox 回溯 → 三段伤害 → hp/armor 进快照 → 死亡/重生 → 穿透/衰减。

---

## 2026-10-20（傍晚）· 清债：AUD-19/13/17/14/15（S58）

### 0. 目标

五条 P2 债，按「炸得最响」排序，一次清完：**换机即炸的安装脚本**（AUD-19）→
**桥的编译标志不一致**（AUD-13）→ **重连双击**（AUD-17）→ **端口四处写死**（AUD-14）→
**脚本公共块复制**（AUD-15）。不加新功能。验收标准不是「改了」，而是「换台机器能装、
桥坏了闸门会红、端口改了只改一处」。

### 1. 做了什么

| 编号 | 症状（改之前） | 改法 | 判据 |
|---|---|---|---|
| AUD-19 | setup 调 `7z`（本机没装）；ninja 取 `releases/latest` | `Expand-Archive`；三个工具全锁确切版本；ZIP magic 校验；目录统一 `mingw64/ cmake/ ninja/`；老 `cmake-4.4.4-windows-x86_64/` 改名复用 | 本机 adopt 成功并打印 g++ 16.2.0 / cmake 4.4.4 / ninja 1.13.2；真实 zip 与假 zip 各跑一次 |
| AUD-13 | 桥缺 `-Werror`/`-ffp-contract=off`/`-DNDEBUG`/BP_BUILD_ID | 标志与 CMake 逐条对齐；godot-cpp 用 `-isystem`；build id 来自 `git rev-parse`；版本号收进 version.h | 桥零告警；Godot 打 `build <hash>` 且 hash == HEAD |
| AUD-17 | 双击重连叠两次 join；`joined_printed` 不复位 | `_do_reconnect()` + 成功上锁；复位三个一次性日志 | `reconnect double=0`；重连后又打 `joined slot 1` |
| AUD-14 | 27115/27016/27130/27116 四处，游戏端口一处 `+1` 一处写死 | `sim/net/ports.h` 唯一定义；桥暴露 `get_default_signal_port()`；lint 禁止别处出现 | `--hud-check` 打 `port=27115`；两个 Godot 进程不带 `--port` 也能连上 |
| AUD-15 | Godot 路径与首跑导入复制多份 | `tools/godot_env.ps1`（唯一知道路径与导入的地方）+ `tools/godot_check.ps1`（第 17 项闸门） | smoke 第 17 项 PASS；没装 Godot 时 SKIP 而不是假绿 |

提交：`adbaf84`（AUD-13）→ `efacb11`（AUD-19）→ `b3a37f5`（AUD-17）→ `13b88e2`（AUD-14）→
`08334fd`（AUD-15）。一次一条债，互相不缠绕。

### 2. 顺手挖出来的两个真问题（这一段最值钱）

**① 扩展从来就没加载过。** 做 AUD-13 时我加了 `get_build_id()`，第一次跑 `--hud-check`
准备「看它打印 build id」，结果什么都没有。真实症状链是：

```
ERROR: No GDExtension library found for current OS and architecture (windows.x86_64)
  at: parse_gdextension_file (core/extension/gdextension_library_loader.cpp:360)
ERROR: GDExtension dynamic library not found: 'res://breakpoint.gdextension'
SCRIPT ERROR: Parse Error: Identifier "SimWorld" not declared in the current scope
```

DLL 就在 `godot/bin/` 里，导出表里 `breakpoint_library_init` 也在，但
`breakpoint.gdextension` 只写了 `windows.debug.x86_64`，而我们用的是 **release 编辑器二进制**
跑 headless——它只认 `windows.editor.x86_64`。报错里的 `(windows.x86_64)` 是 os.arch，
不是被查找的键，所以第一眼会往「文件不存在」上查，越查越远。修法是两个键都指向 template_debug DLL。

更糟的是它的**失败方式**：main.gd 解析失败 → `--hud-check` 那行永远执行不到 →
进程不退出 → 我三次尝试留下三个僵尸 Godot，把 DLL 锁住，下一次 `build_bridge.ps1` 报
`cannot open output file: Permission denied`。所以 `godot_check.ps1` 里所有 headless 调用都带超时
（单个 60 s、重连 20 s），超时即失败并打印日志路径——「静默挂住」必须变成可读的失败。

**② 桥缺 `-ffp-contract=off`。** DLL 与 `headless_sim` 编的是同一份 sim 源码，一个允许 FMA 收缩、
一个不允许：浮点结果可能差最后一位，影响 demo 哈希回放、回滚一致性、两端预测。没有任何闸门会报，
因为闸门只跑 exe。这条被写成教训 12：**两个入口编同一份源码，标志必须逐条一致**。

### 3. 为什么这么做（架构判断）

- **端口的所有者只能是 C++ 头。** 端口要同时被 C++（信令/闸门）、GDScript（菜单）、
  PowerShell（两进程脚本）读到。跨语言时「抄一份常量」必然漂移（已经漂了：27016 是上古默认）。
  所以 `sim/net/ports.h` 是唯一所有者，其余人**读**：桥转成方法、脚本解析头文件、
  net_loop 用常量做默认值。再加一条 lint 把「抄」这件事变成红色。
- **闸门的边界画错了，比少一条闸门更危险。** 16 项闸门全绿而桥根本没加载——这不是缺测试，
  是测试只覆盖了 sim 进程。所以第 17 项不是「再检查一遍 HUD」，而是「把桥拉进闸门体系」。
- **版本号只该出现在下载 URL 那一行。** 目录名带版本（`cmake-4.4.4-…`）会把版本号复制到
  `build.ps1`、文档、别人的记忆里。改成无版本名 + 安装脚本负责适配，升版本就只改一行 URL。
- **重连的锁要跟着事实走，不是跟着按钮走。** 按钮 `disabled` 是表现层的延迟，真正的事实是
  「这张票据已经被用过、这条 link 已经被替换」。锁放在 `_do_reconnect()` 里、成功后保持到菜单重开，
  才挡得住同一帧的第二次点击。
- **失败要能失败。** 端口 lint 写完后第一次运行就红在自己的注释上；`--reconnect-check` 故意
  再调一次 `_do_reconnect()` 并断言 `double=0`。判据的价值在于它曾经红过。

### 4. 证据（最终基线）

- `tools/smoke.ps1`：**17 项行为闸门 + bench ALL GREEN**（p99 0.40 µs，0.00 allocs/tick），
  其中第 17 项自带两条：`hud hp=100 armor=0 impulse=4/4 role=solo vertices=360
  build=<hash> protocol=4 port=27115` 与两进程 `reconnect ok / double=0 / signal slot 1 resumed 1`；
  （S59 之后协议升到 5、hud 行多了 protocol/port 之外的快照字段说明，见下一节。）
- `tools/net_selftest.ps1`：`joins=2 accepted=420 far=0 moved=46.69 ports_ok=True clients_ok=True`；
- 两个 Godot 进程**一个 `--port` 都不带**：host 打 `hosting on port 27115`、client 打
  `connecting to 127.0.0.1:27115`，双方互相 `remote slot N visible`；
- `tools/check_sources.ps1`：`sources ok: 27` + `ports ok: literals only in sim/net/ports.h`；
- `build_bridge.ps1`：`-Werror` 下零告警，`built … (build id 08334fd)`。

### 5. 失败与修正

1. **`$Host` 是只读自动变量**：两进程脚本第一版直接崩在 `$host = Start-Process …`
   （PowerShell 内置 `$Host`）。改名 `$hostProcess`。
2. **僵尸 Godot 锁住 DLL**：扩展加载失败时 `--hud-check` 不会退出，我先后留下的三个进程让
   `ld` 报 `cannot open output file … Permission denied`。教训：headless 调用一律带超时，
   改 DLL 前先确认没有 Godot 在跑。
3. **`-DBP_BUILD_ID="abc"` 会被 PowerShell 吃掉引号**：宏最后变成裸标识符
   （`#define BP_BUILD_ID a4cd724`），只有 `-DBP_BUILD_ID=\"abc\"` 才对。用 `g++ -E -dM`
   两种写法各测一遍才定下来——这种细节不能靠「应该可以」。
4. **端口 lint 先红了自己**：注释里写了 27115。把规则文件本身排除，注释改写。
5. **中文路径第四次咬人**：`objdump -p` 对 DLL 报 `No such file or directory`（路径被按 ANSI 解码），
   只能先复制到 ASCII 目录再查导出表。

### 6. 明确未完成（不许含糊）

- **AUD-16**：`get_hp()/get_armor()` 仍是常量 100/0，M6 把 hp/armor 放进快照时一并解决；
- **R6 边界**：只做到 session 三步零分配，序列化/回环传输仍约 40 次/tick；
- **第 17 项在没装 Godot 的机器上是 SKIP**：这是有意的（工具链脚本不下载 Godot），
  但也意味着「换机后忘装 Godot」不会让 smoke 变红，只有 `godot_check.ps1` 会明确说 SKIP；
- **跨机 demo 比对**仍缺第二台机器。

### 7. 下一步

债清了，按计划书进 **M6 射击最小闭环**：WeaponDef(toml) → 开火输入 → hitscan（复用 hitbox 历史）
→ 三段伤害 → hp/armor 进快照 → 死亡/重生。判据先写：固定靶命中/未命中、三段倍率、TTK 0.6–1.2 s。

## 2026-10-20（夜）· M1–M5 全量审计与修复（S59）

### 0. 目标

用户要求「M1 到 M5 完整检查一遍，确定功能完整、代码正常、无无用代码、无屎山，架构合规」。
做法：四个**只读**代理（sim 核心/移动、sim 网络、bridge+Godot、tools+闸门）分别通读，
我再逐条复核、逐条决定「修 / 记 / 待定」。不给"整体良好"这种结论，只给能验证的条目。

### 1. 结论先说

1. **闸门大面积"能打印不能失败"**：`--info`、`--bench`、land 事件、分片 delivered、dup 帧、
   selftest 的 8/21 字段、胸倍率、墙"是否真的到达"、光线 fraction、join 文件写失败静默跳过——
   smoke 只看退出码，所以它们全绿却从不证明任何事。这是本轮最大的一类问题。
2. **两处真数据缺陷**：impulse 着地**每 tick** 回满（4 个 62 ms 到手）；
   回滚快照缺 slide/dash/vault 三个按下沿与 speed_coefficient（按住会被当成"刚按下"重放）。
3. **三处"文档说了、代码没做"**：`--force-paused` 从没被解析（AUD-02 的验证是假的）、
   `entry["state"]` 没删（AUD-18 写着已删）、桥从未接可靠流 configstring（AGENTS 写着已下发）。
   分别改成：真解析、删除、把文档说成真话（真实链路靠 FULL_STATE 两个哈希拒局，功能已覆盖）。
4. 死代码清掉 11 处：rng_at64、clampf、dropped_commands、decode_failures、stale_skips、
   sent_packets、delivered_packets、PlayerState::tick、last_acked_snapshot、Link::ever_seen、
   DemoWriter::m_file。

### 1b. 按里程碑看的检查表

| 里程碑 | 查了什么 | 代表性问题 | 修法与判据 | 仍留 |
|---|---|---|---|---|
| M1 地图/碰撞/哈希 | .map 解析（entity/brush/平面/材质）、单位换算、扫掠与分步、geometry_hash、--info | --info 只打印；origin 在桥里换算；geometry_hash 不含 _climbable；0 brush 地图能加载；bumps 多给一次 | 断言 test_box 形状（2 entities / 8 brushes / 21.0 m）；entity_origin_metres()；哈希含标志并缓存；拒绝空 brush；严格 < | 分步长度与防穿注释/判据表述（AUD-33） |
| M2 基础 pmove | 走跑跳蹲、step-up、蹲下空间、摩擦/加速、数值与计划书 §5.6 | 空中也 step-up；impulse 每 tick 回满；NaN 输入能污染整局 | 仅着地 step；recharge_ticks=32；入口 isfinite/dt 守卫 | 手感常量仍是字面量（AUD-28） |
| M3 parkour | slide/dash/wallrun/wallclimb/vault 状态机、enter/exit 对称、计时、代价 | 滑铲跳那一 tick 仍走 slide 摩擦；爬墙 keep 不查 climbable；翻越/爬墙被当 Jump | 重算 sliding；keep 查标志；Jump 要求 jump 沿 | 空中 mantle 的定位（设计待定 2） |
| M4 demo/比对 | 格式、头部、录制回放、--dump/--compare、分叉定位 | Reader 泄漏句柄；截断当 EOF；build_id 从不比较；compare 一方 EOF 也 PASS | 析构/close；feof 区分；build_id 进 mismatch 且返回 1；同 EOF + rows>0 | demo 初始状态不完整（AUD-30） |
| M5 网络 | 线格式、delta、回滚、输入窗口、时钟、可靠流、票据、组局、分片、UDP、桥 | 快照缺三个沿与 speed_coefficient；幽灵链路永不超时；全零 id 可占房主槽；ack_bits 恒 0 却宣称有；闸门多处只打印 | 协议 v5 + [snapfields]；超时从 add_client 起算；拒全零 id；netchan 注明；9 处判据改成能失败 | AUD-20/21/22/23/24/26/27 |

### 2. 做了什么（按域）

**模拟/碰撞**
- impulse 充能改整数间隔 `recharge_ticks`（toml + params_hash）；`--test-parkour` 断言
  「31 tick 不回、第 32 tick 回 1」。
- 空中不再 step-up（只有 on_ground 才 step_slide_move）；跳跃撞台沿不再免费上台阶。
- Jump 事件要求 jump 按下沿 + 起跳前着地；翻越/爬墙不再被报成跳跃。
- `geometry_hash` 纳入 `_climbable/_wallrunnable` 并缓存；bumps 上限改严格 `<`。
- pmove 入口拒绝 NaN/dt≤0，forward/right 钳到 [-1,1]；爬墙 keep 检查 climbable。
- map_parse 拒绝 0 brush 的图；新增 `entity_origin_metres()`，桥不再做第二次 0.0254 换算。
- 删除 `PlayerState.tick`（不进快照却进哈希 → 回滚后假分叉）。

**网络**
- 协议 **v5**：PlayerSnap 增加三个按下沿（flags bit4-6）、`recharge_progress`、
  `speed_coefficient`；`[snapfields]` 逐字段往返断言。
- 链路超时从 add_client 起算（原来"没收到过包"的 link 永不超时 → 幽灵槽 + 43 KB/s）。
- HELLO 拒绝全零 client_id（原来会被当成"空闲槽"占住，可顶掉房主 slot 0）。
- MatchHost::start 先 stop 旧线程；PeerTransport 收件队列封顶 64；暴露 `bad_snapshots()` 供 net_loop 打印。
- `ack_bits` 在 v0.1 恒 0，netchan.h 写明，不再假装"每包带 32 位 mask"。

**桥/Godot**
- solo 步进改 `step_single`；params_hash 改 combined（含 combat.toml）。
- 眼高 0.9×当前姿态高度（原来恒 1.55，比站姿头顶还高、蹲下不动）。
- join 失败回滚 disconnected；start_host 失败清 TCP/端口；join_impl 先建后换。
- `--force-paused` 真解析；暂停时同步相机并 drain 事件；Esc 过滤 echo；autoplay 跳跃改边沿。
- 删除未读的 `entry["state"]`。

**demo/键位/工具**
- DemoReader 加析构/close、区分短读与 EOF；build_id 进 mismatch，`--play` 遇 mismatch 返回 1。
- `--compare` 要求两边同时 EOF 且 rows>0；`key_from_name("NONE")` 往返。
- CLI：strtol 全量校验、未知 --script 报错、--record/--play 互斥、--dump 需 --play、usage 补 --tolerance。
- `run_net_alloc_check` 从 main.cpp 移进 tests_net.cpp；CMake 源清单过滤空行/注释。
- check_sources 收 untracked，新增三条铁律 lint（sim 无 Godot / 0.0254 只在 map_parse / 无状态 RNG），
  匹配前先剥注释。

### 3. 为什么（架构判断）

- **闸门必须能失败**：只会打印的判据给人"已验证"的错觉，比没有判据更危险（教训 3/13）。
- **状态哈希的字段集 = 快照的字段集**：两者唯一用途都是"两边是不是同一个世界"；
  只要有一个字段只在一侧，回滚与跨机比对就会给出相反结论。v5 就是补齐这个集合。
- **单位转换只能有一个地方**：桥里那次 0.0254 不是"顺手"，是让规矩失效的第一步；
  现在由 map_parse 提供米制接口、lint 兜底。
- **文档里"已完成"的每一句都要有代码/闸门支撑**：本轮三处都是文档跑在代码前面。

### 4. 证据（本轮最终基线）

- 全量构建 `-Werror` 零告警；`smoke.ps1` **17 项 + bench ALL GREEN**，新增判定：
  `p99 within 4 ms / 0 allocs`、`state_fields 21/21`、`[snapfields] 1`、
  `impulse recharge interval … ok`、`[fragment] delivered 9`、`[netalloc] per_tick 0`、
  `[match] … second 1 … slots 1`、`--info` 断言 2 entities/8 brushes/21.0 m。
- `godot_check.ps1`：`hud … protocol=5 port=27115` + 重连 `double=0` + **暂停 7 s 不掉线**。
- `net_selftest.ps1`：`joins=2 accepted≈420 far=0 moved≈46.7 ports_ok=True`。
- `check_sources.ps1`：`sources ok: 27` / `ports ok` / `rules ok`。

### 5. 失败与修正

1. 快照往返断言第一版把 `slow_factor` 算进去——它是 pmove 每 tick 从 slow_ticks 派生的，
   不是线上状态。改成只比真状态，并在注释里写明"派生量不算"。
2. soak 阈值收到 30 tick 立刻红：45 tick 的"不动"是**贴墙顶着走**，不是冻结。最终按
   "solid 才是真冻结"保留 2 tick，frozen 放到 64（1 s）并写清理由。
3. `[fragment]` 一开始要求 delivered≥10，30% 丢包下实测 9。阈值要卡在"能证明重组发生过"
   的最低线（改 5），否则闸门变成运气测试。
4. 改 main.cpp 的 include 后忘了 `tick_seconds`/`load_combat_beside` 的头文件，-Werror 立刻抓住。
5. `probe` 这个变量名与已有的 `World probe` 撞名，编译报 "conflicting declaration"——改名 `probe_field`。

### 6. 明确未完成（都在 AUDIT 6.2/6.3，带 lead）

- AUD-20 客户端落后不补 tick（>110 ms 卡顿后输入永久 Late）；
- AUD-21 序列化/接收热路径仍每次 4-8 次分配（`[netalloc]` 只覆盖 session 三步）；
- AUD-22 可靠流 u16 回绕不安全；AUD-23 分片传输未接真实链路；
- AUD-24 admission 无 TTL；AUD-26 UDP 不校验源地址；AUD-27 正常离开不释放组局槽位；
- AUD-28 手感常量仍是字面量（铁律 I）；AUD-29 combat 诊断与 max_health；
- AUD-31 桥的信令在主线程同步（最坏 ~11 s 冻结）；AUD-32..35 表现层/主循环项；
- 设计待定：impulse 充能速率、空中 step-up 是否做成 mantle、护甲 66%、configstring 的定位。

### 7. 下一步

M6 射击，顺序不变：WeaponDef(toml) → 开火输入 → hitscan + hitbox 回溯 → 三段伤害 →
hp/armor 进快照（顺带 AUD-16/AUD-29）→ 死亡/重生 → 穿透/衰减。
AUD-20/26/27 在 M6 前清（都影响"两个人真打一局"）。

---




---

## S60 · 2026-10-23 · M6 入口条件（审计 7 条）+ 治理迁移

### 1. 做了什么

**代码**（一条 checklist，七个独立缺口）：
1. **协议 v6**：mask/flags 8→16 位，`kFieldAll=0x01FF`，新增 Vitals 组（hp/armor，接 PlayerState 真值）
   与 Weapon 组（预留，恒 0）；四条 static_assert；demo 头、session 的 same_state、桥的 get_hp 全部跟上。
2. **AUD-23**：分片接进真实链路——`UdpPeerHub::add_peer` 返回 fragmenting view，客户端 join 也包一层。
3. **AUD-26**：`UdpTransport::connect_peer` 规范化地址，`recv` 只收对端 (host,port)，其余丢弃计数。
4. **AUD-27**：`NetServer::add_client(..., ClientId*)` → `remove_client` 进 departure 队列 →
   网关 drain 后 `MatchHost::release`。
5. **AUD-20**：`NetClient::catch_up()`（落后 >4 tick 重锚时间轴）；`main.gd` 不再清零累加器。
6. **本地渲染插值**：桥暴露 `get_prev_position/get_prev_eye_height`，相机在两 tick 间混合。
7. **E 键让位交互**：WallClimb → V，`Action::Use` 改名 `Interact` 绑 E；腿倍率进 `movement.toml`。

**治理**：`docs/STATE.md`（≤120 行，唯一"现在"）、`docs/budgets.md`（资源台账）、
`docs/contracts/M6.md`（契约，入口条件已勾）、`docs/BACKLOG.md`（设计待定/功能缺口拆出）、
`tools/doc_check.py`（8 条检查 + `--selftest`，已进 smoke）；HANDOFF/GAPS/AUDIT/8.3 归档到
`docs/archive/` 与 `docs/audits/`，AGENTS 只留指针（"17 项闸门"从五个文件收到一处）。

### 2. 判据（命令 + 实际输出）

- `--test-snapshot`：`[wire] full 1 vitals 1 flags 0x7FF 1 idle delta 15 B；10 players 833 B, 82 B/player`
- `--test-net`：`[udpsrc] rejected 1 injection-drop 1 legit-after 1`；
  `[frag] real-link 10p 833 B ten 1 decode 1 nofrag 1 3000B 1 split 1 small 1`；
  `[slots] first 1 full 1 reported 1 freed 1 cycles 3/1 anonymous 1`；
  `[catchup] freeze 300 behind 293 catchups 1 accepted 196->203->259 late 0->0 settled 1`
- `--test-bindings`：`wall climb on V` / `interact on E` / `E is reserved for interact` 全 ok
- `--test-hitbox`：`leg multiplier reads movement.toml` ok（`hitbox.cpp` 里没有 0.7 了）
- `godot_check.ps1`：`hud … protocol=6 port=27115` + `interp prev=-16.0000 cur=-15.9877 gap=0.0123`
- `--bench 1000 --players 10`：p99 **2.40 µs** / max 4.20 µs / 0 allocs
- `python tools\doc_check.py`：9 ok / 2 warn（都是"契约里还没建的文件"）/ 0 fail；`--selftest` 6 fail

### 3. 失败与修正

1. 协议 v6 第一次跑闸门就红：encode 里 `w.u8(now.flags)` 忘了改成 u16，flags 0x7FF 被截成 0xFF；
   idle delta 的期望长度也多算了一个 count 字节。两条都是新闸门当场抓的。
2. `[slots]` 第一版 `cycles 0/0`：测试自己忘了释放"第二个被拒绝后又被放进来"的那个 id，
   不是产品 bug；把每轮释放写全才过。
3. `[catchup]` 第一版 `behind 5`：冻住客户端时 LoopbackTransport 把 300 份快照都排队了，
   恢复时逐份解完就追上了。真实 UDP 缓冲区不会留 4.7 秒——补了"丢积压"这一步才复现出 `behind 293`。
4. E 键改到 V 之后 `--test-bindings` 失败：测试把 jump 重绑到 V 与新的爬墙键冲突。
   换到 Z，并顺手加了"E 不许被任何移动动作占用"的检查。
5. doc_check 抓到六处真实漂移：AGENTS 指向已归档的 HANDOFF/GAPS、README 的 4.7/4.5 自相矛盾、
   STATE 的 `2.40 / 4.20` 被当成闸门计数（正则改成认 `smoke … N 项`）。

### 4. 下一步

拍 D1（穿透模型）/ D2（护甲 66% 与再生）/ D3（movement.toml 热重载）→ 签 M6 契约 →
W1 `sim/data/weapons.toml`（待建）+ WeaponDef。射击功能本身一行没写，不要把它算成"M6 已完成"。

## S61 · 2026-10-23 · 开 M6 前的代码体检（去冗余 + 架构）

### 1. 做了什么

- **测试去重与拆分**：删掉 `--test-net` 里的 `[snapfields]`（和 `--test-snapshot` 重复）；
  `tests_net.cpp` 81 KB → 拆成 `tests_net`（会话）/ `tests_transport`（UDP+分片）/ `tests_lobby`（准入+票据+组局），
  全部 ≤ 56 KB；5 份重复的 `poll` lambda 收成一个 `wait_for`。
- **槽位释放改成回调**：`drain_departed` 队列要求每个调用方都记得 drain，`net_loop` 第一天就漏了；
  改成 `set_departure_handler`，桥与 net_loop 各装一次，漏不掉。网关在 `abort_host` 里先清 handler。
- **补 tick 与加入共用一段循环**：`advance_to_clock()`；`kJoinSlack` 从两处局部常量收成一处。
- **收紧 API**：`UdpTransport` 删掉没有生产调用者的 `recv_from/adopt_peer`（只剩"已连接端点"一个职责）；
  `FragmentedTransport` 删掉没人读的 `kMaxDatagramBytes`；`ClientId` 移进 `sim/net/client_id.h`
  （`session.h` 不再为了一个 id 去 include 票据格式）。
- **让诊断量有读者**：30% 丢包闸门现在断言"确实丢了包"（`dropped 1205`）；真实链路分片闸门断言
  `dropped_packets()==0`（`abandon 1` 表示干净链路上没有丢弃半包）。原来这两个计数器写了没人看。
- **首帧相机**：`load_map` 末尾先 `remember_previous()`，插值从出生点开始，不再从世界原点滑入。

### 2. 判据

`tools\smoke.ps1`：19 项 + bench 全绿；`[30% loss] … dropped 1205`；`[frag] … abandon 1`；
`tests_*.cpp` 单文件最大 56 KB（原 81 KB）；`python tools\doc_check.py` 0 fail。

### 3. 失败与修正

1. 拆分后 `tests_lobby.cpp` 少了 `ports.h/session.h/transport.h`，-Werror 一次报出全部 15 个未声明符号。
2. 把 UDP 闸门改成"双端 connect"后忘了删 `from_host/from_port`，编译期报未使用变量才想起来。
3. `[frag] real-link` 的 printf 里一度写成 `ten_bytes.size()`（变量其实叫 `bytes`），编译立刻抓住。

### 4. 下一步

拍 D1/D2/D3 → 签 M6 契约 → W1 `weapons.toml`（待建）。射击功能仍然一行没写。

## S62 · 2026-10-24 · M6.0 规则落地（ADR 0006）

### 1. 做了什么

- **D1 穿透定稿**：武器决定能不能穿、穿几层；地图 `_penetrable 0` 一票否决。写进 ADR 0006。
- **D2 护甲改成按部位结算**（本次唯一改代码的部分）：
  `HitZone::Head` = 真伤，直接扣 HP、护甲不掉；胸/腿先扣护甲池（`absorb_percent = 100`），
  扣完溢出才扣血。`HitZone` 从 `hitbox.h` 移到 `combat.h`（它是伤害概念），
  `apply_damage(vitals, damage, zone, params)` 强制调用方想清楚打的是哪。
- **D3 热重载**：决定不做，文档改成"启动时读"，记 `DEBT-22`（lead M7）。
- 契约 draft → **active**，§6 改成 M6.0–M6.12 的单任务表，做完一个停一次。

### 2. 判据

`--test-health`（13 项）：`light armour: 30 body damage -> 25 armour, 5 HP`、
`headshot ignores armour`、`leg damage uses the armour pool`、`body TTK with heavy armour: 6 shots = 0.60 s`。
smoke 19 项 + bench 全绿；`doc_check` 0 fail。

### 3. 失败与修正

- 换成"护甲全吃"之后，旧测试里"light armour: 30 → 19 absorbed, 11 HP"就错了（那是 66% 模型的数）。
  没有改实现去迁就测试，而是按 ADR 重写了三条断言，并补了爆头真伤 / 腿部走护甲两条。
- `HitZone` 原来在 `hitbox.h`，而 `combat.h` 不能 include 它（`hitbox → pmove → combat` 会成环）。
  移到 `combat.h` 后依赖方向变成 `hitbox → combat`，环消失。

### 4. 下一步

M6.1 武器数据层：`sim/data/weapons.toml`（待建）+ `WeaponDef` 加载 + 独立哈希，
闸门 `--test-weapons`。**做完停下**，等下一句"继续"再开 M6.2。

## S63 · 2026-10-24 · 对照原文复核 M6（只读，不改代码）

### 1. 做了什么

把计划书里所有与 M6 有关的段落重新读了一遍（§4.1–4.2 来源与许可、§6.1–6.10 枪感与判定、
§7.8 确定性随机、§12.2 里程碑），并按"开源来源 → 我们取哪一层"整理成表。结论：
M6 的**设计**是拼装的（Red Eclipse 2 的参数字段 + TacZ 的字段补全 + Q3/OpenArena 的 hitscan 思路 +
CS 的弹道形态），**代码全是自己写的**，数值一个都不许照抄。

### 2. 判据

这次只动文档、没有改代码，所以没有新增闸门；收工仍然跑了 smoke（19 项 + bench，ALL GREEN）
与 doc_check（0 fail）。核对结果以行号引用给出：
`weapons.h/weapdef.h` 结构（L810–848）、TacZ 的 9 个字段（L872–892）、
"弹道必须线段扫掠，写进 M6 验收"（L1006）、三段判定（L1037–1060）、
ADS 灵敏度（L1018–1032）、追击终结（L1080–1113）、`rng_at` 使用清单（L1345–1350）。

### 3. 失败与修正（发现的问题，尚未修）

对照后发现 **3 处缺口 + 1 处范围风险**，写进了提案但没动代码，等拍板：

1. **高速弹道（线段扫掠 vs 胶囊）不在 M6.1–M6.12 里**，而原文把它写进了 M6 验收标准。
   但 v0.1 若只有 hitscan 武器，它没有使用者（验证不了）。倾向：写进 out + DEBT，随 M8 狙击枪做。
2. **`rng_at` 没进 M6 任务**：后坐力随机分量、霰弹、弹孔都要它（§7.8），现在是"实现了但没人用"。
3. **ADS 灵敏度缩放**（§6.7）没有归属：M6 只做 `ads` 状态位，缩放属于表现层，应随 M8 视角模型。
4. **`WeaponDef` 40+ 字段**若在 M6.1 一次全做，就是"没有使用者的代码"；应按任务分批长。

### 4. 下一步

拍板上面 1–4 → 改契约 §6 与 §2 out → M6.1（`weapons.toml` 待建 + WeaponDef + `--test-weapons`）。

## S64 · 2026-10-24 · M6.1 武器数据层

### 1. 做了什么

- **D4/D5 定稿**（按契约 §4 的建议默认执行）：高速弹道不进 M6（v0.1 全是 hitscan，没有使用者）→
  `BACKLOG FEAT-12`；`WeaponDef` 按任务分批，没有使用者的字段不先写。
- **`sim/data/weapons.toml`**：rifle / pistol 两把，11 个字段——伤害近/远、衰减起止、rpm、射程、
  弹匣、备弹、换弹秒数、穿透层数、每层伤害系数。pistol 的用途是给 M6.5 做对照（步枪能穿、手枪被挡）。
- **`sim/game/weapon.{h,cpp}`**：`WeaponDef` / `WeaponParams`（`find`、`hash`）/ `load_weapon_params`。
  加载规则只写在一处：缺键警告并用编译默认值、拼错报 unknown key、不能模拟的值直接报错。
- **`TomlBinder` 提出来共用**：原来是 `load_movement_params` 里的匿名结构，现在在 `sim/data/config.h`，
  两个加载器共用一份"读一个数、类型不对就警告"的逻辑——两份就会漂。
- 数据层硬化：TOML 解析拒绝重复键、容忍 UTF-8 BOM（原因见 §3）。
- 新闸门 `--test-weapons`（18 项检查），smoke 第 20 项；`tools/sim_sources.txt` 28 个 sim 源文件。

### 2. 判据

`[weapons] weapons 2 clean 1 stable 1 independent 1 defaults 1 rejects 5`；
smoke 20 项 + bench ALL GREEN；`sources ok: 28`；`doc_check` 0 fail。
反证（闸门能红）：把 `damage_near` 拼成 `damgae_near` 的一份数据放进 `--data` 同目录，
闸门打印 `clean 0` 并退出 1——不是靠"我看它绿了"。

### 3. 失败与修正

- 首次构建红在 `-Wmissing-field-initializers`（`TomlBinder{t, w}` 少给 `used`）：加构造函数，不改警告等级。
- "缺键保留默认值"一开始用 `damage_near = 10`，撞上编译默认的 `damage_far = 18`，被校验拒了。
  不是 bug，是校验对了——改成 30，并在测试里写清为什么。
- 用 PowerShell 改 toml 做反证时，`Set-Content -Encoding UTF8` 在文件头写了 BOM，
  解析器在第 1 行报 `expected key = value`（真正的病根看不见）。顺手让 `load_toml_file` 容忍 BOM，
  并加一条闸门钉住；这不是放水：编辑器存 BOM 是常态，报错应该指向真问题。
- 顺带发现重复 `[section]` 会静默只生效一半（`find` 取第一个），现在解析器直接报 `duplicate key`。

### 4. 边界 / 下一步

- 武器表还没进 `World`，也还没进 join 哈希：M6.2 做（两台机器枪数据不一致必须拒绝入局）。
- `headshot_mult` / `legshot_mult` 仍在 `movement.toml`，M6.6 定唯一归属，不许两份都活。
- 下一步 **M6.2 开火输入**（三个按下沿 + flags 7–9）；做完停下，等下一句"继续"。
## S65 · 2026-10-24 · M6.2 开火输入 + 许可边界（ADR 0007）

### 1. 做了什么

- **按钮位掩码**：`sim/input/buttons.*` 定义 10 个按钮位（jump/crouch/slide/dash/wallrun/
  wallclimb/vault/fire/reload/ads）。命令包、桥的 `set_input(f, r, yaw, buttons)`、
  GDScript 的 `get_action_bits()` 共用这一份——加按钮不再加一个位置参数。
- **开火链路**：`MoveInput` 加 fire/reload/ads；`PlayerState` 加同名按住状态（不是一 tick 脉冲：
  丢一个包不该把“还在扣扳机”读成“松手”）；进 `state_hash` 与快照 flags 7–9（v6 预留位，未升版本）。
  左键开火 / 右键开镜 / R 换弹进 keybinds.toml 与 Bindings 默认值。
- **枪表进 join 哈希**：`World::weapons` + `load_weapons_beside`；`combined_params_hash(m, c, w)`
  在服务器、客户端、桥、headless 四处一致。两侧枪数据不同 → 拒绝入局。
- **许可边界**（ADR 0007）：GPL 只读事实、实现自写；台账 `docs/assets-licenses.md`（含
  「只读参考」表：RE2/TacZ/OpenArena/Xonotic 各取了哪条事实）；铁律 K；`check_sources.ps1`
  扫到 GPL/AGPL 头就红。
- weapons.toml 头部写清“加一把枪 = 加一段”；闸门新增两条：加第三把枪不改代码、加枪会动哈希。

### 2. 判据

`[fireflags] held 92/92 released 52/52 agree 144/144`；`[config] … weapons 1`（枪表不同被拒）；
`--test-snapshot` 的 trigger 位往返 + `--test-bindings` 的 fire/aim + 掩码往返；
`[weapons] … rejects 5` 且“a new gun is one new section”为 ok。smoke 20 项 + bench ALL GREEN。

### 3. 失败与修正

- `[fireflags]` 第一版直接逐 tick 比服务器与客户端：`held 94/100`、`agree 148/160`。
  不是 bug——客户端把命令提前 4 tick 盖戳，切换后的前几 tick 服务器还在执行旧输入。
  改成跳过 8 tick 的暖机段再采样，数字变成 92/92、52/52、144/144；暖机原因写进注释。
- 桥的 `set_input` 原本要把 10 个 bool 变 13 个；改成传掩码，GDScript 建一次表。
  这条不是顺手：再加两个动作，参数顺序就没人记得住了。

### 4. 边界 / 下一步

- 数字 id 进快照（Weapon 组）留给 M6.7，届时 `weapons.toml` 加显式 `slot`（现在按 id 字符串认）。
- 后坐力、扩散矩阵、ADS 仍按任务分批；仿 TacZ 只仿结构，数值自己推导（ADR 0007）。
- 下一步 **M6.3 静态 hitscan**（射线 vs 玩家盒 + 世界，E1 命中/不中）；做完停下。
## S66 · 2026-10-24 · M6.3 静态 hitscan

### 1. 做了什么

- **`sim/game/hitscan.*`**：`hitscan(world, boxes, count, origin, dir, max_distance, ignore_entity)`。
  先扫世界（`trace_box` 零尺寸 = 点射线），再逐玩家 AABB 求交，**最近者胜**——墙挡人。
  方向在函数内部归一化：传 (10,0,0) 和 (1,0,0) 得到同一个距离；零向量算未命中，不是 NaN。
- **玩家体积复用 `HitboxSample`**：所以 M6.4 的回溯只是把“当前帧”换成“回溯帧”，签名不变；
  M6.6 用 `hit_zone_at(sample, point.z)` 就得到部位。
- 判定形状是 **AABB**，和 `hitbox_contains` 完全一致：判定与可视体积不能各说一套。
- 第 21 项闸门 `--test-hitscan`，14 条检查（E1 的“必中/必不中”在里面）。

### 2. 判据

`[hitscan] chest 1 face 1 wide 1 edge 1 crouch 1 wall 1 range 1`；smoke 21 项 + bench ALL GREEN。
**闸门能红**（这次真做了变异验证）：把世界检测短路掉，`a wall between them stops the shot`
立刻 FAIL、退出码 1；改回来再绿。

### 3. 失败与修正

- 读 `.map` 才发现薄墙只覆盖 y ∈ [-2, 2]（不是整张图）。第一版差点把“瞄准点偏一个盒宽”的测试
  放在 y=0 的车道上——那条射线会先打到墙，测试会“通过”但它证明的是墙，不是盒宽。
  现在玩家测试全在 y=5 的开阔道，墙测试单独在 y=0。
- 0.40 中 / 0.41 不中这一对是故意加的：只测“偏 0.81 不中”无法区分“盒宽正确”和“判定偏小”。

### 4. 边界 / 下一步

- **还没有俯仰**：`MoveInput` 只有 yaw，`hitscan` 接的是显式方向。谁生成方向（以及 pitch 进命令包
  要不要升协议 v7）在 M6.6 决定——这条已经写进契约那一行，不会漏。
- M6.5 的穿透需要知道“打到了哪个 brush”，届时给 `ShotHit` 加 `const Brush*`（现在加了没人用）。
- 下一步 **M6.4 hitbox 回溯**（6 tick 窗口 + 帧间插值）；做完停下。
## S67 · 2026-10-24 · M6.4 hitbox 回溯（公式 + 插值）

### 1. 做了什么

- **`sim/game/rewind.*`**：
  `rewind_ticks(one_way_ms)` = 单程延迟 × 64拍 + **1 tick 插值**，夹到 `kMaxRewindTicks = 6`。
  按 v0.4 §9.1 的推导：30 ms RTT + 15.6 ms 插值 + 5 ms 抖动 ≈ 51 ms → 4 tick，取 6 作裕度。
  这行是公式不是经验值：改输入（延迟/插值）而不是改常数。
- `rewind_frame(histories, count, shot_tick, rewind, out, cap)`：从 64 tick 历史里取
  `shot_tick - rewind` 那一帧，**小数部分用 `HitboxHistory::sample` 插值**（不取整帧）。
  比最旧帧更老 → 夹到最旧，**绝不外推**。零分配：缓冲区由调用方持有。
- 闸门加 8 项到 `--test-hitscan` 的 `[rewind]` 段：靶子以 8 m/s 横穿火线，6 tick 走 0.75 m，
  超过半个盒宽——同一个瞄准点，打回溯帧命中、打当前帧落空。

### 2. 判据

`[rewind] window 6 at-6 1 current-miss 1 interp 1 clamp 1`；smoke 21 项 + bench ALL GREEN。
**闸门能红**：把 `shot_tick - rewind` 改成 `+`，4 项立刻 FAIL（帧位置、命中、插值、夹取）。

### 3. 失败与修正

- 第一版 clamp 项期望 `y = -7.5`（tick 0），实际失败：hitbox 环只有 **64 tick**，
  我录了 101 帧，tick 0 早被覆盖。改成录 61 帧（一个整秒）并把用例改成“早于最旧帧”，
  期望值随之改成 tick 40 的位置——测试因此还顺手钉住了“历史只有 1 秒”这件事。
- 体感上“晚 6 tick 仍命中”靠的是靶子**横穿**火线（沿 y 走）。第一版让靶子沿射线方向走，
  那样不管回不回都命中，测试等于没测。

### 4. 边界 / 下一步

- 回溯的**生产消费者**还没出现：真正开枪（用输入方向 + `rewind_ticks(实测 RTT)`）在 M6.6/M6.7。
- 客户端预测命中（v0.4 §9.3）需要“同一个函数 + 同一份历史”，也就是这套 API；但要等伤害落地。
- 下一步 **M6.5 穿透**（地图 `_penetrable` + 每层 ×0.7）；做完停下。
## S68 · 2026-10-24 · M6.5 穿透（地图否决 + 每层 ×0.7）

### 1. 做了什么

- **地图侧**：`Brush::penetrable`，来自实体的 `_penetrable`（和 `_climbable` 同一个解析点）。
  ADR 0006 D1 的默认值是**挡**：没写这个键的墙就是混凝土。它同时进 `geometry_hash`——
  两张只差 `_penetrable` 的图不能互通，否则一发子弹的结果取决于谁当房主。
- **判定侧**：`hitscan_penetrating`。先取射线上最近的世界表面与最近的玩家：玩家在前就是命中；
  世界在前就先问地图（不可穿 → `stopped_by_map`），再问武器预算（层数用完 → `out_of_layers`）；
  都放行就穿过这一层、`damage_scale *= 0.7`，从**凸体出口**（六个半空间里沿射线方向最近的那个面）
  再推进 1 mm 继续。原来的 `hitscan` 现在是它的 `layers = 0` 包装——只有一条射线实现。
- **接口**：`PenetrationParams { layers, damage_scale }` 代替两个位置参数。以后要按材质给不同系数，
  改的是这个结构体怎么填（或让它带一个 per-brush 乘数），不是 `hitscan_penetrating` 的签名。
- 新地图 `sim/data/maps/penetrate_test.map`：x=0 与 x=2 两堵可穿墙、x=5 一堵混凝土；
  专供闸门，不污染 movement 基准。

### 2. 判据

`[penetrate] walls 2 one 1 two 2 scale 0.49 pistol 1 concrete 1 open 1`；smoke 21 项 + bench ALL GREEN。
**闸门能红**：把地图否决短路掉，`concrete vetoes even five layers` 立刻 FAIL（步枪穿过了混凝土）。

### 3. 失败与修正

- 生成测试地图时把 worldspawn 的闭括号切掉了（12 个房间 brush 其实都在同一个实体里），
  解析器报 `line 59: expected a plane`。改成显式拼实体块，`--info` 校验 9 个 brush 才算过。
- `dot` 与 `types.h` 里的同名函数撞了（匿名命名空间遮蔽），编译报 ambiguous；删掉自造的那份，
  直接用现成的 `dot` / `length`——同一类工具不该有第二份。

### 4. 边界 / 下一步

- 穿透**还没有伤害**：`damage_scale` 只是结果里的一个数，M6.6 才把它乘进 `apply_damage`。
- 每层目前一个系数（来自武器）；按材质分系数时，在循环里再乘一个 per-brush 数即可，
  调用点与 `ShotResult` 都不用动。
- 下一步 **M6.6 三段伤害**（部位倍率 × 距离衰减；同一定下“谁提供射线方向”与倍率的唯一归属）；做完停下。
## S69 · 2026-10-24 · 射击链路贯通（M6.6/M6.7 框架，协议 v7）

### 1. 做了什么

- **俯仰**：`MoveInput.pitch`，命令包加 f32 → **协议 v7**。`view_forward(yaw, pitch)` 与
  `eye_position()` 成为“眼睛在哪、朝哪看”的唯一实现；桥的 `get_eye_height()` 改用同一个
  `kEyeHeightScale`。没有 pitch，子弹永远只能平着飞。
- **武器身份**：`WeaponDef.slot` 是显式字段（0..63、唯一），不是文件下标——在文件中间插一把枪
  不会改别人的 id。`find_by_slot` + `equip_weapon`。
- **运行时状态**：`PlayerState` 加 `weapon_id / ammo / reserve / fire_cooldown / reload_left`，
  全部进 `state_hash` 与 Weapon 快照组（教训 16）。满编快照 833 → **873 B**（227 B 余量）。
- **`sim/game/shoot.*`**：`update_weapon` 先走计时器再开火；服务器与客户端都调它（弹药/冷却一致），
  但客户端世界里没有受害者，所以伤害仍只有服务器算（A4）。开火 = `eye_position` +
  `view_forward` + `rewind_frame` + `hitscan_penetrating` + `hit_zone_at` + `damage_at_range`
  → `apply_damage`。射速 `round(60×64/rpm)`（600 rpm → 6 tick），换弹 `round(秒×64)`。
- **第 22 项闸门 `--test-damage`**：胸 25、护甲多吃两发、爆头 125 一枪、墙全挡、换弹填满。

### 2. 判据

`[damage] body 4 rounds armour 10 head 125 wall hits 0 reload 30`；smoke 22 项 + bench ALL GREEN；
`--test-snapshot`：`protocol v7 … 873 B, 86 B/player`；`[fireflags] 92/92 52/52 144/144` 仍绿。

### 3. 失败与修正（以及一个必须记下的发现）

- 眼高 0.9×身高 = 1.278 m，而头段起点是 0.82×身高 = 1.164 m → **平射同身高对手必中头**。
  这不是 bug 而是参数冲突，但会让 TTK 直接崩（×5 一枪）。记成 `BACKLOG DESIGN-09`，
  等拍板（抬头段起点 / 压眼高 / 接受）；闸门里打胸时显式压了 -0.05 rad。
- 服务器还没有测 RTT，回溯只能用 1 tick（`rewind_ticks(0)`）。记成 `DEBT-23`，
  不在本轮顺手造一个假的延迟估计。
- 编译期两次小错：`near()` 未使用（-Werror）、`tests_shoot.cpp` 少 include；都按规矩修，没降警告。

### 4. 边界 / 下一步

- 半自动 vs 自动、开镜扩散、后坐力、命中提示、死亡重生都还没有；**M6.8 的两端 HUD 断言**是下一件。
- 现在能从扣扳机一路走到“对方掉血”，但画面上还看不出来——这是下一步要补的。
## S70 · 2026-10-24 · M6.8 与表现层（曳光、敌方红轮廓）

### 1. 做了什么

- **M6.8**：桥加了一个**只给闸门用**的 `apply_test_damage(slot, amount)`（走真实 `apply_damage`），
  房主在客户端加入后打 slot 1 三十点；`godot_check.ps1` 新开一段双进程检查，客户端 HUD 打出
  `hud hp=70` 即通过。伤害来源→快照→对端 HUD 这条链第一次有证据（AUD-16 关闭）。
- **曳光**：`EventType::Shot`（a = 枪口、b = 命中点或射程尽头、param = 武器 slot），
  `update_weapon(..., events)` 每次开火发一个；桥 `drain_events()` 本来就带 a/b；
  GDScript 生成一根细盒子、`look_at` 对齐、0.09 s 淡出。
- **单机也能开火**：之前 solo 的 `step()` 只走 `step_single`，扣扳机什么都不会发生。
  现在 solo 也调同一个 `update_weapon`，所以弹药/冷却/曳光在离线时同样工作。
- **敌方红轮廓**：远端玩家节点加一个略大的胶囊壳（`CULL_FRONT` + unshaded 红），
  只画背面得到红边；蹲下随节点缩放。

### 2. 判据

- `godot_check.ps1`：`== tracers ==` → `tracer events=14`；`== damage reaches the other HUD ==` →
  房主 `damage slot 1 hp=70`、客户端 `hud hp=70`。
- `--test-damage` 加两条：每次开火恰好一个 Shot 事件、事件从眼睛出发且沿枪口方向。
- smoke 仍是 22 项 + bench 全绿（没有新增顶层闸门，检查落在第 18 项里）。

### 3. 失败与修正

- `NetServer::world()` 只有 const 版，桥的伤害钩子拿不到可变世界；加了非 const 重载。
  （这是测试钩子，但重载本身是对的：服务器本来就拥有这个世界。）
- 第一版把 solo 开火漏了：`--fire-check` 会打印 `tracer events=0`。不是猜测，是闸门先红。

### 4. 边界

- 别人的枪口火光/曳光看不到（事件还没上网络；M7 事件通道）。
- 没有队伍，所以“远端 = 敌方”；FEAT-14 记下分队后要按队伍着色。
- hitmarker、伤害数字、枪口火光仍未做（M6.11 剩余）。
## S71 · 2026-10-24 · M6.9 死亡与重生

### 1. 做了什么

- **死亡不是新字段**：`hp > 0` 就是活着，flags bit10 继续预留。理由写在 AGENTS §8.14——
  派生值进两份真值，迟早有一份是错的（教训 16 的同一类问题）。
- 死后输入进不了 pmove（换成空 `MoveInput`），但**照样走重力和落地**：冻结整个 step 会让尸体
  悬在半空，看起来像 bug。`update_weapon` 第一行拦掉扳机。
- **尸体不挡枪**：`HitboxSample` 加 `alive`，`hitscan` 跳过 dead 帧；插值取两帧的 and。
  这条让 `--test-damage` 的老断言“7 发打 7 次命中”变成 4——不是回归，是行为变了，断言跟着改。
- 重生：`sim/game/respawn.*` 的 `tick_respawn` 计 320 tick（5 s），到点用 `make_spawn_player()`
  （与入局同一份定义）重造玩家，并 reset 该槽的 hitbox 历史。服务器侧独有，客户端只从快照看到。
- HUD 在 hp<=0 时显示 `DEAD`。

### 2. 判据

- `--test-damage` 新增 6 项：杀死、死后不能开火、计时器带回、重生是全新玩家（满血/满弹/回出生点）、
  尸体不挡射线、活人挡射线。
- `godot_check.ps1` 的伤害段扩成三段：房主 30 → 70，再 100 → 死，5 s 后 HUD 回 100；
  断言是**顺序**（`hurt@310 dead@616 back@2171`），不是三个独立的 -match。

### 3. 失败与修正

- 第一次写闸门时用绝对 tick 做阶段判断（`now > 160`），但房主在客户端加入前已经跑到 800+ tick，
  三个阶段在同一帧连发，客户端只看到 0 与 100，`hud hp=70` 一次都没打出来。改成相对 `damage_hurt_tick`。
- 排查时以为快照没带 Vitals，加了 `get_snapshot_hp()` 诊断才看清：客户端日志尾部全是重生后的 100，
  死亡窗口在更早的位置。**先看全量日志再改代码**。

### 4. 边界

- 没有比分/击杀播报（E5 的另一半），M8。
- 重生点固定（`spawn_point_for(i)` 按槽位偏移），没有出生点选择与保护。
- HUD 没有重生倒计时（客户端不知道服务器计时器）。
## S72 · 2026-10-24 · M6.10 查表后坐力（协议 v8）

### 1. 做了什么

- **表在数据里**：`recoil_pitch` / `recoil_yaw` 是 weapons.toml 里的数字数组（每发一步，弧度），
  `recoil_recovery_ticks` 是沉默复位时间。第 1 发是直的，之后逐发累积；到表尾不再涨。
  TOML 子集为此加了一个类型（`[1.0, 2.0]`），只支持数字。
- **应用在方向**：`view_forward(yaw + kick.yaw, pitch + kick.pitch)`，命中和相机用同一个偏移；
  相机通过 `get_recoil_offset()` 读，所以准星和弹着点不会分家。
- **进快照的是索引**（`recoil_index`/`recoil_recovery`，协议 v8）：它们决定命中方向，必须回滚一致。
  punch 本身不进快照——它是索引的纯函数，客户端自己算（A4）。
- 沉默 24 tick（0.375 s）复位，不是松扳机就复位：点射能恢复准度，按住不放一路爬到表尾。

### 2. 判据

`recoil: shots 12 first z 1.278 fifth z 1.848` —— 12 发打 15 m 外的墙，第 5 发比第 1 发高 0.57 m；
`--test-weapons` 的“一个后坐力步进也进哈希”；`--test-snapshot` 打 `protocol v8 … 903 B, 89 B/player`。
smoke 22 项 + bench ALL GREEN；godot_check 全过（protocol 8）。

### 3. 失败与修正

- 第一次把 `recoil_recovery_ticks` 当必填键，`--test-weapons` 的“每个缺失键都报警”从 10 变成 11 而红。
  想清楚后改成可选（没有表就不需要复位计时），并加“有表则必须 > 0”的校验——比硬凑计数正确。
- 打印行 `reload 30` 一度变成 `reload 18`：后坐力测试排在换弹后面又打空了半个弹匣，
  汇总行读的是“当前 ammo”。改成在换弹断言处存快照值，汇总读那个值。

### 4. 边界

- 开镜（ADS）降低后坐力还没做：M6.11 的 `ads_*` 字段要乘到 `recoil_offset` 上。
- 半自动/自动的区分仍未做（手枪现在也是按住连发）。
- 没有后坐力恢复的视觉平滑（相机直接跟索引走，一格一格），等 M7 的视角层。
## S73 · 2026-10-24 · M6.11 表现层（hitmarker / 伤害数字 / 枪口火光 / ADS）

### 1. 做了什么

- **hitmarker 走快照，不走事件**：铁律 C 的原话是「伤害数字只走快照」。`update_weapon` 命中时把
  `hit_target/amount/zone/serial` 写在射手身上，进新的 Hit 组（5 B/人，协议 **v9**）；客户端比较
  serial 变没变判断“是不是新命中”，显示 0.3 s。打空、被墙挡、打尸体都不亮。
- **枪口火光**：同一份 `Shot` 事件在枪口生成一个 0.06 m 发光球，0.05 s 回收。
- **ADS**：`ads_recoil_scale` 乘在 `recoil_offset` 上（步枪 0.55），FOV 75→55 是纯表现。
- **闸门**：`godot_check` 新开 `== hitmarker ==`——房主**真的瞄准并开枪**（不是测试钩子），
  命中后打出 `hitmark serial=1 amount=25 target=1 zone=1`。

### 2. 判据

- `--test-damage` 4 条：命中记录字段、第二发 serial+1、打空不动 serial、ADS 弹着点更低
  （`plain fifth z 1.848` → `ads fifth z 1.592`）。
- `--test-snapshot`：`protocol v9 … 953 B, 94 B/player`；`godot_check` 全过（hitmark 一段）。

### 3. 失败与修正

- 房主的 hitmarker 永远不亮：`local_player()` 对房主返回**预测**世界里的自己，而那个世界只有一个玩家，
  伤害发生在权威世界。改成：有权威世界（host/solo）时读它，客户端才读预测。
  ——这类“两个世界”的坑值得记：房主的 HUD 数值可以来自预测（会被快照修正），但**命中记录**在预测里
  永远是 0，因为它没打中过任何人。
- 打空的测试最初一直“命中”：靶子挪走后 hitbox 历史还留着旧位置（回溯正好读到）。先空跑 8 tick 再打。

### 4. 边界

- 没有命中音效（M7 音频）。
- 伤害数字是命中者自己看到的；没有“被打的人看到的受伤方向提示”。
- 枪口火光只有自己看得到（事件还没上网络，M7）。
## S74 · 2026-10-24 · M6.12 验收：E3 没达标

### 1. 做了什么

- **TTK 表**：把伤害公式提成 `shot_damage()`（shoot.cpp 与表共用一份，防止各算一套），
  `--test-damage` 用真实 `apply_damage` 循环算出 4 甲 × 3 部位的表，钉住上界 1.2 s 与结构关系。
- **E6**：`--test-net` 新增 `[shotloss]` —— 30% 丢包、20 ms 延迟，客户端朝 2 m 外的目标开火，
  断言「服务器掉血 + 客户端拿到 hitmark」，并且**丢包数必须 > 0**（否则这个检查什么都没证明）。
- **验收记录** `docs/audits/2026-10-M6-acceptance.md`：E1–E9 逐条证据 + E9 手测清单。

### 2. 判据

`[shotloss] loss 30% dropped 187 target 100->0 hitmark 4/25`；
`ttk rifle chest none 0.28s/4 light 0.38s/5 regen 0.38s/5 heavy 0.47s/6`；
bench `p99 2.40 µs / max 6.00 µs / allocs 0`。

### 3. 失败与修正（这一节是本轮的重点）

- **E3 未达标**：目标躯干 0.6–1.2 s，实测裸胸 0.28 s、重甲 0.47 s。25 伤害 × 600 rpm 两处叠加，
  再被 `DESIGN-09`（平射必中头 → 一枪 125）放大。**没有偷偷调数据让它“过”**，记成 `DESIGN-10`，
  验收记录里写了差距和三种调法；闸门只钉上界与结构关系。
- hitmarker 在 30% 丢包下最初永远不亮：`apply_snapshot` 发现“模拟状态相同”就直接丢弃快照，
  而命中记录不算模拟状态，于是被一起丢了。改成“状态相同也要把表现字段抄过去”。
  这类 bug 只有把整条链路跑一遍才会露头，单元测试永远测不到。
- `printf` 里的 `30%` 被当成格式符（`-Werror=format`），改成 `30%%`。

### 4. 边界 / 下一步

- M6 **不宣布关闭**：E3 等调参、E9 等手测。机制层面没有已知缺口。
- 下一步优先级：先拍 DESIGN-09/10（它决定 M7 的手感基线），再手测 E9。
## S75 · 2026-10-24 · DESIGN-09/10 定案（长 TTK）

### 1. 做了什么

所有者确认原始设定是**长 TTK**，短 TTK 会让玩法乱套。所以两条一起定：

- **DESIGN-09（几何）**：眼高从 0.9 压到 **0.82×身高 = 1.164 m**，头段起点从 0.82 抬到
  **0.88×身高 = 1.25 m**。于是**平射同身高对手是躯干**，抬头 1.15° 才是头；两者之间留 8.6 cm，
  后坐力抬枪不会把躯干自动变成爆头。两个数都进了 `movement.toml` 并计入 params_hash。
- **DESIGN-10（数值）**：步枪 **21 伤害 / 400 rpm**。躯干 5 发（0.62 s），重甲 8 发（1.09 s），
  **爆头 21×5 = 105 仍是一发**。腿 0.94–1.41 s（刻意的，契约目标写的是躯干）。

顺带把“部位分段的阈值”和“眼高”从代码常量搬进数据（`[damage] head_band/chest_band`、
`[base] eye_height_scale`）：以前是 `hitbox.cpp` 里的 0.82/0.45 和 `pmove.h` 的 0.9，
改手感要重编，和铁律 I 冲突。

### 2. 判据

```
ttk rifle head  none 0.00s/1 light 0.00s/1 regen 0.00s/1 heavy 0.00s/1
ttk rifle chest none 0.62s/5 light 0.78s/6 regen 0.78s/6 heavy 1.09s/8
ttk rifle legs  none 0.94s/7 light 1.25s/9 regen 1.25s/9 heavy 1.41s/10
ttk target 0.60-1.20 s (chest): none 0.62 light 0.78 regen 0.78 heavy 1.09
```

新闸门 `a level shot is a body shot, not a headshot`（`--test-hitbox`）：直接断言
`eye_height_scale + 0.05 < head_band`，谁把这两个数调反了都会红。

### 3. 失败与修正

- 先试了 15 伤害：躯干 0.66 s 达标，但**爆头变成 2 发**（15×5=75），把“一枪爆头”的判定特色弄丢了。
  改成 21：爆头 105 仍一发，躯干仍在 0.62 s。这是“长 TTK 不等于放弃爆头”的具体取舍。
- 改完眼高后 `--test-hitbox` 的蹲下头段测试落在边界（0.88×0.91=0.8008，测试点正好 0.80）→ 用
  `(head_band + 0.02)` 表达，而不是写死 0.80。
- `--test-damage` 里原来满是 `4 发 / 25 伤害 / 7 次射击` 的字面量，一调数据全红。改成**从数据推导**
  （`rounds_to_kill(zone, armor)`、`1 + (window-1)/interval`），以后调参只动 toml。

### 4. 边界 / 下一步

- E3 达标，E1–E8 全部有证据；**只剩 E9（两人局域网 10 分钟）**。
- 这些数值是“能落在目标区间”的起点，不是打磨过的：M7 手感验收还会动。
## S76 · 2026-10-24 · M6 收尾：债改期 + 教训 17 + 10 分钟 soak

### 1. 做了什么

- **DEBT 里 11 条 lead=M6 的账全部改 lead=M7**（AUD-21/22/24/28/30、DEBT-01/05/09/12/20/23）。
  理由写进契约 §7：它们都是 v0.1 打磨项（网络零分配、可靠流回绕、admission TTL、
  demo 初始状态、手感字面量、RTT 测量……），没有一条阻塞 M6 的射击链路。
  **没有一条是“假装修好”**：状态还是 open，只是换了个 lead。
- **契约 §8 关闭清单**逐条填了（E9 那格留空），预算回填 budgets.md。
- **教训 17 + `docs/pitfalls/level-shot-headshot.md`**：几何会静默废掉数值设计。
  眼高落在头段里 → 平射永远爆头 → 长 TTK 不存在。这条值得单开一篇，因为每一层单独看都合理。
- **`tools/net_soak.ps1`**：E9 的自动化半边。两个客户端 + 服务器跑满 N 秒（默认 600），
  断言 join 2/2、无 far 命令、两边都持续收到快照（不是“进程没崩”这种弱判据）。

### 2. 判据

soak 的输出（600 s 跑完才有意义）：`soak 600s joins=2 accepted=… late=… far=0 snapshots=…/…`。
它不进 smoke：10 分钟属于验收，不属于每次提交。

### 3. 失败与修正

- `job_output` 的默认等待上限（120 s）比 10 分钟的 soak 短，第一次拉取直接超时；
  改成后台跑 + 非阻塞轮询。这不是产品 bug，是**我调用工具的方式**错了。

### 4. 边界 / 下一步

- M6 剩下的两件事都**不在我手里**：E9 需要两个人打一局；M7 契约需要所有者签字。
- M7 契约的草案我可以写，但按本项目的规矩，未签字的契约不能开工。

**soak 结果（补记）**：`soak 600s joins=2 accepted=76538 late=0 far=0 snapshots=38268/38267` ——
10 分钟里两个客户端各收到约 3.8 万份快照，0 迟到、0 超前命令。链路不会随时间腐烂。
## S77 · 2026-10-24 · M7 契约草案 + “怎么绕 GPL”写清楚

### 1. 做了什么

- `docs/contracts/M7.md`（**draft**）：音频 / 第三人称动画 / 视角模型 / 枪感旋钮 / 玩家反馈，
  6 条退出判据，8 个单任务。**没有开工**——按规矩，未签字的契约不能动手。
- §4 D1 把“仿 TacZ 手感”的边界写成可执行的：净室（ADR 0007 原文）——读源只为确认**有哪些旋钮、
  表怎么组织**，事实记进 `assets-licenses.md` 只读表；然后**关掉源**按规格自写；
  不抄代码、不抄数值、不搬资产。判据是 `check_sources.ps1` + 台账有记录。
- BACKLOG 加 FEAT-15（9 个枪感旋钮）与 FEAT-16（视角模型与枪口对齐）。

### 2. 判据

`python tools\doc_check.py` 0 fail；契约草案本身不是闸门。

### 3. 关键判断（写在这里，免得以后忘）

- **“绕过 GPL”只有一种解释能成立**：不复制表达、只取事实、自己实现（净室）。
  “抄进来再改改”“换个变量名”不是绕过，是侵权，而且 `check_sources.ps1` 会当场红。
- **TacZ 的数值搬不过来也不该搬**：它是 Minecraft 20 Hz + 手柄 + 强辅助瞄准，我们是 64 Hz + 鼠标。
  能搬的是**结构**（状态 × 距离的扩散矩阵、按状态配置的后坐力、开镜过渡时间）。
- **E9 不能外包给一个模组**：E9 是“我们这版打 10 分钟不崩、手感能接受”，TacZ 只能当参照物。

### 4. 边界 / 下一步

- 等两件事：E9 手测、M7 签字。
- 签字后第一个任务是 M7.1（枪感旋钮）——它不依赖任何资产，也能立刻让手感可调。

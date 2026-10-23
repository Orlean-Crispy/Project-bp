# 慢客户端永远追不上服务器（AUD-20）

**症状**：一次超过 110 ms 的卡顿（窗口拖动、GC、切桌面）之后，客户端还在跑、画面还在动，
但服务器把它的每一条命令都判成 Late，两个人再也同步不上，只能退出重进。

**根因**（两层，缺一不可）：

1. `godot/main.gd` 的 tick 循环：

   ~~~gdscript
   while accumulator >= TICK and ticks < 5:
       sim.step_network()
       accumulator -= TICK
       ticks += 1
   if ticks == 5:
       accumulator = 0.0      # ← 把没跑完的时间整段丢掉
   ~~~

   卡 300 tick 时，剩下的 295 tick 被清成 0：**相位**也一起被抹掉（余量本该是 fmod(acc, TICK)）。

2. `NetClient` 只有"领先纠正"（`ahead > min+3` 时跳一帧），没有"落后纠正"。
   客户端的 tick 轴一旦落在服务器已执行 tick 的后面，命令盖的 tick 已经过去，
   `InputWindow` 全部返回 Late，而本地 tick 和服务器一样是每 tick +1，**差距永远不会缩小**。

**为什么闸门没发现**：`--test-net` 从来没有"客户端停一会儿"的用例。30% 丢包测的是
丢包下的收敛，卡顿不是丢包：本地的世界根本没停，只是停在了过去。

**修法**：

- `NetClient::receive()`：快照比本地轴超前 > 4 tick（lead 4 + slack 3 之外）时 `catch_up()`
  ——重锚时钟到 `snap.tick + 3`、清掉会变 Late 的 pending 命令、用空输入补跑缺的 tick
  并记进 history/hitbox（回滚仍然可用）。
- `main.gd`：保留 `fmod(accumulator, TICK)` 的余量，把丢弃的整 tick 数计数并打印。

**闸门**（`--test-net` 的 `[catchup]`）：跑 200 tick → 客户端既不 send 也不 receive 地冻 300 tick
（同时把 socket 里积压的旧快照丢掉：真实 UDP 缓冲区不会留 4.7 秒的数据）→ 恢复 64 tick。
要求 `catchups >= 1`、`behind >= 268`、Late 计数在恢复 8 tick 后**不再增长**、accepted 继续涨、
玩家位置继续前进、`full_states == 1`（没有偷偷重连）。

实测：`[catchup] freeze 300 behind 293 catchups 1 accepted 196->203->259 late 0->0 settled 1 tick 206->570 moved 1`。

**教训**：时钟纠正只做一半等于没做。领先和落后是同一个问题的两侧，写闸门时要问
"如果本地轴不是偏前而是偏后，哪条代码负责把它拉回来？"

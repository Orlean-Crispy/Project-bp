# 走了的人还占着房间（AUD-27）

**症状**：两槽房（房主占 slot 0，只剩 1 个客户端位）。第一个人进来、正常退出；第二个人
（或第一个人再来）就永远是 `RoomFull`，只能重启房主。

**根因**：槽位归 **MatchHost**（TCP 信令）所有，链路的生死归 **NetServer**（UDP 游戏面）所有，
中间没有任何人把"这个人走了"这件事告诉对方。

- `MatchHost::release()` 存在，但只在握手半途失败时被调用（R4 修的那条路径）。
- `NetServer::remove_client()` 会摘链路、把玩家置 inactive，**但没带上 client_id**，
  所以它想说"谁走了"也说不出来——`Link` 里根本没有存 id。
- 于是正常离开（关窗口、5 秒超时）之后，MatchHost 的 `m_slots` 仍然记着那个 id。

**为什么闸门没发现**：`[match]` 只测"两人能进、第三人被拒"。它没测"第一个人走后再进"。
这类"资源归还"必须成对测：**占用**与**释放**各一条，否则只证明了一半。

**修法**（一条链，三层各改一处）：

1. `NetServer::add_client(..., const ClientId* who)` 记住 id；
2. `remove_client()` 把 id 推进 `m_departed`，`drain_departed()` 交给调用方
   （不在 `step()` 里回调 MatchHost：那是两个状态机互踩）；
3. 网关每 tick 在 `send()` 之后 drain，并对每个 id 调 `MatchHost::release()`。

**闸门**（`[slots]`）：`players=2, reserved=1` 的房；A 进（slot 1）→ B 被拒（RoomFull）→
模拟链路被摘 → 必须吐出 A 的 id → release 之后 B 能进 slot 1 → 再连续 3 轮 join/leave；
外加"匿名链路（没有 id）不许进 departure 队列"，否则会释放到那个全零 id 上。

实测：`[slots] first 1 full 1 reported 1 freed 1 cycles 3/1 anonymous 1`。

**教训**：任何"有上限的资源"（槽位、票据、admission 队列）都要同时有占用与归还的闸门。
只测"满了会拒"，等于没测"走了能还"。

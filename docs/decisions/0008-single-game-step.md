# ADR 0008 · 唯一权威 `game_step`

**日期**：2026-11-01 · **状态**：已采纳 · **来源**：外部审查 AUD-36

## 问题

一个权威 tick 被拆给四个调用方各自拼接：服务器 `NetServer::step`、客户端 `NetClient::step_local`、
solo 的桥 `SimWorld::step`、demo 回放。它们都在 `World::step` 之后自己调 `update_weapon`，
参数还各不相同（`tick - 1`、`rewind_ticks(0)`、histories 给还是不给）。

后果不是“代码不好看”，是**行为分叉**：S81 的换弹 bug 只存在于真实顺序里，因为测试直接调函数。

## 决定

**一个函数** `game_step(World&, inputs, dt, GameStep)`，顺序写死：

```
1. movement    World::step        —— 含 held/edge 镜像（reload_edge 在这里算）
2. weapon      update_weapon      —— 每 tick 一次，tick 用 world.tick - 1（命令所属 tick）
3. vitals      tick_vitals           —— 在武器之后：同 tick 的伤害必须先落地（S93）
4. hitbox      HitboxHistory::record —— 在武器之后，因为射击会回溯到旧帧
```

**仍然在 `game_step` 之外**（并写明理由）：

| 阶段 | 位置 | 为什么不在里面 |
|---|---|---|
| 重生 `tick_respawn` | 服务器 `NetServer::step` 的后置段 | 客户端不预测重生；它只从快照看到结果 |
| 伤害结算 | `update_weapon` 内部 | 已经在正确的位置，且必须与武器同 tick |
| 快照发送 / 输入窗口 | 会话层 | 与模拟无关 |

## 参数

```cpp
struct GameStep {
    EventQueue* events = nullptr;      // 表现事件（服务器传 null）
    HitboxHistory* histories = nullptr; // 每玩家一个；solo 传 null
    int history_count = 0;
    float rewind = 1.0f;               // rewind_ticks(单程延迟)
    ShotReport* report = nullptr;      // 只给诊断用，玩家 0
};
```

## 判据

- `--test-damage` 的换弹/射击块走 `game_step`（与服务器同一个入口），不再自己拼接。
- 服务器 200 tick 的 `state_hash` 与收拢前**逐 tick 一致**（行为不变，只换入口）。
- 客户端预测与服务器在同一输入下 20 tick 内位置一致（既有 `--test-net` 覆盖）。

## 不做的事

- 不趁机改规则、不改阶段顺序。顺序调整必须另开变更（外部审查的原话：不要与重构混在一起）。
- ~~demo 回放暂不接 `game_step`~~ —— AUD-39（S92）已接上，demo 走同一入口。
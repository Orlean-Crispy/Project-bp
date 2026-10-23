# ADR 0006 — 穿透、护甲结算与热重载

- 日期：2026-10-24
- 状态：**已定**（D1 / D2 / D3 由项目所有者口述）
- 取代：`docs/decisions/0003-health-armor.md` 中"吸收比例 66%"与"结算顺序待确认"两部分
- 影响文件：`sim/game/combat.*`、`sim/data/combat.toml`、`docs/contracts/M6.md`、`sim/collision/map_parse.*`（M6.5 才动）

---

## D1 — 穿透：武器决定，地图能否决

**规则**

1. 地图只有两种表面：`_penetrable 0`（默认，混凝土/砖）与 `_penetrable 1`（薄木/铁皮）。
2. `_penetrable 0`：**任何**武器都打不穿，射线停在这里。地图拥有一票否决。
3. `_penetrable 1`：只有 `penetration_count >= 1` 的武器能穿过；`0` 的武器（手枪）照样停。
4. 每穿过一层，伤害乘 `penetration_damage_scale`（默认 0.7，进 weapons.toml），整数运算。
5. 同一条射线最多穿过 `penetration_count` 层；穿完后继续推进命中判定。

**为什么不让地图说了算**：若"地图开了就能穿"，手枪也能穿木板，武器的身份被地形抹平；
若"只看武器"，混凝土也挡不住步枪，掩体概念消失。武器定行为、地图定材质，两边各管一件事。

## D2 — 护甲：按命中部位结算，爆头是真伤

| 命中部位 | 结算 |
|---|---|
| 头 | **真伤**：直接扣 HP，护甲一点不掉（部位倍率 ×5 已在 `hitbox` 里） |
| 胸 / 腿 | **先扣护甲池**，护甲扣完后的溢出部分才扣 HP（`absorb_percent = 100`） |

- 护甲数值沿用 ADR 0003：小甲 25 / 再生甲 25 / 大甲 50；再生甲 5 s 无伤害后开始，10 s 回满。
- 全部整数运算（确定性要求）；`absorb_percent` 保留为数据字段，默认 100。
- 实现：`apply_damage(vitals, damage, zone, params)`；`HitZone` 因此从 `hitbox.h` 移到 `combat.h`
  （它是伤害概念，命中盒只负责判断高度落在哪一段）。

**平衡后果（实测，不是估计）**

- 重甲躯干 25 伤害/发 @600 RPM：**6 发 = 0.60 s**，仍在计划书 0.6–1.2 s 内，但贴着下限。
- 爆头 25 × 5 = 125 ≥ 100 HP：**一枪击杀**，有没有护甲都一样。
  这是"瞄准优先"的明确声明；如果将来嫌爆头太强，改的是 `headshot_mult` 或 HP，不是这条规则。

## D3 — 热重载：暂不做，文档改成真话

- 现状：`movement.toml` / `combat.toml` 在**启动时读一次**，算进 `params_hash` 发给对端做一致性校验；
  运行时改文件**不会**生效，也没有重读路径。
- 决定：**不实现** `reload()`，把"运行时可重载"的说法从文档里删掉；调参靠重启游戏（几秒）。
- 理由：M6 真正要反复调的是还没建出来的 `weapons.toml`；现在做只能覆盖 movement，
  W1 之后还要改同一个函数。等武器表落地、确有调参痛点（例如每次重启超过十几次），
  一次性做"movement + weapons 都重读"的 reload，一个机制管两个文件。
- 记账：`docs/DEBT.md` 的 `DEBT-22`（lead M7），带一条可执行的关闭判据。

---

## 回滚方式

- D2 回滚：`combat.toml` 把 `absorb_percent` 改回 66，并让 `apply_damage` 忽略 `zone`（不可回到 66% 与真伤并存）。
- D1 回滚：`docs/decisions` 写新 ADR 覆盖本节；代码里穿透只有 `weapons.toml` 一个开关。

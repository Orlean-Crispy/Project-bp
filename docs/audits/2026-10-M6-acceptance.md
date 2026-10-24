# M6 验收记录（2026-10-24）

> 契约 `docs/contracts/M6.md` §3 的 E1–E9 逐条证据。数字全部是本机跑出来的，命令写在每行后面。
> **E9（两人局域网真打 10 分钟）需要人**，见 §3。

## 1. 判据与证据

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| E1 | 固定靶：正对躯干必中、偏出 1 个盒宽必不中 | ✅ | `--test-hitscan`：`[hitscan] chest 1 face 1 wide 1 edge 1 crouch 1 wall 1 range 1`（14 项） |
| E2 | 三段倍率 ×5 / ×1 / ×0.7，误差 0 | ✅ | `--test-damage`：头 `125`、胸 `25`、腿 `18`（25×0.7=17.5→18 整数） |
| E3 | TTK 落 0.6–1.2 s（躯干，100 HP，三甲复算） | ✅（S75 调参后） | 见 §2：实测胸 0.62 / 0.78 / 0.78 / 1.09 s |
| E4 | 打 30 伤害后两端 HUD 同步 70 | ✅ | `godot_check`：`death seq: hurt@… dead@… back@…`，客户端 HUD 70→0→100 |
| E5 | 死亡后输入忽略、重生回出生点 | ✅ 部分 | `--test-damage` 6 项（死后不能开火、重生是新玩家、尸体不挡枪）；**比分留 M8** |
| E6 | 30% 丢包下射击仍能命中并结算 | ✅ | `--test-net`：`[shotloss] loss 30% dropped 187 target 100->0 hitmark 4/25` |
| E7 | 全闸门绿：smoke + bench + doc_check | ✅ | `tools\smoke.ps1` 22 项 ALL GREEN；`doc_check` 9 ok / 0 warn / 0 fail |
| E8 | `0 allocs/tick`，p99 ≤ 4 ms | ✅ | `--bench 1000 --players 10`：p99 **2.40 µs**、max 6.00 µs、`allocs 0.00 / tick` |
| E9 | 两人局域网真打 10 分钟不崩、无瞬移 | ◐ **自动化半边通过，人手待测** | `tools/net_soak.ps1`：`soak 600s joins=2 accepted=76538 late=0 far=0 snapshots=38268/38267`；手感见 §3 |

## 2. E3：从 0.28 s 调到 0.62 s（S75 定案）

`--test-damage` 打印（10 m，步枪，整数伤害走真实 `apply_damage`）：

```
ttk rifle head  none 0.00s/1 light 0.00s/1 regen 0.00s/1 heavy 0.00s/1
ttk rifle chest none 0.28s/4 light 0.38s/5 regen 0.38s/5 heavy 0.47s/6
ttk rifle legs  none 0.47s/6 light 0.56s/7 regen 0.56s/7 heavy 0.75s/9
ttk rifle head  none 0.00s/1 light 0.00s/1 regen 0.00s/1 heavy 0.00s/1
ttk rifle chest none 0.62s/5 light 0.78s/6 regen 0.78s/6 heavy 1.09s/8
ttk rifle legs  none 0.94s/7 light 1.25s/9 regen 1.25s/9 heavy 1.41s/10
ttk target 0.60-1.20 s (chest): none 0.62 light 0.78 regen 0.78 heavy 1.09
```

腿超出 1.2 s（0.94–1.41）是刻意的：打腿本来就该慢，契约的 TTK 目标写的是**躯干**。

**当时的两个问题**：

1. `DESIGN-09`：眼高 1.278 落在头段起点 1.164 **以上**，所以平射必中头 —— 一枪 125，长 TTK 不可能存在。
2. `DESIGN-10`：`damage_near = 25`（4 发致死）× `rpm = 600`（6 tick）→ 躯干 0.28 s。

**定案（按“长 TTK”的原始设定）**：

- 眼高压到 **0.82×身高 = 1.164 m**，头段起点抬到 **0.88×身高 = 1.25 m**，两者都进了
  `movement.toml`（`[base] eye_height_scale`、`[damage] head_band/chest_band`）并计入 params_hash。
  于是**平射是躯干**，抬头 1.15° 才是头；余量 8.6 cm，后坐力抬枪不会把躯干自动变成爆头。
- 步枪 **21 伤害 / 400 rpm**：躯干 5 发（0.62 s），重甲 8 发（1.09 s），**爆头 105 仍是一发**。

新表（`--test-damage`）：

```


## 3. E9 手测清单（需要两台机器或两个人）

**自动化半边已完成**：`tools/net_soak.ps1` 让两个客户端 + 服务器跑满 600 s，断言双方持续收到快照
（600 s × 64 Hz ≈ 38k 份，实测 38268/38267），0 条 late、0 条 far 命令。这证明**链路不会随时间腐烂**；
证明不了手感。

前置：`demo.bat` → Host（端口 27115）与 Join（同网段 IP）。

- [ ] 两人各打 10 分钟，不崩、不出现瞬移或卡在墙里
- [ ] 对枪时命中反馈与画面一致（开火有曳光/枪口火光，命中才亮 hitmarker）
- [ ] 死亡 5 s 后回出生点，HUD 显示 `DEAD` 后恢复
- [ ] 换弹时按住扳机不会“隔空开火”；打空有明确的换弹动作
- [ ] 后坐力：连续射击弹道可预期（往右上爬），停火 0.4 s 后恢复
- [ ] 丢包观感：拔网线 1 s 再插上，人物回到正确位置（无长期漂移）

记录格式：`日期 / 构建 hash / 地图 / 异常现象 / 复现步骤`，写进 PROGRESS 的下一节。

## 4. 结论

M6 的**机制**全部完成（射击链路、命中判定、回溯、穿透、伤害、弹药、死亡重生、后坐力、表现层），
E1/E2/E4/E5/E6/E7/E8 有闸门证据；**E3 的数值没达标，E9 待手测**。
因此 M6 不宣布关闭：契约 §8 的关闭清单要等这三件事有结论（E3 调参、E9 手测、M8 比分）。
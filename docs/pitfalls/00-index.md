# 事故索引

| # | 文件 | 一句话 |
|---|---|---|
| 1 | [toolchain-nonascii.md](toolchain-nonascii.md) | MinGW ld 读不了中文路径，只能在物理 ASCII 路径下链接 |
| 2 | [minkowski-support.md](minkowski-support.md) | 盒体扫掠的支撑距离用 max 而非 min，导致全部穿透 |
| 3 | [contact-epsilon.md](contact-epsilon.md) | 进入分数被 epsilon 顶成负数 → 贴墙每 tick 挤入 19 mm |
| 4 | [tick-deficit.md](tick-deficit.md) | 时钟纠正只做了"领先"，卡顿后客户端永远落后（AUD-20） |
| 5 | [slot-leak.md](slot-leak.md) | 槽位只在握手中断时释放，正常离开后房间永久 RoomFull（AUD-27） |
| 6 | [level-shot-headshot.md](level-shot-headshot.md) | 眼高落在头段里 → 平射永远爆头，长 TTK 根本不存在（DESIGN-09） |

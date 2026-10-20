# 坑：Windows 上 bind 成功不代表端口是你的

- 日期：2026-10-20
- 症状：Godot 客户端连信令端口，报 "receive failed"（WSAECONNRESET）；服务端日志一切正常
- 代价：两次 headless 复现 + 一次误判（以为是自己写的 recv 逻辑坏了）

## 现场

net_loop 的 [match] 闸门（同进程）全绿，两进程 net_selftest 也全绿；但 Godot 客户端连不上。
排查顺序是：先怀疑 recv、再怀疑协议、最后才查端口。

```
Get-NetTCPConnection -LocalPort 27015
  State   OwningProcess
  Listen  6500
Get-Process -Id 6500
  ProcessName : AppleMobileDeviceService
```

**27015/tcp 被 Apple 的移动设备服务占着。** 我们的 `TcpListener::listen` 里写了
`SO_REUSEADDR`——在 POSIX 上它只表示「可以复用 TIME_WAIT」，**在 Windows 上它允许第二个 socket
绑上一个已经有人在 listen 的端口**。于是：

- `bind` 返回 0，`getsockname` 也报 27015，`listen` 成功 → 我们以为房主在线；
- 客户端连上去，实际被 Apple 的服务接走（或者我们的 accept 永远拿不到），
  它不回我们的 HELLO，直接关连接 → 客户端 recv 得到 RST。

## 修法

1. `TcpListener::listen` 在 Windows 改用 `SO_EXCLUSIVEADDRUSE`：绑不上就返回错误，
   **让失败可见**，而不是静默共享。POSIX 保留 `SO_REUSEADDR`。
2. 默认信令端口 27015 → **27115**（UDP 27116）。v0.4 里写的 27015 是「不和游戏端口撞」的
   好建议，但没查过本机占用；端口号是环境事实，不是设计常量。

## 判据

- `[match]` 闸门仍绿（同进程，不经过 27015）。
- 两个 headless Godot 进程：host 打 `signal slot 1 resumed 0 / peer joined slot 1 / peers 2`，
  client 打 `joined slot 1 / remote slot 0 visible`。
- 端口被占时 `sim.host()` 返回 false，菜单打印 `host failed`——不再假装成功。

## 教训

> 在网络代码里，「调用返回成功」是最弱的一种证据。
> 绑定端口之后，唯一能证明端口属于你的是**独占语义**和一个**端到端握手**，而不是返回值。

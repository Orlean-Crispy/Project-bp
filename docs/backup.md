# 存档与备份（2026-10-19）

## 结论

**现在就存，不要等 v0.1。** v0.1 还要几周，而整个仓库目前只有约 2 MB：
一次磁盘故障就会丢掉 28 个会话的全部历史。git 历史本身是存档，但它和代码在同一块盘上，
所以还需要一份**离开这块盘**的副本。

## 三层（3-2-1 的简化版）

| 层 | 做什么 | 现在状态 |
|---|---|---|
| 1. 版本历史 | 每个功能一个 commit；里程碑打 tag | 已有 28 个会话的提交 |
| 2. 本地离线副本 | `git bundle` 全历史单文件，放到另一块盘/U 盘 | 每次收工跑 `tools\backup.ps1`；最新一版在 `G:\BreakpointBackups\`（verify 通过） |
| 3. 异地副本 | GitHub 私有仓库，或云盘同步 bundle | ✅ 已推送 main + tag |

## GitHub

- 仓库：<https://github.com/Orlean-Crispy/Project-bp>（私有）。**2026-10-23 由 `unknow` 改名**，
  GitHub 仍会重定向旧地址，但 push 会打 "This repository moved" 警告；本机 remote 已改成新名。
- 本机 remote：`ssh://git@ssh.github.com:443/Orlean-Crispy/Project-bp.git`
  （SSH 走 443，绕过本机指向 7897 的 HTTPS 代理）。
- 首次推送时远端已有 `Initial commit`（LICENSE + README），本地用
  `git merge --allow-unrelated-histories` 合并；README 保留本项目的版本。
- 日常推送：

  ```powershell
  git push
  git push --tags
  ```

- 网络备注：全局 `http.proxy` 指向 `127.0.0.1:7897`，**Clash Verge 没启动时 HTTPS 会失败**；
  SSH 443 可绕过，但本机到 GitHub 会间歇性 reset，失败就重试 2–3 次。
  需要稳定 HTTPS 时先启动 Clash Verge。
- 凭据用 SSH key 或 Git Credential Manager，**不要**写进仓库或脚本。
- 网络差时先 `tools\backup.ps1` 出 bundle，再找机会 push；push 失败不影响本地历史。

如果 GitHub 推送长期不稳，可以加一个 Gitee 私有镜像，或用坚果云/OneDrive 同步
`G:\BreakpointBackups` 目录——bundle 是单文件，同步即可。

## 恢复

```powershell
git clone G:\BreakpointBackups\breakpoint-20261019.bundle breakpoint-restore
cd breakpoint-restore
git remote set-url origin <新的远端地址>   # 需要时
```

bundle 自带全部 refs（main + tags），clone 出来就是完整历史，不需要原机器。

## 节奏

- **每个里程碑**（M5 收尾、M6 首版、v0.1）打一个 tag：`git tag -a v0.1 -m "..."`。
- **每周**跑一次 `tools\backup.ps1`，把新 bundle 复制到另一块盘/云盘。
- 每次 push 成功后，GitHub 就是最新的异地副本；tag 也要 push（`git push --tags`）。

## 注意

- **bundle 不要放进仓库**（已在 .gitignore 里忽略 `*.bundle`），否则每次备份都会把旧备份提交进去。
- 将来加入贴图/音频等二进制资产时，用 **Git LFS** 或单独的资产包；不要把几百 MB 直接塞进 git。
- 仓库保持**私有**直到决定发布；CC BY-NC-ND 的资产（如 TacZ 模型）不得进构建产物（铁律 H）。
- 密钥、token、本机路径不要进仓库（铁律 G）。

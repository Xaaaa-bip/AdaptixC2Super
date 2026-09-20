# systemd 托管（adaptixserver + axrelay）

本目录三件与**现网跑着的那份逐字节相同**（md5 见下），可直接投放。
投放前先读「换环境要改什么」。

| 文件 | 现网位置 | mode | md5 |
|---|---|---|---|
| `adaptixserver.service` | `/etc/systemd/system/adaptixserver.service` | 600 | `b1c9122932d82d34a668e4d37f906518` |
| `axrelay.service` | `/etc/systemd/system/axrelay.service` | 600 | `f166f9d51747cf515526be44ab0a49bd` |
| `axstart.sh` | `/root/axstart.sh` | **700（必须有 exec 位）** | `f3f3d8d2900b35bc588ef0ef019c87f5` |

```bash
install -m 600 adaptixserver.service /etc/systemd/system/adaptixserver.service
install -m 600 axrelay.service      /etc/systemd/system/axrelay.service
install -m 700 axstart.sh           /root/axstart.sh
systemctl daemon-reload
systemctl enable --now axrelay.service adaptixserver.service
```

★ 用 `install` 而不是 `cp` 是为了**一次原子落位**（`install` 内部是写临时文件 + rename，
天然是新 inode）。**运行中的程序与它的 `.so` 绝不能用 `cp` 原地覆盖** —— 进程 mmap 的映射页
会失效，实测把长跑实例打成 GC SIGSEGV（见交付 README §0.4-1）。

## 为什么这样写（三条都是踩出来的，别「简化」掉）

1. **`ExecStart=/root/axstart.sh`，不是 `./adaptixserver`。**
   profile 与 axscripts 的路径按**进程 cwd** 解析，`axstart.sh` 里的 `cd /home/ad/dist` 是必需的。
2. **`Restart=always`，不是 `on-failure`。**
   本机已知有**四种退出码 0 的死法**，`on-failure` 对它们一律不生效 —— 配了等于没配。
   ★ 代价：`InitJWT()` 每次启动**现生成** JWT 签名密钥 ⇒ **每自动重启一次，所有在线
   操作员被踢回登录页**（token 前端存着也没用，密钥一换即全 401）。这是拿「自愈」换「会话不保」。
3. **`Type=simple` + `exec`** ⇒ `MainPID` 就是 `adaptixserver` 本体（不是一层 shell），
   `systemctl stop` 能直达。日志走 `StandardOutput/Error=append:`，**不进 journald**
   ⇒ `journalctl -u` 里看不到业务日志，去 `/home/ad/dist/server.log`。
   ★ 但 `journalctl -u adaptixserver` 仍要看 —— 启动失败（如 exec 位丢失）只在那里报。

## 换环境要改什么

| 位置 | 现值 | 说明 |
|---|---|---|
| 两个 unit 的 `WorkingDirectory` / 日志路径 / `ExecStart` 路径 | `/home/ad/dist` | 部署根目录变了就整批替换 |
| `axrelay.service` 的 `-key` | `/etc/axrelay/relaynode.key` | ★★ **换 key 就换 peer id，已发出去的 agent 地址全废**。迁移时要把 key 一起带走 |
| `axstart.sh` 读的 `/root/axserv.environ` | 20 项 `ADAPTIX_*` 等 | **本目录不交付此文件**（它是 `/proc/<pid>/environ` 的原样快照，含口令）。缺失**不阻止启动**，只是 linux_agent stager 表单的回退缺省值会变 |

`axserv.environ` 的正确形态：**NUL 分隔**（不是换行分隔）——
它本来就是 `/proc/<pid>/environ` 的**原样快照**，`axstart.sh` 用 `read -r -d ''` 读它。
★ 因此 **`wc -l` 会报 0 行，但它不是空文件**，别据此以为坏了。

从正在跑的进程上取一次快照即可（之后它就是静态文件，不再更新）：

```bash
cp /proc/<pid>/environ /root/axserv.environ          # ssh 会话里的本机 pid，或 ps 找 adaptixserver
chmod 600 /root/axserv.environ                       # 建议（现网是 644）；里面有口令
# 核对：只看**键名**和条数，绝不打印值
tr '\0' '\n' < /root/axserv.environ | grep -oE '^[A-Za-z_][A-Za-z0-9_]*=' | sort
tr '\0' '\n' < /root/axserv.environ | grep -c '='    # 现网 = 20
```

## 判它到底起没起（四个都会骗人的信号）

- ★★ **`PPID=1` 不能用来判「没人管」**：systemd 自己就是 PID 1，托管与否 PPID 都是 1。
  判据只认 **cgroup**：`cat /proc/<pid>/cgroup` ⇒ `0::/system.slice/adaptixserver.service`。
- `Type=simple` 的 `Active: active (running)` **不等于「在监听」**。
  别 grep 日志判就绪：`[+] The AdaptixC2 server is ready` 是**无条件**打印的，端口被占时进程也不退出。
  只认 `ss -ltn | grep ':38212'`。
- **`NRestarts` 不因手工 `stop`/`start` 归零**，`ActiveEnterTimestamp` 在停止后**仍是旧值**
  ⇒ 判「它到底在不在崩」要把 `NRestarts` + `ExecMainStartTimestamp` + `journalctl -u` 一起看。
- `dist/adaptixserver.pid` 是**陈旧**的，别信。

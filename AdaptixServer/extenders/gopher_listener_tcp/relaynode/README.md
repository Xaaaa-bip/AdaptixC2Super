# axrelay —— 自建 libp2p relay 节点

批次5（C2 IP 隐藏）的配套交付物。C2 挂在 relay 后面时，agent 手里只有
`<relay>/p2p-circuit/p2p/<c2id>` 这样的地址，看不到 C2 的真实 IP。

## 为什么必须自建

用公共 relay 等于把 **C2 的 peer id + 全时段流量**交给第三方：relay 能看到
谁在跟谁通话、通话多久、流量多大（circuit v2 的内容是端到端加密的，relay 读不到
明文，但元数据全在）。自建则只有"这台 relay 的运营者"知道这些。

## 编译

在 `gopher_listener_tcp/`（module 根，`go.mod` 的 `replace` 指向 `./third_party/go-libp2p`）里：

```sh
env GOWORK=off GOTOOLCHAIN=local CGO_ENABLED=0 GOOS=linux GOARCH=amd64 \
    go build -trimpath -ldflags="-s -w" -o axrelay ./relaynode
```

产物 ~18 MB（strip 后）。

## 部署

★ **relay 必须跑在另一台机器上**（至少另一个 IP）。与 C2 同机的话，C2 的真实 IP
可以从 relay 的流量里直接读出来，隐藏就白做了。

★ 只需要对外开 **一个 TCP 端口**（默认 4446）。

```sh
# 首次运行会生成身份文件（0600），之后每次都用它，peer id 不变
./axrelay -listen /ip4/0.0.0.0/tcp/4446 -key /etc/axrelay/relaynode.key
```

启动后打印：

```
NODE-PEER  12D3KooW...
NODE-ADDR  /ip4/<本机IP>/tcp/4446/p2p/12D3KooW...
NODE-LIMIT data=1073741824 duration=1h0m0s circuits=512 reservations=512 reservationTTL=2h0m0s
NODE-READY ...
```

把每条 `NODE-ADDR` **原样**填进 C2 的 `l2p_relays`（可填多条互为备份，C2 侧会
逐条预约、失败自动换下一条）。端口若在 NAT 后面，`NODE-ADDR` 里要换成公网地址 +
映射后的端口 —— 这一条没有自动探测。

### systemd 示例

```ini
[Unit]
Description=axrelay (libp2p circuit v2 relay)
After=network-online.target

[Service]
ExecStart=/usr/local/bin/axrelay -listen /ip4/0.0.0.0/tcp/4446 -key /etc/axrelay/relaynode.key
Restart=always
RestartSec=3
# 身份文件所在目录
StateDirectory=axrelay
User=axrelay
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

★ **`/etc/axrelay/relaynode.key` 必须备份。** 它是 peer id 的来源，丢了等于换了一台
relay：C2 的 `l2p_relays` 与所有已发出的 agent 地址**同时失效**。

## 三个非做不可的点

程序里都写死了，此处说明**为什么**，因为改错任何一个的症状都极难归因：

| 点 | 省掉的后果 |
|---|---|
| `libp2p.ForceReachabilityPublic()` | relaysvc 只在 `ReachabilityPublic` 事件上建 relay。省掉后进程**照常监听、不报任何错，但一台预约都不接受** —— 症状与"防火墙挡了"完全同形 |
| 限额放宽（默认 `2m` / 128 KiB） | C2 会话必然超过 128 KiB ⇒ **能连上，几十秒到几分钟后随机断**。实测：默认档传 200000 B 得 `read=0 err=stream reset`；放宽到 1h/1GiB 得 `read=200000 err=<nil>` |
| 身份持久化 | relay 重启即换 peer id ⇒ C2 起不来 / agent 连不上，与"配置写错"完全同形 |

★ 放宽 relay 只是**一半**：circuit v2 的限额是**协商**出来的，C2 侧的
`Reservation.LimitData` / `LimitDuration` 也不得小于这里的值（C2 侧由
`pl_libp2p.go` 的 `axLibp2pReserveLoop` 负责，它按 relay 给的值校验并告警）。

## 参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `-listen` | `/ip4/0.0.0.0/tcp/4446` | 多个用逗号分隔 |
| `-key` | `relaynode.key` | 身份私钥；★ 必须保留 |
| `-limit-data` | `1<<30`（1 GiB） | 单 circuit 数据上限；上游默认 `1<<17` |
| `-limit-duration` | `1h` | 单 circuit 时长上限；上游默认 `2m` |
| `-max-circuits` | `512` | 并发 circuit；上游默认 `128` |
| `-max-reservations` | `512` | 并发预约；上游默认 `128` |
| `-reservation-ttl` | `2h` | 预约有效期；C2 侧按 `Expiration/2` 续约 |

★ **不要**把 `-max-reservations` 设成 0：`relayv2.Resources` 的零值会让
`MaxReservations=0`，结果是**谁都预约不上，且不报错**。

## 验收

```sh
# 在 gopher_listener_tcp/ 下
python3 patches/l2p_relay_e2e.py verify
```

跑真代码的 9 条判据（其中 3 条针对本节点），外加 5 个破坏样本的自证 ——
包括"删掉 `ForceReachabilityPublic()` 后 `TestRelayNodeAcceptsReservation` 必须翻红"。

# LIVE_TEST_CHECKLIST — Phase 4 手动实测清单（缺口 7 / 10 / 11）

## 缺口 7 — 18 个 BOF 空/垃圾参数韧性（每个 BOF 都要过）

前置：agent 在线（e2e roundtrip --keep 或 Web UI 会话）。每个 BOF 用以下三组参数各执行一次：

1. **空参数**：`execute bof <bofs>/<name>.o`
2. **垃圾参数**：`execute bof <bofs>/<name>.o AAAAAAAA`
3. **超长参数**：`execute bof <bofs>/<name>.o $(python3 -c 'print("A"*4096)')`

验收：每次都有 CALLBACK_ERROR 回显或正常输出，agent 不崩（pgrep 存活 + 下一轮 getuid 正常）。

| # | BOF | 空 | 垃圾 | 超长 | 签名 |
|---|---|---|---|---|---|
| 1 | container_detect | ☐ | ☐ | ☐ | |
| 2 | cred_harvest | ☐ | ☐ | ☐ | |
| 3 | cron_persist | ☐ | ☐ | ☐ | |
| 4 | ebpf_load | ☐ | ☐ | ☐ | |
| 5 | ebpf_unload | ☐ | ☐ | ☐ | |
| 6 | host_recon | ☐ | ☐ | ☐ | |
| 7 | kernel_exploit_check | ☐ | ☐ | ☐ | |
| 8 | ko_load | ☐ | ☐ | ☐ | |
| 9 | ld_preload_check | ☐ | ☐ | ☐ | |
| 10 | ld_preload_install | ☐ | ☐ | ☐ | |
| 11 | memfd_exec | ☐ | ☐ | ☐ | |
| 12 | net_enum | ☐ | ☐ | ☐ | |
| 13 | proc_enum | ☐ | ☐ | ☐ | |
| 14 | service_enum | ☐ | ☐ | ☐ | |
| 15 | shadow_dump | ☐ | ☐ | ☐ | |
| 16 | ssh_keys | ☐ | ☐ | ☐ | |
| 17 | ssh_persist | ☐ | ☐ | ☐ | |
| 18 | suid_scan | ☐ | ☐ | ☐ | |

## 缺口 10 — Phase 2 rootkit 对抗自检（已由用户闸过，复验即可）

- [ ] ebpf_load 加载 4 模块 → bpftool prog list 可见
- [ ] Hide 激活 → ps/getdents 枚举不可见 → unhide 恢复
- [ ] Knock 魔包 → ringbuf 事件 → helper 触发
- [ ] ebpf_unload 干净移除（bpftool 恢复可见）

## 缺口 11 — 完整 operator 会话 60 分钟零掉线

步骤：
1. Web UI 登录，生成 agent（TestHTTP）
2. 每 5 分钟一个操作轮次：任务命令（getuid/ps/ls）→ 终端 shell 会话 → BOF 执行 → 文件浏览/下载 → 隧道开/关
3. 全程记录：`tail -f server.log` 无断连 AXDBG（GetHostedAll 持续、无 E 标记）、agent tick age 恒 < 60s
4. 会话中至少一次轮换（AX_ROTATE_MS 不设，等自然轮换或临时设 120000）

验收：60 分钟零掉线、全部操作完成、轮换时操作不丢。

## 签收记录

| 缺口 | 执行人 | 日期 | 结果 |
|---|---|---|---|
| 7 | | | |
| 10 | | | |
| 11 | | | |

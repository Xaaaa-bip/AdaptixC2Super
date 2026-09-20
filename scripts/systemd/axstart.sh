#!/bin/bash
# 重启 adaptixserver —— 原样复刻 2026-09-20 13:09 那次运行的环境与参数。
#
# 环境变量从 /proc/<旧pid>/environ 固化的快照恢复（20 个变量，含 ADAPTIX_* 三个
# 只被 linux_agent stager 当表单回退读的项），避免重启后 stager 表单缺省值改变。
#   ADAPTIX_TEAMSERVER_SSL / ADAPTIX_TEAMSERVER_PORT / ADAPTIX_ENDPOINT
#
# 用法（2026-09-20 起由 systemd 托管）：
#   systemctl start|stop|restart adaptixserver.service
#   该 unit 的 ExecStart 就是本脚本；Type=simple + exec ⇒ MainPID 是 adaptixserver。
#
# 手工起（不走 systemd）时才需要 setsid，否则 ssh-mcp 会在 MCP 层超时：
#   setsid --fork /root/axstart.sh </dev/null >>/home/ad/dist/server.log 2>&1
#
# ★ 旧注释曾写「本机无 systemd」—— 那是错的：PID1 就是 systemd 255.4，
#   只是当时没有任何 unit 管这两个程序。别再据此判断「本机不能自启」。

cd /home/ad/dist || exit 1

if [ -r /root/axserv.environ ]; then
    while IFS= read -r -d '' kv; do
        case "$kv" in
            ''|*=) continue ;;
        esac
        export "$kv" 2>/dev/null
    done < /root/axserv.environ
fi

exec ./adaptixserver -web -profile central.profile

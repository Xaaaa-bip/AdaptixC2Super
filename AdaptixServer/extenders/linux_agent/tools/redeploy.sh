#!/bin/bash
# 换装 linux_agent.so + 重启服务器。全部 fd 重定向，避免 ssh 因 stdout 未关而挂住。
set -u
SRC=/root/cc/AdaptixServer/extenders/linux_agent/dist
RUN=/root/cc/run/extenders/linux_agent
TS=$(date +%Y%m%d-%H%M%S)

cp -p "$RUN/agent_linux.so" "$RUN/agent_linux.so.bak-$TS"
cp -p "$RUN/ax_config.axs"  "$RUN/ax_config.axs.bak-$TS"
cp -f "$SRC/agent_linux.so" "$RUN/agent_linux.so"
cp -f "$SRC/ax_config.axs"  "$RUN/ax_config.axs"
chmod 644 "$RUN/ax_config.axs"
echo "[swap] $TS -> $(md5sum "$RUN/agent_linux.so" | cut -c1-12)"

pkill -x adaptixserver
for i in $(seq 1 25); do pgrep -x adaptixserver >/dev/null || break; sleep 1; done
pgrep -x adaptixserver >/dev/null && { pkill -9 -x adaptixserver; sleep 2; }

cd /root/cc/run
setsid nohup ./adaptixserver -web -profile central.profile >> /root/cc/run/server.log 2>&1 < /dev/null &
sleep 16
echo "[server] pid=$(pgrep -x adaptixserver)"
echo "[mem] $(free -m | sed -n 2p)"

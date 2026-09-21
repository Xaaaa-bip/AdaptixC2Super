#!/usr/bin/env bash
# 串行：等回归结束 → 真空闲 RSS 基线 → p14（G5 systemd + H2 evacuate）验收
# 用 pgrep -x（精确进程名）而不是 pgrep -f：后者会命中"命令行里提到该名字"的
# bash 壳（启动命令文本本身含 adaptixserver），拿到的是壳的 rss=2172KB/thr=1。
set -u
cd /root/cc/AdaptixServer/extenders/linux_agent || exit 1
rm -f /tmp/p3b.log

while ! grep -q REGRESSION_DONE /tmp/reg.log 2>/dev/null; do sleep 5; done
sleep 5

P=$(pgrep -x adaptixserver | head -1)
echo "=== 空闲基线 pid=$P ===" >> /tmp/p3b.log
for i in 1 2 3 4; do
    echo "$(date +%H:%M:%S) rss_kb=$(awk '/VmRSS/{print $2}' /proc/$P/status) thr=$(awk '/Threads/{print $2}' /proc/$P/status)" >> /tmp/p3b.log
    sleep 15
done

echo "=== p14_systemd_evac_verify ===" >> /tmp/p3b.log
timeout 700 python3 tools/p14_systemd_evac_verify.py >> /tmp/p3b.log 2>&1
echo "P14_EXIT=$?" >> /tmp/p3b.log

P=$(pgrep -x adaptixserver | head -1)
echo "post-p14 rss_kb=$(awk '/VmRSS/{print $2}' /proc/$P/status)" >> /tmp/p3b.log
free -m | sed -n 2p >> /tmp/p3b.log
echo "P3B_DONE" >> /tmp/p3b.log

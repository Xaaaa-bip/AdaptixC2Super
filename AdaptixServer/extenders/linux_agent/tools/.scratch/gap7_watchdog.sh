#!/bin/bash
# gap7 看门狗：无论测试如何结束，超时后强制清理残留
sleep 900
rm -f /etc/ld.so.preload
rm -f /etc/cron.d/sys-update
find /etc/cron.d -maxdepth 1 -name 'AAAA*' -delete 2>/dev/null
# ⚠ P8 起 pin 名会进 hide_names（ISS-062）→ `ls /sys/fs/bpf` **列不出被藏起来的 pin**。
#   本看门狗只做"兜底大扫除"，扫不到的会静默残留；真正负责清自己 pin 的是各套件
#   自己的 cleanup（它们按**显式路径**删）。这里保留 ls 只为清"别人留下的、没被藏"的项。
for p in $(ls /sys/fs/bpf 2>/dev/null); do bpftool link detach pinned /sys/fs/bpf/$p 2>/dev/null; rm -f /sys/fs/bpf/$p; done
echo "[gap7-watchdog] cleanup done at $(date -Is)" >> "/root/cc/AdaptixServer/extenders/linux_agent/tools/.scratch/gap7_watchdog.log"

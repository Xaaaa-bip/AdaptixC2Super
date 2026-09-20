#!/bin/sh
# ISS-084 回归：p25（/etc/ld.so.preload 内容层，路径无逗号 ⇒ 应逐位不变）→ p24（类A 整块抹平）。
# ★ 必须串行：都加载 redact（改全机 read）。
cd /root/cc/AdaptixServer/extenders/linux_agent/tools || exit 1
: > .scratch/run84b.log
for t in p25_ldpreload_idem_verify p24_hide_content_verify; do
  [ -f "$t.py" ] || { echo "SKIP 缺 $t.py" >> .scratch/run84b.log; continue; }
  echo "===== $t =====" >> .scratch/run84b.log
  python3 "$t.py" >> .scratch/run84b.log 2>&1
  echo "EXIT_$t=$?" >> .scratch/run84b.log
done
echo "===== 完成 =====" >> .scratch/run84b.log

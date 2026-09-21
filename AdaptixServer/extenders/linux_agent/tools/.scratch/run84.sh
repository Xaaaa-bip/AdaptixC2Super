#!/bin/sh
# ISS-084 验收串行跑：探针（打印 hide_names 的键）→ p15（MODULES 内容层回归）。
# ★ 必须串行：两者都加载 redact，而 redact 改的是**全机 read**。
cd /root/cc/AdaptixServer/extenders/linux_agent/tools || exit 1
: > .scratch/run84.log
echo "===== p26 探针 =====" >> .scratch/run84.log
python3 .scratch/p26.py >> .scratch/run84.log 2>&1
echo "EXIT26=$?" >> .scratch/run84.log
echo "===== p15 回归 =====" >> .scratch/run84.log
python3 p15_redact_verify.py >> .scratch/run84.log 2>&1
echo "EXIT15=$?" >> .scratch/run84.log
echo "===== 完成 =====" >> .scratch/run84.log

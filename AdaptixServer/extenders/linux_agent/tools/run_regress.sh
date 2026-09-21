#!/usr/bin/env bash
# 串行回归跑批。用 flock 防重复：本文件被并发启动时，后到者直接退出。
# 之所以要有这个锁：曾因 ssh_run_background 超时后进程仍在 + 手工再启动一次，
# 两套回归并发跑同一台机器（共用 rootkit / pin / hide 状态）→ p13 假红 6/8。
#
# ⚠ 反过来也成立：**后台启动"失败"≠ 命令没跑起来**。实测 ssh_run_background 报
# launch_failed，但那次的 python3 已经在远端跑；叠加手工再启一次 = 两个并发实例，
# 同名的 pin / veth 互相踩 → 现场残留 + 1 个孤儿载荷（ISS-059）。
# 所以启动后必须 `ps -C python3 --no-headers | wc -l` 确认**恰好 1 个**。
set -u
cd "$(dirname "$0")/.." || exit 1

exec 9>/tmp/reg.lock
if ! flock -n 9; then
    echo "另一份回归正在跑，退出"
    exit 0
fi

# ── ★★ 前置门禁：源码树 vs 运行树的漂移（ISS-078 家族）──────────────────
# 为什么必须挡在跑批**之前**：pl_main.go 的 `NativeSrcDir` 是**相对路径**
# （`src_agent/agent`，相对服务器进程 cwd=/root/cc/run），所以服务端现编 agent
# 读的是 **run/ 下的源码副本**；BOF 更是直接取 run/bofs/*.o。
# 「改了源码树不 deploy == 没改」是 ISS-005 那条教训在**构建轴**上的同一个坑。
#
# 实测代价：17 项全部跑完之后才发现 run/ 里的 `bof_api.h` 与 3 个 BOF
# （evacuate / ld_preload_check / ld_preload_install）都还是旧的 ——
# **整批结论的适用对象都不对**，而且这是事后才发现的（`deploy.sh` 一跑就打印出来）。
# 靠人记得 deploy 是不可靠的；这条不变量只能由台面自己守。
rm -f /tmp/reg.log
# 名字:单测超时(s)。gap10/p18 要建 veth、加载 eBPF 程序、逐条等 BOF 回执，故明显更慢。
# gap10 有前置守卫：发现上次的 axg10* pin 未清会直接 FATAL 退出（不是失败，是提醒先清理）。
# 末尾两项是**按副作用半径**排的：
#   p22 要 insmod 真 .ko（模块自带 taint 位 + dmesg 痕迹）⇒ 它的内核级副作用
#       不许落在其他判据的观测窗口里；
#   p25 还要外一层：它写 /etc/ld.so.preload —— 那是**每一个进程启动**都要读的
#       文件，副作用半径覆盖全机（若被批的超时截断而留下残骸，后续每一项的输出
#       都可能被 ld.so 的告警行污染）⇒ 放**最末尾**，后面没有判据可被它影响。
# 另：p29 量的是 dmesg 行的 **Δ**，而那行文案走 ratelimit（**全局共用一个
#   10 行/5 秒的桶**）⇒ 它自带 settle() 等窗口复位；即便如此也不排在 p22 之后，
#   免得把 insmod 的 dmesg 噪声算进来。
# p31 也是低副作用（一个 agent + lo 抓包，pcap 落 tools/.scratch/）；
#   它是 **Gate 19 的常态门**（防 JA4 退化），带阴性对照自证**能红**。
# p32 同样低副作用（一个 agent + 一个 tmpfs 载荷）；它是 **Gate 17 的
#   "杀载荷之后"**（p01 只覆盖到 spawn 完成为止），判据是 /proc 回收 +
#   全系统僵尸零新增 + /dev/shm 零新增。
# p30 只起一个 agent（外加一个对照 sleep），副作用半径 = 一个进程 + 一个 tmpfs
#   载体（迁移时即 unlink，无残留），**无 BPF / 无 rootkit / 不碰全机状态**
#   ⇒ 紧随 p29，仍留在"低副作用区"内。
drift_out=$(./tools/deploy.sh --check 2>&1)
if [ $? -ne 0 ]; then
    {
        echo "FATAL: 源码树与 run/ 有漂移，拒绝跑批 —— 跑出来的结论不适用于任何东西。"
        echo "       先执行 ./tools/deploy.sh 同步，再重跑本批。"
        echo "$drift_out"
    } >> /tmp/reg.log
    exit 1
fi
echo "[batch] deploy --check 通过：**四条轴**（agent 源码 / mbedtls / BOF / BPF 产物）与源码树一致" >> /tmp/reg.log

for spec in \
    p13_hide_self_verify:400 p12_log_redact_verify:400 \
    p11_proc_meta_verify:400 p01_detached_spawn:400 p03_hide_link_verify:400 \
    p14_systemd_evac_verify:500 p15_redact_verify:500 p16_alias_probe:500 \
    p17_no_artifact_verify:600 p24_hide_content_verify:600 \
    p18_bpftool_hide_verify:600 \
    p19_g7_closure_verify:600 p20_arg_channel_verify:1200 \
    p21_pin_autohide_verify:700 p23_args_cstr_check:120 \
    p27_ssh_persist_verify:500 p29_dmesg_budget_verify:400 \
    p30_environ_scrub_verify:400 \
    p31_ja4_regress:700 \
    p32_post_kill_verify:700 \
    gap10_rootkit_verify:900 \
    p22_wiring_verify:700 \
    p25_ldpreload_idem_verify:600 ; do
    t="${spec%%:*}"; to="${spec##*:}"
    echo "=== $t (timeout ${to}s) ===" >> /tmp/reg.log
    # ★ 必须把"被批的超时截断"显式写下来：没有这行时，"timeout 杀掉的项"与
    #   "脚本本来就没有汇总行"在日志里**完全同形** —— P8 的 p20 就是这样，
    #   tail -6 里看不到 RESULT，人被逼去猜是失败还是抖动（ISS-070 绕了一大圈）。
    # ★ 每项写**独立**全量日志：原先所有项共用 /tmp/reg_item.out，下一项一开始
    #   就把它覆盖 → 某项失败时 `tail -6` 之外的行**永久丢失**（ISS-072 那一批的
    #   p17 就是这样，"FAIL 的是哪一条"都没能留下，只能重跑）。日志比磁盘便宜。
    timeout "$to" python3 "tools/$t.py" > "/tmp/reg_$t.out" 2>&1
    rc=$?
    # ★ 6 → 12 行：p15 的 `RESULT:` 行曾被 `tail -6` 挤出 /tmp/reg.log
    #   （输出变长之后 6 行只够容纳前缀噪声），于是"汇总行没进日志"看起来像
    #   "脚本没有汇总行"。行数是判据的**载具**，载具太短 = 判据丢失。
    tail -12 "/tmp/reg_$t.out" >> /tmp/reg.log
    if [ "$rc" = "124" ]; then
        echo "[batch] ★ TIMEOUT：本项被批的超时截断（${to}s），上面的输出不完整、无结论" >> /tmp/reg.log
    fi
    echo "[batch] rc=$rc  全量日志=/tmp/reg_$t.out" >> /tmp/reg.log
    # 每项之间留出清理窗口：BPF 程序 unload / 进程退出都不是瞬时的
    sleep 3
done
echo "REGRESSION_DONE" >> /tmp/reg.log
free -m | sed -n 2p >> /tmp/reg.log

#!/usr/bin/env bash
# e2e_bypass.sh — Linux agent 修复（DNS / opsec_vm_exit）的旁路端到端验证
#
# ★ 旁路原则：全程在 /root/ax_e2e 工作，绝不碰 /root/axsrv 现网与 8080 端口。
#
# 前置：本次改动的文件已同步到 /root/axsrv（git pull 或 scp）：
#   extenders/linux_agent/src_agent/agent/{dns.c,dns.h,connector.c,ws_connector.c,
#     ws_connector.h,https_emu.c,opsec.c}
#   extenders/linux_agent/{pl_main.go,ax_config.axs}
#   extenders/linux_agent/tools/test_dns_host/  （可选）
#
# 用法（VPS 上）: bash tools/e2e_bypass.sh
set -u
SRC=/root/axsrv
E2E=/root/ax_e2e

echo "== [1] 旁路树 =="
rm -rf "$E2E"
cp -r "$SRC" "$E2E" || { echo "拷贝 $SRC 失败（确认现网路径）"; exit 1; }

EXT="$E2E/AdaptixServer/extenders/linux_agent"
cd "$EXT"

echo "== [2] 全量验证（verify_linux_agent.sh：编译/stager/BOF/插件/静态链接/加密 roundtrip）=="
bash tools/verify_linux_agent.sh || { echo "verify 失败 —— 修复未过编译/加密层"; exit 1; }

echo "== [3] 旁路 teamserver（端口 18080，独立 profile）=="
cd "$E2E/AdaptixServer"
export GOEXPERIMENT=jsonv2,greenteagc
go build -o "$E2E/adaptixserver-e2e" . || { echo "teamserver 构建失败"; exit 1; }

# profile：克隆现网 profile，只改端口 8080→18080（Web 入口）与证书路径不动。
# ★ 现网 profile 名以 $SRC/dist 下实际文件为准（profile.yaml）。
mkdir -p "$E2E/run"
cp "$SRC"/dist/profile.yaml "$E2E/run/profile.yaml" 2>/dev/null \
  || cp "$SRC"/AdaptixServer/profile.yaml "$E2E/run/profile.yaml" 2>/dev/null \
  || { echo "找不到 profile.yaml —— 手动指定"; exit 1; }
sed -i 's/:8080/:18080/; s/0\.0\.0\.0:8080/0.0.0.0:18080/' "$E2E/run/profile.yaml"
grep -nE '18080|endpoint|port' "$E2E/run/profile.yaml" | head -8

cd "$E2E/run"
nohup "$E2E/adaptixserver-e2e" -profile profile.yaml > "$E2E/ts.out" 2>&1 &
sleep 3
ps aux | grep '[a]daptixserver-e2e' || { echo "teamserver 没起来，看 $E2E/ts.out"; exit 1; }

echo ""
echo "旁路 teamserver 已起（18080）。接下来的手工验收："
echo "  1) Web 打开 https://110.41.87.17:18080/<Web.path> 登录（门禁必须拦住匿名访问）"
echo "  2) 监听器页建 LinuxTCP（地址可填域名 —— 验证 DNS 链路）"
echo "  3) 载荷页生成 Binary ELF：默认 OPSEC 勾选 + opsec_vm_exit 不勾"
echo "     → 在本 VPS（本身是 KVM 云主机！）上执行 → 必须能上线"
echo "     （修复前：opsec VM 检测会把它当沙箱直接退出）"
echo "  4) 再勾 opsec_vm_exit 生成一份 → 同机执行 → 应不上线（自证开关有效）"
echo "  5) callback 填域名再测一轮（dns.c 链路）"
echo "  6) Web 新壳回归：工具条导航 / 主机 dock 收合拖高 / 事件 dock / G H / ⌘K"
echo "验收完：kill 掉 e2e 进程并 rm -rf $E2E"

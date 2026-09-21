#!/bin/bash
# 9.1b 承重实验：authorized_keys 里的**注释字段**是不是可以在写入时剥掉而不伤认证。
#
# 只回答一个问题（判别力设计）：
#   E1  带 IOC 注释的 key            ⇒ 能登录（基线：证明 key 本身有效）
#   E2  **同一把 key、注释剥掉**     ⇒ 仍能登录（★ 这就是全部结论的承重事实）
#   E3  换成别把 key（阴性对照）     ⇒ 不能登录（证明 E1/E2 不是"sshd 根本没在查钥匙"）
#   E4  多词注释                     ⇒ 能登录（说明剥注释要按字段而非按空格数）
#
# ★★ 安全边界（逐字遵守）：**绝不碰真实的 /root/.ssh/authorized_keys**。
#    独立 sshd 实例 + 独立端口(2222) + 独立测试文件 + 临时测试密钥。
#
# ★ 测试目录为什么在 /root/.axp27 而不在源码树 .scratch/（第一版就栽在这）：
#    sshd 的 **StrictModes** 会检查 AuthorizedKeysFile 的**整条目录链**的 owner/mode。
#    源码树是 0777（drwxrwxrwx，文件传输留下的），必然被拒：
#      "Authentication refused: bad ownership or modes for directory .../tools"
#    错误只落在 /var/log/auth.log 里，ssh 客户端只回 "Permission denied" ——
#    **台面故障与假设被证伪在这里逐字节同形**。所以：①换干净链（/root 0700）；
#    ②加 E0 台面自检门 —— E1 不绿则后面全部标 N/A，绝不让"全红"廉价地满足阴性对照。
#
# 为什么不用后台 + 轮询：本机 MCP 有已知陷阱 —— `&` 与轮询写在同一条命令里会让
# 后台作业**连日志一起消失**。sshd 用 `-f cfg`（不带 -D/-d）**自己 daemonize**，
# 所以全程不需要任何 `&`。
set -u
D=/root/.axp27
AK=$D/authorized_keys
PORT=2222
IOC_COMMENT='root@kali-AXTESTMARKER'
R=0; N=0; GATE=1

rec() { N=$((N+1)); if [ "$2" = 1 ]; then R=$((R+1)); echo "  [PASS] $1";
        else echo "  [$( [ "$GATE" = 0 ] && echo 'N/A ' || echo FAIL)] $1   $3"; fi }

cleanup() {
  [ -f $D/sshd.pid ] && kill "$(cat $D/sshd.pid)" 2>/dev/null
  sleep 0.3
}
trap cleanup EXIT

rm -rf "$D"; mkdir -p "$D"; chmod 700 "$D"
mkdir -p /run/sshd

ssh-keygen -q -t ed25519 -N '' -C "$IOC_COMMENT"                  -f "$D/id_test" || exit 1
ssh-keygen -q -t ed25519 -N '' -C 'other'                         -f "$D/id_other" || exit 1
ssh-keygen -q -t ed25519 -N '' -C 'root@kali multi word comment'  -f "$D/id_sp" || true
chmod 600 "$D"/id_*
echo "[env] 测试公钥（带 IOC 注释）= $(cat $D/id_test.pub)"
echo "[env] 目录链 mode: $(stat -c '%a' /root) /root  ← 必须 700 才过 StrictModes"

cat > "$D/sshd_config" <<EOF
Port $PORT
ListenAddress 127.0.0.1
HostKey /etc/ssh/ssh_host_ed25519_key
PidFile $D/sshd.pid
AuthorizedKeysFile $AK
LogLevel VERBOSE
UsePAM no
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin yes
StrictModes yes
EOF

start_sshd() {
  [ -f $D/sshd.pid ] && kill "$(cat $D/sshd.pid)" 2>/dev/null && sleep 0.3
  /usr/sbin/sshd -f "$D/sshd_config" || { echo "[abort] sshd 起不来"; exit 1; }
  sleep 0.8
}
try_login() {   # $1 = 私钥, 回显 LOGIN_OK 或空
  ssh -p $PORT -i "$1" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
      -o BatchMode=yes -o ConnectTimeout=6 -o PreferredAuthentications=publickey \
      root@127.0.0.1 'echo LOGIN_OK' 2>"$D/last_ssh.err" | tr -d '\r'
}
authlog() { grep -a "$1" /var/log/auth.log | tail -3; }

# ── E1：带 IOC 注释的 key（★ 同时是台面自检门）────────────────────────────
cp "$D/id_test.pub" "$AK"; chmod 600 "$AK"
start_sshd
OUT=$(try_login "$D/id_test")
if [ "$OUT" = LOGIN_OK ]; then
  rec "E0 台面自检：分离 sshd 起来了且带注释的 key 能登录" 1
else
  GATE=0
  echo "  [GATE] 台面自检未过 ⇒ 后面全部 N/A（判断依据不是假设，是 sshd 自己的日志）"
  authlog "Authentication refused\|Failed publickey\|Connection from" | sed 's/^/        /'
  rec "E0 台面自检：分离 sshd 起来了且带注释的 key 能登录" 0 "out=$OUT"
fi

# ── E2：同一把 key，**注释剥掉**（sshd 无需重启：authorized_keys 每次认证现读）──
awk '{printf "%s %s\n", $1, $2}' "$D/id_test.pub" > "$AK"; chmod 600 "$AK"
echo "[env] 剥注释后 = $(cat $AK)"
grep -q "$IOC_COMMENT" "$AK" && ISO=0 || ISO=1
rec "E2a 文件里确实不再出现 IOC 注释串（改的是内容不是文件名）" "$ISO" "仍有注释"
OUT=$(try_login "$D/id_test")
rec "E2b ★★★ 剥掉注释后**仍能登录**（sshd 不使用 comment 字段）" \
    "$([ "$OUT" = LOGIN_OK ] && echo 1 || echo 0)" "out=$OUT $(authlog 'Failed publickey')"

# ── E3：阴性对照 ─────────────────────────────────────────────────────────
OUT=$(try_login "$D/id_other")
rec "E3 阴性对照：别人的 key 登不进（证明 E1/E2 不是[根本没在查钥匙]）" \
    "$([ "$OUT" != LOGIN_OK ] && echo 1 || echo 0)" "out=$OUT"

# ── E4：多词注释 ─────────────────────────────────────────────────────────
cp "$D/id_sp.pub" "$AK"; chmod 600 "$AK"
OUT=$(try_login "$D/id_sp")
rec "E4 多词注释的 key 也能登录（剥注释要按字段而非按空格数）" \
    "$([ "$OUT" = LOGIN_OK ] && echo 1 || echo 0)" "out=$OUT"

echo "[env] sshd 版本 = $(/usr/sbin/sshd -V 2>&1 | head -1)"
echo
echo "RESULT: $([ $R -eq $N ] && echo PASS || echo FAIL)  ($R/$N)"
[ $R -eq $N ]

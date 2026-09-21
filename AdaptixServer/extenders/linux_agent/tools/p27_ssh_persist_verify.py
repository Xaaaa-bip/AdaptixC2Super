#!/usr/bin/env python3
"""P9 / 9.1b 验收：`~/.ssh/authorized_keys` 的内容层收口（ISS-085 / ISS-086）。

被测对象 = `ssh_persist.c` 追加进 authorized_keys 的那一行。

── ISS-085（注释是 IOC 的唯一载体）────────────────────────────────────
  `ssh_persist` 原本把操作者给的行**原样**追加：`ssh-ed25519 AAAA… root@kali`。
  key 本体必须在（sshd 要用它），但**注释不参与认证** ⇒ 剥掉零自伤。
  承重实测：`tools/.scratch/p27probe.sh`（独立 sshd + 独立端口 + 测试密钥，5/5）。

── 为什么**不做**内核针式抹平（方案文档 §2b「类B」原路线）──────────────
  ① 内核抹平**连 sshd 一起盲**。豁免通道 = `access(R_OK)` 探一次 + 带 `O_CLOEXEC`
     开（`AxReadFileReal` 的协议），那是**我们 BOF 的协议**；sshd 不做那次
     `access()` ⇒ 它读到的同样是被抹过的内容 ⇒ **我们自己的 key 对 sshd 也消失
     = 自己踢掉自己**。
  ② 内核抹平只在**模块加载后**生效；而 authorized_keys 是**我们自己写的文件** ⇒
     写入时不留 IOC 是**磁盘级干净**（任何通道、任何时刻）。
  ⇒ "文件内容必须含 IOC 才能工作"的才需要读时伪造（cron/systemd/preload 的载荷路径
    就是那种）；这里的 IOC 只是注释 ⇒ 直接不写更彻底。
  **这个判断记录在 `ssh_persist.c` 头注，是有意不做，不是遗漏。**

── ISS-086（写这版时读代码读出来的）──────────────────────────────────
  补换行的守卫写反了：`if (total < key_len)` 是"**写短了**才补 `\\n`"，本意是
  "末尾没有 `\\n` 才补"。⇒ 参数不带尾随换行（BOF 界面粘贴的常见形态）时，
  新行与上一行**粘连**：新 key 静默失效；若文件本人就不以 `\\n` 结尾
  （手改过的 authorized_keys 很常见），粘连会**毁掉上一行** —— 那可能是运维自己的钥匙。

★★ 安全边界（逐字遵守）：**一次都不碰真实的 /root/.ssh/authorized_keys**。
  靠 BOF 的**可选第二参数**（目标路径）把落点指到独立测试文件，走的是
  **完全相同的代码路径**。T8 把这条边界本身变成判据 —— 否则"我以为没碰"没有证据。
"""
import hashlib
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10            # BOF_DIR
import p03_hide_link_verify as p03            # run_bof / blob
import p15_redact_verify as p15               # kill

LISTENER = "TestHTTP"
D = "/root/.axp27"
PORT = 2222
REAL_AK = "/root/.ssh/authorized_keys"        # ★ 只读、只算哈希，绝不写
AK_MAIN = D + "/authorized_keys"              # T1-T3 / T7 的落点（也是 sshd 读的那个）
IOC = "root@kali-AXTESTMARKER"
OPS_CMT = "ops@corp-example"
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def sh(cmd, timeout=20):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True,
                          timeout=timeout).stdout.strip()


def sha(path):
    try:
        with open(path, "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return "MISSING"


def body(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return b""


def put(path, data):
    with open(path, "wb") as f:
        f.write(data)
    os.chmod(path, 0o600)


def gen_key(name, comment):
    p = "%s/%s" % (D, name)
    for f in (p, p + ".pub"):
        if os.path.exists(f):
            os.remove(f)
    subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", comment,
                    "-f", p], check=True)
    os.chmod(p, 0o600)
    return body(p + ".pub"), p


def sshd_start(cfg):
    sh("mkdir -p /run/sshd")
    pid = D + "/sshd.pid"
    if os.path.exists(pid):
        sh("kill $(cat %s) 2>/dev/null" % pid)
        time.sleep(0.3)
    r = subprocess.run(["/usr/sbin/sshd", "-f", cfg], capture_output=True, text=True)
    if r.returncode != 0 or r.stderr.strip():
        return "start failed: %s" % (r.stderr.strip() or r.returncode)
    time.sleep(0.8)
    return ""


def try_login(priv, timeout=8):
    r = subprocess.run(
        ["ssh", "-p", str(PORT), "-i", priv,
         "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
         "-o", "BatchMode=yes", "-o", "ConnectTimeout=6",
         "-o", "PreferredAuthentications=publickey",
         "root@127.0.0.1", "echo LOGIN_OK"],
        capture_output=True, text=True, timeout=timeout + 8)
    return "LOGIN_OK" in r.stdout


def main():
    pid0, a0 = 0, None
    pre_real = sha(REAL_AK)                      # ★ T8 的基线，在**任何动作之前**取
    print("[env] 真实 %s sha256[0:16] = %s（只读，全程不写）"
          % (REAL_AK, pre_real[:16]), flush=True)

    try:
        shutil.rmtree(D, ignore_errors=True)
        os.makedirs(D, mode=0o700)
        os.chmod(D, 0o700)
        our_pub, our_priv = gen_key("id_our", IOC)
        ops_pub, ops_priv = gen_key("id_ops", OPS_CMT)
        our_line = our_pub.strip()               # ★ **故意不带尾随换行**（ISS-086 的形态）
        ops_line = ops_pub.strip()
        # ★ 期望落盘的**精确字节**：`ssh-<type> <blob>`，注释被剥掉。
        #   第一版这里拿"带注释的整行长度"去比，判据算错了（p27 首轮 T4 假红）——
        #   期望值必须从**产品契约**算，不能从输入原样抄。
        norm = our_line.split(b" ")[0] + b" " + our_line.split(b" ")[1]
        print("[env] 我们的行 = %s" % our_line.decode(), flush=True)
        print("[env] 期望落盘 = %s（%d 字节）" % (norm.decode(), len(norm)), flush=True)

        cfg = D + "/sshd_config"
        with open(cfg, "w") as f:
            f.write("Port %d\nListenAddress 127.0.0.1\n"
                    "HostKey /etc/ssh/ssh_host_ed25519_key\nPidFile %s/sshd.pid\n"
                    "AuthorizedKeysFile %s\nLogLevel VERBOSE\nUsePAM no\n"
                    "PasswordAuthentication no\nKbdInteractiveAuthentication no\n"
                    "PermitRootLogin yes\nStrictModes yes\n" % (PORT, D, AK_MAIN))

        # ── T1 剥离 + 运维行不受影响（预置一行"运维自己的 key"，带它自己的注释）──
        put(AK_MAIN, ops_line + b"\n")
        bin0 = e2e.gen_agent(LISTENER, "p27a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] aid=%s pid=%d" % (a0, pid0), flush=True)

        out1 = p03.run_bof(a0, "ssh_persist", p03.blob(our_line, AK_MAIN), timeout=90) or ""
        c1 = body(AK_MAIN)
        rec("T1a 我们的 key 本体进了文件（持久化真的做成了）",
            our_line.split()[1] in c1, "文件 %d 字节" % len(c1))
        rec("T1b ★★ IOC 注释串**不在**文件里（ISS-085 核心）",
            IOC.encode() not in c1,
            "已剥除 %r" % IOC if IOC.encode() not in c1 else "**仍含** %r" % IOC)
        rec("T1c 回执说明剥了注释（不是静默改写）",
            "comment stripped" in out1, (out1.strip().splitlines() or [""])[-1][:60])
        rec("T1d ★★ 阳性对照：预置的**运维那一行逐字节不变**",
            c1 == ops_line + b"\n" + norm + b"\n",
            "首行=%r" % c1.split(b"\n")[0][:40])

        # ── T2 ★★★ 端到端：干净形态**仍然是可用的持久化** ────────────────────
        err = sshd_start(cfg)
        rec("T2a 台面自检：独立 sshd 起来了，且**运维私钥**能登录（证明它真在读这个文件）",
            not err and try_login(ops_priv), err or "sshd:%d ok / ops 登录成功" % PORT)
        rec("T2b ★★★ 我们那把私钥**能登录** —— 注释被剥掉不影响认证",
            try_login(our_priv), "独立 sshd:%d，与真实 authorized_keys 无关" % PORT)

        # ── T3 幂等（同一把 key 不重复追加；重复行本身就是 IOC）───────────────
        b3 = len(body(AK_MAIN))
        out3 = p03.run_bof(a0, "ssh_persist", p03.blob(our_line, AK_MAIN), timeout=90) or ""
        rec("T3a 再跑一次回 already present", "already present" in out3,
            (out3.strip().splitlines() or [""])[-1][:60])
        rec("T3b ★ 字节数不变（没有长出第二行）", len(body(AK_MAIN)) == b3,
            "%d→%d" % (b3, len(body(AK_MAIN))))

        # ── T4 ★ ISS-086：参数**不带尾随换行**时必须是独立一行 ────────────────
        AK4 = D + "/ak_nl"
        put(AK4, b"")                            # 空文件：不需要分隔符
        p03.run_bof(a0, "ssh_persist", p03.blob(our_line, AK4), timeout=90)
        c4 = body(AK4)
        rec("T4 ★ 无尾随换行的参数写出的是**完整一行**（修前：末尾没有 \\n）",
            c4 == norm + b"\n",
            "%r（期望 %r）" % (c4[:20], (norm + b"\n")[:20]))

        # ── T5 ★★★ ISS-086 反向：**不毁上一行**（预置"末行不以 \n 结尾"的手改形态）──
        #   ★ 这条首轮就红了 —— 而且是**源码真缺陷**：第一版只保证"新行自己以 \n 结尾"，
        #     没保证"追加**前**文件已以 \n 结尾" ⇒ 新行仍与上一行粘成一行、毁掉上一行。
        #     阳性对照的价值就在这里：它把"我修好了"与"我修了一半"分开。
        AK5 = D + "/ak_raw"
        pre5 = (b"ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIopsKeyMaterialPlaceholder0000 "
                + OPS_CMT.encode())
        put(AK5, pre5)                            # ★ 故意**没有**结尾换行
        p03.run_bof(a0, "ssh_persist", p03.blob(our_line, AK5), timeout=90)
        c5 = body(AK5)
        rec("T5 ★★★ 上一行**逐字节保留且被封口**，新行独立成行（自伤面）",
            c5 == pre5 + b"\n" + norm + b"\n",
            "got=%r" % c5[-30:])

        # ── T6 认不出就原样（保底：宁可留着，也不要把人家的 key 改坏）─────────
        AK6 = D + "/ak_unk"
        put(AK6, b"")
        junk = b"this is not a pubkey at all"
        out6 = p03.run_bof(a0, "ssh_persist", p03.blob(junk, AK6), timeout=90) or ""
        rec("T6 不像 pubkey 的输入**原样**写入且回执标明（不猜）",
            body(AK6) == junk + b"\n" and "kept verbatim" in out6,
            "写入=%r" % body(AK6)[:40])

        # ── T7 mtime 对齐（挡住"文件比目录新得多"这个一眼可见的取证标记）──────
        sh("touch -d '2020-01-01 00:00:00' %s" % D)
        put(AK4, b"")
        sh("touch -d '2020-01-01 00:00:00' %s" % D)
        p03.run_bof(a0, "ssh_persist", p03.blob(our_line, AK4), timeout=90)
        mt_f, mt_d = os.stat(AK4).st_mtime, os.stat(D).st_mtime
        rec("T7 追加后 mtime 对齐所在目录（不是「刚刚」）", abs(mt_f - mt_d) <= 1,
            "file=%d dir=%d 差=%d 秒" % (mt_f, mt_d, abs(mt_f - mt_d)))

        # ── T8 ★★ 安全边界本身是判据：真实文件全程未被写 ──────────────────────
        rec("T8 ★★ 真实 /root/.ssh/authorized_keys 的 sha256 与开跑前一致",
            sha(REAL_AK) == pre_real, "%s vs %s" % (pre_real[:16], sha(REAL_AK)[:16]))

        return 0 if all(R) else 1
    finally:
        pid = D + "/sshd.pid"
        if os.path.exists(pid):
            sh("kill $(cat %s) 2>/dev/null" % pid)
        time.sleep(0.3)
        p15.kill(pid0)
        print("[env] 真实文件收尾哈希 = %s（应与开跑前相同）" % sha(REAL_AK)[:16], flush=True)
        shutil.rmtree(D, ignore_errors=True)
        print("\n[warn] 台面 = %d/%d" % (sum(R), len(R)), flush=True)
        print("RESULT: %s" % ("PASS" if R and all(R) else "FAIL"), flush=True)


if __name__ == "__main__":
    sys.exit(main())

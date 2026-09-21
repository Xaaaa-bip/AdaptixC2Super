#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_102_install_batch15.py —— 批次15 装机（交付树 -> 运行态 /root/cc/run）

======================= 为什么需要这个脚本 =======================
1) 「装机 == 交付 dist」这条断言现在**不成立**：批次15 之后 core / 两枚 .so /
   ax_config.axs / Web UI 在交付树与运行态之间**全部有偏差**（见下表）。
2) `do_install` **不碰** `/root/cc/run/adaptixserver` ⇒ **core 必须手工换**。
3) **新 .so + 旧 core = 必挂** —— 插件里的 `Ts = ts.(Teamserver)` 是对
   插件本地接口的**运行时类型断言**，core 与 .so 必须**同一批**替换。
   ⇒ 所以这里把 6 个文件收在一个 APPLY 里，不允许只跑一半。
4) 无 git ⇒ 回滚只能靠**先于写入存在**的具名副本。所以 APPLY 与 REVERT
   同时写在本脚本里，且 BAKDIR 在 apply 的第一步就落盘。

======================= 本次替换清单 =======================
  #  run 路径                                            old md5 (替换前)                    new md5 (替换后)
  1  adaptixserver                                       529c56dd86f82ed1def15afce71da6ef  b3b23c4fb4365b35615e4e51174400e0
  2  extenders/linux_listener_tcp/listener_linux_tcp.so  0ae3ba263d14d6b73aab3ebbf3225e26  313b30bb4e82af5c9645bf5b45604190
  3  extenders/linux_agent/agent_linux.so                9d317c0598024726c5303a0b7d23c4eb  ede85bc662cf2f4f29b4539ea73560fa
  4  extenders/linux_listener_tcp/ax_config.axs           5285a14bfa922dc726474c812ebba0fc  0b6d89b9fe1da7f0e492a314e56ab5ab
  5  web/index.html                                       63be4072e11d3fd663729833896c57ef  825ea20b9317ef70afb3b16f5065e60e
  6  web/assets/index-CsbzVo2I.js                         (不存在，新增)                      7d95b26600fac528c333a355e5d39cf8

★ 未动的：#7 web/assets/index-DHzY06nb.css —— 交付与运行态**已经同 md5**
  (`00b145a98f7156d8313ec9f0987d1f9d`)，字体目录同。所以 Web 侧只有 1 js + 1 html。

★ #6 由**本机**上传（`AdaptixWeb/` 源码只在本地，服务器上没有这个目录），
  上传落点 = STAGE(`/root/gp5probe/stage15/web/`)，本脚本只认 STAGE 的 md5。
  先跑 `stage` 子命令看重什么，再用 scp 传。

★ **不删旧 js**：`web/assets/index-Ceqte44b.js` 原地保留。理由 = 只移不删 +
  REVERT 只需还原 index.html 就能回到旧界面（旧 js 还在，链接立刻有效）。

======================= 用法 =======================
  python3 patch_102_install_batch15.py status     # 只读：现状 vs 期望
  python3 patch_102_install_batch15.py stage      # 只读：还缺哪些 STAGE 文件
  python3 patch_102_install_batch15.py verify     # 只读：装机后对账（md5+进程+端口+活体插件+本进程日志）
  python3 patch_102_install_batch15.py install    # ★ 写：停 → 换 → 起 → 对账（**推荐入口**）
  python3 patch_102_install_batch15.py apply      # 写：只换文件（要求运行态**已停**，否则 ETXTBSY）
  python3 patch_102_install_batch15.py restart    # 写：只重启运行态
  python3 patch_102_install_batch15.py revert     # 写：从回滚点整批还原

★ 幂等：apply 在「运行态已经是新 md5」时判为已完成并跳过该文件；
  但**只要有一个文件的运行态既不是新也不是旧 md5 ⇒ 整体拒绝**（有人动过，先查）。

★ 与 build_bpf.sh / e2e.py / run_regress.sh / deploy.sh **无关**，不触碰。
"""

import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

RUN = "/root/cc/run"
DELIVER = "/root/cc/AdaptixServer"
BAKDIR = "/root/gp5probe/bak15/run_pre_install"
STAGE = "/root/gp5probe/stage15/web"

# ★ 参数化（批次16 起由 patch_103 覆盖）：日志轮转前缀 + 「本机侧源文件」提示。
#   动机 = 这两处原本把**批次号与 md5 写死**，第二个批次复用本脚本时会打印**别批次的**
#   回滚/上传提示 —— 回滚提示写错比没有提示更危险（照它做会还原错的文件）。
#   ⇒ 做成常量，由驱动脚本（patch_103_install_batch16.py）覆盖。
TAG = "b15"
LOCAL_HINTS = [
    ("AdaptixWeb/dist/index.html", "825ea20b9317ef70afb3b16f5065e60e"),
    ("AdaptixWeb/dist/assets/index-CsbzVo2I.js", "7d95b26600fac528c333a355e5d39cf8"),
]

# (标签, run 相对路径, 来源绝对路径, 来源类型, old_md5, new_md5)
#   来源类型 'd' = 交付树, 's' = STAGE 上传区
ARTIFACTS = [
    ("core",        "adaptixserver",
     DELIVER + "/adaptixserver", "d",
     "529c56dd86f82ed1def15afce71da6ef", "b3b23c4fb4365b35615e4e51174400e0"),
    ("lst_linux",   "extenders/linux_listener_tcp/listener_linux_tcp.so",
     DELIVER + "/extenders/linux_listener_tcp/dist/listener_linux_tcp.so", "d",
     "0ae3ba263d14d6b73aab3ebbf3225e26", "313b30bb4e82af5c9645bf5b45604190"),
    ("agent_linux", "extenders/linux_agent/agent_linux.so",
     DELIVER + "/extenders/linux_agent/dist/agent_linux.so", "d",
     "9d317c0598024726c5303a0b7d23c4eb", "ede85bc662cf2f4f29b4539ea73560fa"),
    ("axs",         "extenders/linux_listener_tcp/ax_config.axs",
     DELIVER + "/extenders/linux_listener_tcp/dist/ax_config.axs", "d",
     "5285a14bfa922dc726474c812ebba0fc", "0b6d89b9fe1da7f0e492a314e56ab5ab"),
    # ★ 第 7 件：**补装**（2026-09-17 用户实测"前端没有这些功能"暴露的漏装）。
    #   反向 pivot 在 UI 上是两半，这一半是 agent 控制台的 `listen` 命令定义：
    #   core 启动时按 config.yaml 的 `ax_file` **从盘上读**（core/extender/extender.go:126
    #   `os.ReadFile(filepath.Dir(config_path) + "/" + configAgent.AxFile)`）
    #   ⇒ 换 web bundle / 换 listener 的 axs 都不管用，**必须换这一件 + 重启**。
    ("axs_agent",   "extenders/linux_agent/ax_config.axs",
     DELIVER + "/extenders/linux_agent/dist/ax_config.axs", "d",
     "d282c93e0744dab78995fc9a4f284585", "7124852c2d46327075e851e32aca06c8"),
    ("web_index",   "web/index.html",
     STAGE + "/index.html", "s",
     "63be4072e11d3fd663729833896c57ef", "825ea20b9317ef70afb3b16f5065e60e"),
    ("web_js",      "web/assets/index-CsbzVo2I.js",
     STAGE + "/assets/index-CsbzVo2I.js", "s",
     None, "7d95b26600fac528c333a355e5d39cf8"),   # old=None ⇒ 本次新增
]

LAUNCH_ARGS = ["./adaptixserver", "-web", "-profile", "central.profile"]
LOGFILE = RUN + "/server.log"
API_PORT = "38212"              # central.profile 的 teamserver.port（实测生产实例就听这个）
SPEC_PATH = BAKDIR + "/launch_spec.json"

# ★★ 踩过的坑（低 token 归档）：**不要硬编码启动参数**。
#   第一版写死 `-profile profile.yaml`，而生产现役进程用的是
#   `-profile central.profile` —— 两个 profile 的 teamserver.port 分别是
#   **8080** 与 **38212**，加载的扩展集也不同（实测插件数 18 ≠ 20）。
#   后果极隐蔽：`apply` 全绿、进程起来了、日志漂亮 —— **跑的是另一个实例配置**。
#   ⇒ 现在 `_stop()` 在杀进程**之前**把现役 argv/environ 记到 SPEC_PATH，
#     `_start()` 优先用它；硬编码只作「记录文件缺失时」的兜底。
#     自证：`tr '\0' ' ' < /proc/<pid>/cmdline`

# ★ 必须与运行态现役进程**逐字一致**：实测 `/proc/400325/environ` 里就有这两条
#   （见交付清单 §1.3「启动 GOMEMLIMIT=600MiB GOGC=50」）。
#   漏掉就会让新进程用默认 GC 跑 ⇒ 内存行为与验收时的配置**不一致**，
#   而本机只有 1965 MB 物理内存，这不是风格问题。
LAUNCH_ENV = {"GOMEMLIMIT": "600MiB", "GOGC": "50"}

# 最近一次 `_start()` 是否达成了稳态（端口在听 + 插件集合稳定）。
# ★ 为什么要这个标志：`_start()` 返回非空 pid 只说明「**有个**进程起来了」，
#   不说明「起的是**对的那个实例**」。实测踩到的正是这个缝：argv 写错 ⇒ 进程起来了、
#   日志漂亮，但监听 8080 而非 38212、插件 18 而非 20。若只看 pid 非空就放过，
#   装机就会**报成功而实际跑的是另一个配置**。⇒ 把它当作失败，交给 install 自动回滚。
_STEADY_OK = True


def md5(path):
    if not os.path.isfile(path):
        return None
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def bak_name(rel):
    """run 相对路径 -> BAKDIR 内的**具名**扁平名（/ 换 __，避免同名歧义）。"""
    return "run__" + rel.replace("/", "__")


def run_pids():
    """找出 cwd == RUN 的 adaptixserver 进程（按 cwd 认，不按进程名）。"""
    pids = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            if os.readlink("/proc/%s/cwd" % name) != RUN:
                continue
            with open("/proc/%s/cmdline" % name, "rb") as f:
                if b"adaptixserver" in f.read():
                    pids.append(int(name))
        except OSError:
            continue
    return sorted(pids)


def classify(label, run_path, cur, old, new):
    if cur is None:
        return "新增(未装)" if old is None else "缺失!!"
    if cur == new:
        return "已新"
    if cur == old:
        return "待换"
    return "★未知★"


def cmd_status():
    print("=== 运行态 %s ===" % RUN)
    print("%-12s %-52s %-34s %s" % ("标签", "run 相对路径", "当前 md5", "状态"))
    bad = 0
    for label, rel, src, kind, old, new in ARTIFACTS:
        cur = md5(os.path.join(RUN, rel))
        st = classify(label, rel, cur, old, new)
        if st.startswith("★"):
            bad += 1
        print("%-12s %-52s %-34s %s" % (label, rel, cur or "(无)", st))
    print()
    print("=== 来源（交付树 / STAGE）===")
    for label, rel, src, kind, old, new in ARTIFACTS:
        got = md5(src)
        ok = "OK" if got == new else ("缺文件" if got is None else "md5 不符(%s)" % got)
        print("  %-12s [%s] %-72s %s" % (label, kind, src, ok))
    print()
    pids = run_pids()
    print("运行态进程: %s" % (pids if pids else "无"))
    print("回滚点目录: %s %s" % (BAKDIR, "(已有 %d 个文件)" % len(os.listdir(BAKDIR))
                                if os.path.isdir(BAKDIR) else "(尚未创建)"))
    print()
    if bad:
        print("结论：有 %d 个文件的运行态 md5 **既不是旧的也不是新的** ⇒ 先查清再动。" % bad)
    return 0 if bad == 0 else 3


def cmd_stage():
    need = []
    for label, rel, src, kind, old, new in ARTIFACTS:
        if kind != "s":
            continue
        got = md5(src)
        if got != new:
            need.append((src, new, got))
    if not need:
        print("STAGE 就绪：上传区文件齐且 md5 全对。")
        return 0
    print("STAGE 缺以下文件（从本机 AdaptixWeb/dist 上传到 %s）：" % STAGE)
    for src, new, got in need:
        print("  %s" % src)
        print("      期望 md5 %s%s" % (new, "" if got is None else "  当前 %s" % got))
    print()
    if LOCAL_HINTS:
        print("本机侧源文件（md5 已核对）：")
        for hp, hm in LOCAL_HINTS:
            print("  %-44s %s" % (hp, hm))
    return 2


def _preflight():
    """返回 (待换列表, 已完成数)。任何一处不符预期 ⇒ 抛 SystemExit。"""
    todo, done = [], 0
    for label, rel, src, kind, old, new in ARTIFACTS:
        s = md5(src)
        if s is None:
            raise SystemExit("[preflight] 来源不存在：%s" % src)
        if s != new:
            raise SystemExit("[preflight] 来源 md5 不符：%s\n  期望 %s\n  实际 %s" % (src, new, s))
        cur = md5(os.path.join(RUN, rel))
        if cur == new:
            done += 1
            print("  [skip] %-12s 运行态已是新 md5" % label)
            continue
        if cur != old:
            raise SystemExit("[preflight] 运行态 md5 既非旧也非新：%s\n  期望(旧) %s\n  实际     %s\n"
                             "  ⇒ 有人动过运行态，先查清再决定，别硬覆盖。" % (rel, old, cur))
        todo.append((label, rel, src, old, new))
    return todo, done


def cmd_apply(allow_running=False):
    # ★ 硬墙②：核在跑 ⇒ `open(dst,'wb')` 直接 ETXTBSY（见 cmd_install 的说明）。
    #   这里做成**前置拦截**，而不是等 copy2 崩在中途：停机是换文件的**前提**，
    #   不是可选项。要用 `install`（停→换→起）而不是裸 `apply`。
    if not allow_running and run_pids():
        raise SystemExit("运行态还在跑（%s）⇒ 原地覆盖必然 ETXTBSY。\n"
                         "  正确入口：python3 %s install   （停 → 换 → 起 → 对账）"
                         % (run_pids(), os.path.abspath(__file__)))
    print("=== [1/5] preflight ===")
    todo, done = _preflight()
    if not todo:
        print("%d 个文件运行态全部已是新 md5，装机已完成，无需再动。" % len(ARTIFACTS))
        return 0
    print("  待换 %d 个，已新 %d 个" % (len(todo), done))

    print("=== [2/5] 建回滚点（先于写入存在）===")
    os.makedirs(BAKDIR, exist_ok=True)
    for label, rel, src, old, new in todo:
        # ★ old is None ⇒ 本次**新增**的文件，运行态里根本没有 ⇒ 无物可备。
        #   （第一版漏了这句，于是 `copy2(/root/cc/run/web/assets/index-CsbzVo2I.js)`
        #    直接 FileNotFoundError —— 所幸崩在备份阶段，**一个字节都没写**。
        #    这也是「回滚点必须建在写入之前」这条纪律的副作用：坏了也是安全的坏法。）
        if old is None:
            print("  [new ] %-12s 本次新增，运行态无物可备" % label)
            continue
        dst = os.path.join(BAKDIR, bak_name(rel))
        if os.path.exists(dst):
            if md5(dst) != old:
                print("  [keep] %s 已存在且 md5 不同（更早的回滚点），改名保留" % dst)
                shutil.move(dst, dst + ".older-" + time.strftime("%Y%m%d-%H%M%S"))
            else:
                print("  [have] %s" % dst)
                continue
        shutil.copy2(os.path.join(RUN, rel), dst)
        print("  [bak ] %s  (%s)" % (dst, old))

    print("=== [3/5] 换入新文件 ===")
    for label, rel, src, old, new in todo:
        tgt = os.path.join(RUN, rel)
        os.makedirs(os.path.dirname(tgt), exist_ok=True)
        shutil.copy2(src, tgt)
        print("  [put ] %-12s -> %s" % (label, rel))

    print("=== [4/5] 复核 ===")
    bad = 0
    for label, rel, src, kind, old, new in ARTIFACTS:
        got = md5(os.path.join(RUN, rel))
        ok = "OK" if got == new else "FAIL(%s)" % got
        if got != new:
            bad += 1
        print("  %-12s %-52s %s" % (label, rel, ok))

    print("=== [5/5] 结果 ===")
    if bad:
        print("★ 有 %d 个文件复核不通过 ⇒ 立刻 `revert` 回到替换前。" % bad)
        return 4
    print("装机写入完成：%d/%d md5 与交付一致。" % (len(ARTIFACTS), len(ARTIFACTS)))
    print("★ **必须重启**才能生效：core 与 .so 必须同批（新 .so + 旧 core = 必挂）。")
    print("  下一步：python3 %s restart" % os.path.abspath(__file__))
    print("  回滚  ：python3 %s revert 然后 restart" % os.path.abspath(__file__))
    return 0


def cmd_revert():
    print("=== 还原运行态 ===")
    if not os.path.isdir(BAKDIR):
        raise SystemExit("回滚点目录不存在：%s ⇒ 无从还原" % BAKDIR)
    miss = []
    for label, rel, src, kind, old, new in ARTIFACTS:
        if old is None:            # 本次新增的文件：移走，不删
            tgt = os.path.join(RUN, rel)
            if os.path.exists(tgt):
                dst = os.path.join(BAKDIR, bak_name(rel) + ".installed")
                shutil.move(tgt, dst)
                print("  [mv  ] %s -> %s" % (rel, dst))
            else:
                print("  [none] %s 本就不在" % rel)
            continue
        b = os.path.join(BAKDIR, bak_name(rel))
        if md5(b) != old:
            miss.append((rel, b, md5(b)))
            continue
        shutil.copy2(b, os.path.join(RUN, rel))
        print("  [rest] %-12s %s" % (label, rel))
    if miss:
        print("★ 以下回滚点缺失或 md5 不符，**未还原**：")
        for rel, b, got in miss:
            print("    %s  期望 %s  实际 %s" % (rel, old, got))
        return 5
    print()
    print("还原完成。★ 本脚本对 web 只做「移入」，从不删除 ⇒ index.html 还原后自动指回旧 js，"
          "界面随之一并回旧版（无需手工清 assets）。")
    print("★ 同样**必须重启**（core/.so 已换回旧版）。")
    return 0


def _capture_launch_spec(pid):
    """杀进程**之前**把现役 argv/environ 记下来 —— 这是「不硬编码启动参数」的唯一可靠来源。

    ★ **冻结基线语义**：一旦记录存在就**不再覆盖**（除非 `P102_RECAPTURE=1`）。
      理由：这份记录是**回滚/还原时该用什么参数**的基线，必须来自「上一次已知正确的
      运行态」。若每次 stop 都无条件重写，那么**一次错误的启动就会被固化成新基线**，
      错误会自我传播下去 —— 这正是"回滚点被污染"的同一类问题（见坑J）。
    """
    if os.path.isfile(SPEC_PATH) and os.environ.get("P102_RECAPTURE") != "1":
        print("  [spec] 已有基线 %s，**保留不覆盖**（要重采设 P102_RECAPTURE=1）" % SPEC_PATH)
        try:
            with open(SPEC_PATH) as f:
                print("         argv = %s" % " ".join(json.load(f).get("argv", [])))
        except (OSError, ValueError):
            pass
        return
    spec = {}
    try:
        with open("/proc/%d/cmdline" % pid, "rb") as f:
            argv = [a for a in f.read().decode("utf-8", "replace").split("\0") if a]
        if argv:
            spec["argv"] = argv
    except OSError as e:
        print("  ★ 读 cmdline 失败: %s" % e)
    try:
        with open("/proc/%d/environ" % pid, "rb") as f:
            envs = f.read().decode("utf-8", "replace").split("\0")
        spec["env"] = {kv.split("=", 1)[0]: kv.split("=", 1)[1]
                       for kv in envs if "=" in kv and kv.split("=", 1)[0] in LAUNCH_ENV}
    except OSError as e:
        print("  ★ 读 environ 失败: %s" % e)
    if spec:
        os.makedirs(BAKDIR, exist_ok=True)
        with open(SPEC_PATH, "w") as f:
            json.dump(spec, f, indent=2, ensure_ascii=False)
        print("  [spec] 记录现役启动参数 -> %s" % SPEC_PATH)
        print("         argv = %s" % " ".join(spec.get("argv", [])))
        print("         env  = %s" % spec.get("env", {}))


def _launch_spec():
    args, env = list(LAUNCH_ARGS), dict(LAUNCH_ENV)
    if os.path.isfile(SPEC_PATH):
        try:
            with open(SPEC_PATH) as f:
                spec = json.load(f)
            if spec.get("argv"):
                args = spec["argv"]
            env.update(spec.get("env") or {})
            print("  启动参数来源: %s" % SPEC_PATH)
        except (OSError, ValueError) as e:
            print("  ★ 启动参数记录不可用(%s) ⇒ 回退默认 %s" % (e, " ".join(LAUNCH_ARGS)))
    else:
        print("  ★ 无 %s ⇒ 用默认 %s（**先确认它就是现役那一条**）"
              % (SPEC_PATH, " ".join(LAUNCH_ARGS)))
    print("  argv = %s" % " ".join(args))
    return args, env


def _wait_steady(pid, timeout=45.0):
    """等运行态进入稳态：API 端口在听 **且** 活体插件集合连续两次相同。

    ★ 为什么必须等：`verify` 第一版在启动 2 秒时就对账 ⇒ 报
      「:38212 ★无 / 活体插件 18 个」两条红 —— 全是**启动竞态假红**：
      判据跑在系统到达稳态**之前**。判据纪律：**对"刚起来的进程"做断言，
      必须先等稳态，不能睡一个拍脑袋的固定秒数。**
    """
    t0 = time.time()
    prev = None
    while time.time() - t0 < timeout:
        out = subprocess.run(["ss", "-Htln"], capture_output=True, text=True).stdout
        listening = (":" + API_PORT) in out
        got = live_plugins(pid)
        if listening and prev is not None and got == prev:
            print("  稳态达成: :%s 在听, 活体插件 %d 个 (%.1fs)"
                  % (API_PORT, len(got), time.time() - t0))
            return True, got
        prev = got
        time.sleep(1.0)
    print("  ★ %.0fs 内未达稳态（最后活体插件 %d 个, :%s %s）"
          % (timeout, len(prev or ()), API_PORT,
             "在听" if (":" + API_PORT) in subprocess.run(
                 ["ss", "-Htln"], capture_output=True, text=True).stdout else "未在听"))
    return False, prev or set()


def _stop():
    """停运行态，等到进程真的消失才返回。"""
    print("=== 停运行态 ===")
    pids = run_pids()
    print("  当前进程: %s" % (pids if pids else "无"))
    if pids:
        _capture_launch_spec(pids[0])       # ★ 必须在杀之前
    for p in pids:
        os.kill(p, 15)
    for _ in range(40):                 # 最多等 10s
        if not run_pids():
            break
        time.sleep(0.25)
    left = run_pids()
    if left:
        print("  SIGTERM 未退，SIGKILL: %s" % left)
        for p in left:
            os.kill(p, 9)
        time.sleep(0.5)
    if run_pids():
        raise SystemExit("旧进程仍在：%s" % run_pids())
    print("  已停。")
    return 0


def _start():
    """起运行态（带 LAUNCH_ENV），返回新 pid 列表（空 = 没起来）。"""

    if os.path.exists(LOGFILE):
        kept = "%s.pre_%s_%s" % (LOGFILE, TAG, time.strftime("%Y%m%d-%H%M%S"))
        shutil.move(LOGFILE, kept)
        print("  [mv  ] 旧日志 -> %s" % kept)

    # setsid 脱离 ssh 会话，nohup 忽略 HUP，stdin 接 /dev/null ⇒ ssh 通道关闭后仍存活
    args, genv = _launch_spec()
    env = dict(os.environ)
    env.update(genv)
    print("  环境: %s" % " ".join("%s=%s" % kv for kv in sorted(env.items())
                                  if kv[0] in LAUNCH_ENV))
    with open(LOGFILE, "wb") as lf:
        subprocess.Popen(args, cwd=RUN, stdout=lf, stderr=lf, env=env,
                         stdin=subprocess.DEVNULL, start_new_session=True)
    time.sleep(2.0)
    new = run_pids()
    print("  新进程: %s   cwd=%s" % (new, RUN))
    if not new:
        print("★ 没起来 —— 看日志：tail -40 %s" % LOGFILE)
        return []
    # 复核环境变量确实带上了（防「以为传了其实没传」）
    try:
        with open("/proc/%d/environ" % new[0], "rb") as f:
            gotenv = f.read().decode("utf-8", "replace").split("\0")
        for kv in sorted(LAUNCH_ENV.items()):
            line = "%s=%s" % kv
            print("  env %-18s %s" % (line, "OK" if line in gotenv else "★缺失"))
    except OSError as e:
        print("  env 复核跳过: %s" % e)
    # ★ 等稳态：不睡固定秒数就断言 = 启动竞态假红（实测踩到）
    global _STEADY_OK
    ok, got = _wait_steady(new[0])
    _STEADY_OK = ok
    if not ok:
        print("  ★ 未达稳态 ⇒ 优先怀疑**启动参数**（profile 决定端口与扩展集）")
        print("     自证：tr '\\0' ' ' < /proc/%d/cmdline" % new[0])
    return new


def _tail_log(n=25):
    print()
    print("=== 启动日志前 %d 行 ===" % n)
    if not os.path.isfile(LOGFILE):
        print("  (无日志)")
        return
    with open(LOGFILE, "r", errors="replace") as f:
        for i, line in enumerate(f):
            if i >= n:
                break
            sys.stdout.write("  " + line)


def cmd_restart():
    _stop()
    if not _start():
        return 6
    _tail_log()
    print()
    print("★ 监听端口复核（应含 38212 = API）：ss -Htlnp | grep adaptixserver")
    return 0


def cmd_install():
    """停 → 换 → 起 → 对账。**这是唯一安全顺序。**

    ★★ 为什么 `apply` 与 `restart` 必须合成一步（实测踩到的第二堵墙）：
       `open('/root/cc/run/adaptixserver','wb')` 在核**正在运行**时直接
       `OSError: [Errno 26] Text file busy` —— 跑着的可执行文件**根本不允许**
       原地覆盖（ETXTBSY）。清单 §4 记的墙①是「`.so` 被 mmap ⇒ SIGBUS」，
       实测**核自己**也挡，而且挡得更早：所以「先换后停」在第一步就失败。
       反过来说这是**安全的失败**：整步崩在写入任何字节**之前**，运行态一字未改。
       ⇒ 正确顺序只能是 **停 → 换 → 起**；为把停机窗口压到最短，三步收在
         同一个进程里跑完，而不是让人分三次敲（中间任何一次中断都留半成品态）。
    """
    _stop()
    rc = cmd_apply(allow_running=True)
    if rc != 0:
        print("★ 换文件失败 ⇒ 立刻还原并起回旧版。")
        cmd_revert()
        _start()
        return rc
    if not _start():
        print("★ 起不来 ⇒ 立刻还原并起回旧版。")
        cmd_revert()
        _start()
        return 6
    if not _STEADY_OK:
        print("★ 进程起来了但**没达稳态**（端口/插件集不对）⇒ 判定为「起错了实例」，")
        print("  立刻还原文件并起回旧版。★ 这一条就是拦住「profile 写错」那个坑的闸门。")
        cmd_revert()
        _start()
        return 8
    _tail_log(15)
    print()
    rc = cmd_verify()
    if rc != 0:
        print("★ 装机后对账红 ⇒ 还原并起回旧版（回滚点已在 %s）。" % BAKDIR)
    return rc


ANSI = re.compile(r"\x1b\[[0-9;]*m")
TSRE = re.compile(r"\[(\d{2})/(\d{2}) (\d{2}):(\d{2}):(\d{2})\]")
# ★ 启动横幅：每次启动**恰好一条**，且**不带时间戳**（实测 361 KB 历史件里 17 条 = 17 次启动）。
#   它才是"进程生命周期"的正确锚 —— 见 proc_slice() 的说明。
BANNER = "===== Adaptix Framework"
READY = "The AdaptixC2 server is ready"

# 装机后必须**活体在册**的插件（其余只数总数）。
# ★ 这四件覆盖两个 pivot 通道 + 本次要换的两枚 linux .so：
#   若 `listener_linux_tcp.so` 因「新 .so + 旧 core」被拒载，它会**不出现**在 maps 里。
KEY_PLUGINS = [
    "linux_listener_tcp/listener_linux_tcp.so",
    "linux_agent/agent_linux.so",
    "gopher_listener_tcp/listener_gopher_tcp.so",
    "gopher_agent/agent_gopher.so",
]
EXPECT_PLUGINS_MIN = 20
# ★ 插件装载链上**每一个**失败分支的日志指纹（源：core/extender/extender.go:71-113 +
#   ex_service.go:35-40）。只查 "failed to open plugin" 会漏掉"打开了但没注册上" ——
#   而「新 .so + 旧 core」恰恰可能死在**后半段**（符号签名 / 依赖版本 / 注册被拒），
#   不是死在前半段。maps 只能证明 plugin.Open 成功，证明不了后续任何一步。
PLUGIN_ALARMS = [
    "failed to open plugin",                 # plugin.Open 失败
    "different version of package",          # go plugin 依赖版本指纹不一致
    "failed to find InitPlugin in",          # 无 InitPlugin 符号
    "unexpected signature from InitPlugin",  # 签名断言失败（core/插件 ABI 不符）
    "returned nil",                          # InitPlugin 返回 nil
    "does not registered:",                  # TsListenerReg / TsAgentReg 被拒
    "Error config parse:",                   # extender 配置解析失败
    "failed to read ax file",                # .axs 读不到（★ 本次装了新 axs，这条要在）
]


def proc_slice(path):
    """取**最后一段进程生命周期**的日志行。返回 (干净行列表, 起始行号, 是否锚定成功)。

    ★★ 为什么不能用时间戳切片（假绿第 15 形态 · 空切片当绿）：
    `TSRE` 期望 `[17/09 08:07:20]`，但**日志里真实的时间戳是被 ANSI 包住的**
    `[\\x1b[1m17/09 08:07:20\\x1b[0m]` ⇒ 原版 `log_lines_since()` 的行谓词
    **恒不匹配**，返回**空列表** ⇒ "本进程插件告警 无" 是**空洞为真**，
    不是"扫过了、没发现"。实测：现在的 `server.log` 全文 0 行能被 `TSRE` 命中。
    ⇒ 锚改用**启动横幅**：每进程一条、无时间戳、跨进程可数（实测 17 次启动 = 17 条）。
    判据必须**先证明切片非空且属于本进程**，再谈"告警 0 条"。
    调用方必须检查第三个返回值：锚定失败 = **红**（日志被截断/轮转 ⇒ 无法归因）。
    """
    if not os.path.isfile(path):
        return [], 0, False
    with open(path, "r", errors="replace") as f:
        clean = [ANSI.sub("", l).rstrip("\n") for l in f]
    idx = [i for i, l in enumerate(clean) if BANNER in l]
    if not idx:
        return clean, 0, False
    a = idx[-1]
    return clean[a:], a + 1, True


def proc_start_epoch(pid):
    """进程启动的墙钟时刻 = now - (uptime - starttime/HZ)。
    ★ /proc/<pid>/stat 的 comm 字段可含空格与括号 ⇒ 必须从**最后一个 ')' 之后**切，
      不能按空格 split 整个文件（那是经典的解析坑）。"""
    with open("/proc/%d/stat" % pid) as f:
        raw = f.read()
    after = raw[raw.rfind(")") + 1:].split()
    starttime = int(after[19])                 # 字段22（去掉 pid/comm 后 index 19）
    with open("/proc/uptime") as f:
        uptime = float(f.read().split()[0])
    return time.time() - (uptime - starttime / float(os.sysconf("SC_CLK_TCK")))


def live_plugins(pid):
    """活体在册的 extender .so 集合 —— 取自 /proc/<pid>/maps，是**日志无关的真相**。"""
    got = set()
    try:
        with open("/proc/%d/maps" % pid) as f:
            for line in f:
                i = line.find(RUN + "/extenders/")
                if i < 0:
                    continue
                j = line.find(".so", i)
                if j < 0:
                    continue
                got.add(line[i + len(RUN) + len("/extenders/"):j + 3])
    except OSError:
        pass
    return got


def log_lines_since(path, epoch):
    """只取**本进程生命周期内**的日志行（按行内时间戳）。

    ★ 坑（假红第14形态）：`server.log` 是**追加**的、跨多次进程生命周期 ⇒
      对全文 grep `failed to open plugin` 会把**历史进程**的失败算到**当前进程**头上。
      实测本次就踩到：日志里 2 条 16/09 的拒载，而活体 20 个插件全在册。
    ★★ 本函数**不可单独作为判据闸门**：日志里真实时间戳被 ANSI 包住
      （`[\\x1b[1m17/09 08:07:20\\x1b[0m]`），本函数行谓词一旦失配就返回**空列表**，
      "没扫到"会被读成"没问题"（假绿第15形态）。⇒ 闸门用 `proc_slice()`（横幅锚定
      + 非空洞性自证）；本函数只留作时间维度的**交叉核对**。"""
    out = []
    if not os.path.isfile(path):
        return out
    now = time.time()
    year = time.localtime(now).tm_year
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = ANSI.sub("", line)           # ★ 必须先剥 ANSI 再匹配时间戳
            m = None
            for m in TSRE.finditer(line):
                pass                            # 取**最后一个**时间戳（行内可能还有别的）
            if m is None:
                continue
            mo, d, h, mi, s = (int(x) for x in m.groups())
            try:
                ts = time.mktime((year, mo, d, h, mi, s, 0, 0, -1))
            except (ValueError, OverflowError):
                continue
            if ts > now + 86400:                # 跨年回绕
                ts = time.mktime((year - 1, mo, d, h, mi, s, 0, 0, -1))
            if ts >= epoch - 5:
                out.append((ts, line.strip()))
    return out


def cmd_verify():
    """装机后只读对账：不启停任何进程、不连任何 agent、不碰 .so 加载。"""
    print("=== 装机后对账（只读）===")
    bad = 0
    for label, rel, src, kind, old, new in ARTIFACTS:
        got = md5(os.path.join(RUN, rel))
        ok = got == new
        if not ok:
            bad += 1
        print("  %-12s %-52s %s" % (label, rel, "OK" if ok else "FAIL(%s)" % got))

    pids = run_pids()
    print("  进程            %s" % (pids if pids else "★无（没起来？）"))
    if not pids:
        print("\n结论：进程不在，其余检查无意义。看日志：tail -60 %s" % LOGFILE)
        return 7

    # ★ 先等稳态再断言：对刚起来的进程做即时断言 = 启动竞态假红（实测踩到）
    _wait_steady(pids[0])

    out = subprocess.run(["ss", "-Htln"], capture_output=True, text=True).stdout
    api = (":" + API_PORT) in out
    print("  监听 :%s     %s" % (API_PORT, "OK" if api else "★无"))
    if not api:
        bad += 1

    pid = pids[0]
    got = live_plugins(pid)
    miss = [p for p in KEY_PLUGINS if p not in got]
    print("  活体插件        %d 个%s" % (len(got), "" if not miss else "  ★缺 %s" % miss))
    if len(got) < EXPECT_PLUGINS_MIN or miss:
        bad += 1

    start = proc_start_epoch(pid)
    print("  本进程启动      %s" % time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(start)))

    # ★ 告警模式必须覆盖 extender.go / ex_service.go 里**全部**失败分支，
    #   否则"加载了"（maps 有）会被误读成"注册了"。顺序即插件装载链：
    #   plugin.Open → Lookup("InitPlugin") → 签名断言 → 返回非 nil → ax 读取 → TsListenerReg
    # ★★ 但"扫过了没发现"与"什么都没扫"必须分得开（假绿第 13 形态「空切片当绿」）：
    #    先锚定本进程的日志段，并**证伪空洞**（锚定成功 + 段够长 + 含就绪标记）。
    sl, anchor, anchored = proc_slice(LOGFILE)
    stamp = [l for l in sl if TSRE.search(l)]
    print("  日志段          %s" % ("锚定于第 %d 行（启动横幅），共 %d 行，带时间戳 %d 行"
                                    % (anchor, len(sl), len(stamp)) if anchored
                                    else "★未锚定（找不到启动横幅）"))
    if not anchored:
        print("      ★ 日志里没有启动横幅 ⇒ 无法把日志归因到本进程（被截断/轮转/未写入）")
        bad += 1
    if anchored and len(sl) < 5:
        print("      ★ 日志段只有 %d 行 ⇒ 切片可疑，不作数" % len(sl))
        bad += 1
    if anchored and not any(READY in l for l in sl):
        print("      ★ 日志段里没有就绪标记 %r ⇒ 这段日志不属于一个**起完了**的进程" % READY)
        bad += 1

    hits = [l for l in sl if any(k in l for k in PLUGIN_ALARMS)]
    print("  本进程插件告警  %s" % ("无（在 %d 行里扫 %d 种指纹）" % (len(sl), len(PLUGIN_ALARMS))
                                    if not hits else "★%d 条" % len(hits)))
    for line in hits[:10]:
        print("      %s" % line)
    if hits:
        bad += 1

    # ── 派生判据：覆盖面**不**由上面那份人工清单决定 ─────────────────────
    # ★★ 动机（本批次实测漏装）：`ARTIFACTS` 是**手写清单**，它的覆盖面就是判据的覆盖面。
    #    我漏了 `linux_agent/ax_config.axs` ⇒ 6/6 全绿，而运行态**根本没有 pivot 的 `listen`
    #    命令**（用户在 Web UI 上一眼看出来的）。⇒ 凡"树里有、运行态也有"的同类文件，
    #    一律**从文件系统派生**出应检集合，而不是靠人列。
    dx = sorted(glob.glob(DELIVER + "/extenders/*/ax_config.axs"))
    dax = []
    for p in dx:
        name = os.path.basename(os.path.dirname(p))
        r = RUN + "/extenders/" + name + "/ax_config.axs"
        if not os.path.isfile(r):          # 交付树有、运行态没有 ⇒ 由人工清单负责（见 ARTIFACTS）
            continue
        if md5(p) != md5(r):
            dax.append((name, md5(r), md5(p)))
    print("  axs 全量扫描    %s" % ("%d 个 extender 的 ax_config.axs 全部与交付树同字节" % len(dx)
                                    if not dax else "★%d/%d 个不同：%s"
                                    % (len(dax), len(dx), [d[0] for d in dax])))
    for name, a, b in dax:
        print("      %-22s 运行态 %s ≠ 交付树 %s" % (name, a, b))
    if not dx:
        print("      ★ 一个 ax_config.axs 都没扫到 ⇒ 路径写错，本项不作数（fail-closed）")
        bad += 1
    if dax:
        bad += 1

    # ★ 服务端**真正发出**的 web 字节（判据不是"文件换进去了"，而是"发出来的是哪一份"）
    try:
        import ssl
        import urllib.request
        ctx = ssl._create_unverified_context()
        base = "https://127.0.0.1:%s" % API_PORT
        html = urllib.request.urlopen(base + "/", timeout=15, context=ctx).read().decode("utf-8", "replace")
        m = re.search(r"/assets/(index-[A-Za-z0-9_-]+\.js)", html)
        js_name = m.group(1) if m else ""
        want = dict((r.split("/")[-1], n) for _, r, _, _, _, n in ARTIFACTS if r.startswith("web/assets/"))
        blob = urllib.request.urlopen(base + "/assets/" + js_name, timeout=30, context=ctx).read() if js_name else b""
        got = hashlib.md5(blob).hexdigest() if blob else "-"
        exp = want.get(js_name)
        if exp is not None and got == exp:
            print("  服务端发 web    OK：index.html 指向 %s（%d B / %s）= 交付件"
                  % (js_name, len(blob), got[:8]))
        else:
            print("  服务端发 web    ★index.html 指向 %r（%d B / %s），交付件是 %s"
                  % (js_name, len(blob), got, list(want)))
            bad += 1
    except Exception as e:
        print("  服务端发 web    ★取不到（%s）⇒ 判红，不把「没检查」当放过" % e)
        bad += 1

    print()
    if bad:
        print("结论：%d 项红 ⇒ 不要继续；先 tail -60 %s，必要时 revert + restart。" % (bad, LOGFILE))
        return 7
    print("结论：装机对账全绿（%d/%d md5 + axs 全量 + 服务端 web 字节 + 进程 + 端口 + 活体插件 + 本进程日志）。"
          % (len(ARTIFACTS), len(ARTIFACTS)))
    return 0


def cmd_selftest():
    """★ 判据自证：把**同一段告警检测代码**跑在**历史轮转日志**上，必须真的命中
    早前那次**真实拒载**。证明三件事：
      ① 检测器不是橡皮图章（能在真日志上变红）；
      ② 启动横幅锚定能把 17 次启动切成 17 段；
      ③ 切片确实**改变结论**（全文 grep 有 2 条，末段=现役进程 0 条）
         —— 这就是假红第 14 形态的量化复现。
    只读，不碰生产件。"""
    import glob
    cands = sorted(glob.glob(RUN + "/server.log.pre_b15_*") + glob.glob(RUN + "/server.log.*"))
    if not cands:
        print("★ 找不到任何轮转日志（%s/server.log.*）⇒ 无法自证" % RUN)
        return 2
    # 取最大的那份（= 装机前那份跨多次启动的完整日志）
    path = max(cands, key=os.path.getsize)
    with open(path, "r", errors="replace") as f:
        lines = [ANSI.sub("", l).rstrip("\n") for l in f]
    idx = [i for i, l in enumerate(lines) if BANNER in l]
    print("历史件          %s（%d 行，%d B）" % (os.path.basename(path), len(lines),
                                              os.path.getsize(path)))
    print("启动横幅        %d 条 ⇒ 可切成 %d 段进程生命周期" % (len(idx), len(idx)))
    segs = []
    for n, a in enumerate(idx):
        b = idx[n + 1] if n + 1 < len(idx) else len(lines)
        segs.append(lines[a:b])
    total = 0
    for n, seg in enumerate(segs):
        h = [l for l in seg if any(k in l for k in PLUGIN_ALARMS)]
        total += len(h)
        if h:
            print("  段%-3d 行 %-5d 告警 ★%d 条   %s" % (n + 1, len(seg), len(h), h[0][:90]))
    whole = [l for l in lines if any(k in l for k in PLUGIN_ALARMS)]
    last = [l for l in segs[-1] if any(k in l for k in PLUGIN_ALARMS)]

    print()
    print("  ① 检测器可变红    %s（分段共命中 %d 条真实拒载）"
          % ("OK" if total >= 1 else "★否 —— 橡皮图章！", total))
    print("  ② 横幅可分段      %s（%d 段）" % ("OK" if len(idx) >= 2 else "★否", len(idx)))
    print("  ③ 切片改变结论    %s（全文 %d 条 → 末段 %d 条）"
          % ("OK" if (len(whole) > len(last)) else "★否", len(whole), len(last)))
    ok = total >= 1 and len(idx) >= 2 and len(whole) > len(last)
    print()
    print("结论：%s" % ("自证通过 —— 告警检测器真能红，且必须切片。" if ok
                     else "★自证失败 ⇒ verify 的『告警 无』不可信，先修检测器。"))
    return 0 if ok else 1


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    table = {"status": cmd_status, "stage": cmd_stage,
             "apply": cmd_apply, "revert": cmd_revert,
             "restart": cmd_restart, "verify": cmd_verify,
             "install": cmd_install, "selftest": cmd_selftest}
    if cmd not in table:
        print(__doc__)
        return 1
    return table[cmd]()


if __name__ == "__main__":
    sys.exit(main())

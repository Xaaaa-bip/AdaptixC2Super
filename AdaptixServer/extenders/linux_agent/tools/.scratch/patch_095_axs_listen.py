#!/usr/bin/env python3
"""patch_095_axs_listen.py — 给 linux_agent 补 `listen` 命令的 axscript 声明。

为什么必须补（p33 判据抓到的真缺陷）
------------------------------------
`pl_main.go` 里的 `case "listen":` 只是**分发器**的一个分支。teamserver 在把
cmdline 交给插件之前，先过 `core/axscript/registry.go:106`：

    resolved := cs.Resolve(agentName, listener, os, commandName)
    if resolved == nil { return nil, fmt.Errorf("command '%s' not found ...") }

命令表来自 `ax_config.axs` 的 `ax.create_command(...)` + `create_commands_group`。
`link`/`unlink`（同属 pivot 家族）在 205/209 行都声明了，**`listen` 漏了** ⇒
即使 C 侧和 Go 侧代码全对，`listen <port>` 也永远到不了 agent：

    send_raw -> {'ok': False, 'message': "unknown command: command 'listen'
                 not found for agent 'linux' os=2"}

★ 归档教训：**"命令能被发出去"和"命令被实现了"是两件事**。只写 case 分支
不写 axs 声明，症状是"功能静默不可达"，而且只在**发命令**时才暴露 ——
比编译错误更难发现。

用法
----
  python3 patch_095_axs_listen.py apply    # 三个副本一起打
  python3 patch_095_axs_listen.py revert   # 从 BAKDIR 还原
  python3 patch_095_axs_listen.py status

★ 交付树不得留 `.bak*`（l2p_bakdir.py 要求树内为 0），所以备份一律进 BAKDIR。
"""
import hashlib, os, shutil, sys, time

BAKDIR = "/root/gp5probe/bak15/p33_axs"
LA = "/root/cc/AdaptixServer/extenders/linux_agent"
RIG = "/root/gp5probe/revpivot_rig/extenders/linux_agent"

TARGETS = [
    ("tree",     LA + "/ax_config.axs"),
    ("tree-dist", LA + "/dist/ax_config.axs"),
    ("rig",      RIG + "/ax_config.axs"),
]

DECL_ANCHOR = '    cmd_unlink.addArgString("id", true);\n'
DECL_NEW = (
    '    cmd_unlink.addArgString("id", true);\n'
    '\n'
    '    // 反向 pivot：父 agent 监听，子 agent 拨入（见 pivot.c pivot_listen_tcp\n'
    '    // / main.c bind 分支）。port=0 停止监听。\n'
    '    let cmd_listen = ax.create_command("listen", "反向 pivot：本机监听端口，等待子 agent 拨入（0 = 停止）", "listen 4444", "Task: listen pivot");\n'
    '    cmd_listen.addArgInt("port", true);\n'
)

GROUP_OLD = "cmd_link, cmd_ls,"
GROUP_NEW = "cmd_link, cmd_listen, cmd_ls,"


def md5(p):
    return hashlib.md5(open(p, "rb").read()).hexdigest()


def bak_path(p):
    tag = p.replace("/", "__")
    return os.path.join(BAKDIR, tag)


def apply_one(tag, path):
    src = open(path, encoding="utf-8").read()
    if "cmd_listen" in src:
        print("  [=] %-9s 已含 cmd_listen，跳过 (%s)" % (tag, md5(path)[:12]))
        return True
    for anchor, name in ((DECL_ANCHOR, "DECL_ANCHOR"), (GROUP_OLD, "GROUP_OLD")):
        n = src.count(anchor)
        if n != 1:
            print("  [X] %-9s 锚点 %s 出现 %d 次（应为 1）-- 拒绝改动" % (tag, name, n))
            return False
    src = src.replace(DECL_ANCHOR, DECL_NEW).replace(GROUP_OLD, GROUP_NEW)
    bp = bak_path(path)
    os.makedirs(BAKDIR, exist_ok=True)
    if not os.path.exists(bp):
        shutil.copy2(path, bp)
    tmp = path + ".p33new"
    open(tmp, "w", encoding="utf-8").write(src)
    os.replace(tmp, path)
    print("  [V] %-9s %s -> %s" % (tag, md5(bp)[:12], md5(path)[:12]))
    return True


def revert_one(tag, path):
    bp = bak_path(path)
    if not os.path.exists(bp):
        print("  [X] %-9s 无备份 %s" % (tag, bp))
        return False
    shutil.copy2(bp, path)
    print("  [V] %-9s 还原 -> %s" % (tag, md5(path)[:12]))
    return True


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    if cmd == "apply":
        print("APPLY patch_095 (axs listen 声明)")
        ok = all([apply_one(t, p) for t, p in TARGETS])
        print("APPLY: %s  BAKDIR=%s" % ("OK" if ok else "FAILED", BAKDIR))
        return 0 if ok else 1
    if cmd == "revert":
        print("REVERT patch_095")
        ok = all([revert_one(t, p) for t, p in TARGETS])
        print("REVERT: %s" % ("OK" if ok else "FAILED"))
        return 0 if ok else 1
    print("STATUS patch_095")
    for t, p in TARGETS:
        src = open(p, encoding="utf-8").read()
        print("  %-9s cmd_listen=%-5s md5=%s" % (t, "cmd_listen" in src, md5(p)[:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

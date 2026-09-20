#!/usr/bin/env python3
"""patch_096_argint.py — 修 `addArgInt` 参数读不到（整族命令失效）。

根因
----
`core/axscript/parser.go:148-153` 把 `addArgInt` 的实参解析成 **Go `int`**：

    case ArgTypeInt:
        intVal, err := strconv.Atoi(strings.TrimSpace(val))
        result.Args[commandArg.Name] = intVal        // <- int，不是 float64

而 `pl_main.go` 读这些参数用的是 `args[key].(float64)` 断言 ⇒ **必然失败**。
`getFloatArg` 失败后调用方 `goto RET`，此时 `var cmd Command` 仍是零值 ⇒
发出的任务 code=0 ⇒ agent 侧 `commander.c` 落到 `default:` ⇒ 回 "Unknown command"。

实测（p33 判据抓到的）：
    send_raw(父, "listen 42002") -> a_text = "Unknown command"
    send_raw(父, "link 127.0.0.1 9") -> a_text = "Unknown command"   <- 既有命令同样受害

受影响的一整族（8 个命令，全部是本文件里 addArgInt 的参数）
----------------------------------------------------------
    inject <pid>        getFloatArg  -> Unknown command
    kill <pid>          getFloatArg  -> Unknown command
    ps kill <pid>       getFloatArg  -> Unknown command
    link <ip> <port>    getFloatArg  -> Unknown command   （既有，一直没被发现：
                          rportfwd 走 tunnel 通道，从不经过 pivot 的 cmdline）
    listen <port>       getFloatArg  -> Unknown command   （本次新增）
    timestomp <ts>      直接 .(float64) -> 断言失败，时间戳**静默变 0**
    socks start <port>  直接 .(float64) -> 断言失败，**跳过范围校验且 port 恒为 0**
                                          （内核随机挑一个端口，是最坏的一种：
                                           不报错、端口不对）
★ 归档教训：**"命令能被发出去" ≠ "命令被实现了" ≠ "参数被收到了"**。
  三层里任何一层静默失败，台面上都长得像"这条命令不存在"。

★ `pl_main.go:378` 的 `listenerMap["port_bind"].(float64)` **不动** —— 那个值来自
  `json.Unmarshal` 进 `map[string]any`，本来就是 float64，是正确的。

改法（3 处，全在本文件）
------------------------
  1. `getFloatArg` 收 int/int64/float64 三种形态（缺参/非数值仍报错，语义不变）
     ⇒ 一处修好 5 个调用点
  2. `timestomp` 的直接断言改用 getFloatArg
  3. `socks` 的直接断言改用 getFloatArg，并把"port 拿不到"从静默变响

用法
----
  python3 patch_096_argint.py apply | revert | status
"""
import hashlib, os, shutil, sys

BAKDIR = "/root/gp5probe/bak15/p33_argint"
TARGET = "/root/cc/AdaptixServer/extenders/linux_agent/pl_main.go"

OLD_GETFLOAT = '''func getFloatArg(args map[string]any, key string) (float64, error) {
	v, ok := args[key].(float64)
	if !ok {
		return 0, fmt.Errorf("parameter '%s' must be set", key)
	}
	return v, nil
}
'''

NEW_GETFLOAT = '''// getFloatArg 读一个数值参数。
//
// ★ 必须收 int：axscript 的 addArgInt 在 core/axscript/parser.go:152 走
// strconv.Atoi，落到 CreateCommand 的 args 里是 Go int，不是 float64。
// 原来这里只做 .(float64) 断言 ⇒ 对 addArgInt 参数必然失败 ⇒ 调用方
// goto RET ⇒ 发出 code=0 的任务 ⇒ agent 回 "Unknown command"。
// 受影响：inject / kill / ps kill / link / listen，见各调用点。
//
// 缺参或非数值仍然报错 —— 放宽的是类型，不是错误语义。
// （续行不留前导空格：gofmt 会把缩进的续行当代码块重排。）
func getFloatArg(args map[string]any, key string) (float64, error) {
	switch v := args[key].(type) {
	case float64:
		return v, nil
	case int:
		return float64(v), nil
	case int64:
		return float64(v), nil
	}
	return 0, fmt.Errorf("parameter '%s' must be set", key)
}
'''

OLD_TIMESTOMP = '''		timestamp := uint64(0)
		if ts, ok := args["timestamp"].(float64); ok {
			timestamp = uint64(ts)
		}
'''

NEW_TIMESTOMP = '''		timestamp := uint64(0)
		// ★ 同 getFloatArg 头注：addArgInt 的值是 int，原来的 .(float64)
		// 断言拿不到 ⇒ 时间戳**静默变 0**（不报错、结果错）。
		if ts, errTs := getFloatArg(args, "timestamp"); errTs == nil {
			timestamp = uint64(ts)
		}
'''

OLD_SOCKS = '''		portNumber, ok := args["port"].(float64)
		port := int(portNumber)
		if ok {
			if port < 1 || port > 65535 {
				err = errors.New("端口必须在 1 到 65535 之间")
				goto RET
			}
		}
'''

NEW_SOCKS = '''		// ★ 同 getFloatArg 头注：addArgInt 的值是 int，原来的 .(float64) 断言拿不到
		// ⇒ ok=false ⇒ **跳过范围校验**，且 port 恒为 0（内核随机挑端口）。
		// 那是最坏的一种失败：不报错、端口却不对。现在拿不到就明说。
		portF, errPort := getFloatArg(args, "port")
		if errPort != nil {
			err = errors.New("参数 port 未设置")
			goto RET
		}
		port := int(portF)
		if port < 1 || port > 65535 {
			err = errors.New("端口必须在 1 到 65535 之间")
			goto RET
		}
'''

EDITS = [
    ("getFloatArg", OLD_GETFLOAT, NEW_GETFLOAT),
    ("timestomp", OLD_TIMESTOMP, NEW_TIMESTOMP),
    ("socks", OLD_SOCKS, NEW_SOCKS),
]

MARK = "addArgInt 的值是 int"


def md5(p):
    return hashlib.md5(open(p, "rb").read()).hexdigest()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    src = open(TARGET, encoding="utf-8").read()

    if cmd == "status":
        print("STATUS patch_096")
        print("  已打标记 = %s   md5=%s" % (MARK in src, md5(TARGET)[:12]))
        for name, old, _ in EDITS:
            print("  %-11s 旧文锚点出现 %d 次（apply 需为 1）" % (name, src.count(old)))
        return 0

    if cmd == "apply":
        if MARK in src:
            print("  已打过，跳过 (md5=%s)" % md5(TARGET)[:12])
            return 0
        for name, old, _ in EDITS:
            n = src.count(old)
            if n != 1:
                print("  [X] %-11s 锚点出现 %d 次（应为 1）-- 拒绝改动" % (name, n))
                return 1
        for name, old, new in EDITS:
            src = src.replace(old, new)
        os.makedirs(BAKDIR, exist_ok=True)
        bp = os.path.join(BAKDIR, "pl_main.go")
        if not os.path.exists(bp):
            shutil.copy2(TARGET, bp)
        tmp = TARGET + ".p96new"
        open(tmp, "w", encoding="utf-8").write(src)
        os.replace(tmp, TARGET)
        print("  [V] APPLY %s -> %s (bak %s)" % (md5(bp)[:12], md5(TARGET)[:12], bp))
        return 0

    if cmd == "revert":
        bp = os.path.join(BAKDIR, "pl_main.go")
        if not os.path.exists(bp):
            print("  [X] 无备份 %s" % bp)
            return 1
        shutil.copy2(bp, TARGET)
        print("  [V] REVERT -> %s" % md5(TARGET)[:12])
        return 0

    print("unknown command", cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main())

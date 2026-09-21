package main

/* 批次20：BOF 模块的**服务端权威视图 + 上传 + 编译**（list / upload / compile）。
 *
 * ── 为什么必须有服务端接口，而不是把清单写在前端 ──────────────────────────
 * AdaptixWeb/src/components/Bofs.tsx:15 的 BOFS 表硬编码 18 项，而运行树
 * /root/cc/run/extenders/linux_agent/bofs/ 实测 26 个 .o（批次20 实测计数）。
 * 硬编码清单还有第二重错、且更隐蔽：Bofs.tsx:81 把基路径默认成
 * "/root/cc/run/extenders/linux_agent/bofs/" —— 这是**本机部署路径**，
 * 换机器/换目录即静默失效（点了「执行」才在控制台报
 * "neither an existing file nor valid base64"，pl_main.go:1604）。
 * ⇒ 清单、计数、基路径三者都必须由服务端回答。本文件就是那个回答。
 *
 * ── 为什么走**鉴权**端点而不是 public ────────────────────────────────────
 * /bofs/upload 与 /bofs/compile 的语义就是「在 teamserver 上落文件并执行编译器」，
 * 做成无鉴权等于把 RCE 挂到公网上。public 变体只该给「agent 必须无凭据取件」的
 * stager 用（pl_stager_linux.go:148）。核心两套都实现了：
 *   core/server/ts_endpoints.go:10  TsEndpointRegister      鉴权，handler 收 username
 *   core/server/ts_endpoints.go:43  TsEndpointRegisterPublic  公开
 * 鉴权组的挂载点：connector.go:368 `api_group.Use(… token.ValidateAccessToken() …)`；
 * username 来源 token/jwt.go:101 `ctx.Set("username", claims.Username)`。
 * profile.yaml `Teamserver.endpoint: "/api"` ⇒ 下面注册的 "/bofs/list"
 * 对外实际路径是 `/api/bofs/list`（gin Group 前缀）。
 * ★ 零核心改动：extender.Teamserver（core/extender/utils.go:86-88）本就声明了
 *   鉴权三件套，而 server.go:58 `NewExtender(ts)` 传进去的就是
 *   *server.Teamserver 本人 ⇒ 本插件只需在本地接口补声明（pl_main.go）。
 *
 * ── 编译配方与 deploy.sh **逐字节同源** ─────────────────────────────────
 *     gcc -c -o <out.o> <src.c> -include bof_api.h -Os -fPIC \
 *         -fno-stack-protector -I <agent_src>
 * （tools/deploy.sh:63-64）。本项目铁律：批量重编脚本必须与被验证配方同源，
 * 否则「绿」只证明两条配方各自自洽。区别仅在于 deploy.sh 在 SRC 树编、再把 .o
 * 拷进 RUN 树，而本接口在 RUN 树原地编。
 *
 * ★★ 两种做法产出的 .o 是否同字节 —— **实测过**（批次20，gcc 13.3.0）：
 *    同一份 host_recon.c 从三个不同绝对路径 + 三个不同 -I 目录编译，
 *    三份 .o 全同（A vs B / A vs C 均 IDENTICAL），且与 run/bofs/host_recon.o
 *    的现役产物同字节 ⇒ **.o 字节不含路径**。
 *    推论一（安全性）：运行态重编那 26 个共享 BOF 产出与 deploy.sh 完全一致的字节
 *      ⇒ 不会让 deploy.sh --check / l2p_threelayer 变红，即本功能不引入漂移。
 *    推论二（判据）：源码未改时编译必须是 **no-op** ——
 *      bofsCompileOne 返回 Changed=false，前端据此显示「已是当前版本」。
 *      这条同时是自检：若某次编译 Changed=true 而源与产物本应一致，
 *      说明有人在别处改了编译输入（头文件漂移等），值得当场看见。
 *
 * ── 内存纪律（服务器 1.9G，实测 available ≈1.5G）───────────────────────
 *   · 上传上限 .c 1MiB / .o 4MiB；编译输出只回传头尾共 8KiB。
 *   · bofsCompileMu 串行化 gcc —— 并发 gcc 是最容易把内存吃穿的路径。
 *   · 产物先写同目录点号临时文件再 rename（原子落盘，且点文件被清单过滤）。
 *     ★ 全程不落 /tmp：本项目 p17 T6 已归档「任何文件进 /tmp 会误触红判据」。
 */

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	bofsSrcSub = "src_agent/bofs"  // .c 源码（上传目标；deploy.sh 里叫 BOF_SRC）
	bofsIncSub = "src_agent/agent" // bof_api.h 所在，等价 deploy.sh 的 $AGENT_SRC
	bofsOutSub = "bofs"            // .o 产物（agent 侧 execute bof <path> 读这里）

	bofsMaxSrcBytes = 1 << 20 // 1 MiB —— BOF 源码量级 5~20KiB，留两个数量级余量
	bofsMaxObjBytes = 4 << 20 // 4 MiB
	bofsOutClipMax  = 8 << 10 // 编译器输出回传上限
	bofsCompileWait = 60 * time.Second
)

var (
	// 模块名白名单。★ 这是**落盘路径**与 **argv** 的双重边界：
	// 名字直接参与 filepath.Join 与 gcc 的 -o/-I 参数，必须排除 "/"、".."、
	// 空格、引号、前导 "-"。字符集收到 [A-Za-z0-9_-] 且限长 64。
	// ★★ 首字符单独收紧成 [A-Za-z0-9_]：写成 `^[A-Za-z0-9_-]{1,64}$` 时
	//    `-x` / `--help` 是**合法**的（'-' 在字符集里），与上一段"必须排除
	//    前导 -"自相矛盾 —— 由 TestBofsNameWhitelist 抓出。
	//    在当前调用形态下它不可直接利用（由名字派生的每个 argv 项不是绝对路径
	//    就是 '.' 前缀的临时名），但这条守卫的意义正是"将来有人重构成相对路径
	//    或走 shell 时仍然成立" ⇒ 按承诺收紧代码，而不是改注释迁就代码。
	bofsNameRe = regexp.MustCompile(`^[A-Za-z0-9_][A-Za-z0-9_-]{0,63}$`)

	// 串行化编译。理由见文件头「内存纪律」。
	bofsCompileMu sync.Mutex
)

// ---------------------------------------------------------------- 路径

// bofsAbs —— 把 module 内的路径绝对化（折进进程 cwd）。
//
// ★★ 本文件对路径的铁律：**交给 OS 或 gcc 的每一条路径都必须是绝对的**。
//
//    为什么不能只靠 InitPlugin 里那一次 filepath.Abs(moduleDir)：ModuleDir 是
//    包级变量，测试、将来的其它调用方都可能直接赋值；一个自洽的功能单元不该把
//    自己的正确性押在「别人已经规范化过这个全局」上。收口在这里，则不论
//    ModuleDir 是绝对、相对、还是带 "./" 前缀，本文件的路径语义都恒定。
//
//    为什么非要绝对：这些路径的终点全是**按 cwd 解析**的系统调用 ——
//    os.Stat / os.ReadFile / exec.Cmd 的 Dir+argv。一旦 ModuleDir 是相对的，
//    gcc 会把**相对源路径**再按**相对 cmd.Dir** 解析一次，得到
//        extenders/linux_agent/extenders/linux_agent/src_agent/bofs/x.c
//    ⇒ `cc1: fatal error: No such file or directory`。
//    这是批次20 运行态**实测**到的真故障，不是推演（见 TestBofsCompileUnderRelativeModuleDir）。
//
//    为什么原来的测试没抓到：所有编译用例都把 ModuleDir 设成 t.TempDir 系的
//    **绝对**路径，相对形态这个生产实况一次都没进过 —— 判据恒真。
func bofsAbs(p string) string {
	if abs, err := filepath.Abs(p); err == nil {
		return abs
	}
	return p
}

func bofsOutDir() string { return bofsAbs(filepath.Join(ModuleDir, bofsOutSub)) }
func bofsSrcDir() string { return bofsAbs(filepath.Join(ModuleDir, bofsSrcSub)) }
func bofsIncDir() string { return bofsAbs(filepath.Join(ModuleDir, bofsIncSub)) }

// bofsBaseForClient —— 前端「BOF 基路径」输入框该填的值。
// 末尾补 "/" 与 Bofs.tsx:111 的拼接习惯一致（`endsWith('/') ? s : s + '/'`）。
//
// 这个值最终被前端拼成 `execute bof <base><name>.o`，交给服务端
// pl_main.go:1595 的 os.Stat/os.ReadFile 去解析 —— 同样按 cwd 解析。
// 若回相对路径，os.Stat 失败后**静默**落进 base64 分支，agent 收到一串
// **路径文本**当 BOF 字节，报错却是「BOF 格式错误」，离真因十万八千里。
// bofsOutDir() 已由 bofsAbs 保证绝对，这里直接拼即可。
func bofsBaseForClient() string { return bofsOutDir() + "/" }

// ---------------------------------------------------------------- 小工具

func bofsJSON(code int, v any) (int, []byte) {
	b, err := json.Marshal(v)
	if err != nil {
		return http.StatusInternalServerError,
			[]byte(`{"ok":false,"error":"response marshal failed","message":"response marshal failed"}`)
	}
	return code, b
}

// bofsErr —— 统一错误信封。
//
// ★ 同一个错因写进 "error" 与 "message" 两个键，因为它们服务两个不同的消费方：
//
//	"error"   —— 本接口自有的结构化错因，细粒度（含 gcc 输出等），前端 BOF 页读它。
//	"message" —— **框架既有约定**：AdaptixWeb/src/api/http.ts:92 里
//	             `if (data && data.ok === false) throw new Error(data.message || 'request failed')`。
//
//	只给 "error" 不给 "message" 的后果（批次20 实测，非推演）：上传失败时前端一律
//	弹 "request failed"，**真错因在传输层就被吞掉**——用户在页面上永远看不到
//	「非 ELF」「已存在」「名字不合规」。这正是本项目反复归档的
//	「UI 填了但没生效」族：后端明明回了话，前端听不见。
//
//	两个键同值，不引入第二套语义，也不改动任何既有前端代码。
func bofsErr(code int, format string, a ...any) (int, []byte) {
	msg := fmt.Sprintf(format, a...)
	return bofsJSON(code, map[string]any{"ok": false, "error": msg, "message": msg})
}

func bofsSHA256File(path string) string {
	f, err := os.Open(path)
	if err != nil {
		return ""
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return ""
	}
	return hex.EncodeToString(h.Sum(nil))
}

// bofsClip —— 超长时保留**头尾**：gcc 的第一条错误在头部，汇总行在尾部，
// 只留尾部会丢掉「第一个真正的错误」（历史教训：只看尾行会误判成别的问题）。
func bofsClip(s string, max int) string {
	if len(s) <= max {
		return s
	}
	half := max / 2
	return s[:half] + fmt.Sprintf("\n…（省略 %d 字节）…\n", len(s)-max) + s[len(s)-half:]
}

// bofsAtomicWrite —— 同目录临时文件 + rename。点号前缀使其中途不可见（清单过滤点文件），
// 且 rename 在同一文件系统上是原子的 ⇒ 读方永远看不到半截文件。
func bofsAtomicWrite(path string, data []byte, mode os.FileMode) error {
	dir := filepath.Dir(path)
	tmp, err := os.CreateTemp(dir, "."+filepath.Base(path)+".tmp*")
	if err != nil {
		return err
	}
	name := tmp.Name()
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		os.Remove(name)
		return err
	}
	if err := tmp.Chmod(mode); err != nil {
		tmp.Close()
		os.Remove(name)
		return err
	}
	_ = tmp.Sync()
	if err := tmp.Close(); err != nil {
		os.Remove(name)
		return err
	}
	if err := os.Rename(name, path); err != nil {
		os.Remove(name)
		return err
	}
	return nil
}

// ---------------------------------------------------------------- 清单

type bofEntry struct {
	Name      string `json:"name"`
	Obj       bool   `json:"obj"`
	ObjSize   int64  `json:"obj_size"`
	ObjMTime  int64  `json:"obj_mtime"`
	ObjSHA256 string `json:"obj_sha256"`
	Src       bool   `json:"src"`
	SrcSize   int64  `json:"src_size"`
	SrcMTime  int64  `json:"src_mtime"`
	// Stale：有源码但产物缺失/比源码旧 ⇒ 需要重编。前端据此把「编译」按钮点亮。
	Stale bool `json:"stale"`
}

// bofsListExt 列出 dir 下扩展名为 ext 的模块名 ⇒ FileInfo。
// 不合规的文件名**收集进 ignored 而不是静默跳过** —— 静默吞掉正是本项目反复
// 归档的「UI 列了 N 个但实际有 N+2 个」族。
func bofsListExt(dir, ext string) (map[string]os.FileInfo, []string) {
	out := map[string]os.FileInfo{}
	var ignored []string
	des, err := os.ReadDir(dir)
	if err != nil {
		return out, ignored
	}
	for _, de := range des {
		n := de.Name()
		if strings.HasPrefix(n, ".") {
			continue // 点文件：原子落盘的中间态，不属于清单
		}
		if !strings.HasSuffix(n, ext) {
			ignored = append(ignored, n)
			continue
		}
		base := strings.TrimSuffix(n, ext)
		if !bofsNameRe.MatchString(base) {
			ignored = append(ignored, n)
			continue
		}
		info, err := de.Info()
		if err != nil {
			continue
		}
		out[base] = info
	}
	return out, ignored
}

func bofsScan() ([]bofEntry, []string) {
	objs, ignObj := bofsListExt(bofsOutDir(), ".o")
	srcs, ignSrc := bofsListExt(bofsSrcDir(), ".c")

	seen := map[string]bool{}
	for n := range objs {
		seen[n] = true
	}
	for n := range srcs {
		seen[n] = true
	}
	names := make([]string, 0, len(seen))
	for n := range seen {
		names = append(names, n)
	}
	sort.Strings(names)

	entries := make([]bofEntry, 0, len(names))
	for _, n := range names {
		e := bofEntry{Name: n}
		if fi, ok := objs[n]; ok {
			e.Obj = true
			e.ObjSize = fi.Size()
			e.ObjMTime = fi.ModTime().Unix()
			e.ObjSHA256 = bofsSHA256File(filepath.Join(bofsOutDir(), n+".o"))
		}
		if fi, ok := srcs[n]; ok {
			e.Src = true
			e.SrcSize = fi.Size()
			e.SrcMTime = fi.ModTime().Unix()
		}
		// ★ 用「产物比源码旧」而不是「哈希不符」判 stale：哈希要在编译后才算得出，
		//   而这里要回答的是「**还需不需要**编」。mtime 是充分的启发式，
		//   真正的裁决点在 compile（Changed 字段，基于字节比对）。
		e.Stale = e.Src && (!e.Obj || e.ObjMTime < e.SrcMTime)
		entries = append(entries, e)
	}

	var notes []string
	if _, err := os.Stat(bofsSrcDir()); err != nil {
		notes = append(notes, fmt.Sprintf("源码目录不存在（尚未上传过 .c，只能执行不能重编）：%s", bofsSrcDir()))
	}
	if _, err := os.Stat(bofsIncDir()); err != nil {
		notes = append(notes, fmt.Sprintf("★ 头文件目录不存在，上传 .c 后的编译必然失败：%s", bofsIncDir()))
	}
	if len(ignObj) > 0 {
		notes = append(notes, fmt.Sprintf("bofs/ 下有 %d 个非模块文件（未计入清单）：%s",
			len(ignObj), strings.Join(ignObj, ", ")))
	}
	if len(ignSrc) > 0 {
		notes = append(notes, fmt.Sprintf("src_agent/bofs/ 下有 %d 个非模块文件（未计入清单）：%s",
			len(ignSrc), strings.Join(ignSrc, ", ")))
	}
	return entries, notes
}

// ---------------------------------------------------------------- GET /bofs/list

func bofsHandleList(username string, _ []byte) (int, []byte) {
	entries, notes := bofsScan()
	nObj, nSrc, nStale := 0, 0, 0
	for _, e := range entries {
		if e.Obj {
			nObj++
		}
		if e.Src {
			nSrc++
		}
		if e.Stale {
			nStale++
		}
	}
	if notes == nil {
		notes = []string{}
	}
	return bofsJSON(http.StatusOK, map[string]any{
		"ok":      true,
		"user":    username,
		"base":    bofsBaseForClient(),
		"count":   len(entries),
		"n_obj":   nObj,
		"n_src":   nSrc,
		"n_stale": nStale,
		"dirs": map[string]string{
			"obj": bofsOutDir(),
			"src": bofsSrcDir(),
			"inc": bofsIncDir(),
		},
		"entries": entries,
		"notes":   notes,
	})
}

// ---------------------------------------------------------------- 编译

type bofsCompileResult struct {
	Name      string `json:"name"`
	OK        bool   `json:"ok"`
	Changed   bool   `json:"changed"`
	ObjSize   int64  `json:"obj_size"`
	ObjSHA256 string `json:"obj_sha256"`
	Output    string `json:"output"`
	Error     string `json:"error,omitempty"`
	Millis    int64  `json:"ms"`
}

// bofsCompileOne —— 单个模块。**唯一**的 gcc 调用点，list/upload/compile 三条路
// 都汇到这里，避免出现第二份配方（本项目铁律）。
func bofsCompileOne(name string) bofsCompileResult {
	res := bofsCompileResult{Name: name}
	if !bofsNameRe.MatchString(name) {
		res.Error = fmt.Sprintf("模块名不合规（只允许 [A-Za-z0-9_-]，≤64 字符）：%q", name)
		return res
	}
	src := filepath.Join(bofsSrcDir(), name+".c")
	if _, err := os.Stat(src); err != nil {
		res.Error = fmt.Sprintf("源码不存在：%s", src)
		return res
	}
	gcc, err := exec.LookPath("gcc")
	if err != nil {
		res.Error = "找不到 gcc（deploy.sh 同样依赖 PATH 里的 gcc）"
		return res
	}
	if err := os.MkdirAll(bofsOutDir(), 0755); err != nil {
		res.Error = fmt.Sprintf("创建产物目录失败：%v", err)
		return res
	}

	bofsCompileMu.Lock()
	defer bofsCompileMu.Unlock()

	tmp, err := os.CreateTemp(bofsOutDir(), "."+name+".o.tmp*")
	if err != nil {
		res.Error = fmt.Sprintf("创建临时产物失败：%v", err)
		return res
	}
	tmpName := tmp.Name()
	tmp.Close()
	defer os.Remove(tmpName) // 成功路径下 rename 已把它移走，这里是失败/异常兜底

	// ★ 参数与 tools/deploy.sh:63-64 逐字同源（顺序也照抄，便于人眼对拍）。
	//   -o 指向同目录临时文件 ⇒ rename 原子替换；绝不写 /tmp。
	//
	// ★★ 不依赖进程 cwd 的**充要条件**是「argv 里的路径全绝对」，而不是
	//    「设了 cmd.Dir」。上一版注释在这里写"cmd.Dir 显式设为 ModuleDir：
	//    本行不依赖进程 cwd"—— **这句话是假的**：当 ModuleDir 是相对路径时，
	//    src 与 cmd.Dir 都相对，gcc 会把 src 按 cmd.Dir 再解析一次，得到
	//        extenders/linux_agent/extenders/linux_agent/src_agent/bofs/x.c
	//    ⇒ cc1: fatal error: No such file or directory（批次20 运行态实测）。
	//    现在 src / tmpName / -I 三者都由 bofsAbs 保证绝对，cmd.Dir 只是
	//    给 gcc 一个确定的工作目录（它不再参与任何路径解析）。
	//    唯一仍是裸文件名的是 `-include bof_api.h`：它由绝对 -I 解析，同样与 cwd 无关。
	args := []string{
		"-c", "-o", tmpName, src,
		"-include", "bof_api.h",
		"-Os", "-fPIC", "-fno-stack-protector",
		"-I", bofsIncDir(),
	}
	ctx, cancel := context.WithTimeout(context.Background(), bofsCompileWait)
	defer cancel()
	cmd := exec.CommandContext(ctx, gcc, args...)
	cmd.Dir = bofsAbs(ModuleDir)
	var buf bytes.Buffer
	cmd.Stdout = &buf
	cmd.Stderr = &buf

	start := time.Now()
	runErr := cmd.Run()
	res.Millis = time.Since(start).Milliseconds()
	res.Output = bofsClip(buf.String(), bofsOutClipMax)

	if ctx.Err() == context.DeadlineExceeded {
		res.Error = fmt.Sprintf("编译超时（>%s）", bofsCompileWait)
		return res
	}
	if runErr != nil {
		res.Error = fmt.Sprintf("gcc 失败：%v", runErr)
		return res
	}

	fresh, err := os.ReadFile(tmpName)
	if err != nil || len(fresh) == 0 {
		res.Error = "gcc 报告成功但产物为空"
		return res
	}

	// ★ 与现有产物**比字节**判定 Changed（不是比 mtime）：
	//   由文件头那条实测性质（.o 字节不含路径）可知，源码未改时必然同字节，
	//   所以 Changed=true 是一个可信的「编译输入确实变了」信号。
	old, _ := os.ReadFile(filepath.Join(bofsOutDir(), name+".o"))
	res.Changed = !bytes.Equal(old, fresh)
	if res.Changed {
		if err := os.Rename(tmpName, filepath.Join(bofsOutDir(), name+".o")); err != nil {
			res.Error = fmt.Sprintf("落盘失败：%v", err)
			return res
		}
	}
	res.OK = true
	res.ObjSize = int64(len(fresh))
	h := sha256.Sum256(fresh)
	res.ObjSHA256 = hex.EncodeToString(h[:])
	return res
}

func bofsSrcNames() []string {
	srcs, _ := bofsListExt(bofsSrcDir(), ".c")
	names := make([]string, 0, len(srcs))
	for n := range srcs {
		names = append(names, n)
	}
	sort.Strings(names)
	return names
}

// ---------------------------------------------------------------- POST /bofs/upload

type bofsUploadReq struct {
	Name      string `json:"name"`
	Kind      string `json:"kind"` // "c" = 源码（落盘后编译）；"o" = 直接投产物
	Content   string `json:"content_b64"`
	Overwrite bool   `json:"overwrite"`
	// Compile 只对 kind="c" 有意义。nil ⇒ 默认 true（用户要的就是"上传并编译"）。
	Compile *bool `json:"compile"`
}

// bofsELFType 读 ELF 头 e_type（offset 16，小端 2 字节）。
// 返回 -1 表示不是 ELF。ET_REL=1 才是可重定位目标文件 —— agent 侧的 BOF 加载器
// 走的是重定位解析（src_agent/agent/elf_bof.c），喂给它 ET_DYN/ET_EXEC 只会在
// 目标机上以晦涩错误失败。宁可在上传口就把类型说清楚。
func bofsELFType(data []byte) int {
	if len(data) < 18 || data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F' {
		return -1
	}
	return int(data[16]) | int(data[17])<<8
}

func bofsHandleUpload(username string, body []byte) (int, []byte) {
	var req bofsUploadReq
	if err := json.Unmarshal(body, &req); err != nil {
		return bofsErr(http.StatusBadRequest, "请求体不是合法 JSON：%v", err)
	}
	if !bofsNameRe.MatchString(req.Name) {
		return bofsErr(http.StatusBadRequest,
			"模块名不合规（只允许 [A-Za-z0-9_-]，≤64 字符）：%q", req.Name)
	}
	if req.Kind != "c" && req.Kind != "o" {
		return bofsErr(http.StatusBadRequest, "kind 必须是 \"c\" 或 \"o\"（收到 %q）", req.Kind)
	}
	limit := bofsMaxSrcBytes
	if req.Kind == "o" {
		limit = bofsMaxObjBytes
	}
	// base64 的长度上界先卡一道，避免为超大体积分配内存（1.9G 机器上的实际防护）
	if len(req.Content) > limit/3*4+1024 {
		return bofsErr(http.StatusRequestEntityTooLarge,
			"内容超过上限（%s 上限 %d 字节）", req.Kind, limit)
	}
	data, err := base64.StdEncoding.DecodeString(strings.TrimSpace(req.Content))
	if err != nil {
		return bofsErr(http.StatusBadRequest, "content_b64 不是合法 base64：%v", err)
	}
	if len(data) == 0 {
		return bofsErr(http.StatusBadRequest, "内容为空")
	}
	if len(data) > limit {
		return bofsErr(http.StatusRequestEntityTooLarge,
			"内容 %d 字节超过上限 %d（kind=%s）", len(data), limit, req.Kind)
	}

	out := map[string]any{"ok": true, "user": username, "name": req.Name, "kind": req.Kind,
		"size": len(data)}

	if req.Kind == "o" {
		if t := bofsELFType(data); t < 0 {
			return bofsErr(http.StatusBadRequest, "kind=o 的内容不是 ELF 文件")
		} else if t != 1 {
			return bofsErr(http.StatusBadRequest,
				"kind=o 的 ELF 类型是 %d，不是 ET_REL(1) —— BOF 必须是可重定位目标文件（gcc -c 的产物）", t)
		}
		if err := os.MkdirAll(bofsOutDir(), 0755); err != nil {
			return bofsErr(http.StatusInternalServerError, "创建产物目录失败：%v", err)
		}
		dst := filepath.Join(bofsOutDir(), req.Name+".o")
		if !req.Overwrite {
			if _, err := os.Stat(dst); err == nil {
				return bofsErr(http.StatusConflict, "已存在 %s；如需覆盖请在请求里带 overwrite=true", dst)
			}
		}
		if err := bofsAtomicWrite(dst, data, 0644); err != nil {
			return bofsErr(http.StatusInternalServerError, "写入失败：%v", err)
		}
		out["obj"] = dst
		out["sha256"] = bofsSHA256File(dst)
		out["base"] = bofsBaseForClient()
		return bofsJSON(http.StatusOK, out)
	}

	// kind == "c"
	if err := os.MkdirAll(bofsSrcDir(), 0755); err != nil {
		return bofsErr(http.StatusInternalServerError, "创建源码目录失败：%v", err)
	}
	dst := filepath.Join(bofsSrcDir(), req.Name+".c")
	if !req.Overwrite {
		if _, err := os.Stat(dst); err == nil {
			return bofsErr(http.StatusConflict, "已存在 %s；如需覆盖请在请求里带 overwrite=true", dst)
		}
	}
	if err := bofsAtomicWrite(dst, data, 0644); err != nil {
		return bofsErr(http.StatusInternalServerError, "写入失败：%v", err)
	}
	out["src"] = dst

	doCompile := true
	if req.Compile != nil {
		doCompile = *req.Compile
	}
	if !doCompile {
		out["compiled"] = false
		out["base"] = bofsBaseForClient()
		return bofsJSON(http.StatusOK, out)
	}

	res := bofsCompileOne(req.Name)
	out["compiled"] = res.OK
	out["compile"] = res
	out["base"] = bofsBaseForClient()
	if !res.OK {
		// ★ 源码已落盘、编译失败 ⇒ 201 而不是 400：文件确实写进去了，
		//   前端要能区分「没上传」和「上传了但编不过」，并把 gcc 输出显示出来。
		return bofsJSON(http.StatusCreated, out)
	}
	return bofsJSON(http.StatusOK, out)
}

// ---------------------------------------------------------------- POST /bofs/compile

type bofsCompileReq struct {
	Name string `json:"name"`
	All  bool   `json:"all"`
}

func bofsHandleCompile(username string, body []byte) (int, []byte) {
	var req bofsCompileReq
	if len(bytes.TrimSpace(body)) > 0 {
		if err := json.Unmarshal(body, &req); err != nil {
			return bofsErr(http.StatusBadRequest, "请求体不是合法 JSON：%v", err)
		}
	}

	if req.All {
		names := bofsSrcNames()
		if len(names) == 0 {
			return bofsErr(http.StatusNotFound, "源码目录下没有可编译的 .c：%s", bofsSrcDir())
		}
		results := make([]bofsCompileResult, 0, len(names))
		nOK, nChanged := 0, 0
		for _, n := range names {
			r := bofsCompileOne(n)
			if r.OK {
				nOK++
			}
			if r.Changed {
				nChanged++
			}
			results = append(results, r)
		}
		return bofsJSON(http.StatusOK, map[string]any{
			"ok": true, "user": username, "all": true,
			"count": len(names), "n_ok": nOK, "n_changed": nChanged,
			"results": results,
		})
	}

	if !bofsNameRe.MatchString(req.Name) {
		return bofsErr(http.StatusBadRequest,
			"模块名不合规（只允许 [A-Za-z0-9_-]，≤64 字符），或改用 {\"all\":true}：%q", req.Name)
	}
	r := bofsCompileOne(req.Name)
	out := map[string]any{
		"ok": r.OK, "user": username, "name": req.Name, "result": r,
		"base": bofsBaseForClient(),
	}
	if r.OK {
		return bofsJSON(http.StatusOK, out)
	}
	// ★ 顶层必须带 message：前端 http.ts:92 在 ok===false 时抛的是 data.message。
	//   只把错因塞在 result 里 ⇒ UI 一律显示 'request failed'，gcc 的真实报错
	//   （缺头文件、语法错、-I 指错）**根本到不了操作员眼前** —— 他看得见「失败」
	//   却看不见「为什么失败」，只能去服务器上翻日志。这正是本项目归档的
	//   「UI 填了但没生效」族的根：后端回了话，前端听不见。
	//   result 原样保留，供脚本/自动化按结构化字段消费。
	out["error"] = r.Error
	out["message"] = bofsCompileErrText(r)
	out["output"] = r.Output
	return bofsJSON(http.StatusBadRequest, out)
}

// bofsCompileErrText —— 把「错因 + 编译器输出」合成一句给人看的 message。
// 输出已由 bofsCompileOne 截到 bofsOutClipMax；这里再收一次是为了让 toast 不至于
// 糊满整屏 —— 被截掉的部分在 out["output"] / result.output 里仍是完整的。
func bofsCompileErrText(r bofsCompileResult) string {
	msg := r.Error
	if msg == "" {
		msg = "编译失败"
	}
	if o := strings.TrimSpace(r.Output); o != "" {
		const clip = 2000
		if len(o) > clip {
			o = o[:clip] + "\n…（已截断，完整输出见 result.output）"
		}
		msg += "\n" + o
	}
	return msg
}

// ---------------------------------------------------------------- 加载时自愈

// bofsAutoCompile 在插件加载时把**缺失或过期**的 .o 补齐。
//
// ★ 为什么必须有这一步：BOF 的 .o **不在构建产物里**，是运行期用 gcc 现编的
// （linux_agent/Makefile:25-26 只把 src_agent/bofs/*.c 拷进 dist/，没有编译步骤；
// 对象目录 extenders/linux_agent/bofs/ 由本文件的 compile 端点 MkdirAll 现建）。
// 所以**全新部署的 n_obj=0**，26 个模块一个都跑不了。
//
// 后果不是"少个功能"这么轻：前端 completions.ts 的 loadBofNames() 只保留
// obj !== false 的条目 ⇒ 候选列表为空 ⇒ 命令行敲 `execute bof ` **一个候选都不弹、
// 也不报任何错**，操作员只会认为补全坏了（2026-09-18 在 154 上实测到的正是它）。
// 从前唯一的补救是起服后手工 POST /bofs/compile {"all":true}
// （build_deploy.sh 的 --bof-compile，而且**默认还是关的**）——
// 与「操作员都是人，不该知道有这一步」相悖，故改为加载时自愈。
//
// 只在真有 stale/missing 时才编：健康部署 n_stale=0，本函数零开销（一次 ReadDir）。
// 编译失败**不阻止插件加载**：路由照常注册，操作员仍可在 BOF 页看到 gcc 的真实
// 报错并手工重试；这里只把结论打成日志，不改变任何既有行为。
func bofsAutoCompile() {
	entries, notes := bofsScan()

	var todo []string
	nSrc := 0
	for _, e := range entries {
		if !e.Src {
			continue
		}
		nSrc++
		// Stale 的定义就是「有源码、但产物缺失或比源码旧」——正是该编的集合。
		if e.Stale {
			todo = append(todo, e.Name)
		}
	}
	if len(todo) == 0 {
		return
	}

	// 头文件目录缺失时编译必然全失败：与其刷 26 行 gcc 报错，不如说清真正的原因。
	if _, err := os.Stat(bofsIncDir()); err != nil {
		fmt.Printf("[linux_agent] BOF 自动编译跳过：头文件目录不存在（%s）—— 上传 .c 后编译也会失败\n", bofsIncDir())
		return
	}

	sort.Strings(todo)
	fmt.Printf("[linux_agent] BOF 产物缺失或过期 %d/%d，正在自动编译（全新部署的正常步骤，约数秒）...\n",
		len(todo), nSrc)

	t0 := time.Now()
	var failed []bofsCompileResult
	for _, n := range todo {
		if r := bofsCompileOne(n); !r.OK {
			failed = append(failed, r)
		}
	}

	if len(failed) == 0 {
		fmt.Printf("[linux_agent] BOF 自动编译完成：%d/%d 成功，耗时 %.1fs\n",
			len(todo), nSrc, time.Since(t0).Seconds())
		return
	}

	names := make([]string, 0, len(failed))
	for _, r := range failed {
		names = append(names, r.Name)
	}
	fmt.Printf("[linux_agent] BOF 自动编译部分失败：%d/%d 成功，失败 %d 个（%s），耗时 %.1fs\n",
		len(todo)-len(failed), nSrc, len(failed), strings.Join(names, ", "), time.Since(t0).Seconds())
	fmt.Printf("[linux_agent]   ⇒ 控制台「BOF」页点「全量重编」可看到每个模块的 gcc 原始报错\n")
	for _, r := range failed {
		fmt.Printf("[linux_agent]   %s: %s\n", r.Name, bofsCompileErrText(r))
	}
	for _, note := range notes {
		fmt.Printf("[linux_agent]   注：%s\n", note)
	}
}

// ---------------------------------------------------------------- /bofs/exec

/* BOF 命令行的**候选清单**（按目标 OS 给）。
 *
 * ── 为什么必须有这个端点：候选给什么名字，决定了 execute bof 能不能用 ──────
 * 执行侧（linux_agent/pl_main.go、beacon_agent/pl_main.go、gopher_agent/pl_main.go、
 * agent_kharon/src_server/pl_agent.go）把实参**先当文件路径、再当 base64** 解析，
 * 而路径是按**进程 cwd** 解析的（cwd = /home/ad/dist）。
 * 前端此前只能凭内置清单猜名字（completions.ts 的 FALLBACK_BOFS，形如
 * "host_recon.o"）——`os.Stat("host_recon.o")` 在 cwd 下必然失败 ⇒ 静默落进
 * base64 分支 ⇒ **agent 把一串路径文本当机器码收下**，报错却是「BOF 格式错误」。
 * ⇒ 候选必须是**服务端绝对路径**，而「哪个绝对路径现在真的存在」只有服务端知道。
 *   这就是本端点存在的全部理由。
 *
 * ── 为什么不复用 /bofs/list ────────────────────────────────────────────
 * /bofs/list 回答的是「linux_agent 自己的 .c/.o 编译状态」（上传/编译页要用），
 * 与「这台主机上 execute bof 能填什么」是**两个不同的问题**：Windows 主机要的是
 * Extension-Kit 的 COFF，而 /bofs/list 里一个都没有。混成一个端点会让
 * 「n_obj=0」这类判据同时表示两件事，正是本项目反复归档的"判据恒真"。
 */

const bofsExecMaxEntries = 400

// bofsExecEntry —— 一条候选。name 是**要敲进命令行的整串**，label 只是显示用。
// 两者分开是因为候选会很长（/home/ad/dist/Extension-Kit/SAL-BOF/_bin/sc_enum.x64.o），
// 弹层里显示 basename 才读得懂，但插入命令行的必须是全路径。
//
// Stale 只对 linux_agent 自己那份（有 .c 可比）有意义；Extension-Kit 的 .o 旁边
// 没有源码，一律 false —— 「不知道」不能标成「确定是新的」以外的东西，见扫描处注释。
type bofsExecEntry struct {
	Name        string `json:"name"`
	Label       string `json:"label"`
	Group       string `json:"group"`
	Mtime       int64  `json:"mtime,omitempty"`
	Stale       bool   `json:"stale,omitempty"`
	StaleReason string `json:"stale_reason,omitempty"`
}

// bofsExecRoot —— 一个候选根，以及**它凭什么被选中**。
// 换一台服务器部署后候选为空时，排障的第一步就是看这个字段：
// source=="axscripts" 说明服务端自己报的目录，source=="fallback" 说明
// 服务端不支持该能力、这里是猜的（见 bofsExecRoots）。
type bofsExecRoot struct {
	Dir    string `json:"dir"`
	Source string `json:"source"`
}

// bofsNormAgent —— 把 a_name 归一成判据用的短名。
// 前端 normType()（completions.ts:103）做同样的归一；两边都归一，
// 才不会被 "linux" / "linux_agent" 这种写法差异拆成两个分支各走各的。
func bofsNormAgent(s string) string {
	s = strings.ToLower(strings.TrimSpace(s))
	return strings.TrimSuffix(s, "_agent")
}

// bofsFoldDirs —— 去重 + 折叠父子目录。
//
// ★ 为什么在**消费侧**而不是让服务端保证：只有这里知道根目录会被**递归**扫描。
//   WalkDir 一个根就覆盖它的全部子目录，父目录已覆盖的子目录再列一次，只会让
//   同一批 .o 出现两遍 —— 候选里重复条目比少一条更难用（看不出该选哪个）。
//   服务端 TsAxScriptDirs 返回的每个目录都是真话（那里确实有脚本），
//   「怎么走」才是本侧的事。放这里也让它能被单测直接覆盖。
func bofsFoldDirs(dirs []string) []string {
	seen := make(map[string]bool, len(dirs))
	uniq := make([]string, 0, len(dirs))
	for _, d := range dirs {
		if d == "" || d == "." || seen[d] {
			continue
		}
		seen[d] = true
		uniq = append(uniq, d)
	}
	sort.Strings(uniq)

	// 排序后父目录必在子目录之前，所以单趟即可。
	var out []string
	for _, d := range uniq {
		covered := false
		for _, kept := range out {
			// ★ 必须比到分隔符：裸 strings.HasPrefix 会把 /a/kit2 当成 /a/kit 的子目录
			//   而整个折掉 —— 那是把一棵真实的根凭空弄丢。
			if strings.HasPrefix(d, kept+string(filepath.Separator)) {
				covered = true
				break
			}
		}
		if !covered {
			out = append(out, d)
		}
	}
	return out
}

// bofsExecRoots —— BOF 候选根。**优先问服务端，问不到才退回相对路径猜测。**
//
// ★ 为什么不再猜：这里原来只试三个相对 ModuleDir 的路径（../../Extension-Kit 等）。
//   换一台服务器部署、扩展包换位置、或跑在 systemd 的另一个 WorkingDirectory 下，
//   三个都猜不中 ⇒ 候选静默为空。而服务端本来就知道真值 —— 就是 profile 里
//   axscripts 加载出来的那些 .axs 所在目录（TsAxScriptDirs，走
//   ScriptManager.ListScripts 的 Path）。**能问的地方不该去猜。**
//
// ★ 兜底分支保留，但它只是**降级路径**：老服务端没有 TsAxScriptDirs（可选断言，
//   见 pl_main.go 的 axScriptDirsProvider），此时退回原来的相对路径猜测，
//   总比整个功能失效强。单测也依赖它。返回的 Source 字段让调用方分得清
//   「服务端说的」和「我猜的」—— 这一点必须回给前端，否则降级是静默的。
func bofsExecRoots() ([]bofsExecRoot, []string) {
	var roots []bofsExecRoot
	var tried []string

	if AxScriptDirs != nil {
		for _, d := range bofsFoldDirs(AxScriptDirs()) {
			if st, err := os.Stat(d); err == nil && st.IsDir() {
				roots = append(roots, bofsExecRoot{Dir: d, Source: "axscripts"})
			} else {
				tried = append(tried, d+"（axscripts 已加载脚本的所在目录，但当前不可读）")
			}
		}
	}
	if len(roots) > 0 {
		return roots, tried
	}

	if AxScriptDirs == nil {
		tried = append(tried, "（本服务端不支持 TsAxScriptDirs，已退回相对路径猜测）")
	}
	// ★ 兜底候选**不能过 bofsFoldDirs**：它内部要 sort，而这三是**按优先级**排的
	//   （越靠近本部署越先试），排序会把优先级搅乱；何况命中即 break，本来就只有一条。
	for _, c := range []string{
		bofsAbs(filepath.Join(ModuleDir, "..", "..", "Extension-Kit")), // <dist>/extenders/linux_agent → <dist>
		bofsAbs(filepath.Join(ModuleDir, "..", "..", "..", "Extension-Kit")),
		bofsAbs("Extension-Kit"), // 兜底：进程 cwd 下（服务端 cwd 就是 <dist>）
	} {
		tried = append(tried, c)
		if st, err := os.Stat(c); err == nil && st.IsDir() {
			roots = append(roots, bofsExecRoot{Dir: c, Source: "fallback"})
			break
		}
	}
	return roots, tried
}

// bofsExecScanLinux —— linux_agent 自己的 BOF 候选。
//
// ★ 陈旧判定**复用 bofsScan()**，不在这里重新 stat 一遍：stale 的语义
//   （有源码 && 产物缺失或更旧）在 bofsScan 里已经定死并被
//   TestBofsScanClassification 覆盖。同一事实写两份配方，迟早给出两个答案 ——
//   而「哪个才是对的」正是本项目最贵的一类 bug。
//
// 副作用（有意接受）：bofsScan 按 bofsNameRe 过滤模块名，所以名字不合规的 .o
// 不会出现在候选里 —— 与 /bofs/list 的可见性保持一致。这类文件在 /bofs/list
// 的 ignored 里会被点名，不会凭空消失。
func bofsExecScanLinux() ([]bofsExecEntry, error) {
	scanned, _ := bofsScan()
	dir := bofsOutDir()
	out := []bofsExecEntry{}
	for _, e := range scanned {
		if !e.Obj {
			continue // 只有 .c 没产物 ⇒ 不可执行，不进候选
		}
		item := bofsExecEntry{
			Name:  filepath.Join(dir, e.Name+".o"),
			Label: e.Name + ".o",
			Group: "linux_agent/bofs",
			Mtime: e.ObjMTime,
			Stale: e.Stale,
		}
		if e.Stale {
			item.StaleReason = "源码 .c 比产物 .o 新，跑的是旧版本；到「BOF」页重新编译"
		}
		out = append(out, item)
	}
	return out, nil
}

// bofsExecStem —— 从 `<名字>.<arch>.o` 取出 `<名字>`，用于和 `<名字>.c` 配对。
//
// 为什么单独一个函数：arch 是通配（""/"*"）时后缀只剩 ".o"，此时**必须**再削掉
// 最后一段扩展名，否则 `thing.x64.o` 会得出 `thing.x64`，配对永远落空 ——
// 而这种失效是静默的：候选照常列出，只是一条陈旧都不标。通配只可能来自直接调
// API 的客户端（前端总带具体 arch），但静默错答比报错更难查，所以照样钉住。
func bofsExecStem(name, arch string) string {
	stem := strings.TrimSuffix(name, ".o")
	if arch == "" || arch == "*" {
		if i := strings.LastIndex(stem, "."); i > 0 {
			return stem[:i]
		}
		return stem
	}
	return strings.TrimSuffix(stem, "."+arch)
}

// bofsExecScanRoots 递归收集 roots 下匹配 *.<arch>.o 的对象文件。
// 实测布局（2026-09-18）：Extension-Kit/<分类>-BOF/_bin/<名字>.<arch>.o，
// 共 221 个 .o（x64 166 / x32 46 / x86 9）—— 深度不是 3，别按固定层数取。
//
// ── 顺带判陈旧（缺陷 C「有些 bof 过时了」）────────────────────────────────
// 原来的注释断言「.o 旁边没有 .c，无从判陈旧」—— **实测是错的**：本树有 236 个
// .c，只是不在 `_bin/` 旁边，而在同一个 `<分类>-BOF/` 下的 `<名字>/<名字>.c`。
// 所以同一次遍历里把 .c 也收进索引，走完再判。判据只有一条：
//
//	**同分类下、同名 stem 的 .c 恰好一个**，且它比 .o 新 ⇒ 标 stale；其余一律不标。
//
// 三个「不标」都是刻意的，方向一致 —— **宁可漏标，不可错标**：
//   - 0 个源码：源码不在本树（在别的子仓），无从判。实测 166 条里 61 条如此。
//   - 多个源码：同名歧义，判不出是哪一个。
//   - 跨分类同名**不借**：whoami 在 SAL-BOF 与 AD-BOF/SQL-BOF 各有一个 .o，
//     而 .c 只有 SAL-BOF 那份。借过来就是给 BOF A 贴 BOF B 的诊断。
//
// 实测覆盖率 103/166（同分类唯一匹配）。与 linux 侧 bofsScan() 的**语义**一致
// （源码比产物新 ⇒ 跑的是旧版本），但源码布局完全不同、映射无法共用 —— 这不是
// 「第二份陈旧配方」，是另一个事实。linux 侧仍是唯一那份 bofsScan()。
//
// 成本：.c 索引与 .o 收集共用同一次遍历（实测 714 文件 / 28ms）。若条目数触顶
// （bofsExecMaxEntries=400）提前 SkipAll，索引可能不全 ⇒ 少标几条，方向安全。
func bofsExecScanRoots(roots []bofsExecRoot, arch string) ([]bofsExecEntry, error) {
	suffix := ".o"
	if arch != "" && arch != "*" {
		suffix = "." + arch + ".o"
	}

	out := []bofsExecEntry{}
	// stem 与 out 一一对应。**必须在这里算，不能事后从 Label 反推**：
	// arch="*" 时 suffix 只是 ".o"，从 Label 反推出来的 stem 是 "whoami.x64"，
	// 永远匹配不上 whoami.c ⇒ 通配下陈旧判定整体失效且不报错（已由
	// TestBofsExecScanRootsArchWildcard 钉住）。
	stems := []string{}
	// 分类 → stem → .c 路径集。分类 = 根下第一级目录（SAL-BOF / AD-BOF …），
	// 与 Group 同口径 —— 判陈旧和分组用的是同一个「同类」定义。
	srcIdx := map[string]map[string][]string{}

	for _, root := range roots {
		base := filepath.Base(root.Dir)
		err := filepath.WalkDir(root.Dir, func(p string, d fs.DirEntry, walkErr error) error {
			if walkErr != nil {
				// 单个子目录不可读不该毁掉整份清单 —— 跳过继续走。
				return nil
			}
			if d.IsDir() {
				return nil
			}
			rel, rerr := filepath.Rel(root.Dir, p)
			if rerr != nil {
				return nil
			}
			group := base
			if i := strings.IndexRune(rel, filepath.Separator); i > 0 {
				group = base + "/" + rel[:i]
			}

			if strings.HasSuffix(d.Name(), ".c") {
				stem := strings.TrimSuffix(d.Name(), ".c")
				byStem := srcIdx[group]
				if byStem == nil {
					byStem = map[string][]string{}
					srcIdx[group] = byStem
				}
				byStem[stem] = append(byStem[stem], p)
				return nil
			}
			if !strings.HasSuffix(d.Name(), suffix) {
				return nil
			}

			e := bofsExecEntry{Name: p, Label: d.Name(), Group: group}
			if fi, ierr := d.Info(); ierr == nil {
				e.Mtime = fi.ModTime().Unix()
			}
			out = append(out, e)
			stems = append(stems, bofsExecStem(d.Name(), arch))
			if len(out) >= bofsExecMaxEntries {
				return fs.SkipAll
			}
			return nil
		})
		if err != nil {
			return out, err
		}
		if len(out) >= bofsExecMaxEntries {
			break
		}
	}

	// 判陈旧放在遍历**之后**：.c 可能在对应的 .o 之后才被走到。
	for i := range out {
		cs := srcIdx[out[i].Group][stems[i]]
		if len(cs) != 1 {
			continue // 0 个 = 源码不在本树；多个 = 同名歧义。都不标。
		}
		fi, serr := os.Stat(cs[0])
		if serr != nil || out[i].Mtime <= 0 {
			continue
		}
		if fi.ModTime().Unix() > out[i].Mtime {
			out[i].Stale = true
			out[i].StaleReason = fmt.Sprintf(
				"同名源码 %s 比该产物新（产物可能是旧版本）；在该分类目录下重新 make",
				cs[0])
		}
	}
	return out, nil
}

// bofsHandleExec —— POST /bofs/exec  {"agent":"beacon", "os":1, "arch":"x64"}
//
// ★ 为什么是 POST + body 而不是 GET ?os=N：extender 侧的 TsEndpointRegister
//   （core/extender/utils.go:86）只把 (username, body) 交给 handler，
//   **没有 *http.Request**，拿不到 query；而 Teamserver 侧的 TsEndpointRegisterRaw
//   （core/server/ts_endpoints.go:17）并未暴露给 extender 接口。为一个查询参数去改
//   核心接口不划算，body 携带同样清楚。
//
// ★ 为什么以 agent 而不是 os 为第一判据：**BOF 能力是 agent 的属性，不是 OS 的。**
//   实测同一台 Linux 上，linux agent 有 BOF、gopher agent 没有（gopher_agent
//   的 execute 分支只在 Os==WINDOWS 下做事）。按 os=2 发 linux_agent 的 .o 给一台
//   Linux gopher，候选它一条也用不了 —— 那不是「提示不准」，是把用户引向必然失败
//   的命令。agent 名取自 AgentData.a_name（前端 a_name，api/types.ts:5）。
//
// os 取值与 adaptix.OsWindows/OsLinux/OsMac、AgentData.a_os 一致（1/2/3）。
// os 仍要传：gopher 的 BOF 能力取决于它跑在哪个系统上，agent 名本身不足以判定。
func bofsHandleExec(_ string, body []byte) (int, []byte) {
	var req struct {
		Agent string `json:"agent"`
		Os    int    `json:"os"`
		Arch  string `json:"arch"`
	}
	if len(body) > 0 {
		if err := json.Unmarshal(body, &req); err != nil {
			return bofsErr(http.StatusBadRequest, "invalid request body: %v", err)
		}
	}
	if req.Arch == "" {
		req.Arch = "x64"
	}

	resp := map[string]any{
		"ok": true, "agent": req.Agent, "os": req.Os, "arch": req.Arch, "source": "",
	}

	// 兼容路径：老前端只发 os。仍然能工作，但必须把「这是推断的」说出去 ——
	// 静默接受一个已知不精确的判据，正是当初按 os 发错清单的成因。
	if strings.TrimSpace(req.Agent) == "" {
		resp["deprecated"] = "请求未带 agent 字段，已按 os 推断；BOF 能力是 agent 的属性而不是 OS 的，请升级前端。"
		switch req.Os {
		case 1:
			req.Agent = "beacon"
		case 2:
			req.Agent = "linux"
		case 3:
			req.Agent = "macos"
		default:
			return bofsErr(http.StatusBadRequest, "unsupported os %d and no agent given (1=Windows 2=Linux 3=macOS)", req.Os)
		}
	}

	var entries []bofsExecEntry
	// capable = 「这台主机的 agent 到底有没有 BOF 加载器」，与「当前有几条候选」
	// 是**两个问题**，必须分开答。
	//
	// ★ 为什么不能让前端拿 entries 是否为空去推断能力：
	//   全新部署时 linux_agent 只有 .c 没有 .o ⇒ 候选 0 条，但能力是**有**的
	//   （编译一次就有了）。若用「空 = 不支持」去隐藏 BOF 入口，操作员就再也
	//   进不去那个**用来编译**的页面 —— 正是本项目反复归档的「静默空」陷阱。
	//   反过来，「不支持」的 agent（macos / Linux gopher）给一份候选也没用。
	//   所以这个字段由服务端明说，前端只负责显示，不自己维护一份能力矩阵
	//   —— 那第二份迟早与这里漂移。
	capable := false

	// scanKit：beacon / kharon / Windows-gopher 共用的 Extension-Kit 收集。
	scanKit := func() {
		roots, tried := bofsExecRoots()
		resp["roots"] = roots
		if len(roots) == 0 {
			resp["note"] = fmt.Sprintf("在服务端找不到可扫描的 BOF 根目录，已尝试：%s。"+
				"Windows 主机的 execute bof 需要 Extension-Kit 下的 .x64.o；"+
				"请确认 profile 的 axscripts 里加载了 Extension-Kit（这是动态根目录的唯一出处）。",
				strings.Join(tried, "、"))
			return
		}
		resp["base"] = roots[0].Dir
		resp["source"] = "Extension-Kit"
		got, err := bofsExecScanRoots(roots, req.Arch)
		if err != nil {
			resp["note"] = fmt.Sprintf("扫描 BOF 根目录时出错：%v", err)
			return
		}
		entries = got
		if len(entries) == 0 {
			resp["note"] = fmt.Sprintf("已扫描 %s，其中没有匹配 *.%s.o 的对象文件（Extension-Kit 需要先 make）。",
				roots[0].Dir, req.Arch)
		}
	}

	switch bofsNormAgent(req.Agent) {
	case "linux":
		capable = true
		got, err := bofsExecScanLinux()
		if err != nil {
			return bofsErr(http.StatusInternalServerError, "cannot list %s: %v", bofsOutDir(), err)
		}
		entries = got
		resp["source"] = "linux_agent"
		resp["base"] = bofsBaseForClient()
		resp["roots"] = []bofsExecRoot{{Dir: bofsOutDir(), Source: "linux_agent"}}
		if len(entries) == 0 {
			resp["note"] = fmt.Sprintf("服务端 %s 下没有 .o 产物；在「BOF」页上传 .c 并编译后才会出现在这里。", bofsOutDir())
		}

	case "beacon", "kharon":
		// 这两个 extender 的 execute 分支没有 OS 守卫，Windows COFF 走 Extension-Kit。
		capable = true
		scanKit()

	case "gopher":
		// ★ gopher_agent/pl_main.go 的 execute 分支是 `if agentData.Os != OS_WINDOWS { goto RET }`，
		//   Linux/macOS 的 gopher 拿到候选也执行不了 ⇒ 给 0 条 + 说明，而不是发一份用不了的清单。
		if req.Os != 1 {
			resp["source"] = "none"
			resp["note"] = fmt.Sprintf("gopher agent 只在 Windows 上有 BOF 加载器（当前 os=%d）。"+
				"该主机的 execute bof 不会被处理；请改用 shell / 该 agent 自带的命令。", req.Os)
			break
		}
		capable = true
		scanKit()

	case "macos":
		resp["source"] = "none"
		resp["note"] = "macos agent 的 extender 里没有 execute 分支，execute bof 不会被执行，因此没有可用候选。"

	default:
		resp["source"] = "none"
		resp["note"] = fmt.Sprintf("未知 agent %q：不知道它是否支持 execute bof，因此不提供候选"+
			"（宁可空着也不给一份可能必然失败的清单）。", req.Agent)
	}
	// 放在 switch 之后统一写：漏写一个分支的表现是「能力说没有、候选却有一堆」，
	// 而这句话在两个地方各写一次就迟早会漏一个。
	resp["capable"] = capable

	// 稳定排序：弹层按 label 过滤，无序清单会让「同一个前缀每次列出不同一批」。
	sort.Slice(entries, func(i, j int) bool {
		if entries[i].Label != entries[j].Label {
			return entries[i].Label < entries[j].Label
		}
		return entries[i].Name < entries[j].Name
	})
	if entries == nil {
		entries = []bofsExecEntry{}
	}
	resp["entries"] = entries
	resp["count"] = len(entries)

	return bofsJSON(http.StatusOK, resp)
}

// ---------------------------------------------------------------- 注册

func bofsInitRoutes() error {
	routes := []struct {
		method, path string
		h            func(string, []byte) (int, []byte)
	}{
		{"GET", "/bofs/list", bofsHandleList},
		{"POST", "/bofs/upload", bofsHandleUpload},
		{"POST", "/bofs/compile", bofsHandleCompile},
		{"POST", "/bofs/exec", bofsHandleExec},
	}
	var errs []string
	var ok []string
	for _, r := range routes {
		if err := Ts.TsEndpointRegister(r.method, r.path, r.h); err != nil {
			errs = append(errs, fmt.Sprintf("%s %s: %v", r.method, r.path, err))
			continue
		}
		// 只把**注册成功**的列进横幅：一行的存在本身就代表它可用。
		ok = append(ok, fmt.Sprintf("%s /api%s", r.method, r.path))
	}
	if len(ok) > 0 {
		fmt.Printf("[linux_agent] bofs routes ready: %s\n", strings.Join(ok, ", "))
	}
	if len(errs) > 0 {
		return fmt.Errorf("%s", strings.Join(errs, "; "))
	}
	return nil
}

#!/usr/bin/env bash
# Phase5 部署（运行态 /root/cc/run）—— 分阶段，每步带断言，可单独回滚。
#
#   l2p_deploy.sh build      只构建（写 dist/），不碰运行态
#   l2p_deploy.sh install    把 dist/ 四件套拷进运行态两棵树（不重启）
#   l2p_deploy.sh restart    kill + 起 teamserver + 等 38212 LISTEN
#   l2p_deploy.sh verify     e2e 验收（对 38212）
#   l2p_deploy.sh all        build → install → restart → verify
#   l2p_deploy.sh rollback   从 /root/gp5probe/backup 最近一份 tar 恢复两棵树
#   l2p_deploy.sh status     现况（哈希/端口/进程）
#
# ★ 硬前提（脚本自己验，不靠人记）：
#   ① 真树源码已是目标形态（listener 无 DIAG、crypto 三处等价）—— 否则拒绝构建
#   ② teamserver 二进制与插件的工具链/模块版本一致（plugin.Open 逐包比构建 ID）
#   ③ 构建走 Makefile（**显式文件列表**）—— 用 `.`（整包）产物不同（实测差 32B / 682KB）
#   ④ .so 被进程 mmap 时**不能原地覆盖**（实测 ⇒ SIGBUS 打死长跑实例）
#      ⇒ install 与 restart 之间不许有别的动作；且 restart 一定是 kill → 等端口释放 → 起
#   ⑥ 构建期的环境变量**不能漏给交付实例**（本脚本 export 了 GOMAXPROCS=1 / GOCACHE /
#      GOMODCACHE / TMPDIR / GOTOOLCHAIN）—— 直接继承会把运行态 teamserver 限成单核、
#      临时目录指到 scratch ⇒ 启动时逐项 env -u 摘掉
#   ⑤ 没有 git：运行态两棵树的 tar + 本脚本 = 唯一回滚手段
# ★ M5① 修复（批次3）：原为 `set -u`，**无 pipefail** ⇒ 一切 `... | tail` / `... | head`
#   的退出码都变成尾部命令的 0，"失败"被静默吞掉。
#   ★ 复核更正（批次3 实测，勿照抄审计原文）：审计原文称"make 失败会拿**旧产物**继续过
#     need/libp2p/DIAG 三条并装上线"—— 这条**不成立**：两份 Makefile 的默认目标都是
#     `all: clean`（第 1 行），clean 是**第一条**动作且不会失败 ⇒ go build 失败时 dist
#     已被删 ⇒ 下游 `need "$LST/dist/....so"` 必然 die。真实触发面是"**基于 .so 之外
#     的中间产物**的失败"（如 agent 侧 so 成功、cp -r src_gopher 失败）——那条因为
#     `:86` 没有管道、`|| die` 直接有效，也拦得住。
#   ⇒ 加 pipefail 的理由不是"堵住一条已知的上线路径"，而是：**让失败在发生的那一行
#     就带正确理由停住**，而不是依赖下游三条判据"碰巧"也看同一个文件。判据理由正确
#     本身就是价值（见 `doc/问题归档.md` §「红的理由也是判据的一部分」）。
set -uo pipefail
# ★ 批次13：判据套件**不许在交付树里写 __pycache__**（python 字节码缓存是
#   "清完长回来"的唯一真生成者）。本行覆盖本脚本派生的全部 python：
#   l2p_depcheck.py / l2p_crypto.py / l2p_e2e.py。
export PYTHONDONTWRITEBYTECODE=1
LST=/root/cc/AdaptixServer/extenders/gopher_listener_tcp
AGT=/root/cc/AdaptixServer/extenders/gopher_agent
RLST=/root/cc/run/extenders/gopher_listener_tcp
RAGT=/root/cc/run/extenders/gopher_agent
BK=/root/gp5probe/backup
PORT=38212
LOGF=/root/cc/run/server.log
E2E=$LST/patches/l2p_e2e.py
CRY=$LST/patches/l2p_crypto.py
DEPCHK=$LST/patches/l2p_depcheck.py
FOUR="ax_config.axs config.yaml"
# ★ S11（批次3）：原 `TOK=/root/gp5probe/tmp/tok.txt` 是**死变量**（全脚本零引用），删。
#   死变量会让下一个人以为"鉴权 token 是从这里读的"，而真实路径在别处。

export GOWORK=off GOTOOLCHAIN=local GOMAXPROCS=1 GOMODCACHE=/root/go/pkg/mod \
       GOCACHE=/root/gp5cache TMPDIR=/root/gp5probe/tmp GOTMPDIR=/root/gp5probe/tmp

ok()   { echo "[ok  ] $*"; }
bad()  { echo "[BAD ] $*"; }
info() { echo "[info] $*"; }
die()  { bad "$*"; exit 1; }
need() { [ -e "$1" ] || die "缺文件 $1"; }
md5()  { md5sum "$1" | cut -d' ' -f1; }
# 二进制里某字面量**出现在多少行里**（**这是"构建里到底有没有它"的直接判据**，
# 不要用文件尺寸当代理量 —— 实测尺寸对 DIAG 不敏感，我为此误判过一轮）
# ★ 必须 -F：`[DIAG]` 当正则是**字符类**（匹配任意 D/I/A/G）⇒ 任何二进制都非零，
#   于是"有插桩"这个断言**永远为真**（本轮实测过一次假红）。字面量判据一律 -F。
# ★★ 名字里的"次数"要打折扣（审计 S11）：`grep -c` 数的是**匹配行数**，不是出现次数。
#   二进制里"行"= 换行符分隔的块，同一块里出现两次只算 1。
#   ⇒ 本函数只能用于 **`= 0` / `> 0` 的存在性判据**；任何 `== N` 的精确计数都是错的。
#   现有调用点已全部是存在性比较（见 M2 的双向符号判据），不要把这条忘了。
cnt()  { grep -acF -- "$2" "$1" 2>/dev/null || true; }

# ★ teamserver 的 pid：**必须按端口认领，不能用 `pgrep -f <命令行>`** ——
#   本机曾经用 `bash -l -c '... ./adaptixserver -web -profile central.profile ...'` 起的实例，
#   那个**外层包装 shell 的命令行里含有同样的字符串** ⇒ `pgrep -f | head -1` 选中的是它，
#   kill 掉的是包装 shell、teamserver（setsid 过的）安然无恙、端口不释放
#   ⇒ 脚本报"38212 仍被占用，拒绝启动"（2026-09-16 实测踩到）。
#   端口占用者才是权威；退路是 `pgrep -x`（精确进程名，不匹配命令行）。
# ★ pipefail 配套：把"$PORT 是否在 LISTEN"收成一个函数。
#   原脚本有 4 处散落的 `ss -ltn | grep -q ":$PORT "`；开 pipefail 后，`ss` 自身的
#   偶发失败与"真的没在监听"都是非零 —— 二者在**此处**语义相同（无法确认在监听
#   ⇒ 当作没在监听，是安全方向），所以不需要额外分支，但**必须统一**，否则四处
#   行为会随写法漂移。
listening() { ss -ltn 2>/dev/null | grep -qF ":$PORT "; }

# ★ S11（批次3）修复：原兜底是裸 `pgrep -x adaptixserver | head -1`，**不校验端口归属**。
#   本机同时存在 rig(39012) 与运行态(38212) 两个 adaptixserver 是常态（本项目一直这样跑
#   e2e）⇒ `head -1` 可能选中 rig ⇒ do_restart/do_rollback 会 kill 掉**不该动的那个实例**，
#   而脚本会打印"[ok] 旧实例已停"。★ 这是"杀错进程"级别的破坏，且**表现为成功**。
#   修：兜底路径也逐个核端口归属，核不出就返回空（调用方走"未运行"分支，不 kill 任何东西）。
tspid() {
  local p; p=$(ss -ltnp 2>/dev/null | grep -F ":$PORT " | grep -oE 'pid=[0-9]+' | head -1 | cut -d= -f2)
  [ -n "${p:-}" ] && { echo "$p"; return 0; }
  local q
  for q in $(pgrep -x adaptixserver 2>/dev/null); do
    ss -ltnp 2>/dev/null | grep -F ":$PORT " | grep -q "pid=$q," && { echo "$q"; return 0; }
  done
  return 0   # 明确返回空 ⇒ 调用方不得 kill（宁可"没停成"也不要"停错"）
}

pre() {
  info "== 前提 =="
  [ "$(/usr/local/go/bin/go version | awk '{print $3}')" = "go1.25.4" ] || die "工具链不是 go1.25.4"
  local tv; tv=$(/usr/local/go/bin/go version -m /root/cc/run/adaptixserver | head -1 | awk '{print $2}')
  [ "$tv" = "go1.25.4" ] || die "teamserver 二进制工具链 $tv ≠ go1.25.4"
  ok "teamserver 与构建工具链一致（$tv）"
  [ "$(cnt $LST/pl_transport.go DIAG)" = "0" ] || die "pl_transport.go 仍带 DIAG 插桩，拒绝构建"
  ok "listener 源码无 DIAG 插桩"

  # ★ 批次6：`pkgsanity` 提前到**源码树**，不再只查 dist 的副本。
  #   实测真事件：`src_gopher/transport_libp2p.go.bak-relay` 被 `Makefile` 的
  #   `cp -r src_gopher ./dist/src_gopher` 一起带进交付树。
  #   dist 是**派生**的 ⇒ 查源码等于查两者，而且**在 make 之前**就红、
  #   理由也对（原路径要等 make 跑完，且报的是 dist，看不出"谁把它带进来的"）。
  pkgsanity "$AGT/src_gopher"
  ok "agent 源码树无备份/中间文件（它们会被 cp -r 带进交付树）"

  # ★ D3（批次4）：`go.mod:116` 有一条 `replace github.com/libp2p/go-libp2p => ./third_party/go-libp2p`。
  #   它是**隐藏构建前提**：整棵依赖树只在"这个目录存在**且**这行 replace 还在"时才成立。
  #   实测澄清（别把它想复杂了）：那份副本是**上游原样 5.0MB / 584 文件的完整拷贝，
  #   没有任何本地改动**（全树 grep `axc2|Adaptix|AXC2` 无命中）—— 它的作用是**钉版本**，
  #   不是"打过补丁的分支"。缺了它会去拉 **v0.49.0** ⇒ **静默升级工具链**（本项目已归档的坑）。
  #   ⇒ 两道断言都要有：目录在 + replace 行在。少任何一条，`go build` 要么报一个
  #     看不懂的错，要么**默默地换一个 libp2p 版本**继续（后者才是危险的）。
  [ -d "$LST/third_party/go-libp2p" ] || die "缺 $LST/third_party/go-libp2p（go.mod 的 replace 指向它；缺了会去拉 v0.49.0 ⇒ 静默升级工具链）"
  grep -q '^replace github.com/libp2p/go-libp2p => ./third_party/go-libp2p' "$LST/go.mod" \
    || die "go.mod 里没有那条 replace ⇒ 构建会拉上游最新版（v0.49.0），工具链会被静默换掉"
  local nfile; nfile=$(find "$LST/third_party/go-libp2p" -type f 2>/dev/null | wc -l)
  [ "${nfile:-0}" -gt 500 ] || die "third_party/go-libp2p 只有 ${nfile} 个文件（实测完整副本是 584）⇒ 副本被削过"
  ok "D3 replace 前提成立（go-libp2p 本地副本 $nfile 个文件）"

  # ★ M6（批次3）：头注释 ② 承诺"工具链/模块版本一致（plugin.Open 逐包比构建 ID）"，
  #   而原 pre() **只比了工具链版本字符串**。这里补上依赖矩阵的**诊断**。
  #   ★★ 但它**不是门** —— 我实测推翻了"dep diff 可以当判据"这个想法（详见 l2p_depcheck.py 头注释）：
  #      本机 `beacon_listener_dns.so`(dns v1.1.69) 与 `listener_gopher_tcp.so`(dns v1.1.61)
  #      **都真的链接了 dns**（pclntab 符号各 2 处，对照 notifier=0）且**此刻共存于活进程**
  #      ⇒ "同名 module 版本不同" **不蕴含** "plugin.Open 会失败"。
  #      把它当 die 用 ⇒ pre() 每次必死、理由还是错的，而且 pre() 被 build/install 调用
  #      ⇒ **整条部署链会被一个假红判据永久卡死**。
  #   ⇒ 这里只打印（诊断）；**权威判据在 do_restart 的加载日志检查**。
  if [ -f "$DEPCHK" ]; then
    python3 "$DEPCHK" | tail -14
    ok "依赖版本矩阵已打印（诊断；真判据看 do_restart 的插件加载日志）"
  else
    info "（跳过依赖矩阵诊断：缺 $DEPCHK）"
  fi

  # ★ 原为"跑两遍"（一遍 `| tail -2` 给人看、一遍 `>/dev/null || die` 做判据）——
  #   crypto verify 要跑几秒，两遍没有意义。合并成一遍：失败时把尾部打全再 die。
  local cout; cout=$(python3 "$CRY" verify 2>&1) || { echo "$cout" | tail -20; die "crypto 等价性判据未通过"; }
  echo "$cout" | tail -2
  ok "crypto 判据通过（agent/builder/listener 三处 ChaCha20 等价）"
}

# ★ M4 修复（批次3）：打包前的杂物断言。
#   危害不是"编译失败"（Go 只编译 .go 结尾，`.bak-chacha` 不参与），而是
#   ① 它是**变更记录级痕迹**（把"这个载荷的加密被改过 + 改之前长什么样"连同交付物给 operator）；
#   ② 它是第 4 份密码学副本 —— 将来任何"按 `crypt.go*` 通配替换"的动作会改坏活文件。
#   ★ 只**检测**不删除：清理动作必须具名（本项目的交付树里还有既有的回滚备份，
#     通配删除会连它们一起清掉）。检测范围 = 要投递的那棵树。
pkgsanity() {
  local d="$1" j
  j=$(find "$d" \( -name '*.bak' -o -name '*.bak-*' -o -name '*.orig' -o -name '*.pyc' \
                  -o -name '*~' -o -name '*.rej' \) 2>/dev/null)
  [ -z "$j" ] || die "待投递树 $d 里混进了备份/中间文件（会被一起交付给 operator）：$(echo "$j" | tr '\n' ' ')"
}

do_build() {
  pre
  info "== 构建 listener（Makefile：显式文件列表）=="
  ( cd "$LST" && make 2>&1 | tail -3 ) || die "listener make 失败"
  need "$LST/dist/listener_gopher_tcp.so"
  # ★ 判别力边界（如实标注）：这条**没有反向对照** —— gopher_listener_tcp 的功能就是
  #   libp2p listener，不存在"合法的、不带 libp2p 的 listener 产物"。它能红的情形是
  #   "Makefile 的显式文件列表漏了 pl_libp2p.go"（那会让计数掉到 0）。
  local n; n=$(cnt "$LST/dist/listener_gopher_tcp.so" 'libp2p')
  [ "${n:-0}" -gt 0 ] || die "新 listener .so 里没有 libp2p 痕迹（Makefile 文件列表漏了 pl_libp2p.go？）"
  [ "$(cnt "$LST/dist/listener_gopher_tcp.so" '[DIAG]')" = "0" ] || die "新 .so 里还有 [DIAG] 字面量"
  ok "listener .so: $(stat -c%s "$LST/dist/listener_gopher_tcp.so") B, libp2p 字样 $n 处, DIAG 0"

  info "== 构建 agent 插件（Makefile：so + src_gopher 六平台冒烟 + 拷 src）=="
  ( cd "$AGT" && make >/dev/null 2>&1 ) || die "agent make 失败"
  need "$AGT/dist/agent_gopher.so"
  local so="$AGT/dist/agent_gopher.so" n_new n_old
  n_new=$(cnt "$so" 'gopherChaCha20Xor'); n_old=$(cnt "$so" 'crypto/rc4')
  # ★ M2 修复（批次3）：原判据 `cnt ... 'chacha' -gt 0` **恒真**，因为它匹配的是 Go 运行时
  #   自身的 internal/chacha8rand（runtime.fastrand）—— **任何** Go 产物都含它。
  #   实测（2026-09-16，`grep -acF`）：
  #     小写 chacha      旧 RC4 时代 agent_kharon.so = 6 ，新 ChaCha20 agent = 6  ⇒ **零判别力**
  #     大写 ChaCha20    旧 agent = 0 ，新 agent = 2（但 agent_beacon 等也 = 4 —— 那是
  #                      quic-go/tls 里的 ChaCha20Poly1305，所以单用它也不可靠）
  #     gopherChaCha20Xor 旧 = 0 ，新 = 2 ，且**本机其余 19 个 .so 全是 0** ⇒ 唯一强判据
  #   ⇒ 改成**双向**符号判据（正向"新实现在"、反向"旧实现不在"），两个方向都实测过判别力。
  #   ★ 反向判据**绝不能**写成 `cnt ... 'rc4Stream' = 0`：该串在**新旧产物里都是 0**
  #     （真名是 `crypto/rc4.(*Cipher).XORKeyStream`）⇒ 那条断言**永远通过**，看着在查
  #     RC4 残留、实际什么都没查。恒真判据（本条的病因）有个镜像：**用在反向断言上的
  #     恒假判据，同样是恒真**。反向判据必须用一个"在旧产物上真的非零"的串。
  [ "${n_new:-0}" -gt 0 ] || die "新 agent .so 里没有 ChaCha20 实现符号 gopherChaCha20Xor —— 加密没被改造，或符号被裁掉了"
  [ "${n_old:-0}" = "0" ] || die "新 agent .so 里仍有 RC4 符号 crypto/rc4（$n_old 处）—— 有代码路径没改到 ChaCha20"
  ok "agent .so: $(stat -c%s "$so") B, ChaCha20 符号 $n_new 处 / rc4 残留 0 处"

  pkgsanity "$AGT/dist/src_gopher"
  ok "dist/src_gopher 无 .bak/.orig/.pyc 等会被一起投递的杂物"
  info "dist 清单："; ls -la "$LST/dist" "$AGT/dist"
}

# ★ 判据：要装的那个 .so 必须是**当次源码**构建的。仅靠 pre() 不够 ——
#   `l2p_diag.py apply` → `revert`（**不碰 dist**）→ `install` 这条链上，源码是干净的，
#   而 dist 里的 .so 是 apply 时构建的（带 [DIAG]）⇒ 会带着插桩上线，脚本还会打
#   "[ok] 四件套 + src_gopher 全部按 md5 核对一致"（★ **循环自证**：比的是 dist↔运行态，
#   而 dist 未被任何判据绑定到源码）。
#   两条一起：① .so 里不得有 [DIAG]；② .so 的 mtime 必须新于它那一侧的**每一个**源文件。
fresh() {
  local so="$1" src="$2" newer
  [ "$(cnt "$so" '[DIAG]')" = "0" ] || die "$so 里还有 [DIAG] 插桩（dist 不是干净源码构建的），拒绝安装"
  # ★ 批次7（部署链解阻·续）：**必须排除 `dist/`** —— 它是本次构建**自己的输出**。
  #   实测真事件：`install` 报 `$AGT/dist/agent_gopher.so 比这些源文件旧`，列出 27 个文件
  #   **全部**在 `dist/src_gopher/` 下。根因是 `gopher_agent/Makefile` 的顺序：
  #     ① `go build -o ./dist/agent_gopher.so .`（T1）
  #     ② `(cd src_gopher && make)`（六平台冒烟）
  #     ③ `cp -r src_gopher ./dist/src_gopher`（T2 > T1，**cp 不保 mtime**）
  #   ⇒ `dist/src_gopher/**/*.go` 的 mtime **必然**晚于 .so ⇒ 含 dist 的原判据**恒红**。
  #   ★ 定性：**不是"门对树错"，是门的观测量里混进了它自己的输出**（红是**假红**）。
  #     本判据要抓的是"改了 `src_gopher` 却忘了重建" —— 那个场景下 `src_gopher/**/*.go`
  #     确实会新于 .so，**排除 dist 之后判据照样红**（patches/l2p_freshfix.py 的 verify 有红侧实证）。
  #   实测对照：含 dist ⇒ newer=27（且全部在 dist/ 内）；排除后 ⇒ newer=0。
  #   ★ 本判据**首次被行使**就是批次7 —— `build`/`verify`/`restart` 都不调 `fresh`，
  #     而 `install` 此前从未跑成功过 ⇒ 它此前只有"设计"没有"绿证"。
  newer=$(find "$src" -name '*.go' -not -path '*/dist/*' -newer "$so" 2>/dev/null)
  [ -z "$newer" ] || die "$so 比这些源文件旧 ⇒ dist 不是当次构建的，拒绝安装：$(echo "$newer" | tr '\n' ' ')"
}

do_install() {
  # ★ M5② 修复（批次3）：原 `case` 里 `install) do_install ;;` 而 do_install 内部**不调 pre**
  #   ⇒ 可以完全绕过"源码无 DIAG / crypto 等价 / 依赖无冲突"三条前提，直接装 dist。
  pre
  need "$LST/dist/listener_gopher_tcp.so"; need "$AGT/dist/agent_gopher.so"
  fresh "$LST/dist/listener_gopher_tcp.so" "$LST"
  fresh "$AGT/dist/agent_gopher.so"          "$AGT"
  pkgsanity "$AGT/dist/src_gopher"
  # ★ 批次7：把 `fresh()` 那条假红**真正想说的话**换成一条内容判据 —— **比字节，不比 mtime**。
  #   它比原来那条更强：原来只能间接推断"大概没改过"，这条直接证明
  #   **交付给 operator 的源码副本与源码树逐文件逐字节一致**。
  #   （这条检查原来**不存在**：批次5 是手工 `diff -rq` 过一次，从未进判据。）
  diff -rq "$AGT/src_gopher" "$AGT/dist/src_gopher" >/dev/null 2>&1 \
    || die "交付的源码副本 dist/src_gopher 与源码树 src_gopher 不一致 ⇒ 拒绝安装（operator 会拿到不同源的源码）"
  ok "dist/src_gopher 与 src_gopher 内容一致（逐文件逐字节）"
  ok "dist 两枚 .so 均为当次源码构建、无 DIAG 插桩"

  info "== 备份 ==（回滚判据）"
  mkdir -p "$BK"
  local ts; ts=$(date +%m%d-%H%M%S)
  local bkf="$BK/run-extenders-$ts.tgz"
  tar czf "$bkf" -C /root/cc/run/extenders gopher_agent gopher_listener_tcp \
    || die "备份失败，拒绝安装"
  # ★ M5③/S12 修复（批次3）：原脚本创建备份后**从未验证可解**，成功判据只有"tar 退出 0"。
  #   而回滚是这套无 git 流程**唯一的护栏** —— 它是最不可能红的一环，也就是最危险的一环。
  #   就地验证：能列出成员 + 两个关键 .so 都在 + 每个成员可解出并记 md5。
  tar tzf "$bkf" >/dev/null 2>&1 || die "刚创建的备份 $bkf 解不开，拒绝安装（回滚后路已断）"
  # 备份内容的 md5 → 回滚时的**对照值**（原脚本只打印"恢复后"的 md5、没有对照值 ⇒
  # "恢复成功"无法判定，打印一个数字不等于核对）。
  local tag; tag=$(basename "$bkf" .tgz)
  tar xzOf "$bkf" gopher_agent/agent_gopher.so                 > "$BK/$tag.agent.bin"    || die "备份里取不出 agent .so（回滚会缺件）"
  tar xzOf "$bkf" gopher_listener_tcp/listener_gopher_tcp.so   > "$BK/$tag.listener.bin" || die "备份里取不出 listener .so（回滚会缺件）"
  { echo "agent=$(md5 "$BK/$tag.agent.bin")"; echo "listener=$(md5 "$BK/$tag.listener.bin")"; } > "$BK/$tag.expected.txt"
  rm -f "$BK/$tag.agent.bin" "$BK/$tag.listener.bin"
  ok "备份 $bkf 已就地验证可解、两枚 .so 均可解出；对照值 → $BK/$tag.expected.txt"
  info "装前哈希：agent=$(md5 $RAGT/agent_gopher.so) listener=$(md5 $RLST/listener_gopher_tcp.so)"

  info "== 安装（四件套 + src_gopher 必须一起换）=="
  for f in $FOUR; do cp "$LST/dist/$f" "$RLST/$f" || die "拷 $f 失败"; done
  cp "$LST/dist/listener_gopher_tcp.so" "$RLST/" || die "拷 listener .so 失败"
  for f in $FOUR; do cp "$AGT/dist/$f" "$RAGT/$f" || die "拷 agent $f 失败"; done
  cp "$AGT/dist/agent_gopher.so" "$RAGT/" || die "拷 agent .so 失败"
  rm -rf "$RAGT/src_gopher" && cp -r "$AGT/dist/src_gopher" "$RAGT/src_gopher" || die "拷 src_gopher 失败"

  [ "$(md5 $RLST/listener_gopher_tcp.so)" = "$(md5 $LST/dist/listener_gopher_tcp.so)" ] || die "listener .so 拷贝不一致"
  [ "$(md5 $RAGT/agent_gopher.so)"          = "$(md5 $AGT/dist/agent_gopher.so)" ]          || die "agent .so 拷贝不一致"
  [ "$(md5 $RAGT/src_gopher/utils/crypt.go)" = "$(md5 $AGT/dist/src_gopher/utils/crypt.go)" ] || die "src_gopher 拷贝不一致"
  ok "四件套 + src_gopher 全部按 md5 核对一致"
}

do_restart() {
  local pid; pid=$(tspid)
  if [ -n "${pid:-}" ]; then
    info "停 $pid（按端口认领）"
    kill "$pid"; for i in $(seq 1 30); do listening || break; sleep 1; done
    kill -0 "$pid" 2>/dev/null && { info "TERM 未生效，KILL"; kill -9 "$pid"; for i in $(seq 1 10); do listening || break; sleep 1; done; }
  fi
  listening && die "$PORT 仍被占用（占用者 pid=$(tspid)），拒绝启动"
  ok "旧实例已停、端口已释放"

  local base; base=$(wc -l < "$LOGF")
  ( cd /root/cc/run && env -u GOWORK -u GOTOOLCHAIN -u GOMAXPROCS -u GOMODCACHE -u GOCACHE -u TMPDIR -u GOTMPDIR \
      GOMEMLIMIT=600MiB GOGC=50 setsid nohup ./adaptixserver -web -profile central.profile >> "$LOGF" 2>&1 < /dev/null & )
  for i in $(seq 1 30); do listening && break; sleep 1; done
  listening || die "启动后 $PORT 未 LISTEN（看 $LOGF）"
  kill -0 "$(tspid)" 2>/dev/null || die "启动后进程不在了（看 $LOGF）"
  ok "新实例 LISTEN $PORT (pid $(tspid))"

  # ★★★ M6 核心（批次3）：「端口 LISTEN + 进程还在」**不能证明插件已加载** ——
  #   插件加载失败的 teamserver 照样 LISTEN、照样活着，脚本会打 `[ok] 新实例 LISTEN 38212`。
  #   2026-09-16 当晚就是这么发生的（`server.log:4273`/`:4346` 两次
  #   `failed to open plugin ...: plugin was built with a different version of package
  #   github.com/miekg/dns`，而实例"看起来正常"）。
  #   两道观测，缺一不可：
  #     ① 日志：**权威信号**（它就是 plugin.Open 的真实报错），但可能轮转/被截断
  #     ② /proc/<pid>/maps：进程**实际 mmap 的** .so —— 日志会骗人，maps 不会
  local seg; seg=$(tail -n +$((base+1)) "$LOGF" 2>/dev/null)
  if printf '%s\n' "$seg" | grep -qE 'failed to open plugin|plugin was built with a different version of package'; then
    printf '%s\n' "$seg" | grep -E 'failed to open plugin|plugin was built with a different version of package' | head -5
    die "插件加载失败 ⇒ 该实例不可用（LISTEN 与进程存活都不是插件可用的证据）"
  fi
  ok "本次启动日志无插件加载失败"

  local rp; rp=$(tspid)
  local nso; nso=$(grep -oE "/[^ ]*extenders/[^ ]*\.so" /proc/${rp}/maps 2>/dev/null | sort -u | wc -l)
  [ "${nso:-0}" -gt 0 ] || die "进程 maps 里一个 extender .so 都没有 ⇒ 插件一个都没加载（空壳实例）"
  ok "已加载插件 $nso 个（/proc/$rp/maps 实测）"
  printf '%s\n' "$seg" | grep -E "Started listener '|Restored [0-9]+ listeners" | head -20 || true
}

do_verify() {
  need "$E2E"
  info "== 运行态 listener 清单 =="
  python3 "$E2E" --port "$PORT" --listeners
  info "== e2e 验收（对运行态实例）=="
  python3 "$E2E" --port "$PORT" gopher --timeout 120
}

do_status() {
  echo "--- 运行态 .so ---"
  md5sum "$RAGT/agent_gopher.so" "$RLST/listener_gopher_tcp.so" 2>/dev/null
  echo "--- dist ---"
  ls -la "$LST/dist/listener_gopher_tcp.so" "$AGT/dist/agent_gopher.so" 2>/dev/null
  echo "--- 进程/端口 ---"
  # ★ S11（批次3）：原为 `pgrep -af 'adaptixserver -web -profile central.profile'` ——
  #   本文件 `:50-55` **自己明令禁止**这个反模式（本机曾有外层包装 shell 的命令行含有
  #   同样字符串 ⇒ pgrep -f 选中的是包装 shell，不是 teamserver）。status 里犯这个错
  #   不会杀错进程，但会**报错 pid**，进而让 operator 基于错 pid 做判断。
  #   改成与 tspid 同源：按端口认领，再读它的真实 cmdline。
  local p; p=$(tspid)
  if [ -n "${p:-}" ]; then
    echo "teamserver pid=$p  cmdline: $(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null)"
    local nso; nso=$(grep -oE "/[^ ]*extenders/[^ ]*\.so" /proc/$p/maps 2>/dev/null | sort -u | wc -l)
    echo "已加载插件 $nso 个（maps 实测）"
  else
    echo "(未运行 / $PORT 无监听者)"
  fi
  listening && echo "$PORT LISTEN" || echo "($PORT 未 LISTEN)"
  echo "--- 备份（回滚后路）---"
  ls -lt "$BK"/run-extenders-*.tgz 2>/dev/null | head -3 || echo "(无备份)"
}

# ★ M5③ 修复（批次3）—— 这一条是本脚本里**最危险**的地方。
#   原顺序：`rm -rf "$RAGT" "$RLST"` 在 `tar xzf … || die` **之前**。
#   从 rm 那一刻起，唯一的护栏就已经断了：tar 若失败（备份损坏、磁盘满、权限），
#   运行态两棵树**已经没了**，脚本只打一句"解包失败"，回滚本身成了灾难。
#   而且原脚本的成功判据只有"tar 退出 0 + 打印两个 md5"（**无对照值** ⇒ 打印数字 ≠ 核对），
#   并且**不恢复源码树** ⇒ 回滚后"源码=新、运行态=旧"，下次 build+install 又把坏版本装回来。
#   ⇒ 改为**先验后毁**：解到临时目录 → 校验关键件 + 与装机时记录的对照值比对 → 才动运行态；
#      替换本身也做成"失败可回退"（旧目录先改名，替换成功才删）。
do_rollback() {
  local t="${2:-}"
  [ -n "$t" ] || t=$(ls -t "$BK"/run-extenders-*.tgz 2>/dev/null | head -1)
  [ -n "${t:-}" ] || die "$BK 里没有备份"
  [ -f "$t" ] || die "备份不存在：$t"
  info "从 $t 恢复（备份时间 $(date -r "$t" '+%F %T')）"

  local tmpd; tmpd=$(mktemp -d /root/gp5probe/tmp/rollback.XXXXXX) || die "建临时目录失败"
  tar xzf "$t" -C "$tmpd" || { rm -rf "$tmpd"; die "备份解不开（$t）—— 运行态**未被动过**"; }
  [ -f "$tmpd/gopher_agent/agent_gopher.so" ] || { rm -rf "$tmpd"; die "备份里没有 agent .so —— 运行态未被动过"; }
  [ -f "$tmpd/gopher_listener_tcp/listener_gopher_tcp.so" ] || { rm -rf "$tmpd"; die "备份里没有 listener .so —— 运行态未被动过"; }
  local ea el; ea=$(md5 "$tmpd/gopher_agent/agent_gopher.so"); el=$(md5 "$tmpd/gopher_listener_tcp/listener_gopher_tcp.so")

  local tag; tag=$(basename "$t" .tgz)
  if [ -f "$BK/$tag.expected.txt" ]; then
    grep -qF "agent=$ea"    "$BK/$tag.expected.txt" || { rm -rf "$tmpd"; die "备份内 agent .so 与装机对照值不符（备份被动过？）—— 运行态未被动过"; }
    grep -qF "listener=$el" "$BK/$tag.expected.txt" || { rm -rf "$tmpd"; die "备份内 listener .so 与装机对照值不符 —— 运行态未被动过"; }
    ok "备份内容与装机时记录的对照值一致"
  else
    info "（该备份没有对照值文件 $BK/$tag.expected.txt —— 只校验了可解与关键件齐全）"
  fi

  local pid; pid=$(tspid)
  [ -n "${pid:-}" ] && { info "停 $pid"; kill "$pid"; for i in $(seq 1 30); do listening || break; sleep 1; done; }

  rm -rf "$RAGT.old" "$RLST.old"
  mv "$RAGT" "$RAGT.old" 2>/dev/null
  mv "$RLST" "$RLST.old" 2>/dev/null
  if mv "$tmpd/gopher_agent" "$RAGT" && mv "$tmpd/gopher_listener_tcp" "$RLST"; then
    rm -rf "$RAGT.old" "$RLST.old"
  else
    rm -rf "$RAGT" "$RLST"
    [ -d "$RAGT.old" ] && mv "$RAGT.old" "$RAGT"
    [ -d "$RLST.old" ] && mv "$RLST.old" "$RLST"
    rm -rf "$tmpd"
    die "替换运行态失败 —— 已退回替换前的状态（备份仍在 $t）"
  fi
  rm -rf "$tmpd"

  # 恢复后的核对：与**对照值**比，不是只打印一个数字
  [ "$(md5 $RAGT/agent_gopher.so)" = "$ea" ]          || die "恢复后 agent .so 与备份不符"
  [ "$(md5 $RLST/listener_gopher_tcp.so)" = "$el" ]   || die "恢复后 listener .so 与备份不符"
  ok "已恢复并核对：agent=$ea listener=$el"
  info "★ 注意：本脚本只恢复**运行态**，不恢复源码树。若源码树已是新版本，"
  info "  下次 build+install 会把新版本再装回来 —— 要彻底回滚需同时还原源码（见 patches/）。"
  do_restart
}

case "${1:-status}" in
  build)    do_build ;;
  install)  do_install ;;
  restart)  do_restart ;;
  verify)   do_verify ;;
  status)   do_status ;;
  rollback) do_rollback "$@" ;;   # 可选第二参数：指定某个 .tgz（缺省用最新一份）
  all)      do_build && do_install && do_restart && do_verify ;;
  *) echo "用法: $0 build|install|restart|verify|status|rollback [备份.tgz]|all"; exit 2 ;;
esac

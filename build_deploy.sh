#!/usr/bin/env bash
# =============================================================================
# AdaptixC2 1.6.0 一键构建 / 部署
#
#   用法：
#     ./build_deploy.sh                 # 全量构建 + 产物自检
#     ./build_deploy.sh --up            # 构建+自检通过后，直接起服务（一键部署）
#     ./build_deploy.sh --up --bof-compile   # 起服务并触发首次 BOF 全量编译
#     ./build_deploy.sh --skip-deps     # 跳过依赖自检（依赖已确认装好时）
#     ./build_deploy.sh --slim-src      # 构建+自检通过后，删掉构建源码，只留 dist/
#     ./build_deploy.sh --help
#
#   产物：<仓库根>/dist/  —— 自包含，可直接部署
#
#   不带 --up 时本脚本**不起服务**，只在结尾打印部署指引。
#
#   --up 部署阶段（默认关，必须显式指定）：
#     顺序 = 构建 → 自检 → [可选瘦身] → 部署。部署段在最后一行
#     `[ "$RECHECK_FAIL" -eq 0 ] || exit 1` **之后**，故自检未过时根本到不了。
#     · 停旧：**按 cwd 匹配** —— 只停工作目录等于本 dist/ 的 adaptixserver，
#       同机其他实例（例如现网 /root/cc/run 那个）一律不碰。pid 文件会校验
#       comm 与 cwd，防 pid 复用导致误杀。
#     · 起新：cwd 必须是 dist/（data/、证书、web/、extenders、axscripts 全按
#       进程 cwd 解析），setsid nohup 后台常驻，日志 dist/server.log。
#     · 就绪判定**只认 HTTP 探测**，不 grep 日志 —— 源码证明端口被占时进程
#       **不退出**，而 `[+] The AdaptixC2 server is ready` 是**无条件打印**的。
#       另：profile 校验失败时退出码是 **0**，别被退出码骗了。
#     开关：--bof-compile  起服后登录并 POST /api/bofs/compile {"all":true}
#                          （**默认关**；口令只经内存与 stdin，不打印不上 argv）
#           --no-web      不加 -web（默认加）
#           --debug       加 -debug
#           --profile=<名>  默认 central.profile
#
#   环境变量：
#     WEB_SRC=<dir>      指定现成的 Web 静态产物目录（含 index.html；正常还应含
#                        assets/，若没有会告警 —— 那几乎肯定是 Vite 源码树）。
#                        不给则依次尝试 AdaptixWeb/dist、AdaptixWeb/，且**只认构建
#                        产物**（index.html + assets/）；若只有源码树则**拒收**并说明
#                        原因（源码树的 index.html 指向 /src/main.tsx，拷进 dist/web
#                        必然白屏）。都没命中且本机有 Node >= 18 时，会**自动**
#                        `npm ci && npm run build` 构建 AdaptixWeb/（--no-web-build
#                        可关）；Node 由 pre_install_linux_all.sh 负责装。
#                        最终都没有时**只告警不中断**（前端产物不进本交付树），
#                        服务照常起，控制台走 503「未部署」占位页。
#     NPM_REGISTRY=<url> 透传给 npm（国内镜像用，例如 https://registry.npmmirror.com）。
#                        默认不改 registry。
#     EXTKIT_SRC=<dir>   指定 Extension-Kit 目录（含 extension-kit.axs）。
#                        不给则试 ./Extension-Kit、AdaptixServer/Extension-Kit。
#     SKIP_RELAY=1       跳过 relaynode（axrelay）构建。
#     SLIM_SRC=1         构建+自检**全部通过**后，删掉只用于构建的源码目录
#                        （AdaptixServer/ AdaptixWeb/ Extension-Kit/ Makefile），
#                        只留 dist/ 与运维脚本。**默认关闭**；等价于 --slim-src。
#                        三条硬护栏：① 自检未过一律拒绝执行；② ROOT 不是本交付树
#                        （缺少 build_deploy.sh / dist/）一律拒绝；③ 绝不触碰 dist/。
#                        ⚠ 本交付树**没有 git**，删除不可逆 —— 重建需重新上传源码树。
#                        只删「构建输入」，**不删 dist/ 里的任何东西**：dist 里的
#                        src_agent/ src_beacon/ src_core/ 等看着像源码，实为运行期
#                        载荷与 BOF 编译输入（见下方部署指引第 1 条）。
#
#   为什么不用 `make server-ext`：
#     该目标 = clean server extenders web extension-kit。其中 **web 在交付树里
#     必然失败**（无 AdaptixWeb/dist，且 AdaptixWeb/ 下无 assets/，三个分支都不
#     命中 ⇒ Makefile:120 `exit 1`），一旦失败整体中断。extension-kit 则视情况：
#     交付树根有 Extension-Kit/（479 文件）时它会成功。
#     本脚本拆开调用 server / extenders，并把 web 与 extension-kit 降级为
#     「有则带上，无则告警」，从而 web 缺失不再阻断其余产物。
# =============================================================================

set -euo pipefail

GREEN=$(printf '\033[1;32m')
YELLOW=$(printf '\033[1;33m')
RED=$(printf '\033[1;31m')
BLUE=$(printf '\033[1;34m')
NC=$(printf '\033[0m')

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

DIST="$ROOT/dist"
# extenders/ 下有 Makefile 的目录数。
# ★ 2026-09-20 §6.30 由 22 订正为 24：二开陆续加了 topology 与 relay 两个 extender，
#   而 22 是「加它们之前」的数。最后一次成功构建早于两者 ⇒ 这个陈旧常量**从未被跑到**，
#   一旦重跑，24 个 .so 全部产出正常、[3/3] 照样报红并 exit 1 —— **恒真的假失败**。
#   判据不是「脚本说 22」，而是 `ls -d AdaptixServer/extenders/*/Makefile | wc -l`。
EXPECT_EXTENDERS=24
EXPECT_BOFS=26           # linux_agent/src_agent/bofs/*.c
EXPECT_AGENT_HDRS=26     # linux_agent/src_agent/agent/*.h

SKIP_DEPS=0
SLIM_SRC="${SLIM_SRC:-0}"      # 1 = 自检通过后删掉构建源码，只留 dist/
UP="${UP:-0}"                  # 1 = 自检通过后起服务（--up）
BOF_COMPILE=0                  # 1 = 起服后登录并触发 BOF 全量编译（--bof-compile）
USE_WEB=1                      # 0 = 起服务时不加 -web（--no-web）
USE_DEBUG=0                    # 1 = 起服务时加 -debug（--debug）
WEB_BUILD="${WEB_BUILD:-1}"    # 0 = 不自动 npm 构建前端（--no-web-build）
PROFILE_NAME="central.profile"
for arg in "$@"; do
    case "$arg" in
        --skip-deps) SKIP_DEPS=1 ;;
        --slim-src)  SLIM_SRC=1 ;;
        --up)        UP=1 ;;
        --bof-compile) BOF_COMPILE=1 ;;
        --no-web)    USE_WEB=0 ;;
        --debug)     USE_DEBUG=1 ;;
        --no-web-build) WEB_BUILD=0 ;;
        --profile=*) PROFILE_NAME="${arg#--profile=}" ;;
        -h|--help)
            # 打印文件头注释块（第 2 行起，直到第二条 `# ====` 分隔线）。
            # 用 awk 而不是 `sed -n '2,30p'`：以后改文档不必再同步行号。
            awk 'NR==1{next} /^# =====/{if(n++)exit; next} {sub(/^# ?/,""); print}' "${BASH_SOURCE[0]}"
            exit 0 ;;
        *) echo "未知参数: $arg（--help 查看用法）" >&2; exit 1 ;;
    esac
done

# 参数组合校验：--bof-compile 只在起服务之后才有意义，单独给它是笔误而非无害。
if [ -z "$PROFILE_NAME" ]; then
    echo "--profile= 不能为空" >&2; exit 1
fi
if [ "$BOF_COMPILE" = "1" ] && [ "$UP" != "1" ]; then
    echo "--bof-compile 需要配合 --up（它是在服务起来之后才调的接口）" >&2; exit 1
fi

step()  { echo; echo "${BLUE}==>${NC} $1"; }
ok()    { echo "  ${GREEN}[+]${NC} $1"; }
warn()  { echo "  ${YELLOW}[!]${NC} $1"; }
fail()  { echo "  ${RED}[x]${NC} $1"; }
die()   { fail "$1"; exit 1; }

FAILED_OPTIONAL=()   # 非致命项的失败记录

# -----------------------------------------------------------------------------
# [1/3] 环境自检
# -----------------------------------------------------------------------------
dep_check() {
    step "[1/3] 环境自检"

    # -buildmode=plugin 是全部 22 个 extender 的构建方式，Windows 不支持。
    case "$(uname -s)" in
        Linux)  ok "平台 Linux" ;;
        Darwin) warn "平台 Darwin：plugin 模式可用，但 Windows 载荷需 mingw 交叉编译" ;;
        *)      die "平台 $(uname -s)：-buildmode=plugin 仅支持 Linux/macOS，无法在 Windows 上构建" ;;
    esac

    local missing=()
    need() {  # need <命令> <用途>
        if command -v "$1" >/dev/null 2>&1; then
            ok "$(printf '%-24s' "$1") $(command -v "$1")"
        else
            fail "$(printf '%-24s' "$1") 缺失 —— $2"
            missing+=("$1")
        fi
    }

    need go    "核心与全部 extender（Go 1.25.4+）"
    need make  "驱动各 extender 的 Makefile"
    need gcc   "linux_agent 运行期编译 BOF（.c -> .o）"
    need g++   "gopher_agent 等 C++ 源"
    need git   "部分 extender 取依赖"

    # Windows 载荷交叉编译。注意实际编译器是 **clang++**，不是
    # x86_64-w64-mingw32-g++：beacon_agent/src_beacon/Makefile:4 与
    # agent_kharon/src_beacon/Makefile:7 写的是
    #     clang++ -target x86_64-w64-mingw32
    # mingw-w64 提供的是 GCC 交叉链，装了它**不等于**有 clang++。之前这里只探
    # mingw-g++，探到了就放行，结果 `make extenders` 在 beacon_agent 处
    # `clang++: No such file or directory`（Error 127）；而根 Makefile 的
    # extender 循环是 `|| exit 1` —— 22 个 .so 一个都不产出。故列为必需项。
    need clang++ "Windows 载荷交叉编译（beacon_agent / agent_kharon 出 .o）"
    need x86_64-w64-mingw32-g++ "clang++ 的 MinGW 目标链（提供头文件与库）"
    # 同一条链上的第二个独立工具：src_beacon 用 nasm 汇编 x64/x86 的
    # entry_*.o 与 indirect_syscall（`nasm -f win64/win32`）。clang++ 与 nasm
    # 互不替代，缺任一都在 `make extenders` 中 Error 127 并因 `|| exit 1`
    # 导致 22 个 .so 全部不产出。
    need nasm "Windows 载荷汇编（beacon_agent / agent_kharon 的 entry_*.o）"

    if command -v go >/dev/null 2>&1; then
        local gov; gov="$(go env GOVERSION 2>/dev/null || echo unknown)"
        ok "$(printf '%-24s' 'Go 版本') $gov"
        # go.work 声明 go 1.25.4
        case "$gov" in
            go1.2[5-9]*|go1.[3-9][0-9]*) : ;;
            *) warn "Go 版本低于 1.25.4（go.work 声明值），可能出现构建差异" ;;
        esac
    fi

    if [ "${#missing[@]}" -gt 0 ]; then
        echo
        die "缺少必需依赖：${missing[*]}
     一键装依赖（需 root、需联网，约 5-10 分钟）：
       sudo ./pre_install_linux_all.sh server"
    fi
    ok "依赖自检通过"
}

[ "$SKIP_DEPS" -eq 1 ] && step "[1/3] 环境自检 —— 已跳过（--skip-deps）" || dep_check

# 校验交付树完整性：这些文件缺了就别往下走
for f in Makefile AdaptixServer/main.go AdaptixServer/profile.yaml \
         AdaptixServer/profile.initial.yaml AdaptixServer/central.profile \
         AdaptixServer/404page.html AdaptixServer/ssl_gen.sh; do
    [ -f "$ROOT/$f" ] || die "交付树缺件：$f"
done
ok "交付树必备文件齐全"

# -----------------------------------------------------------------------------
# [2/3] 构建
# -----------------------------------------------------------------------------
step "[2/3] 构建"

echo "  --- 核心：adaptixserver ---"
make clean >/dev/null 2>&1 || true
make server
[ -x "$DIST/adaptixserver" ] || die "adaptixserver 未产出"

echo
echo "  --- 22 个 extender ---"
make extenders

echo
echo "  --- relaynode（axrelay）---"
# gopher_listener_tcp/relaynode 是 gopher TCP 的反向隧道中继节点，独立 main 包，
# 不在任何 Makefile 的构建目标里，需单独构建。
if [ "${SKIP_RELAY:-0}" = "1" ]; then
    warn "已跳过（SKIP_RELAY=1）"
    FAILED_OPTIONAL+=("relaynode/axrelay（人为跳过）")
else
    RELAY_SRC="$ROOT/AdaptixServer/extenders/gopher_listener_tcp/relaynode"
    if [ -f "$RELAY_SRC/main.go" ]; then
        if ( cd "$RELAY_SRC" && GOWORK=off go build -buildvcs=false -ldflags="-s -w" -o "$DIST/axrelay" . ); then
            ok "axrelay -> dist/axrelay"
        else
            fail "axrelay 构建失败"
            FAILED_OPTIONAL+=("relaynode/axrelay")
        fi
    else
        warn "未找到 $RELAY_SRC/main.go"
        FAILED_OPTIONAL+=("relaynode/axrelay（源码缺失）")
    fi
fi

echo
echo "  --- Web 控制台静态产物 ---"
# 判据必须是「构建产物」而不是「Vite 源码树」。产物特征 = index.html + assets/
# （vite.config.ts 只设了 build.outDir='dist'，assetsDir 未自定义 ⇒ 默认 assets/，
# 1.5.5 的真产物即 assets/index-*.js + assets/index-*.css）。
# 只判 index.html 会让 AdaptixWeb/ 这个源码树分支命中：它的 index.html 是 **vite 开发
# 入口**（<script type="module" src="/src/main.tsx">），拷进 dist/web 后浏览器取到的是
# 未编译的 .tsx（MIME 非 JS 模块 ⇒ 拒绝执行）⇒ **白屏**，而本脚本与 [3/3] 会双双报 [+]
# —— 与 agent_kharon 同一类的「半可用但全绿」。
WEB_PICK=""
WEB_SUSPECT=""
if [ -n "${WEB_SRC:-}" ] && [ -f "${WEB_SRC:-}/index.html" ]; then
    # 显式指定：照收（用户说了算），但形态可疑时明确告警
    WEB_PICK="$WEB_SRC"
    [ -d "$WEB_SRC/assets" ] || WEB_SUSPECT="$WEB_SRC"
else
    for cand in "$ROOT/AdaptixWeb/dist" "$ROOT/AdaptixWeb"; do
        [ -f "$cand/index.html" ] || continue
        if [ -d "$cand/assets" ]; then WEB_PICK="$cand"; break; fi
        WEB_SUSPECT="$cand"
    done
fi

# --- 三个候选都没有 assets/ 时，若本机有 Node >= 18，就从源码构建一次再重扫 ---
# 判据与 pre_install_linux_all.sh 的 actual_node_version_install 一致：Vite 5 要求 >= 18。
# 交付树里**没有任何东西**会装 Node（apt 也不行：Ubuntu 22.04 的 nodejs 是 v12），
# 所以这里只探测、不代装，缺失时把装法写进提示。
WEB_BUILD_NOTE=""
if [ -z "$WEB_PICK" ] && [ "$WEB_BUILD" = "1" ] && [ -z "${WEB_SRC:-}" ]; then
    if [ ! -f "$ROOT/AdaptixWeb/package.json" ] || [ ! -f "$ROOT/AdaptixWeb/package-lock.json" ]; then
        WEB_BUILD_NOTE="AdaptixWeb/ 下缺 package.json 或 package-lock.json ⇒ 无法自动构建"
    else
        NODE_V=""
        if command -v node >/dev/null 2>&1; then
            NODE_V="$(node -v 2>/dev/null || true)"
        fi
        NODE_MAJOR="${NODE_V#v}"
        NODE_MAJOR="${NODE_MAJOR%%.*}"
        if [ -z "$NODE_V" ]; then
            WEB_BUILD_NOTE="本机没有 node ⇒ 无法自动构建（装法：sudo ./pre_install_linux_all.sh server）"
        elif ! [ "$NODE_MAJOR" -ge 18 ] 2>/dev/null; then
            WEB_BUILD_NOTE="本机 node $NODE_V 低于 v18，Vite 5 会拒绝构建 ⇒ 未自动构建"
        else
            echo
            echo "  --- Web 控制台：从 AdaptixWeb/ 构建（node $NODE_V）---"
            # registry 走环境变量而不是 argv：少一处拼接，也不会把 URL 带进 ps。
            if [ -n "${NPM_REGISTRY:-}" ]; then
                export npm_config_registry="$NPM_REGISTRY"
                echo "      npm registry = $NPM_REGISTRY"
            fi
            if ( cd "$ROOT/AdaptixWeb" && npm ci --no-audit --no-fund && npm run build ); then
                ok "npm 构建完成 -> AdaptixWeb/dist"
                # 重扫一遍：npm run build 产出 AdaptixWeb/dist/（vite.config.ts 只设了
                # build.outDir，未自定义 assetsDir ⇒ 默认 assets/）。
                for cand in "$ROOT/AdaptixWeb/dist" "$ROOT/AdaptixWeb"; do
                    if [ -f "$cand/index.html" ] && [ -d "$cand/assets" ]; then WEB_PICK="$cand"; break; fi
                done
                if [ -z "$WEB_PICK" ]; then
                    WEB_BUILD_NOTE="npm 构建跑完了，但产物里仍没有 index.html + assets/ ⇒ 拒收"
                fi
            else
                WEB_BUILD_NOTE="npm ci / npm run build 失败（见上方输出）⇒ 未自动构建"
            fi
        fi
    fi
fi

if [ -n "$WEB_PICK" ]; then
    rm -rf "$DIST/web"; mkdir -p "$DIST/web"
    cp -a "$WEB_PICK"/. "$DIST/web/"
    ok "web <- $WEB_PICK"
    if [ "$WEB_SUSPECT" = "$WEB_PICK" ]; then
        warn "  但它没有 assets/ 目录，不像构建产物 —— 若其 index.html 指向 /src/*.tsx"
        warn "  则 dist/web 会**白屏**。请改用 WEB_SRC=<已构建的 dist>"
    fi
    # qqwry.ipdb 是 IP 归属地库，Vite 产物有时不在 dist 里，回补一份。
    # 这里必须用 if 而不是 `[ -f "$p" ] && { ...; }`：后者在「两个候选都不存在」时
    # 会让 for 循环以 1 收尾，被 set -e 判为失败 ⇒ 整个脚本在 [2/3] 中途静默退出
    # （已打印过 `[+] web <-`，看着像正常结束）。只在候选缺失时才踩到，故潜伏至今。
    if [ ! -f "$DIST/web/qqwry.ipdb" ]; then
        for p in "$ROOT/AdaptixWeb/public/qqwry.ipdb" "$ROOT/client/qqwry.ipdb"; do
            if [ -f "$p" ]; then
                cp -f "$p" "$DIST/web/qqwry.ipdb"; ok "qqwry.ipdb <- $p"; break
            fi
        done
    fi
else
    warn "未找到 Web 静态产物 —— dist/web 将缺失"
    if [ -n "$WEB_BUILD_NOTE" ]; then
        warn "  未自动构建：$WEB_BUILD_NOTE"
    fi
    if [ -n "$WEB_SUSPECT" ]; then
        warn "  $WEB_SUSPECT 里只有 index.html、没有 assets/ —— 看着是 **Vite 源码树**，"
        warn "  其 index.html 引用 /src/main.tsx，拷进 dist/web 会白屏，故**拒收**。"
    fi
    warn "  Web 控制台是 ./adaptixserver -web 的前提。补法（任选其一）："
    warn "    1) make web WEB_SRC=/path/to/dist     # 用现成的线上 dist"
    warn "    2) cd AdaptixWeb && npm ci && npm run build   # 从源码构建"
    FAILED_OPTIONAL+=("dist/web")
fi

echo
echo "  --- Extension-Kit（可选）---"
EXTKIT_PICK=""
for cand in "${EXTKIT_SRC:-}" "$ROOT/Extension-Kit" "$ROOT/AdaptixServer/Extension-Kit"; do
    [ -n "$cand" ] && [ -f "$cand/extension-kit.axs" ] && { EXTKIT_PICK="$cand"; break; }
done
if [ -n "$EXTKIT_PICK" ]; then
    rm -rf "$DIST/Extension-Kit"; mkdir -p "$DIST/Extension-Kit"
    cp -a "$EXTKIT_PICK"/. "$DIST/Extension-Kit/"
    rm -rf "$DIST/Extension-Kit/.git"
    ok "Extension-Kit <- $EXTKIT_PICK"

    # ★ 拷过去**不等于**能用。20 个 .axs 会在启动时被加载、把 BOF 注册成控制台
    #   命令，但每条命令的 preHook 都是在**运行时**拼
    #       ax.script_dir() + "_bin/<name>." + ax.arch(id) + ".o"
    #   （见 Process-BOF/process.axs 的 findmodule.x64.o）。而 `_bin/` 由各 BOF
    #   子目录的 Makefile 现场编译产生，**不随源码分发** —— 所以不在这里编一次，
    #   dist/Extension-Kit 就是个「命令看得见、一敲就报错」的半可用态。
    #   2026-09-18 在 154 上实测到的原始故障正是它：_bin 不存在、全树 0 个 .x64.o，
    #   而上一层校验只看 extension-kit.axs 在不在位，于是「绿」着放了行。
    if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "  --- Extension-Kit BOF 编译 ---"
        if make -C "$DIST/Extension-Kit" -j"$(nproc 2>/dev/null || echo 2)" \
                >"$DIST/.extkit-bof.log" 2>&1; then
            rm -f "$DIST/.extkit-bof.log"
            ok "$(printf '%-34s' 'Extension-Kit BOF') $(find "$DIST/Extension-Kit" -name '*.x64.o' | wc -l | tr -d ' ') 个 .x64.o"
        else
            fail "Extension-Kit BOF 编译失败 —— 完整日志留在 $DIST/.extkit-bof.log"
            tail -n 20 "$DIST/.extkit-bof.log" >&2
        fi
    else
        fail "x86_64-w64-mingw32-gcc 缺失 —— Extension-Kit BOF 无法编译"
        fail "  后果：.axs 仍会注册命令，但每条 BOF 命令都因 _bin/*.o 缺失而失败"
    fi
else
    warn "未找到 Extension-Kit —— 已跳过（非致命）"
    warn "  profile.yaml / central.profile 的 axscripts 均为 []，运行时不加载，不影响启动"
fi

echo
echo "  --- 运行态配置 ---"
# `make server` 只带 profile.yaml / ssl_gen.sh / 404page.html；
# 另外两份配置（部署模板 + 当前运行态）随包补齐，让 dist/ 自包含。
for f in profile.initial.yaml central.profile; do
    cp -f "$ROOT/AdaptixServer/$f" "$DIST/$f" && ok "$f -> dist/"
done

# -----------------------------------------------------------------------------
# [3/3] 产物自检
# -----------------------------------------------------------------------------
step "[3/3] 产物自检"

RECHECK_FAIL=0

# 核心二进制
if [ -x "$DIST/adaptixserver" ]; then
    ok "$(printf '%-34s' 'dist/adaptixserver') $(stat -c%s "$DIST/adaptixserver" 2>/dev/null || stat -f%z "$DIST/adaptixserver") B"
else
    fail "dist/adaptixserver 缺失"; RECHECK_FAIL=1
fi

# extender 数量与 .so
EXT_N=0; SO_N=0; SO_MISS=()
for d in "$ROOT"/AdaptixServer/extenders/*/; do
    [ -f "$d/Makefile" ] || continue
    EXT_N=$((EXT_N+1))
    name="$(basename "$d")"
    if compgen -G "$DIST/extenders/$name/*.so" >/dev/null 2>&1; then
        SO_N=$((SO_N+1))
    else
        SO_MISS+=("$name")
    fi
done
if [ "$EXT_N" -eq "$EXPECT_EXTENDERS" ] && [ "$SO_N" -eq "$EXPECT_EXTENDERS" ]; then
    ok "$(printf '%-34s' 'extenders') $SO_N/$EXPECT_EXTENDERS 个 .so"
else
    fail "extenders：目录 $EXT_N/$EXPECT_EXTENDERS，.so $SO_N/$EXPECT_EXTENDERS"
    [ "${#SO_MISS[@]}" -gt 0 ] && fail "  缺 .so：${SO_MISS[*]}"
    RECHECK_FAIL=1
fi

# ★ agent_kharon 的运行期载荷目录 —— 只有 `make` 的默认目标是 all 时，
#   `bundle` 才会把 src_beacon / src_loader / src_core / beacon_bin 搬进 dist/。
#   Makefile 若缺 `.DEFAULT_GOAL := all`，默认目标会变成第一条真规则 `plugin`，
#   日志里**只有一句** `make[1]: Nothing to be done for 'plugin'.`：
#   .so 照常产出 ⇒ 上面那条 .so 计数抓不到；但运行期 LoadExtModule 与
#   pl_agent.go 读不到文件 ⇒ 载荷生成与约 20 条命令报 failed to read BOF module。
KHA="$DIST/extenders/agent_kharon"
KHA_MISS=()
for d in src_beacon src_loader src_core beacon_bin; do
    [ -d "$KHA/$d" ] || KHA_MISS+=("$d/")
done
KHA_OBJ=""
compgen -G "$KHA/src_core/dist/*.x64.o" >/dev/null 2>&1 || KHA_OBJ="src_core/dist/*.x64.o"
KHA_BIN=""
compgen -G "$KHA/beacon_bin/*.bin"    >/dev/null 2>&1 || KHA_BIN="beacon_bin/*.bin"
if [ "${#KHA_MISS[@]}" -eq 0 ] && [ -z "$KHA_OBJ" ] && [ -z "$KHA_BIN" ]; then
    KHA_N=$(ls -1 "$KHA/src_core/dist"/*.x64.o 2>/dev/null | wc -l | tr -d ' ')
    KHA_B=$(ls -1 "$KHA/beacon_bin"/*.bin       2>/dev/null | wc -l | tr -d ' ')
    ok "$(printf '%-34s' 'agent_kharon 载荷') src_core ${KHA_N} 个 .x64.o，beacon_bin ${KHA_B} 个 .bin"
else
    KHA_D=""
    [ "${#KHA_MISS[@]}" -gt 0 ] && KHA_D="${KHA_MISS[*]} "
    KHA_LOST="$KHA_D"
    [ -n "$KHA_OBJ" ] && KHA_LOST="${KHA_LOST}${KHA_OBJ} "
    [ -n "$KHA_BIN" ] && KHA_LOST="${KHA_LOST}${KHA_BIN} "
    fail "agent_kharon 载荷不全：缺 ${KHA_LOST% }"
    fail "  ⇒ 该 Makefile 缺 \`.DEFAULT_GOAL := all\`（默认目标成了 plugin，bundle 没执行）"
    fail "    日志特征：make[1]: Nothing to be done for 'plugin'."
    RECHECK_FAIL=1
fi

# ★ BOF 运行期编译输入 —— linux_agent 最容易漏、且漏了是「半可用」的静默故障
LA="$DIST/extenders/linux_agent"
BOFS_C=$(ls -1 "$LA/src_agent/bofs"/*.c   2>/dev/null | wc -l | tr -d ' ')
BOFS_H=$(ls -1 "$LA/src_agent/agent"/*.h  2>/dev/null | wc -l | tr -d ' ')
if [ "$BOFS_C" -ge "$EXPECT_BOFS" ] && [ "$BOFS_H" -ge "$EXPECT_AGENT_HDRS" ]; then
    ok "$(printf '%-34s' 'BOF 编译输入') bofs/*.c=$BOFS_C  agent/*.h=$BOFS_H"
else
    fail "BOF 编译输入不全：bofs/*.c=$BOFS_C（期望 $EXPECT_BOFS） agent/*.h=$BOFS_H（期望 $EXPECT_AGENT_HDRS）"
    fail "  后果：BOF 页全部显示「无源码」、单编与全量重编一并失败"
    RECHECK_FAIL=1
fi

# bof_api.h / elf_bof.h 是 gcc -I 直接依赖的两个头
for h in bof_api.h elf_bof.h; do
    [ -f "$LA/src_agent/agent/$h" ] && ok "$(printf '%-34s' "  $h") 在位" \
                                    || { fail "  缺 $h（gcc -I 依赖）"; RECHECK_FAIL=1; }
done

# 配置三件套
for f in profile.yaml profile.initial.yaml central.profile 404page.html ssl_gen.sh; do
    [ -f "$DIST/$f" ] && ok "$(printf '%-34s' "dist/$f") 在位" \
                      || { fail "dist/$f 缺失"; RECHECK_FAIL=1; }
done

# 可选产物
# ★ web 必须查 assets/：只有 index.html 而没 assets/ ⇒ 拷进去的是 Vite 源码树，
#   浏览器取到 /src/main.tsx（未编译、MIME 非 JS 模块）⇒ 白屏。
if [ -f "$DIST/web/index.html" ]; then
    if [ -d "$DIST/web/assets" ]; then
        ok "$(printf '%-34s' 'dist/web') index.html + assets/ 在位"
    else
        fail "dist/web 有 index.html 却无 assets/ —— 疑为未构建的源码树，控制台会白屏"
        fail "  修法：WEB_SRC=<已构建的 dist> ./build_deploy.sh"
        RECHECK_FAIL=1
    fi
else
    warn "dist/web 缺失（-web 模式需要）"
fi
[ -x "$DIST/axrelay" ] && ok "$(printf '%-34s' 'dist/axrelay') 在位" \
                       || warn "dist/axrelay 缺失（gopher TCP 反向隧道需要）"
if [ -f "$DIST/Extension-Kit/extension-kit.axs" ]; then
    # ★ 只查 .axs 在不在位是**恒真判据**：.axs 从来不缺，缺的一直是 _bin/ 产物。
    #   照它放行的后果不是「少个功能」，而是控制台里**列得出命令、敲下去必失败**
    #   （preHook 拼出的 _bin/*.o 不存在 ⇒ pl_main.go 的 os.Stat 落进 base64 分支
    #   ⇒ agent 收到一串路径文本当 BOF 字节，报「BOF 格式错误」）。所以这里数产物。
    EK_O=$(find "$DIST/Extension-Kit" -name '*.x64.o' 2>/dev/null | wc -l | tr -d ' ')
    if [ "$EK_O" -gt 0 ]; then
        ok "$(printf '%-34s' 'dist/Extension-Kit') .axs 在位，BOF 产物 $EK_O 个 .x64.o"
    else
        fail "dist/Extension-Kit 有 .axs 但 0 个 .x64.o —— _bin/ 没构建"
        fail "  现象：控制台补全能列出 findobj / screenshot_bof 等命令，执行必失败"
        fail "  修法：make -C $DIST/Extension-Kit   （需 x86_64-w64-mingw32-gcc）"
        RECHECK_FAIL=1
    fi
else
    warn "dist/Extension-Kit 缺失（可选项）"
fi

# -----------------------------------------------------------------------------
# 结果
# -----------------------------------------------------------------------------
echo
echo "============================================================================="
if [ "$RECHECK_FAIL" -eq 0 ]; then
    echo "${GREEN}构建完成${NC} —— 产物在 $DIST"
else
    echo "${RED}构建完成但自检未通过${NC} —— 见上方 [x] 项"
fi
if [ "${#FAILED_OPTIONAL[@]}" -gt 0 ]; then
    echo "${YELLOW}非致命项${NC}："
    for x in "${FAILED_OPTIONAL[@]}"; do echo "  - $x"; done
fi
echo "============================================================================="
# 部署指引只在**不带 --up 时**打印。带 --up 时下面那段就在做这件事，
# 再打一遍手工步骤只会让人以为还得自己敲。
# ★ 2026-09-20 §6.31 订正：原指引教的是 `cd /root/cc/dist` + `nohup …`，并写着
#   「后台常驻（与现网一致，**无 systemd**）」—— 路径与托管方式**双双过时**：
#   现网跑在 /home/ad/dist，且自 2026-09-20 起由 systemd unit 托管（PID1 本来就是
#   systemd 255.4，缺的只是 unit）。照原文操作会得到一个「手工 nohup 起、没有自愈、
#   主机重启不自启」的实例，而且它会和 unit 抢同一个端口。
if [ "$UP" = "1" ]; then
    echo "部署由 --up 阶段执行（见下）—— 手工步骤已省略。"
else
    cat <<'EOF'

部署（本脚本只产出 dist/，落地与起停交给 build_run_remove.sh）：

  # 0) 目标机若是新系统，先装依赖（需 root）
  sudo ./pre_install_linux_all.sh server

  # 1) 上传整棵交付树（dist/ + build_run_remove.sh + scripts/ 都要在）
  #    ★ dist/extenders/linux_agent/src_agent/ 必须完整带上 ——
  #      BOF 源码是运行期编译的输入，漏了会「上传能用、编译不能用」

  # 2) 落地并起服务：停 → 备份 → 原子换位 → 装 systemd unit → 起 → 验活
  sudo ./build_run_remove.sh install --root /home/ad/dist --yes
  #    不加 --yes 只打印计划；★ 运行根里**现场那份 profile 默认被保留**，
  #    要让构建出的新默认值生效，得显式加 --take-profiles

  # 3) 首次起服后，BOF 的 26 个 .o 产物需触发一次全量编译：
  #      POST /api/bofs/compile   {"all": true}
  #    或直接在 Web 控制台 BOF 页点「全量重编」

  # 其余子命令：build / run / stop / restart / status / remove（remove 分档，
  # 默认只摘 unit，不碰 dist/data/relay key）
  #   ./build_run_remove.sh status --root /home/ad/dist
  # unit 与 axstart.sh 的写法与理由见 scripts/systemd/README.md
EOF
fi
echo

# -----------------------------------------------------------------------------
# [可选] 瘦身：删掉只用于构建的源码，只留 dist/
#   默认关闭（SLIM_SRC=1 或 --slim-src 才执行）。删的是**构建输入**，
#   不是 dist/ 里的东西 —— 后者含运行期载荷与 BOF 编译输入，一件都不能少。
# -----------------------------------------------------------------------------
if [ "$SLIM_SRC" = "1" ]; then
    step "瘦身：删掉构建源码，只留 dist/"

    # 护栏 ①：自检没过就绝不删 —— 源码是唯一的重建手段
    [ "$RECHECK_FAIL" -eq 0 ] || die "自检未通过 ⇒ 拒绝删除源码（那是唯一的重建手段）"

    # 护栏 ②：ROOT 必须确实是本交付树。空值 / 根目录 / 认错目录一律拒绝，
    #         避免 `${ROOT}` 意外为空时把 rm -rf 打到别处。
    if [ -z "${ROOT:-}" ] || [ "$ROOT" = "/" ] || [ ! -f "$ROOT/build_deploy.sh" ] || [ ! -d "$DIST" ]; then
        die "ROOT='${ROOT:-}' 不像是本交付树（缺 build_deploy.sh 或 dist/）⇒ 拒绝执行删除"
    fi

    SLIM=( "$ROOT/AdaptixServer" "$ROOT/AdaptixWeb" "$ROOT/Makefile" )
    # Extension-Kit 只在 dist/ 里已有副本时才删源目录，否则会净丢组件
    if [ -f "$DIST/Extension-Kit/extension-kit.axs" ]; then
        SLIM+=( "$ROOT/Extension-Kit" )
    else
        warn "dist/Extension-Kit 不在位 ⇒ 保留源目录 Extension-Kit/（否则会净丢该组件）"
    fi

    # 护栏 ③：目标绝不能是 dist/ 本身或落在 dist/ 内
    for p in "${SLIM[@]}"; do
        [ "$p" = "$DIST" ] && die "内部错误：删除目标与 dist/ 相同"
        case "$p" in "$DIST"/*) die "内部错误：删除目标落在 dist/ 内：$p" ;; esac
    done

    echo "  将删除（构建输入，运行期不需要）："
    n_del=0
    for p in "${SLIM[@]}"; do
        [ -e "$p" ] || continue
        n_del=$(( n_del + 1 ))
        printf '    %-22s %s\n' "$(basename "$p")" "$(du -sh "$p" 2>/dev/null | cut -f1)"
    done
    if [ "$n_del" -eq 0 ]; then
        warn "没有可删的目标（可能已经瘦身过）"
    else
        for p in "${SLIM[@]}"; do
            [ -e "$p" ] && rm -rf -- "$p"
        done
        ok "已删除 ${n_del} 项构建源码"
        ok "保留：dist/  pre_install_linux_all.sh  scripts/  README.md"
        warn "本交付树无 git，删除不可逆 —— 要重建需重新上传源码树"
    fi
    echo
fi

[ "$RECHECK_FAIL" -eq 0 ] || exit 1

# =============================================================================
# 部署阶段（--up）
# -----------------------------------------------------------------------------
# 为什么「起没起来」只能靠 HTTP 探测、不能 grep 日志 —— 三条源码级事实：
#
#   1) **端口被占用时进程不退出**。core/connector/connector.go:653-658 只
#      logs.Error 然后 return，主协程永远阻塞在 core/server/server.go 的
#      `<-stopped` ⇒ 进程活着、日志正常、就是不听。
#   2) `[+] Starting server -> …`（server.go:245）与 `[+] The AdaptixC2 server is
#      ready`（server.go:249）**都是无条件打印的**，bind 失败照样打。
#   3) profile 校验失败走 main.go 的 `os.Exit(0)` —— **退出码是 0**。
#      （同类还有 main.go:49 建不出 data/、server.go:216/222 初始化失败后
#        Start() 提前 return ⇒ main 正常返回 ⇒ 也是 0。）
#
# 为什么探针打 endpoint 下的**未注册路径**、而不是 /api/login：
#   login 限流是**按源 IP 每分钟 N 次**（N = profile 的 login_requests_per_minute），
#   只挂在 login_group 上（connector.go:348-355，覆盖 POST /login 与 /refresh）。
#   ★ 二开已把出厂默认改成 0 = 关闭（§6.30），但**探针的设计不依赖这个取值** ——
#     现场随时可能自行改回非 0（或带上别人的 profile），那时 1 秒一次地探 /api/login，
#     5 秒就把配额烧光：不但本次部署判不出来，还会把随后真登录的操作员挡在 429 外面。
#   未注册路径走 gin 的 404，既不鉴权也不限速，同样能证明 TLS 在应答。
#
# set -euo pipefail 纪律（见第 69 行）：本段大量使用「非零退出属于正常控制流」的
#   命令。逐条规避，尤其禁止把 `[ … ] && …` 写成函数体/循环体的最后一条 ——
#   本脚本 :224-226 已经踩过一次，代价是 [2/3] 中途静默退出、看着像正常结束。
# =============================================================================

if [ "$UP" != "1" ]; then
    exit 0
fi

# 纵深防御：与瘦身段护栏 ① 同型。自检不过时上面那行 `|| exit 1` 早已退出，
# 这里是防「以后有人在中间插分支把 exit 绕开」。
[ "$RECHECK_FAIL" -eq 0 ] || die "自检未通过 ⇒ 拒绝部署"

UP_PROFILE_FILE="$DIST/$PROFILE_NAME"
UP_PORT=""
UP_ENDPOINT=""
UP_LOG="$DIST/server.log"
UP_PIDF="$DIST/adaptixserver.pid"
UP_READY_TIMEOUT="${UP_READY_TIMEOUT:-30}"   # 就绪轮询上限（秒）
UP_CONFIRM="${UP_CONFIRM:-10}"               # 超时后的「慢启动 vs 真故障」确认窗（秒）
UP_TERM_WAIT="${UP_TERM_WAIT:-15}"           # TERM 后等进程消失的上限（秒）
UP_KILL_WAIT="${UP_KILL_WAIT:-5}"            # KILL 后等内核回收的上限（秒）

# 服务端参数用数组而非字符串：避免参数里有空格时被拆开。
# 空数组展开必须写成 ${A[@]+"${A[@]}"} —— bash 4.4 以前 set -u 下空数组直接报未绑定。
UP_ARGS=()
if [ "$USE_WEB" = "1" ];   then UP_ARGS+=(-web);   fi
if [ "$USE_DEBUG" = "1" ]; then UP_ARGS+=(-debug); fi

# -----------------------------------------------------------------------------
# 辅助函数
# -----------------------------------------------------------------------------

# 从 profile 里取 Teamserver.<key> 的标量值，结果写进 REPLY（**不打印**）。
# 只解析 Teamserver 这一个块：遇到下一个顶层键就停 —— 别的块（含 operators
# 的逐人口令）绝不读进来。
profile_scalar() {
    local key="$1" line
    line="$(awk -v k="$key" '
        /^Teamserver:/            { inblk = 1; next }
        inblk && /^[^[:space:]#]/ { exit }
        inblk {
            l = $0
            sub(/^[[:space:]]+/, "", l)
            if (index(l, k ":") == 1) { sub("^" k ":[[:space:]]*", "", l); print l; exit }
        }' "$UP_PROFILE_FILE" 2>/dev/null || true)"
    case "$line" in
        \"*) line="${line#\"}"; line="${line%%\"*}" ;;   # "值"        -> 值
        \'*) line="${line#\'}"; line="${line%%\'*}" ;;   # '值'        -> 值
        *)   line="${line%%[[:space:]]#*}" ;;            # 裸值 # 注释 -> 裸值
    esac
    REPLY="$line"
}

# 末尾的 `|| true` 不是装饰：目录不存在时子 shell 返回非零，而调用处
# `X="$(deploy_dir_real …)"` 是**裸赋值**，set -e 会当场杀掉脚本 —— 下面那句
# 「空值 ⇒ die 一句人话」就永远执行不到。空值由调用方判定。
deploy_dir_real() { ( cd "$1" 2>/dev/null && pwd -P ) || true; }

# /proc/<pid>/stat 第 22 字段 = starttime（USER_HZ 滴答）。comm 可能含空格与
# 右括号，故先剥掉 "pid (comm) " 前缀：剥掉后 state 是 $1、starttime 是 $20。
# 同样必须 `|| true`：进程在 `pids_ours` 枚举与这里之间消失时 awk 打不开
# /proc/<pid>/stat（退出码 2），而 `st="$(pid_starttime "$pid")"` 是裸赋值 ⇒
# set -e 直接终止部署。**「进程刚好没了」是这条路径上最正常的控制流**，
# 不是错误 —— 空输出即「这次没读到」，由调用方跳过。
pid_starttime() {
    awk '{ sub(/^[^)]*\) /, ""); print $20 }' "/proc/$1/stat" 2>/dev/null || true
}

# 三道闸：数字合法 → comm 精确名 → cwd 规范匹配。
# comm 是必须的：pid 被复用后 /proc/<pid>/cwd 完全可能也是某个目录，
# 单靠 cwd 排除不掉；两者同时命中的巧合概率可忽略。
# comm 是 15 字符截断的，adaptixserver = 13 字符 ⇒ 精确比较安全。将来若改名
# 超过 15 字符，这里会**恒不匹配 ⇒ 静默漏杀**（比误杀更难发现）。绝不要改用
# `pgrep -f` 绕：启动包装 shell 的命令行含同样的字符串，pgrep -f 会选中包装
# shell，杀错对象而且**表现为成功**。
pid_is_ours() {
    local pid="$1" want="$2" comm cwd
    [ -n "$pid" ] || return 1
    case "$pid" in ''|*[!0-9]*) return 1 ;; esac
    [ "$pid" -gt 1 ] || return 1
    [ -d "/proc/$pid" ] || return 1
    comm="$(cat "/proc/$pid/comm" 2>/dev/null)" || return 1
    [ "$comm" = "adaptixserver" ] || return 1
    cwd="$(readlink "/proc/$pid/cwd" 2>/dev/null)" || return 1
    cwd="${cwd% (deleted)}"          # 目录被删/被换过：内核给的是「同名已删 inode」
    [ "$cwd" = "$want" ] || return 1
    return 0
}

# 打印属于 $1 的 pid，每行一个。**尽力而为的观测，不是断言**：进程在这一瞬间
# 消失、或权限不足读不到 cwd，都只是「这次没看到」，一律 continue，永不 die。
# 僵尸进程读不到 cwd ⇒ 自然跳过（它不占端口、也杀不动）。
pids_ours() {
    local want="$1" d pid comm cwd
    for d in /proc/[0-9]*; do
        pid="${d#/proc/}"
        comm="$(cat "$d/comm" 2>/dev/null)" || continue
        [ "$comm" = "adaptixserver" ] || continue
        cwd="$(readlink "$d/cwd" 2>/dev/null)" || continue
        cwd="${cwd% (deleted)}"
        [ "$cwd" = "$want" ] || continue
        printf '%s\n' "$pid"
    done
    return 0
}

# 端口占用者 pid（空格分隔）。ss 优先，fuser 兜底，都没有则返回空。
# 整条管道必须 `|| true`：pipefail 下 grep 未命中会让赋值非零 ⇒ set -e 杀脚本。
port_holders() {
    local p="$1" raw=""
    if command -v ss >/dev/null 2>&1; then
        raw="$(ss -ltnp 2>/dev/null || true)"
        printf '%s\n' "$raw" | grep -F ":$p " | grep -oE 'pid=[0-9]+' | cut -d= -f2 \
            | sort -u | tr '\n' ' ' || true
    elif command -v fuser >/dev/null 2>&1; then
        fuser -n tcp "$p" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' \
            | tr '\n' ' ' || true
    fi
    return 0
}

# 本次启动之后写进 $UP_LOG 的那一段（按字节偏移，避免把上一轮的
# 'Failed to start HTTP Server' 误算到本轮头上）。用字节而不是行数：
# 日志最后一行没有换行符时 wc -l 会差 1，字节偏移没有这个问题。
log_segment() {
    tail -c +$(( ${LOG_BASE:-0} + 1 )) "$UP_LOG" 2>/dev/null || true
}

# 探针：拿到**任意** HTTP 状态码即视为 TLS 已就绪。
#   ① -k            自签证书
#   ② 不加 -f       404/401/400 都算就绪（-f 会把 404 变成退出码 22 ⇒ 假红）
#   ③ --noproxy '*' 环境里有 http(s)_proxy 时 curl 会连 127.0.0.1 一起发给代理 ⇒ 假红
probe_https() {
    local code
    code="$(curl -k -s --noproxy '*' -o /dev/null -m 3 -w '%{http_code}' \
                 "https://127.0.0.1:${UP_PORT}${UP_ENDPOINT}/__up_probe__" 2>/dev/null || true)"
    if [ -n "$code" ] && [ "$code" != "000" ]; then
        return 0
    fi
    return 1
}

# 返回码：0 = 已就绪 / 2 = 进程已死 / 3 = 慢启动（仍判成功）/ 4 = 活着但不应答。
# **不在函数里 die**：它可能被放进 $( ) 调用，那时 exit 只结束子 shell，
# 父 shell 拿到空串继续跑。
wait_ready() {
    local want="$1" t_end t2
    T_READY_START="$SECONDS"
    t_end=$(( SECONDS + UP_READY_TIMEOUT ))
    while :; do
        if probe_https; then READY_SECS=$(( SECONDS - T_READY_START )); return 0; fi
        if [ -z "$(pids_ours "$want")" ]; then return 2; fi
        if [ "$SECONDS" -ge "$t_end" ]; then
            # 超时先看端口占用者：有外人占 ⇒ 证据已定，不必再等确认窗
            PORT_HOLDERS="$(port_holders "$UP_PORT")"
            if [ -n "$PORT_HOLDERS" ]; then return 4; fi
            # 确认窗：把「慢启动」和「真不应答」分开。少了它，31 秒才起来的
            # 好实例会被误报成故障 —— 这正是本项目一贯反对的**假红**。
            t2=$(( SECONDS + UP_CONFIRM ))
            while [ "$SECONDS" -lt "$t2" ]; do
                sleep 0.5
                if probe_https; then READY_SECS=$(( SECONDS - T_READY_START )); return 3; fi
                if [ -z "$(pids_ours "$want")" ]; then return 2; fi
            done
            return 4
        fi
        sleep 0.5
    done
}

report_dead() {
    fail "进程已消失 —— 启动失败（注意：**退出码可能是 0**，别被它骗了）"
    echo "  --- server.log 本次启动段 ---"
    log_segment | tail -30 | sed 's/^/    /' || true
    warn "对照以下已知的「退出码 0 的死法」："
    warn "  1) profile 校验失败  —— main.go 的 os.Exit(0)；日志含校验原文（must be set / 路径不对…）"
    warn "  2) data/ 建不出来    —— main.go:49 os.Exit(0)；dist 不可写或只读挂载"
    warn "  3) 初始化失败        —— 'Web UI: …' / 'Failed to init HTTP handler: …' ⇒ Start() 提前 return ⇒ main 正常返回 ⇒ 退出码 0"
    warn "  4) 正常退出          —— 收到信号（本服务**没有**装信号处理器，SIGTERM 立即生效）"
}

report_not_listening() {
    local p cw
    fail "$UP_PORT 在 ${UP_READY_TIMEOUT}s（+${UP_CONFIRM}s 确认窗）内没有任何 HTTP 应答，但进程仍在（pid ${START_PID:-未知}）"
    if [ -n "${PORT_HOLDERS:-}" ]; then
        for p in $PORT_HOLDERS; do
            cw="$(readlink "/proc/$p/cwd" 2>/dev/null || true)"
            fail "  端口占用者 pid=$p cwd=${cw:-<不可读>}"
        done
    fi
    echo "  --- 本次启动段里应当能命中的关键字 ---"
    log_segment | grep -nE 'Failed to start HTTP Server|Web UI listener failed|Failed to init HTTP handler|Invalid TLS' | sed 's/^/    /' || true
    fail "★ 这种情况下进程会一直活着不退 —— connector.go:653-658 只 logs.Error 后 return，主协程阻塞在 <-stopped"
    fail "★ 绝不要拿 'Starting server ->' / 'The AdaptixC2 server is ready' 当判据：server.go:245/249 是无条件打印的"
    fail "★ 端口取自 $UP_PROFILE_FILE 的 Teamserver.port=$UP_PORT；若运行态实际端口不是它，本判定就是假的"
}

# HTTPS 通了 ≠ 实例可用：插件加载失败的 teamserver **照样 LISTEN、照样活着**。
# 日志会被轮转/截断，/proc/<pid>/maps 不会 —— 两条判据都要。
check_plugins() {
    local pid="$1" seg nso
    seg="$(log_segment)"
    if printf '%s\n' "$seg" | grep -qE 'failed to open plugin|plugin was built with a different version'; then
        fail "插件加载失败 ⇒ 实例不可用（LISTEN 与进程存活都不是插件可用的证据）"
        printf '%s\n' "$seg" | grep -E 'failed to open plugin|plugin was built with a different version' \
            | head -5 | sed 's/^/    /' || true
        return 1
    fi
    nso="$(grep -cF "$DIST/extenders/" "/proc/$pid/maps" 2>/dev/null || true)"
    if [ "${nso:-0}" -eq 0 ]; then
        warn "进程 maps 里没有 $DIST/extenders/ 下的 .so —— 若本应加载 $EXPECT_EXTENDERS 个 extender，说明是空壳实例"
    else
        ok "extender 已映射（maps 里 $nso 行）"
    fi
    return 0
}

# -----------------------------------------------------------------------------
# 停旧：按 cwd 匹配。**绝不用 `pkill -x adaptixserver`** —— 同机别的部署
# （例如现网 /root/cc/run 那个）会被一起误杀。
# -----------------------------------------------------------------------------
stop_own() {
    local want="$1" pids snap pid st alive i n pf
    pids="$(pids_ours "$want")"
    if [ -z "$pids" ]; then
        ok "无属于 $want 的实例（无需停）"
        return 0
    fi

    # 先按 pid 文件试一次快路径，但它只作**诊断**：权威永远是 cwd 匹配。
    # pid 会被复用，不校验就可能杀掉无关进程，所以必须过 pid_is_ours 三道闸。
    if [ -f "$UP_PIDF" ]; then
        pf="$(cat "$UP_PIDF" 2>/dev/null || true)"
        if [ -n "$pf" ] && pid_is_ours "$pf" "$want"; then
            ok "pid 文件认领：$pf"
        elif [ -n "$pf" ]; then
            warn "pid 文件里的 $pf 已不是本目录的 adaptixserver（陈旧或被复用）—— 忽略，改用 cwd 扫描"
        fi
    fi

    # 枚举 + 记下 starttime，作为后面「pid 有没有被复用」的基准
    snap=""
    for pid in $pids; do
        st="$(pid_starttime "$pid")"
        if [ -n "$st" ]; then snap="${snap}${pid} ${st}"$'\n'; fi
    done
    n="$(printf '%s' "$snap" | grep -c . || true)"
    ok "发现 $n 个属于本目录的实例：$(printf '%s' "$snap" | awk '{printf "%s ", $1}')"

    for pid in $pids; do
        if kill -TERM "$pid" 2>/dev/null; then
            ok "TERM -> pid $pid"
        else
            warn "TERM 失败（pid $pid 已消失或权限不足）—— 不中断，接着看它还在不在"
        fi
    done

    # 本服务没有信号处理器（全树无 signal.Notify）⇒ SIGTERM 按默认处置立即终止，
    # 正常是毫秒级。15s 是三个数量级的余量，用来覆盖 D 状态短阻塞。
    i=0
    while [ "$i" -lt $(( UP_TERM_WAIT * 2 )) ]; do
        alive="$(pids_ours "$want")"
        if [ -z "$alive" ]; then break; fi
        i=$(( i + 1 ))
        sleep 0.5
    done

    if [ -n "$alive" ]; then
        warn "TERM ${UP_TERM_WAIT}s 未生效 —— 发 KILL"
        for pid in $alive; do
            st="$(pid_starttime "$pid")"
            if [ -z "$st" ]; then continue; fi
            # 从枚举到此刻 pid 可能已被复用：starttime 变了就不是刚才那个进程，跳过。
            if ! printf '%s\n' "$snap" | grep -qx "$pid $st"; then
                warn "pid $pid 的 starttime 变了（已被复用）—— 跳过，不杀"
                continue
            fi
            kill -KILL "$pid" 2>/dev/null || true
        done
        i=0
        while [ "$i" -lt $(( UP_KILL_WAIT * 4 )) ]; do
            alive="$(pids_ours "$want")"
            if [ -z "$alive" ]; then break; fi
            i=$(( i + 1 ))
            sleep 0.25
        done
    fi

    if [ -n "$alive" ]; then
        die "仍有杀不掉的实例：$(printf '%s' "$alive" | tr '\n' ' ') —— 拒绝启动（继续起会得到「进程活着但永不监听」的实例）"
    fi
    ok "旧实例已全部停止"
    return 0
}

# -----------------------------------------------------------------------------
# 起新：cwd 必须是 dist/
#   data/（logs/repo.go:22 用 os.Getwd()）、证书 server.rsa.crt/key、web/、
#   extenders、axscripts、404page.html **全部按进程 cwd 解析**。
#   另外 tls_autogen 写证书用的是 **profile 所在目录**，而校验与监听读的是
#   **cwd** —— 二者只有在 cwd == dist/ 时才重合，这是必须 cd 的第二个理由。
# -----------------------------------------------------------------------------
start_server() {
    local dir="$1" pid="" i=0 launcher launch
    : > "$UP_PIDF" 2>/dev/null || true     # 清掉陈旧 pid，免得下面读到上一轮的
    LOG_BASE=0
    if [ -f "$UP_LOG" ]; then
        LOG_BASE="$(stat -c %s "$UP_LOG" 2>/dev/null || true)"
        case "${LOG_BASE:-}" in ''|*[!0-9]*) LOG_BASE=0 ;; esac
    fi

    # 内层 bash 在 exec 之前把**自己的** $$ 写进 pid 文件 —— 它随后被 exec 成
    # 服务端，所以那个 pid 就是服务端 pid。这比用 $! 猜确定：setsid 在调用者
    # 已是进程组组长时会 fork，那时 $! 只是那个秒退的父进程。
    # setsid + nohup + </dev/null：新会话脱离控制终端，ssh 断开/脚本退出都打不到它。
    launch=(setsid nohup)
    command -v setsid >/dev/null 2>&1 || launch=(nohup)

    # 产品化 Stager 把载荷挂在**团队服务器自己的 HTTP endpoint** 上，所以 beacon /
    # linux 的 stager 生成器必须知道那个 endpoint 在哪。缺了它生成会**硬报错**；
    # 从前是静默回落到 8080 —— 那是 agent 监听器 —— 于是 stager 在目标机上死在
    # HttpSendRequestA 里，任何日志都不留痕。
    #
    # ★ 这是**回退**，不是主路径。主路径是 stager 从 agent_params（也就是控制台
    #   「生成载荷」表单）取值 —— 约定是「所有操作员都用 WebUI」，而 env 只能由
    #   起服务的这一步注入，控制台里改不了。两者都没有时才硬报错。
    #   值取自**真正加载的那个 profile**，避免与 dist/central.profile 漂移。
    ( cd "$dir" || exit 1
      export ADAPTIX_TEAMSERVER_PORT="${ADAPTIX_TEAMSERVER_PORT:-$UP_PORT}"
      export ADAPTIX_ENDPOINT="${ADAPTIX_ENDPOINT:-$UP_ENDPOINT}"
      export ADAPTIX_TEAMSERVER_SSL="${ADAPTIX_TEAMSERVER_SSL:-1}"
      exec "${launch[@]}" bash -c 'printf "%s\n" "$$" > "$1"; shift; exec "$@"' \
           _ "$UP_PIDF" ./adaptixserver "${UP_ARGS[@]+"${UP_ARGS[@]}"}" \
           -profile "$PROFILE_NAME" >>"$UP_LOG" 2>&1 </dev/null ) &
    launcher=$!

    while [ "$i" -lt 40 ]; do
        pid="$(cat "$UP_PIDF" 2>/dev/null || true)"
        if [ -n "$pid" ] && pid_is_ours "$pid" "$dir"; then break; fi
        pid=""
        i=$(( i + 1 ))
        sleep 0.25
    done
    if [ -z "$pid" ]; then
        pid="$(pids_ours "$dir" | head -1 || true)"
        if [ -n "$pid" ]; then
            printf '%s\n' "$pid" > "$UP_PIDF" 2>/dev/null || true
            warn "自报 pid 没等到，用 cwd 扫描反查到 $pid"
        fi
    fi
    if [ -z "$pid" ]; then
        START_ERR="10s 内没出现属于 $dir 的 adaptixserver 进程（启动器 pid $launcher）"
        return 1
    fi
    START_PID="$pid"
    return 0
}

# -----------------------------------------------------------------------------
# BOF 全量编译（--bof-compile，默认关）
#   POST <endpoint>/login -> access_token，再 POST <endpoint>/bofs/compile {"all":true}。
#   only_password: true ⇒ 走 tc.OnlyHash，**用户名不参与校验**。
#   凭据纪律：口令只存 shell 变量 —— 不上 argv（curl --data @- 走 stdin）、
#   不 echo、不写日志；本脚本内**不得出现 `set -x`**。
# -----------------------------------------------------------------------------
bof_compile() {
    local pw pw_esc body resp token user="adaptix"
    profile_scalar password
    pw="$REPLY"
    if [ -z "$pw" ]; then
        warn "profile 里读不到 Teamserver.password ⇒ 跳过 --bof-compile（控制台 BOF 页点「全量重编」是等价路径）"
        return 0
    fi
    # JSON 转义反斜杠与双引号；请求体在 bash 变量里拼好，不经过任何子进程的 argv。
    pw_esc="${pw//\\/\\\\}"
    pw_esc="${pw_esc//\"/\\\"}"
    body="{\"username\":\"$user\",\"password\":\"$pw_esc\",\"version\":\"1.6.0\"}"

    resp="$(printf '%s' "$body" | curl -k -s --noproxy '*' -m 10 \
              -H 'Content-Type: application/json' --data @- \
              "https://127.0.0.1:${UP_PORT}${UP_ENDPOINT}/login" 2>/dev/null || true)"
    unset pw pw_esc body
    token="$(printf '%s' "$resp" | sed -n 's/.*"access_token"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1 || true)"
    if [ -z "$token" ]; then
        warn "登录失败或没拿到 access_token ⇒ 跳过 BOF 全量编译（口令不符 / 429 限速都可能）"
        warn "  登录口限速是 5 次/分/IP；上面的就绪探针走未注册路径，不占这个配额"
        return 0
    fi

    resp="$(printf '{"all":true}' | curl -k -s --noproxy '*' -m 600 \
              -H 'Content-Type: application/json' -H "Authorization: Bearer $token" \
              --data @- "https://127.0.0.1:${UP_PORT}${UP_ENDPOINT}/bofs/compile" 2>/dev/null || true)"
    unset token
    if printf '%s' "$resp" | grep -q '"ok"[[:space:]]*:[[:space:]]*true'; then
        ok "BOF 全量编译完成：count=$(printf '%s' "$resp" | sed -n 's/.*"count"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' | head -1) n_changed=$(printf '%s' "$resp" | sed -n 's/.*"n_changed"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' | head -1)"
    else
        warn "BOF 全量编译未成功，服务端回："
        printf '%s\n' "$resp" | head -c 800 | sed 's/^/    /' || true
        warn "  可在控制台 BOF 页点「全量重编」重试（等价路径）"
    fi
    return 0
}

# -----------------------------------------------------------------------------
# 主流程：停旧 → 起新 → 验活
# -----------------------------------------------------------------------------
do_up() {
    local h p cw rc axs
    step "部署（--up）：停旧 → 起新 → 验活"

    # --- 前置检查 ---
    # 非 root 必须直接死：/proc/<pid>/cwd 只对属主与 root 可读，读不到就**静默
    # 漏掉**本目录的旧实例 ⇒ 起出第二个 ⇒ 撞上「进程活着但不监听」。宁可拒绝，
    # 也不给「静默漏杀」留机会。
    if [ "$(id -u)" -ne 0 ]; then
        die "部署需要 root —— 非 root 读不到别人的 /proc/<pid>/cwd，按 cwd 匹配会静默漏掉旧实例"
    fi
    [ -x "$DIST/adaptixserver" ] || die "$DIST/adaptixserver 不存在或不可执行"
    [ -f "$UP_PROFILE_FILE" ] || die "profile 不在位：$UP_PROFILE_FILE（--profile=<名> 可改）"
    [ -w "$DIST" ] || die "$DIST 不可写 —— 服务端要在 cwd 下建 data/ 与证书，建不出来时 main.go 走 os.Exit(0)"

    profile_scalar port;     UP_PORT="$REPLY"
    profile_scalar endpoint; UP_ENDPOINT="$REPLY"
    case "$UP_PORT" in
        ''|*[!0-9]*) die "解析不出 Teamserver.port（读到 '${UP_PORT}'）—— profile 格式变了？" ;;
    esac
    [ -n "$UP_ENDPOINT" ] || die "解析不出 Teamserver.endpoint"
    UP_ENDPOINT="${UP_ENDPOINT%/}"
    ok "profile $UP_PROFILE_FILE：port=$UP_PORT endpoint=$UP_ENDPOINT"

    # axscripts 的绝对路径与部署位置绑定，写错只 logs.Error、不阻止启动 ⇒ 顺手核一下。
    axs="$(sed -n 's/^[[:space:]]*-[[:space:]]*"\(\/[^"]*\.axs\)".*/\1/p' "$UP_PROFILE_FILE" 2>/dev/null | head -1 || true)"
    if [ -n "$axs" ]; then
        case "$axs" in
            "$DIST"/*) ok "axscripts 指向本 dist 内：$axs" ;;
            *) warn "profile 的 axscripts 指向 $axs，不在 $DIST 内 —— 该脚本会**静默不加载**（失败只 logs.Error）" ;;
        esac
    fi

    # argfuscator 插件要服务端有 pwsh：它 fork pwsh 调 Invoke-ArgFuscator 生成变体。
    # 缺了**不阻止启动** —— 插件照常加载，只在 /status 里报 pwsh_unavailable，控制台
    # 「参数混淆」页显示「服务端没有可用的 pwsh」。与上面 axscripts 同类：不报错的
    # 半可用，故只告警、不死。放在 --up 段而不是 [1/3] 自检，是为了不动既有输出。
    # 只查 PATH：config.yaml 若填了 pwsh_path，这里探不到也不代表不能用。
    if ! command -v pwsh >/dev/null 2>&1; then
        warn "本机 PATH 没有 pwsh —— argfuscator 会在 /status 报 pwsh_unavailable（参数混淆页显示「服务端没有可用的 pwsh」）"
        warn "  装法：sudo ./pre_install_linux_all.sh server；或把 pwsh 的绝对路径填进 extenders/argfuscator/config.yaml 的 pwsh_path"
    fi

    # 生成 Windows **EXE / Service EXE** 载荷时，服务端 fork openssl + osslsigncode 现场
    # 做一个随机自签名的 Authenticode。与上面 pwsh 不同，这条**不是**「半可用」：
    #   beacon_agent/pl_main.go:1259 对 Exe/Service Exe 无条件签名，缺工具就 return err
    #   ⇒ 整个「生成并下载」失败，控制台红字 pseudo-sign requires osslsigncode in PATH；
    #   而 beacon 侧那个 exe_pseudo_sign 勾选框是**惰性的**（声明了没人读）⇒ 关不掉。
    # 所以这里把两条依赖都探一遍，缺了就明确说「EXE 载荷会直接生成失败」。
    # 仍然只告警不死：不生成 EXE 的话（ELF/Shellcode/Stager 等）服务端一切正常。
    for _tool in openssl osslsigncode; do
        if ! command -v "$_tool" >/dev/null 2>&1; then
            warn "本机 PATH 没有 $_tool —— Windows EXE / Service EXE 载荷**生成会直接失败**（其余格式不受影响）"
            warn "  装法：sudo ./pre_install_linux_all.sh server（apt 装 openssl + osslsigncode）"
        fi
    done
    unset _tool

    DEPLOY_DIR="$(deploy_dir_real "$DIST")"
    [ -n "$DEPLOY_DIR" ] || die "解析不出 $DIST 的规范路径"

    # 并发保护。flock 挂在打开的 fd 上，脚本退出（含 die/异常）时内核自动释放，
    # 没有陈旧锁问题。
    if command -v flock >/dev/null 2>&1; then
        if exec 9>"$UP_PIDF.lock"; then
            flock -n 9 || die "另一次 --up 正在运行（$UP_PIDF.lock 被持有）"
        else
            warn "建不了锁文件 $UP_PIDF.lock —— 跳过并发保护"
        fi
    fi

    # --- 停旧 ---
    stop_own "$DEPLOY_DIR"

    # 安全门：端口必须没人占。枚举可能漏（comm 截断、mount namespace 差异…），
    # 这一条兜住所有「静默漏杀」。进程死则端口必然释放，所以这里不是「等端口」，
    # 而是「查一次当门」—— 把「端口被别人占着」这个真故障直接说出来，而不是
    # 表达成「等了 30 秒」这种假象。
    h="$(port_holders "$UP_PORT")"
    if [ -n "$h" ]; then
        fail "$UP_PORT 仍被占用（pid：$h）—— 拒绝启动"
        for p in $h; do
            cw="$(readlink "/proc/$p/cwd" 2>/dev/null || true)"
            fail "  占用者 pid=$p comm=$(cat "/proc/$p/comm" 2>/dev/null || echo '?') cwd=${cw:-<不可读>}"
        done
        die "继续起会得到「进程活着、日志还报 ready、就是不听」的实例（connector.go:653-658）"
    fi
    ok "端口 $UP_PORT 空闲"

    # --- 起新 ---
    LOG_BASE=0
    START_PID=""
    START_ERR=""
    if ! start_server "$DEPLOY_DIR"; then
        die "${START_ERR:-启动失败}"
    fi
    ok "已启动：pid $START_PID（cwd=$DEPLOY_DIR）"

    # --- 验活 ---
    rc=0
    wait_ready "$DEPLOY_DIR" || rc=$?
    case "$rc" in
        0) ok "HTTPS 已就绪（${READY_SECS}s）" ;;
        3) warn "慢启动：${READY_SECS}s 才应答（超出 ${UP_READY_TIMEOUT}s 窗口）—— 判为成功，但请留意" ;;
        2) report_dead;           die "启动失败：进程已退出" ;;
        4) report_not_listening;  die "启动失败：进程活着但端口不应答" ;;
        *) die "内部错误：wait_ready 返回 $rc" ;;
    esac

    # 二级判据：HTTPS 通了 ≠ 实例可用
    check_plugins "$START_PID" || die "实例在应答但插件未加载（见上）—— 判红"

    if [ "$BOF_COMPILE" = "1" ]; then
        bof_compile
    fi

    echo
    ok "部署完成：$DEPLOY_DIR @ $(date '+%F %T')"
    ok "  日志 $UP_LOG    pid 文件 $UP_PIDF"
    ok "  停止：kill \$(cat $UP_PIDF)"
    return 0
}

do_up
exit 0

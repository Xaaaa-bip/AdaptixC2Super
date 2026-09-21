#!/usr/bin/env bash

GREEN=$(printf '\033[1;32m')
YELLOW=$(printf '\033[1;33m')
RED=$(printf '\033[1;31m')
NC=$(printf '\033[0m')

GO_VERSION='1.25.4'

# Node.js —— 只用于构建 Web 控制台（AdaptixWeb 是 Vite 5 工程），服务端本身不需要。
# 为什么不走 apt：Vite 5 要求 Node >= 18，而 Ubuntu 22.04 的 nodejs 是 v12、
# Debian 12 才是 v18 —— apt 会造出「node -v 有输出、npm run build 必失败」的假成功。
# 版本化目录 https://nodejs.org/dist/v$NODE_VERSION/ 是永久链接，钉死不会随上游更新而 404。
NODE_VERSION='22.23.2'
NODE_MAJOR_MIN=18

# PowerShell —— 只给 argfuscator 插件用（服务端 fork pwsh 调 Invoke-ArgFuscator 生成
# 命令行变体），服务端本身与构建链都不需要它。**不走微软 apt 源**：那要先装
# packages-microsoft-prod.deb 并配 keyring；GitHub release 的 tarball 这条已在目标机上
# 验证可行，且版本化 URL 是永久链接。
# ★ 装了它不等于插件就能用：vendor_ps/（models 那 156 个 json）必须与 .so 同目录，
#   二者缺一都会在生成时报错，见 extenders/argfuscator/vendor_ps/THIRD_PARTY.md。
PWSH_VERSION='7.4.20'

ERROR_FILE="$(date "+%d.%m.%Y_%H-%M-%S")-error.log"

SERVER_DEPENDENCIES=(
    git
    # 下载与探活都要它们：本脚本自己用 wget 拉 Go/Node/pwsh 的 tarball，
    # build_deploy.sh 用 curl 打就绪探针。最小化的 Debian/Ubuntu 上这两个都可能
    # 缺席，而那时脚本已经跑到下载步骤了 ⇒ 提前放进依赖表，别等到用的时候才 127。
    wget
    curl
    # 没有 ca-certificates，wget/curl 对 https 一律证书校验失败 —— 而 Go / Node /
    # pwsh 三家全部走 https 下载，症状是三条都 report_fail，看着像网络不通。
    ca-certificates
    mingw-w64
    make
    gcc
    g++
    g++-mingw-w64
    # beacon_agent / agent_kharon 的 src_beacon 用 `clang++ -target
    # x86_64-w64-mingw32` 交叉编译 Windows 载荷。mingw-w64 只提供 GCC 交叉链
    # （x86_64-w64-mingw32-g++），**不含 clang++**，必须单独装，否则
    # `make extenders` 在 beacon_agent 处 Error 127。
    clang
    # beacon_agent / agent_kharon 的 src_beacon 用 nasm 汇编 x64/x86 的
    # entry_*.o 与 indirect_syscall（`nasm -f win64/win32`）。不装则在
    # objects_http/indirect_syscall.x64.o 处 Error 127。
    nasm
    # beacon_agent / gopher_agent 生成 Windows **EXE / Service EXE** 载荷时，服务端会
    # fork openssl + osslsigncode 现场做一个随机自签名的 Authenticode 伪签名。
    # ★ 现在是**可关的**：beacon_agent/pl_main.go 的 effectiveExePseudoSign() 读
    #   exe_pseudo_sign（三态：省略 = 跟随 buildenv.mk 的 BEACON_PSEUDO_SIGN，当前 true）。
    #   关掉勾选框即整段跳过。缺工具时也**不再**让生成失败 —— pseudoSignMissingTool()
    #   判到就降级为「不签名 + 记构建日志」（只有签名**执行失败**才终止，那可能已写出半份产物）。
    #   注意别把这层降级当成"随便不装也行"：默认就是签的，装上才能拿到和 UI 默认一致的结果。
    #   osslsigncode 在 Ubuntu 的 universe 源里：没启用 universe 时这条会 report_fail
    #   （不致命），需手工 `apt-get install -y osslsigncode`。
    openssl
    osslsigncode
)

report_step() {
    echo
    echo "${YELLOW}[STEP]${NC} $1..."
}

report_success() {
    echo "${GREEN}[SUCCESS]${NC} $1!"
}

report_fail() {
    echo "${RED}[FAIL]${NC} $1!"
}

packets_install() {
    local -n packages_list=$1
    local package err

    for package in ${packages_list[@]}; do
        # ★ DEBIAN_FRONTEND=noninteractive 不能省：不加它，tzdata / needrestart 这类包
        #   会弹交互式提问，无人值守装机会**卡在那里不动** —— 不是报错，是永远等输入，
        #   脚本看起来「还在跑」，实际已经死了。
        # ★ 用 apt-get 而不是 apt：apt 是给人看的 CLI，在脚本里会往 stderr 打
        #   「apt does not have a stable CLI interface」，把真错误淹掉。
        # ★ 失败时把 apt 自己的最后几行打出来：只报「安装失败」等于没说 ——
        #   现场最常见的两种（包名在当前源里没有 / universe 未启用）只有它那句话能区分。
        if err="$(DEBIAN_FRONTEND=noninteractive apt-get install -y "$package" 2>&1 >/dev/null)"; then
            report_success "The \"$package\" package has been successfully installed into the system"
        else
            report_fail "An error occurred during the installation of the \"$package\" package"
            printf '%s\n' "$err" | grep -vE '^\s*$' | tail -3 | sed 's/^/        /'
        fi
    done
}

repo_index_update() {
    report_step "Updating the repository index"

    if DEBIAN_FRONTEND=noninteractive apt-get update > /dev/null; then
        report_success "The repository index has been successfully updated"
    else
        report_fail "An error occurred while updating the repository index"
    fi
}

# osslsigncode 在 Ubuntu 的 universe 源里。标准 Server 安装默认已启用 universe，
# 但最小化/裁剪过的镜像可能没有 —— 那时 apt 会报「Unable to locate package」，
# 而这条依赖**不是**可选的（EXE/Service EXE 载荷生成会直接失败，见上）。
# 所以这里做一次宽松补救：装 software-properties-common → 开 universe → 重装。
ensure_universe_for() {
    local package="$1"

    if dpkg -s "$package" >/dev/null 2>&1; then
        return 0
    fi

    report_step "Enabling the universe repository (required by \"$package\")"
    DEBIAN_FRONTEND=noninteractive apt-get install -y software-properties-common >/dev/null 2>&1 \
        || { report_fail "Could not install software-properties-common — install \"$package\" manually"; return 1; }
    command -v add-apt-repository >/dev/null 2>&1 \
        || { report_fail "add-apt-repository not found — install \"$package\" manually"; return 1; }

    add-apt-repository -y universe >/dev/null 2>&1 || true
    DEBIAN_FRONTEND=noninteractive apt-get update >/dev/null 2>&1 || true
    if DEBIAN_FRONTEND=noninteractive apt-get install -y "$package" >/dev/null 2>&1; then
        report_success "The \"$package\" package has been installed after enabling universe"
        return 0
    fi
    report_fail "Still could not install \"$package\" — install it manually"
    return 1
}

server_packets_install() {
    report_step "Installing the packages dependencies for the server"
    packets_install SERVER_DEPENDENCIES
    # 上面那条循环对 osslsigncode 失败是**容忍**的（只 report_fail），但这条依赖
    # 真不能缺 —— EXE/Service EXE 载荷生成会整个失败。所以在循环之后单独补救一次。
    ensure_universe_for osslsigncode
}

actual_go_version_install() {
    report_step "Downloading the latest version of Go"

    DOWNLOAD_PATH="/tmp/go$GO_VERSION.linux-amd64.tar.gz"

    # ★ 下载成败必须先记住，**再决定动不动 /usr/local/go**。
    #   原始写法是「下载失败也照样 rm -rf /usr/local/go」—— 于是一次网络抖动就能把
    #   一台本来能正常构建的机器拆掉，而报错只有一行 [FAIL]，看不出是网络问题。
    local dl_ok=0
    if wget -q https://go.dev/dl/go$GO_VERSION.linux-amd64.tar.gz -O "$DOWNLOAD_PATH"; then
        dl_ok=1
        report_success "The Go distribution for version $GO_VERSION has been successfully downloaded to \"$DOWNLOAD_PATH\""
    else
        report_fail "An error occurred while downloading the Go distribution version $GO_VERSION to \"$DOWNLOAD_PATH\""
    fi

    if [ "$dl_ok" -ne 1 ]; then
        local have
        have="$(/usr/local/go/bin/go env GOVERSION 2>/dev/null || true)"
        if [ "$have" = "go$GO_VERSION" ]; then
            report_success "Download failed, but /usr/local/go is already $have — keeping it, nothing to do"
        else
            report_fail "Download failed and /usr/local/go is not go$GO_VERSION (found: ${have:-none})"
            report_fail "  → 工具链**原样保留**，先修网络再重跑；不要在这种情况下继续后面的步骤"
        fi
        return 0
    fi

    # ★ 先解到临时目录、验过可执行，再换 —— 直接 tar 到 /usr/local 的话，
    #   解压半路失败（磁盘满 / 包被截断）会留下一个**残缺的 /usr/local/go**，
    #   它看起来「装好了」，实际每个 go 命令都报奇怪的错。
    local tmpdir="/usr/local/.go-extract-$$"
    rm -rf "$tmpdir"; mkdir -p "$tmpdir"
    if tar -xzf "$DOWNLOAD_PATH" -C "$tmpdir" && [ -x "$tmpdir/go/bin/go" ]; then
        rm -rf /usr/local/go
        mv "$tmpdir/go" /usr/local/go
        report_success "The downloaded Go distribution version $GO_VERSION has been successfully extracted to \"/usr/local/go\""
    else
        report_fail "An error occurred while extracting the Go distribution of version $GO_VERSION — /usr/local/go 未被动过"
        rm -rf "$tmpdir"
        return 0
    fi
    rm -rf "$tmpdir"

    # ★ ln -sf 而不是 ln -s：重复执行时 ln -s 会因「File exists」失败，
    #   刷出一条**假的** [FAIL]（下面的 Node 段就是为此改成 -sf 的，这里对齐）。
    if ln -sf /usr/local/go/bin/go /usr/local/bin/go; then
        report_success "A symbolic link to the PATH directory for Go version $GO_VERSION has been created successfully"
    else
        report_fail "An error occurred while creating a symbolic link to the PATH directory for Go version $GO_VERSION"
    fi

    report_step "Installing rsrc (Windows .ico embedding for Gopher EXE builds)"
    if /usr/local/go/bin/go install github.com/akavel/rsrc@latest; then
        RBIN="$(/usr/local/go/bin/go env GOPATH)/bin"
        if [[ -x "$RBIN/rsrc" ]] && ln -sf "$RBIN/rsrc" /usr/local/bin/rsrc 2>/dev/null; then
            report_success "rsrc installed to $RBIN and linked as /usr/local/bin/rsrc"
        else
            report_success "rsrc installed to $RBIN (add GOPATH/bin to PATH if rsrc is not found)"
        fi
    else
        report_fail "go install rsrc failed — Gopher Windows icon option will need manual: go install github.com/akavel/rsrc@latest"
    fi

    report_step "Downloading the latest version of Go, which includes Windows 7 support for Gopher Agent"

    # ★ 重复执行友好：目录已在就 pull，不再 clone。
    #   原写法第二次必然 `fatal: destination path '/tmp/go-win7' already exists`
    #   ⇒ 刷一条**假的** [FAIL]，让人以为装机失败。
    if [ -d /tmp/go-win7/.git ]; then
        if git -C /tmp/go-win7 pull --ff-only >/dev/null 2>&1; then
            report_success "go-win7 is already present — refreshed with git pull"
        else
            report_success "go-win7 is already present — keeping the local copy (pull skipped)"
        fi
    elif git clone https://github.com/Adaptix-Framework/go-win7 /tmp/go-win7; then
        report_success "The latest version of Go, with Windows 7 support for Gopher Agent, has been successfully downloaded"
    else
        report_fail "An error occurred while downloading the latest version of Go with Windows 7 support for Gopher Agent"
    fi

    if [ -d /usr/lib/go-win7 ]; then
        if rm -rf /usr/lib/go-win7; then
            report_success "An existing directory \"/usr/lib/go-win7\" has been successfully deleted"
        else
            report_fail "An existing directory \"/usr/lib/go-win7\" has been found! An error occurred while attempting to delete it"
        fi
    fi

    if mv /tmp/go-win7 /usr/lib/; then
        report_success "The latest version of Go with Windows 7 support has been successfully installed"
    else
        report_fail "An error occurred while installing the current version of Go with Windows 7 support for Gopher Agent"
    fi

    # go-win7 git tree does not ship pkg/include (textflag.h etc.); the official tarball does.
    # Without these headers, "go build" fails in crypto/reflect asm. Copy from the Go we just installed.
    if [ ! -f /usr/lib/go-win7/pkg/include/textflag.h ]; then
        report_step "Completing go-win7 with asm headers from /usr/local/go"
        if [ -f /usr/local/go/pkg/include/textflag.h ]; then
            mkdir -p /usr/lib/go-win7/pkg/include
            cp -a /usr/local/go/pkg/include/* /usr/lib/go-win7/pkg/include/
            report_success "Copied /usr/local/go/pkg/include into /usr/lib/go-win7/pkg/include"
        else
            report_fail "Missing /usr/local/go/pkg/include/textflag.h — reinstall Go tarball or run scripts/repair_go_win7.sh after fixing /usr/local/go"
        fi
    fi
}

actual_node_version_install() {
    report_step "Installing Node.js $NODE_VERSION (needed only to build the Web console)"

    local NODE_ARCH
    case "$(uname -m)" in
        x86_64|amd64)  NODE_ARCH='x64'   ;;
        aarch64|arm64) NODE_ARCH='arm64' ;;
        *)
            report_fail "Unsupported architecture '$(uname -m)' — build the Web console elsewhere and pass WEB_SRC=<built dist> to build_deploy.sh"
            return 0
            ;;
    esac

    local NODE_DIR="/usr/local/lib/nodejs/node-v$NODE_VERSION-linux-$NODE_ARCH"
    local NODE_TARBALL="/tmp/node-v$NODE_VERSION-linux-$NODE_ARCH.tar.gz"
    local NODE_URL="https://nodejs.org/dist/v$NODE_VERSION/node-v$NODE_VERSION-linux-$NODE_ARCH.tar.gz"

    if wget "$NODE_URL" -O "$NODE_TARBALL"; then
        report_success "The Node.js distribution version $NODE_VERSION has been successfully downloaded to \"$NODE_TARBALL\""
    else
        report_fail "An error occurred while downloading \"$NODE_URL\" — the Web console will not be built (pass WEB_SRC=<dist> to build_deploy.sh instead)"
        return 0
    fi

    rm -rf "$NODE_DIR"
    if mkdir -p /usr/local/lib/nodejs && tar -xzf "$NODE_TARBALL" -C /usr/local/lib/nodejs; then
        report_success "The Node.js distribution version $NODE_VERSION has been successfully extracted to \"$NODE_DIR\""
    else
        report_fail "An error occurred while extracting \"$NODE_TARBALL\" to \"/usr/local/lib/nodejs\""
        return 0
    fi

    # 官方 tarball 自带 npm/npx，无需另装。用 `ln -sf` 而不是 `ln -s`：重复执行时
    # 不会因「File exists」刷出假的 [FAIL]（上面 Go 那段的 ln -s 就有这个毛病）。
    if ln -sf "$NODE_DIR/bin/node" /usr/local/bin/node \
    && ln -sf "$NODE_DIR/bin/npm"  /usr/local/bin/npm \
    && ln -sf "$NODE_DIR/bin/npx"  /usr/local/bin/npx; then
        report_success "node / npm / npx have been linked into \"/usr/local/bin\""
    else
        report_fail "An error occurred while linking node/npm/npx into \"/usr/local/bin\""
        return 0
    fi

    # 自检主版本：PATH 上若有一个更旧的 node 把新的盖掉，构建 Web 时会以那个为准，
    # 症状是 `npm run build` 报 Vite 的 engines 错误而不是「没装 node」。
    local HAVE
    HAVE="$(node -v 2>/dev/null)"
    if [ -z "$HAVE" ]; then
        report_fail "node is not on PATH after installation — check that /usr/local/bin precedes any other node in \$PATH"
        return 0
    fi
    local HAVE_MAJOR="${HAVE#v}"
    HAVE_MAJOR="${HAVE_MAJOR%%.*}"
    if [ "$HAVE_MAJOR" -ge "$NODE_MAJOR_MIN" ] 2>/dev/null; then
        report_success "node $HAVE is on PATH (Vite 5 requires >= v$NODE_MAJOR_MIN)"
    else
        report_fail "node $HAVE is older than v$NODE_MAJOR_MIN (found at $(command -v node)) — Vite 5 will refuse to build; remove the older node or fix \$PATH"
    fi
}

actual_pwsh_version_install() {
    report_step "Installing PowerShell $PWSH_VERSION (needed only by the argfuscator plugin)"

    local PWSH_ARCH
    case "$(uname -m)" in
        x86_64|amd64)  PWSH_ARCH='x64'   ;;
        aarch64|arm64) PWSH_ARCH='arm64' ;;
        *)
            report_fail "Unsupported architecture '$(uname -m)' — install pwsh manually, or point pwsh_path in extenders/argfuscator/config.yaml at an existing pwsh"
            return 0
            ;;
    esac

    local PWSH_DIR='/opt/microsoft/powershell/7'
    local PWSH_TARBALL="/tmp/powershell-$PWSH_VERSION-linux-$PWSH_ARCH.tar.gz"
    local PWSH_URL="https://github.com/PowerShell/PowerShell/releases/download/v$PWSH_VERSION/powershell-$PWSH_VERSION-linux-$PWSH_ARCH.tar.gz"

    if wget "$PWSH_URL" -O "$PWSH_TARBALL"; then
        report_success "The PowerShell distribution version $PWSH_VERSION has been successfully downloaded to \"$PWSH_TARBALL\""
    else
        report_fail "An error occurred while downloading \"$PWSH_URL\" — the argfuscator plugin will keep reporting pwsh_unavailable"
        return 0
    fi

    rm -rf "$PWSH_DIR"
    if mkdir -p "$PWSH_DIR" && tar -xzf "$PWSH_TARBALL" -C "$PWSH_DIR"; then
        report_success "The PowerShell distribution version $PWSH_VERSION has been successfully extracted to \"$PWSH_DIR\""
    else
        report_fail "An error occurred while extracting \"$PWSH_TARBALL\" to \"$PWSH_DIR\""
        return 0
    fi

    # GitHub 的 tarball **不带可执行位**：不 chmod 就会得到一个「存在、但无法执行」的
    # pwsh。它的症状与「没装」不同 —— exec.LookPath 找得到它，报错变成 permission
    # denied，更容易被误判成别的问题。
    if chmod +x "$PWSH_DIR/pwsh" && ln -sf "$PWSH_DIR/pwsh" /usr/bin/pwsh; then
        report_success "pwsh has been linked into \"/usr/bin\""
    else
        report_fail "An error occurred while linking pwsh into \"/usr/bin\""
        return 0
    fi

    # 自检跑的就是插件探活用的那条命令（pl_main.go 的 probePwsh），装完即知它认不认。
    local HAVE
    HAVE="$(pwsh -NoProfile -NonInteractive -NoLogo -Command '$PSVersionTable.PSVersion.ToString()' 2>/dev/null)"

    # ★ 失败时先补 libicu 再复探。pwsh 的 tarball **不自带** ICU，靠系统提供；
    #   最小化安装的 Debian/Ubuntu 上没有它，症状是 pwsh「存在、可执行、一跑就死」
    #   （缺 libicu*.so）。这种情况与「根本没装 pwsh」在插件侧表现不同 ——
    #   exec.LookPath 找得到它，于是报的不是 pwsh_unavailable 而是 pwsh 执行失败。
    #   libicu-dev 在 Debian/Ubuntu 上会把对应的 libicu<NN> 运行库一并拉进来。
    if [ -z "$HAVE" ] && command -v apt-get >/dev/null 2>&1; then
        report_step "pwsh did not answer — installing libicu (the pwsh tarball does not bundle it)"
        DEBIAN_FRONTEND=noninteractive apt-get install -y libicu-dev >/dev/null 2>&1 || true
        HAVE="$(pwsh -NoProfile -NonInteractive -NoLogo -Command '$PSVersionTable.PSVersion.ToString()' 2>/dev/null)"
    fi

    if [ -n "$HAVE" ]; then
        report_success "pwsh $HAVE is on PATH (this is the same probe the argfuscator plugin uses)"
    else
        report_fail "pwsh is on PATH but did not report a version — diagnose with: ldd \"\$(command -v pwsh)\" | grep 'not found'"
    fi
}

# 装完给一个**明确结论**。没有这一步，操作员只能从几十行 [SUCCESS]/[FAIL] 里自己数，
# 而「到底能不能开构建」恰好是这一步最该回答的问题 —— 判据取 build_deploy.sh 的 [1/3]
# 环境自检实际会探的那一组（少了或多了一目了然）。
final_verdict() {
    report_step "Final check — the dependency set that build_deploy.sh actually probes"

    # 必需：缺了构建/运行**直接失败**（对应 build_deploy.sh 的 need 调用）
    local hard=(go make gcc git nasm openssl clang++ x86_64-w64-mingw32-g++ x86_64-w64-mingw32-gcc)
    # 视用途：缺了只是某个功能不可用，构建本身能过
    local soft=(node npm pwsh osslsigncode)
    local missing_hard=0 missing_soft=0 t p

    echo "  必需（缺则构建或运行直接失败）:"
    for t in "${hard[@]}"; do
        p="$(command -v "$t" 2>/dev/null || true)"
        if [ -n "$p" ]; then
            printf '    %b[ok]%b %-26s %s\n' "$GREEN" "$NC" "$t" "$p"
        else
            printf '    %b[!!]%b %-26s 缺失\n' "$RED" "$NC" "$t"
            missing_hard=$((missing_hard + 1))
        fi
    done

    echo "  视用途（缺则对应功能不可用，构建本身能过）:"
    for t in "${soft[@]}"; do
        p="$(command -v "$t" 2>/dev/null || true)"
        if [ -n "$p" ]; then
            printf '    %b[ok]%b %-26s %s\n' "$GREEN" "$NC" "$t" "$p"
        else
            printf '    %b[--]%b %-26s 缺失\n' "$YELLOW" "$NC" "$t"
            missing_soft=$((missing_soft + 1))
        fi
    done

    # 两道版本门：go 低于 go.work 声明值会出构建差异；node 低于 18 会被 Vite 5 拒绝。
    local gov nodev
    gov="$(go env GOVERSION 2>/dev/null || true)"
    case "$gov" in
        "go$GO_VERSION"|"go$GO_VERSION".*) report_success "go $gov matches the version declared in go.work" ;;
        "")                                 report_fail "go is not runnable" ;;
        *)                                  report_fail "go $gov != go$GO_VERSION (go.work 声明的值) —— 可能出现构建差异" ;;
    esac
    nodev="$(node -v 2>/dev/null || true)"
    if [ -n "$nodev" ]; then
        local nmaj="${nodev#v}"; nmaj="${nmaj%%.*}"
        if [ "$nmaj" -ge "$NODE_MAJOR_MIN" ] 2>/dev/null; then
            report_success "node $nodev (Vite 5 needs >= v$NODE_MAJOR_MIN)"
        else
            report_fail "node $nodev is older than v$NODE_MAJOR_MIN — the Web console will not build"
        fi
    fi

    # go-win7 只被 Gopher Agent 的 Win7 支持用到；缺了不影响主链。
    if [ -d /usr/lib/go-win7 ]; then
        report_success "go-win7 present at /usr/lib/go-win7 (Gopher Agent Win7 support)"
    else
        report_fail "go-win7 missing at /usr/lib/go-win7 — Gopher Agent's Win7 target will not build"
    fi

    echo
    if [ "$missing_hard" -gt 0 ]; then
        report_fail "$missing_hard required tool(s) missing — 构建会在对应步骤失败，先把上面标 [!!] 的补齐"
        return 1
    fi
    report_success "All required tools present"
    if [ "$missing_soft" -gt 0 ]; then
        echo "  （另有 $missing_soft 项「视用途」的缺失：不挡构建，只影响 Web 控制台 / 参数混淆 / EXE 伪签名）"
    fi
    return 0
}

server_part() {
    repo_index_update
    server_packets_install
    actual_go_version_install
    actual_node_version_install
    actual_pwsh_version_install
    final_verdict
}

# ★ 原写法是 `exec 2> "$ERROR_FILE"` —— 把所有 stderr **只**写进文件。
#   后果：wget/apt/tar 的报错现场一行都看不到，屏幕上只剩脚本自己那句
#   「An error occurred while …」，而那句话恰好**不含原因**（是超时？404？
#   证书？磁盘满？）。改成 tee：文件照旧留档，同时打到 stderr。
exec 2> >(tee -a "$ERROR_FILE" >&2)

if [[ $EUID != 0 ]]; then
    report_fail "This script must be executed as root"
    exit 1
fi

if [[ $# -gt 1 ]]; then
    echo "Usage: pre_install_linux_all.sh [server]"
    exit 1
else
    case "${1:-server}" in
        server|all)
            server_part
            exit $?
            ;;
        client)
            echo "Qt client is no longer part of this repository. Use the Web console instead."
            echo "Usage: pre_install_linux_all.sh [server]"
            exit 1
            ;;
        *)
            echo "Usage: pre_install_linux_all.sh [server]"
            exit 1
            ;;
    esac
fi

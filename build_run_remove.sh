#!/bin/bash
# =============================================================================
# build_run_remove.sh —— 源码「构建 / 运行 / 删除」生命周期脚本（二开）
# =============================================================================
#
# 与 build_deploy.sh 的分工（两者不是互相替代）：
#   build_deploy.sh   在**构建目录**里把源码编成 dist/，并做 [1/3][2/3][3/3] 自检。
#                     它的 --up 是「就地起服务」，**假定 dist 本身就是运行目录**。
#   本脚本            管 dist 的**落地**：换进运行目录 → 起 → 验 → 停 → 删。
#                     构建根与运行根**不是同一个目录**（现网：/root/axbuild 构建、
#                     /home/ad/dist 运行，实测两者 inode 不同），这一步以前全靠手工。
#
# 手工换目录踩过的两个坑（本脚本存在的理由）：
#   ① 跑着的 adaptixserver 把 extenders/*/*.so 都 mmap 了。**cp 原地覆盖会让映射页
#      失效** —— 实测把长跑实例打成 GC SIGSEGV（交付 README §0.4-1）。所以换目录
#      必须「先停服务」，且落位要用 install/rsync/mv 这类**换新 inode** 的方式，
#      绝不 `cp` 覆盖活进程正在用的文件。
#   ② 运行目录里有**运行期状态**：data/（操作员库 + Agents/Tasks/Consoles 三张表）、
#      server.rsa.crt|key、server.log / axrelay.log、*.pid，以及**现场改过的 profile**。
#      这些东西既不该被新 dist 覆盖，也不该在删除时无脑带走。本脚本按白名单保护，
#      且任何删除前先打一份可回滚的 tar 包。
#
# 用法：
#   ./build_run_remove.sh build    [--src DIR] [--no-web-build]   # 调 build_deploy.sh，不起服务
#   ./build_run_remove.sh install  [--root DIR] [--take-profiles] [--yes]
#   ./build_run_remove.sh run      [--root DIR]
#   ./build_run_remove.sh stop     [--root DIR]
#   ./build_run_remove.sh restart  [--root DIR]
#   ./build_run_remove.sh status   [--root DIR]
#   ./build_run_remove.sh remove   [--units-only|--dist|--purge] [--source] [--yes]
#
# 安全约定（三条都是刻意的，不是啰嗦）：
#   * 破坏性动作（install / remove）**默认只打印计划**，必须显式 --yes 才真做。
#   * remove 分档：默认 `--units-only` 只摘 systemd unit，**不碰 dist、不碰 data、
#     不碰 relay key**。要删数据必须自己升档。
#   * 目标目录不像 AdaptixC2 产物（缺 adaptixserver / extenders/ / profile.yaml）时
#     **拒绝删除** —— 防的是「变量拼错把别处删了」。
#   * relaynode.key **换 key 就换 peer id，已发出去的 agent 地址全废**（README 有记），
#     所以删它要额外的 `--i-know-relay-key-dies`，单独一道闸。
#
# 退出码：0 成功；1 参数/校验失败；2 运行态判定为「不健康」；3 需要 --yes 但没给。
# =============================================================================

set -u

GREEN=$(printf '\033[1;32m'); YELLOW=$(printf '\033[1;33m')
RED=$(printf '\033[1;31m');   NC=$(printf '\033[0m')

log()  { echo; echo "${YELLOW}==>${NC} $*"; }
ok()   { echo "  ${GREEN}[+]${NC} $*"; }
warn() { echo "  ${YELLOW}[!]${NC} $*"; }
err()  { echo "  ${RED}[x]${NC} $*" >&2; }
die()  { err "$*"; exit 1; }

# -----------------------------------------------------------------------------
# 默认值
# -----------------------------------------------------------------------------
SRC_ROOT="$(cd "$(dirname "$0")" 2>/dev/null && pwd -P)" || die "解析不出脚本所在目录"
BUILD_DIST="$SRC_ROOT/dist"
RUN_ROOT="/home/ad/dist"
SERVICE="adaptixserver.service"
RELAY_SERVICE="axrelay.service"
UNIT_DIR="/etc/systemd/system"
AXSTART="/root/axstart.sh"
RELAY_KEY="/etc/axrelay/relaynode.key"
ENVIRON_FILE="/root/axserv.environ"

CMD=""; YES=0; TAKE_PROFILES=0; JSON=0; WITH_SOURCE=0
REMOVE_TIER="units-only"
I_KNOW_KEY_DIES=0; I_KNOW_SOURCE_DIES=0
BUILD_ARGS=()

# 运行期状态白名单：换 dist 时**保留**、删除时**单独备份**。
# 判据是「进程跑起来才会写/现场才改的东西」，不是「看起来像配置文件」。
PROTECT_PATHS=(
    "data"                 # 操作员库 adaptixserver.db + wal/shm；三张业务表都在这
    "server.rsa.crt" "server.rsa.key"   # 自签双证书（删了下次起会重新生成 ⇒ 指纹变）
    "server.log" "axrelay.log"
    "adaptixserver.pid" "axrelay.pid"
)

usage() {
    sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

need_root() {
    [ "$(id -u)" -eq 0 ] || die "需要 root：/proc/<pid>/cwd 只对属主与 root 可读，非 root 会**静默漏掉**旧实例，起出第二个"
}

# 目标目录「像不像 AdaptixC2 产物」——删除前的最后一道门。
looks_like_dist() {
    local d="$1"
    [ -d "$d" ] || return 1
    [ -e "$d/adaptixserver" ] || return 1
    [ -d "$d/extenders" ] || return 1
    [ -e "$d/profile.yaml" ] || return 1
    return 0
}

# 从 profile 里抠端口 —— 就绪判据要靠它。yaml 结构简单，用 sed 够了，
# 不引 python（现场 python3 无 pip，但解释器在；不过没必要为一行 yaml 起进程）。
profile_port() {
    local f="$1" p
    [ -r "$f" ] || return 1
    p="$(sed -n 's/^[[:space:]]*port:[[:space:]]*\([0-9]\{1,5\}\).*/\1/p' "$f" | head -1)"
    [ -n "$p" ] || return 1
    printf '%s' "$p"
}

# profile 取哪个：运行目录里**现场那份**为准（不是构建 dist 里那份）。
pick_profile() {
    local d="$1" f
    for f in central.profile profile.yaml profile.initial.yaml; do
        [ -r "$d/$f" ] && { printf '%s' "$d/$f"; return 0; }
    done
    return 1
}

unit_exists() { [ -f "$UNIT_DIR/$SERVICE" ]; }
unit_active() { systemctl is-active --quiet "$SERVICE" 2>/dev/null; }

# 端口占用者（pid 列表）。枚举可能漏（comm 截断、namespace 差异），
# 所以它只用来**报错说清楚**，真正的门是「停完之后再查一次」。
port_holders() {
    local port="$1"
    if command -v ss >/dev/null 2>&1; then
        ss -ltnpH "sport = :$port" 2>/dev/null | grep -oE 'pid=[0-9]+' | cut -d= -f2 | sort -u | tr '\n' ' '
    elif command -v fuser >/dev/null 2>&1; then
        fuser -n tcp "$port" 2>/dev/null | tr -s ' ' ' '
    fi
}

# 停：systemd 优先，其次 pid 文件，最后按 cwd 匹配兜底。
# ★ 不能用 PPID=1 判「没人管」—— systemd 自己就是 PID1，托管与否 PPID 都是 1。
#   判据只认 cgroup（见 scripts/systemd/README.md）。
do_stop() {
    local quiet="${1:-0}"
    [ "$quiet" = "1" ] || log "停服务"
    local stopped=0
    if unit_exists && systemctl is-active --quiet "$SERVICE" 2>/dev/null; then
        systemctl stop "$SERVICE" && { stopped=1; [ "$quiet" = 1 ] || ok "systemctl stop $SERVICE"; }
    fi
    if unit_exists && systemctl is-active --quiet "$RELAY_SERVICE" 2>/dev/null; then
        systemctl stop "$RELAY_SERVICE" && { stopped=1; [ "$quiet" = 1 ] || ok "systemctl stop $RELAY_SERVICE"; }
    fi

    # 兜底：pid 文件（可能是陈旧的 —— 先核对 /proc/<pid> 的 cwd 再杀）
    local pf pid cw
    for pf in "$RUN_ROOT/adaptixserver.pid" "$RUN_ROOT/axrelay.pid"; do
        [ -r "$pf" ] || continue
        pid="$(cat "$pf" 2>/dev/null | tr -dc '0-9')"
        [ -n "$pid" ] || continue
        [ -d "/proc/$pid" ] || continue
        cw="$(readlink "/proc/$pid/cwd" 2>/dev/null || echo '')"
        if [ "$cw" = "$RUN_ROOT" ]; then
            kill "$pid" 2>/dev/null && { stopped=1; [ "$quiet" = 1 ] || ok "kill $pid（pid 文件，cwd=$cw）"; }
        else
            [ "$quiet" = 1 ] || warn "pid 文件里的 $pid 还活着，但 cwd=$cw 不是 $RUN_ROOT ⇒ 不杀（陈旧 pid 文件）"
        fi
    done
    [ "$stopped" = 1 ] || [ "$quiet" = 1 ] || ok "本来就没在跑"
}

# -----------------------------------------------------------------------------
# build：只调 build_deploy.sh（它自己会做三段自检），不在这里重复 make。
# 为什么让它干：构建链的坑（`.DEFAULT_GOAL`、`kharon-sync` 抹行、GOWORK、
# BOF 运行期编译输入…）都收在那一支里，复制一份必然漂移。
# -----------------------------------------------------------------------------
cmd_build() {
    local bd="$SRC_ROOT/build_deploy.sh"
    [ -x "$bd" ] || die "找不到可执行的 $bd（用 --src 指源码树根）"
    log "构建：$bd ${BUILD_ARGS[*]:-}"
    warn "不带 --up ⇒ 只构建+自检，**不起服务**（起服务用本脚本的 run/install）"
    ( cd "$SRC_ROOT" && ./build_deploy.sh "${BUILD_ARGS[@]:-}" )
    local rc=$?
    [ $rc -eq 0 ] || die "build_deploy.sh 退出码 $rc —— 去看它上面的 [3/3] 结论，别只看 .so 有没有出来"
    ok "构建完成，产物：$BUILD_DIST"
}

# -----------------------------------------------------------------------------
# install：把构建 dist 换进运行根。核心是「停 → 备 → 换 → 起 → 验」。
# -----------------------------------------------------------------------------
cmd_install() {
    [ -d "$BUILD_DIST" ] || die "构建产物不在：$BUILD_DIST（先跑 build）"
    [ -x "$BUILD_DIST/adaptixserver" ] || die "$BUILD_DIST/adaptixserver 不存在或不可执行"

    # ★ 校验一律排在**打印计划之前**：这样非 root 也能看到计划（看计划不需要 root），
    #   而真正需要 root 的动作等到 --yes 之后再过闸。
    if [ -d "$RUN_ROOT" ] && ! looks_like_dist "$RUN_ROOT"; then
        die "$RUN_ROOT 存在但不像 AdaptixC2 产物（缺 adaptixserver/extenders/profile.yaml）—— 拒绝覆盖"
    fi

    local ts; ts="$(date '+%Y%m%d-%H%M%S')"
    local stage="$RUN_ROOT.new-$ts"
    local backup_root; backup_root="$(dirname "$RUN_ROOT")/ax-backup-$ts"

    log "落地计划"
    echo "  构建产物   $BUILD_DIST"
    echo "  运行根     $RUN_ROOT"
    echo "  过渡目录   $stage        （换完即删）"
    echo "  备份       $backup_root  （含 data/、证书、现场 profile）"
    echo "  运行期状态 $(printf '%s ' "${PROTECT_PATHS[@]}")—— 保留，不被构建产物覆盖"

    # 现场 profile 与构建产物的 rate_limit 各是多少，摆出来给操作员看。
    # 默认**保留现场那份**：它对在跑的实例已经生效过，替换会静默改变现网行为。
    local cur_p new_p
    cur_p="$(pick_profile "$RUN_ROOT" 2>/dev/null || true)"
    if [ -n "$cur_p" ]; then
        local a b
        a="$(sed -n 's/^[[:space:]]*login_requests_per_minute:[[:space:]]*\([0-9-]*\).*/\1/p' "$cur_p" | head -1)"
        b="$(sed -n 's/^[[:space:]]*login_requests_per_minute:[[:space:]]*\([0-9-]*\).*/\1/p' "$BUILD_DIST/$(basename "$cur_p")" 2>/dev/null | head -1)"
        echo "  profile 限流  现场=${a:-未设}  构建产物=${b:-未设}  $([ "$TAKE_PROFILES" = 1 ] && echo '⇒ 采用构建产物(--take-profiles)' || echo '⇒ 保留现场 (加 --take-profiles 才换)')"
        [ "${a:-}" != "${b:-}" ] && warn "两者不同 —— 保留现场意味着新默认值**不会**随这次落地生效"
    fi

    if [ "$YES" != 1 ]; then
        echo
        warn "以上为计划。确认执行加 --yes"
        exit 3
    fi

    need_root
    log "1/5 停服务（在跑的进程 mmap 了 .so，必须先停 —— cp 覆盖会打成 SIGSEGV）"
    do_stop

    if [ -n "$cur_p" ]; then
        local port; port="$(profile_port "$cur_p" || true)"
        if [ -n "$port" ]; then
            local h; h="$(port_holders "$port")"
            [ -n "$h" ] && die "端口 $port 仍被占用（pid：$h）—— 拒绝继续，先查清是谁"
            ok "端口 $port 空闲"
        fi
    fi

    log "2/5 备份运行期状态 → $backup_root"
    mkdir -p "$backup_root"
    local p
    for p in "${PROTECT_PATHS[@]}"; do
        [ -e "$RUN_ROOT/$p" ] || continue
        cp -a "$RUN_ROOT/$p" "$backup_root/" && ok "备份 $p"
    done
    for p in central.profile profile.yaml profile.initial.yaml; do
        [ -e "$RUN_ROOT/$p" ] && cp -a "$RUN_ROOT/$p" "$backup_root/" && ok "备份 $p"
    done
    ( cd "$backup_root" && tar czf "../$(basename "$backup_root").tgz" . 2>/dev/null ) \
        && ok "打包 $(dirname "$RUN_ROOT")/$(basename "$backup_root").tgz（可整包回滚）" \
        || warn "打包失败（备份目录仍在，手工确认）"

    log "3/5 换：构建产物 → 过渡目录，再把运行期状态搬回去"
    rm -rf "$stage"
    cp -a "$BUILD_DIST" "$stage" || die "拷贝到过渡目录失败"
    for p in "${PROTECT_PATHS[@]}"; do
        if [ -e "$backup_root/$p" ]; then
            rm -rf "$stage/$p"
            cp -a "$backup_root/$p" "$stage/" && ok "回填 $p"
        fi
    done
    if [ "$TAKE_PROFILES" != 1 ]; then
        for p in central.profile profile.yaml profile.initial.yaml; do
            [ -e "$backup_root/$p" ] && { cp -a "$backup_root/$p" "$stage/"; ok "回填 $p（现场版）"; }
        done
    fi

    log "4/5 原子换位（两次 mv，都是新 inode；旧的留着可回滚）"
    local old="$RUN_ROOT.old-$ts"
    if [ -d "$RUN_ROOT" ]; then
        mv "$RUN_ROOT" "$old" || die "移走旧运行根失败"
        ok "旧运行根 → $old"
    fi
    mv "$stage" "$RUN_ROOT" || { [ -d "$old" ] && mv "$old" "$RUN_ROOT"; die "落位失败，已回滚"; }
    ok "新运行根就位：$RUN_ROOT"

    log "5/5 起服务 + 验活"
    if unit_exists; then
        do_run || { err "起服务失败；旧目录在 $old"; exit 2; }
    else
        warn "没有 systemd unit ⇒ 只完成落地，未启动。用本脚本 run 子命令安装并启动"
    fi

    echo
    ok "install 完成 @ $(date '+%F %T')"
    echo "  回滚：停服务后 mv $old $RUN_ROOT（旧目录先别删）"
    echo "  备份：$backup_root"
}

# -----------------------------------------------------------------------------
# run：装 unit（若缺）+ enable --now + 验活。
# -----------------------------------------------------------------------------
do_run() {
    local profile port rc
    profile="$(pick_profile "$RUN_ROOT")" || { warn "$RUN_ROOT 下没有 profile，无法判端口"; return 1; }
    port="$(profile_port "$profile")" || { warn "解析不出 Teamserver.port（profile 格式变了？）"; return 1; }

    # 起之前的安全门：端口必须没人占。进程死则端口必然释放，所以这里不是「等端口」，
    # 而是「查一次当门」—— 把「端口被别人占着」这个真故障直接说出来。
    local h; h="$(port_holders "$port")"
    if [ -n "$h" ]; then
        err "端口 $port 已被占用（pid：$h）—— 拒绝启动"
        local p cw
        for p in $h; do
            cw="$(readlink "/proc/$p/cwd" 2>/dev/null || echo '<不可读>')"
            err "  pid=$p comm=$(cat "/proc/$p/comm" 2>/dev/null || echo '?') cwd=$cw"
        done
        err "硬起会得到「进程活着、日志还报 ready、就是不听」的实例（connector.go:653-658）"
        return 1
    fi
    ok "端口 $port 空闲"

    if ! unit_exists; then
        log "安装 systemd unit"
        local sd="$SRC_ROOT/scripts/systemd"
        [ -d "$sd" ] || { warn "没有 $sd ⇒ 跳过 unit 安装，请手工起"; return 1; }
        # ★ 用 install 而不是 cp：install 内部是「写临时文件 + rename」，一次原子落位，
        #   天然是新 inode。运行中的程序与它 mmap 的 .so 绝不能被 cp 原地覆盖。
        install -m 600 "$sd/$SERVICE"      "$UNIT_DIR/$SERVICE"      || return 1
        install -m 600 "$sd/$RELAY_SERVICE" "$UNIT_DIR/$RELAY_SERVICE" 2>/dev/null || warn "没有 $RELAY_SERVICE（可选）"
        # ★ axstart.sh 必须带 exec 位：丢了 systemd 报 203/EXEC（原子写脚本最易丢）
        install -m 700 "$sd/axstart.sh"    "$AXSTART"                || return 1
        ok "unit + axstart.sh 已落位（$AXSTART mode 700）"
        systemctl daemon-reload || return 1
    fi

    # 换环境必须改的东西，起之前先摊开说，免得「起来了但 profile 不是这份」。
    if [ ! -r "$ENVIRON_FILE" ]; then
        warn "$ENVIRON_FILE 不在 —— 不阻止启动，但 linux_agent stager 表单的回退缺省值会变"
    fi
    if [ ! -r "$RELAY_KEY" ]; then
        warn "$RELAY_KEY 不在 —— axrelay 起不来（-key 是绝对路径，且换 key 就换 peer id）"
    fi
    if ! command -v pwsh >/dev/null 2>&1; then
        warn "PATH 没有 pwsh —— argfuscator 会在插件状态里报 pwsh_unavailable（参数混淆页不可用）"
    fi
    for t in openssl osslsigncode; do
        command -v "$t" >/dev/null 2>&1 || warn "PATH 没有 $t —— Windows EXE/Service EXE 载荷**生成会直接失败**（其余格式不受影响）"
    done

    systemctl enable --now "$SERVICE" || return 1

    # 验活。★ 别拿日志判就绪：'The AdaptixC2 server is ready' 是**无条件**打印的，
    #   端口被占时进程也不退出。只认「端口在听」+「HTTPS 答得动」。
    local i secs=0
    for i in $(seq 1 30); do
        sleep 1; secs=$((secs+1))
        if ss -ltn 2>/dev/null | grep -q ":$port "; then break; fi
    done
    if ! ss -ltn 2>/dev/null | grep -q ":$port "; then
        err "等了 ${secs}s 端口 $port 仍未监听"
        systemctl is-active --quiet "$SERVICE" || err "unit 已不在 active —— journalctl -u $SERVICE 看 exec 失败原因"
        return 1
    fi
    ok "端口 $port 已监听（${secs}s）"

    local code
    code="$(curl -k -s --noproxy '*' -o /dev/null -m 5 -w '%{http_code}' "https://127.0.0.1:$port/__probe_not_registered__" 2>/dev/null)"
    case "$code" in
        000|"") err "HTTPS 无应答（curl code=$code）"; return 1 ;;
        *) ok "HTTPS 已就绪（探未注册路径得 HTTP $code，走 404 兜底；不探 login 以免烧限流配额）" ;;
    esac
    return 0
}

cmd_run() {
    need_root
    [ -d "$RUN_ROOT" ] || die "运行根不存在：$RUN_ROOT"
    looks_like_dist "$RUN_ROOT" || die "$RUN_ROOT 不像 AdaptixC2 产物 —— 拒绝启动"
    log "启动 $SERVICE（$RUN_ROOT）"
    do_run || die "启动/验活失败"
    echo; ok "运行中 @ $(date '+%F %T')"
    echo "  日志 $RUN_ROOT/server.log（unit 里 StandardOutput=append，**不进 journald**）"
    echo "  停止 ./build_run_remove.sh stop --root $RUN_ROOT"
}

cmd_stop() {
    need_root
    log "停止"
    do_stop
    echo; ok "已停"
}

# -----------------------------------------------------------------------------
# status：只用**不会骗人**的判据。
# -----------------------------------------------------------------------------
cmd_status() {
    local profile port
    echo "运行根   $RUN_ROOT"
    if [ ! -d "$RUN_ROOT" ]; then warn "不存在"; return 2; fi
    looks_like_dist "$RUN_ROOT" && ok "像 AdaptixC2 产物" || warn "不像 AdaptixC2 产物"

    profile="$(pick_profile "$RUN_ROOT" 2>/dev/null || true)"
    [ -n "$profile" ] && { echo "profile  $profile"; } || warn "没有 profile"

    if [ -n "$profile" ]; then
        local rl; rl="$(sed -n 's/^[[:space:]]*login_requests_per_minute:[[:space:]]*\([0-9-]*\).*/\1/p' "$profile" | head -1)"
        echo "限流     login_requests_per_minute=${rl:-未设} $([ "${rl:-}" = "0" ] && echo '（0 = 关闭）')"
    fi

    if unit_exists; then
        echo "unit     $(systemctl is-active "$SERVICE" 2>/dev/null) / $(systemctl is-enabled "$SERVICE" 2>/dev/null)"
        # ★ NRestarts 不因手工 stop/start 归零；ActiveEnterTimestamp 停止后仍是旧值
        systemctl show "$SERVICE" -p NRestarts -p ExecMainStartTimestamp -p MainPID 2>/dev/null | sed 's/^/         /'
    else
        warn "没有装 systemd unit（$UNIT_DIR/$SERVICE）"
    fi

    # cgroup 才是「谁在管它」的判据（PPID=1 恒真，不能用来判「没人管」）
    local pid; pid="$(systemctl show "$SERVICE" -p MainPID --value 2>/dev/null)"
    if [ -n "${pid:-}" ] && [ "$pid" != "0" ] && [ -d "/proc/$pid" ]; then
        echo "cgroup   $(cat "/proc/$pid/cgroup" 2>/dev/null)"
        echo "cwd      $(readlink "/proc/$pid/cwd" 2>/dev/null)"
    fi

    if [ -n "$profile" ]; then
        port="$(profile_port "$profile" || true)"
        if [ -n "$port" ]; then
            # ★ 唯一可信的就绪判据
            if ss -ltn 2>/dev/null | grep -q ":$port "; then ok "在监听 $port（ss）"; else err "没有在监听 $port —— 即使日志说 ready 也不算就绪"; fi
            local h; h="$(port_holders "$port")"
            [ -n "$h" ] && echo "占用 pid $h"
        fi
    fi

    if [ -r "$RUN_ROOT/server.log" ]; then
        echo "--- server.log 末 5 行:"
        tail -5 "$RUN_ROOT/server.log" | sed 's/^/    /'
    fi
    return 0
}

# -----------------------------------------------------------------------------
# remove：分档删除。默认最轻。
# -----------------------------------------------------------------------------
cmd_remove() {
    # ★★ 校验必须排在**任何破坏性动作之前**。下面「摘 unit」会改系统，
    #    而 dist/purge 档的「像不像产物」检查原先写在摘完 unit 之后 ——
    #    后果是：指向一个错目录时，脚本会先把 systemd unit 摘掉，再报「拒绝删除」。
    #    那是「拒绝了删除，却已经改坏了系统」。所以两张门都提到最前面。
    if [ "$REMOVE_TIER" != "units-only" ] && [ -d "$RUN_ROOT" ]; then
        looks_like_dist "$RUN_ROOT" || die "$RUN_ROOT 不像 AdaptixC2 产物（缺 adaptixserver/extenders/profile.yaml）—— **拒绝执行**（防变量拼错删了别处）"
    fi
    if [ "$WITH_SOURCE" = 1 ]; then
        { [ -f "$SRC_ROOT/Makefile" ] && [ -d "$SRC_ROOT/AdaptixServer" ]; } \
            || die "$SRC_ROOT 不像 AdaptixC2 源码树（缺 Makefile/AdaptixServer）—— **拒绝执行**"
    fi

    log "删除计划（档位：$REMOVE_TIER）"
    local steps=()
    steps+=("停服务 + disable（$SERVICE / $RELAY_SERVICE）")
    steps+=("删 unit 文件 $UNIT_DIR/{$SERVICE,$RELAY_SERVICE} + daemon-reload")
    steps+=("保留 $AXSTART（axstart.sh）—— 它不含密，重装要用")
    case "$REMOVE_TIER" in
        dist)
            steps+=("备份 data/ + 证书 + profile → $(dirname "$RUN_ROOT")/ax-backup-$(date '+%Y%m%d-%H%M%S')/")
            steps+=("删运行根 $RUN_ROOT（含 dist、extenders、web、Extension-Kit）")
            ;;
        purge)
            steps+=("备份 data/ + 证书 + profile（同上，先备份再删）")
            steps+=("删运行根 $RUN_ROOT")
            steps+=("删 $AXSTART 与 $ENVIRON_FILE（后者含口令，本就是 /proc 快照）")
            if [ "$I_KNOW_KEY_DIES" = 1 ]; then
                steps+=("★删 relay key $RELAY_KEY —— 换 key 即换 peer id，已发出去的 agent 地址全废")
            else
                steps+=("保留 relay key $RELAY_KEY（要删需 --i-know-relay-key-dies）")
            fi
            ;;
    esac
    [ "$WITH_SOURCE" = 1 ] && steps+=("★删源码树 $SRC_ROOT —— 构建输入全没")
    local s; for s in "${steps[@]}"; do echo "  - $s"; done

    if [ "$YES" != 1 ]; then
        echo; warn "以上为计划。确认执行加 --yes"; exit 3
    fi
    need_root

    do_stop

    log "摘 unit"
    local u
    for u in "$SERVICE" "$RELAY_SERVICE"; do
        systemctl disable "$u" 2>/dev/null && ok "disable $u"
        if [ -f "$UNIT_DIR/$u" ]; then rm -f "$UNIT_DIR/$u" && ok "rm $UNIT_DIR/$u"; fi
    done
    systemctl daemon-reload 2>/dev/null && ok "daemon-reload"
    systemctl reset-failed 2>/dev/null || true

    case "$REMOVE_TIER" in
        dist|purge)
            if [ -d "$RUN_ROOT" ]; then
                # 「像不像产物」已在函数开头校验过（那里才是安全的位置）
                local ts bk; ts="$(date '+%Y%m%d-%H%M%S')"
                bk="$(dirname "$RUN_ROOT")/ax-backup-$ts"
                log "备份运行期状态 → $bk"
                mkdir -p "$bk"
                local p
                for p in "${PROTECT_PATHS[@]}" central.profile profile.yaml profile.initial.yaml; do
                    [ -e "$RUN_ROOT/$p" ] && cp -a "$RUN_ROOT/$p" "$bk/" && ok "备份 $p"
                done
                ( cd "$bk" && tar czf "../$(basename "$bk").tgz" . 2>/dev/null ) && ok "打包 $(dirname "$RUN_ROOT")/$(basename "$bk").tgz"
                log "删运行根 $RUN_ROOT"
                rm -rf "$RUN_ROOT" && ok "已删（备份在 $bk）"
            else
                warn "运行根不存在，跳过"
            fi
            ;;
    esac

    if [ "$REMOVE_TIER" = "purge" ]; then
        [ -f "$AXSTART" ] && rm -f "$AXSTART" && ok "rm $AXSTART"
        if [ -f "$ENVIRON_FILE" ]; then
            # 只报键名与条数，绝不打印值 —— 它是 /proc/<pid>/environ 的原样快照，含口令
            local n; n="$(tr '\0' '\n' < "$ENVIRON_FILE" | grep -c '=' || true)"
            warn "$ENVIRON_FILE 含口令（$n 项）；要删自己确认后 rm，本脚本不动它"
        fi
        if [ -f "$RELAY_KEY" ]; then
            if [ "$I_KNOW_KEY_DIES" = 1 ]; then
                rm -f "$RELAY_KEY" && ok "已删 relay key —— 记得新 key 会生成新 peer id"
            else
                warn "保留 relay key（要删加 --i-know-relay-key-dies）"
            fi
        fi
    fi

    if [ "$WITH_SOURCE" = 1 ]; then
        # 「像不像源码树」已在函数开头校验过（那里才是安全的位置）
        rm -rf "$SRC_ROOT" && ok "已删源码树 $SRC_ROOT"
    fi

    echo; ok "remove 完成 @ $(date '+%F %T')"
}

# -----------------------------------------------------------------------------
# 参数解析
# -----------------------------------------------------------------------------
[ $# -gt 0 ] || usage 1
CMD="$1"; shift

while [ $# -gt 0 ]; do
    case "$1" in
        --src)   SRC_ROOT="${2:?}"; BUILD_DIST="$SRC_ROOT/dist"; shift 2 ;;
        --root)  RUN_ROOT="${2:?}"; shift 2 ;;
        --units-only) REMOVE_TIER="units-only"; shift ;;
        --dist)  REMOVE_TIER="dist"; shift ;;
        --purge) REMOVE_TIER="purge"; shift ;;
        --source) WITH_SOURCE=1; shift ;;
        --take-profiles) TAKE_PROFILES=1; shift ;;
        --i-know-relay-key-dies) I_KNOW_KEY_DIES=1; shift ;;
        --i-know-source-dies)    I_KNOW_SOURCE_DIES=1; shift ;;
        --yes|-y) YES=1; shift ;;
        --no-web-build|--bof-compile|--debug) BUILD_ARGS+=("$1"); shift ;;
        -h|--help) usage 0 ;;
        *) die "未知参数：$1（-h 看用法）" ;;
    esac
done

[ "$WITH_SOURCE" = 1 ] && [ "$I_KNOW_SOURCE_DIES" != 1 ] && die "--source 会删掉整棵源码树，需同时给 --i-know-source-dies"

case "$CMD" in
    build)   cmd_build ;;
    # ★ 这里**不要**预先 need_root：install 的「打印计划」不需要 root，
    #   抢在前面要 root 会让非 root 连计划都看不到（各子命令自己在该过闸的位置过）。
    install) cmd_install ;;
    run)     cmd_run ;;
    stop)    cmd_stop ;;
    restart) do_stop; cmd_run ;;
    status)  cmd_status ;;
    remove)  cmd_remove ;;
    -h|--help) usage 0 ;;
    *) usage 1 ;;
esac

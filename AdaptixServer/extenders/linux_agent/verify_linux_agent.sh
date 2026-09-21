#!/usr/bin/env bash
# verify_linux_agent.sh — one-shot verification for the linux_agent
# competition patch (H2 timing / H4 chacha / H5 noise / BOF suite).
#
# Run ON THE LINUX SERVER, from the linux_agent extender directory:
#   cd ~/cc/AdaptixServer/extenders/linux_agent && bash tools/verify_linux_agent.sh
#
# Checks:
#   1. Compile every agent C source (x86_64; aarch64 if toolchain present)
#   2. Compile the stager template (production flags)
#   3. Compile every BOF (bofs/*.c)
#   4. go build both plugins (linux_agent + gopher_listener_tcp)
#   5. go test: chacha + gopher session roundtrips
#   6. Cross-language ChaCha20: Go plugin code vs C agent code, 3 random vectors

set -u
set -o pipefail   # go test 等经管道时保留失败退出码，防假 PASS
HERE="$(cd "$(dirname "$0")" && pwd)"
EXT="$(dirname "$HERE")"          # linux_agent extender dir
AGENT="$EXT/src_agent/agent"
BOFS="$EXT/src_agent/bofs"
TPL="$EXT/templates/stager_linux.c"
MBTLS="$EXT/src_agent/mbedtls"
LISTENER="$EXT/../gopher_listener_tcp"
WORK="$(mktemp -d /tmp/verify_linux_agent.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

PASS=0; FAIL=0
ok()   { echo "  [PASS] $1"; PASS=$((PASS+1)); }
bad()  { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }

export GOPROXY="${GOPROXY:-https://goproxy.cn,direct}"
CFLAGS="-std=gnu11 -Os -fno-stack-protector -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-cast-function-type -I $WORK -I $AGENT -I $MBTLS/include -I $MBTLS -DMBEDTLS_CONFIG_FILE=<mbedtls_config_agent.h> -DDJB2_SEED=1U"

echo "== [1] toolchain =="
for t in musl-gcc gcc go; do
    if command -v "$t" >/dev/null 2>&1; then ok "found $t"; else bad "missing $t"; fi
done
command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 \
    && { ok "found aarch64-linux-gnu-gcc"; HAVE_ARM=1; } \
    || { echo "  [SKIP] aarch64-linux-gnu-gcc not present"; HAVE_ARM=0; }

echo "== [2] agent C sources (x86_64) =="
printf '#define PROFILE_COUNT 0\n' > "$WORK/config.h"
# strings_obf.h is generated per-payload at build time — emit a compile stub
# covering every OBF_* name referenced anywhere in the agent sources.
{
    echo "// verify stub — real header is generated per-payload by generateObfStrings()"
    echo "#ifndef STRINGS_OBF_H"
    echo "#define STRINGS_OBF_H"
    echo "#include <stdint.h>"
    echo "static const uint8_t _build_nonce[] = {0};"
    echo "static const uint8_t _xor_key[] = {0x00};"
    echo "static const int _xor_key_len = 1;"
    echo "#define DEOBF(name, dst) do { (dst)[0] = 0; } while (0)"
    echo "#define ZERO_STR(buf, len) do { volatile char *_p = (volatile char *)(buf); for (unsigned int _i = 0; _i < (unsigned int)(len); _i++) _p[_i] = 0; } while (0)"
    grep -rhoE "OBF_[A-Z0-9_]+" "$AGENT"/*.c 2>/dev/null | sort -u | while read -r n; do
        echo "static const uint8_t ${n}_enc[] = {0};"
    done
    echo "#endif"
} > "$WORK/strings_obf.h"
mkdir -p "$WORK/obj"
for src in "$AGENT"/*.c; do
    base="$(basename "$src" .c)"
    if musl-gcc $CFLAGS -DARCH_X86_64 -c "$src" -o "$WORK/obj/$base.o" 2>"$WORK/$base.err"; then
        ok "compile $base.c"
    else
        bad "compile $base.c"; sed 's/^/      /' "$WORK/$base.err" | head -8
    fi
done

if [ "$HAVE_ARM" = "1" ]; then
    echo "== [2b] agent C sources (aarch64) =="
    for src in "$AGENT"/*.c; do
        base="$(basename "$src" .c)"
        if aarch64-linux-gnu-gcc $CFLAGS -DARCH_AARCH64 -c "$src" -o "$WORK/obj/${base}_arm.o" 2>"$WORK/${base}_arm.err"; then
            ok "compile $base.c (arm64)"
        else
            bad "compile $base.c (arm64)"; sed 's/^/      /' "$WORK/${base}_arm.err" | head -8
        fi
    done
fi

echo "== [3] stager template (production compile) =="
sed -e 's/@@STAGER_HOST@@/127.0.0.1/' -e 's/@@STAGER_PORT@@/443/' \
    -e 's/@@STAGER_PATH@@/\/s\/deadbeef/' -e 's/@@STAGER_SSL@@/1/' \
    -e 's/@@STAGER_UA@@/Mozilla\/5.0 test/' \
    -e 's/@@STAGER_KEY@@/0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f/' \
    "$TPL" > "$WORK/stager_gen.c"
if gcc -O2 -s -o "$WORK/stager_test" "$WORK/stager_gen.c" -lssl -lcrypto 2>"$WORK/stager.err"; then
    ok "stager template compiles"
else
    bad "stager template"; sed 's/^/      /' "$WORK/stager.err" | head -8
fi

echo "== [4] BOFs =="
for src in "$BOFS"/*.c; do
    base="$(basename "$src" .c)"
    if gcc -c -o "$WORK/$base.o" "$src" -include bof_api.h -Os -fPIC -fno-stack-protector -I "$AGENT" 2>"$WORK/$base.err"; then
        ok "compile BOF $base.c"
    else
        bad "compile BOF $base.c"; sed 's/^/      /' "$WORK/$base.err" | head -8
    fi
done

echo "== [5] go build plugins =="
( cd "$EXT" && GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o "$WORK/agent_linux.so" . ) 2>"$WORK/plugin.err" \
    && ok "go build linux_agent" || { bad "go build linux_agent"; sed 's/^/      /' "$WORK/plugin.err" | head -10; }
( cd "$LISTENER" && GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o "$WORK/listener_gopher_tcp.so" . ) 2>"$WORK/listener.err" \
    && ok "go build gopher_listener_tcp" || { bad "go build gopher_listener_tcp"; sed 's/^/      /' "$WORK/listener.err" | head -10; }

echo "== [6] go test (crypto roundtrips) =="
( cd "$EXT" && go test -run 'TestChaCha20XorSymmetric|TestGopherSessionRoundtrip' -v . ) 2>&1 | tail -6 \
    && ok "go test crypto roundtrips" || bad "go test crypto roundtrips"

echo "== [7] cross-language ChaCha20 (Go plugin vs C agent) =="
if gcc -O2 -o "$WORK/test_chacha_c" "$HERE/test_chacha.c" 2>"$WORK/tc.err"; then ok "build test_chacha.c"; else bad "build test_chacha.c"; fi
CROSS_OK=1
for i in 1 2 3; do
    KEY=$(head -c16 /dev/urandom | od -An -tx1 | tr -d ' \n')
    NONCE=$(head -c12 /dev/urandom | od -An -tx1 | tr -d ' \n')
    PLAIN=$(head -c256 /dev/urandom | od -An -tx1 | tr -d ' \n')
    GO_OUT=$(cd "$HERE" && go run test_chacha.go "$KEY" "$NONCE" "$PLAIN" 2>/dev/null)
    C_OUT=$("$WORK/test_chacha_c" "$KEY" "$NONCE" "$PLAIN")
    CHACHA_VECTOR_KEY="$KEY" CHACHA_VECTOR_NONCE="$NONCE" CHACHA_VECTOR_PLAIN="$PLAIN" \
        CHACHA_VECTOR_OUT="$WORK/plugin_out.txt" \
        sh -c 'cd "$1" && go test -run TestDumpChaChaVector -count=1 . >/dev/null 2>&1' _ "$EXT" || true
    PLUGIN_OUT=$(cat "$WORK/plugin_out.txt" 2>/dev/null || echo MISSING)
    if [ -n "$GO_OUT" ] && [ "$GO_OUT" = "$C_OUT" ] && [ "$PLUGIN_OUT" = "$C_OUT" ]; then
        ok "vector $i: go==c==plugin ($(echo "$GO_OUT" | wc -c) hex chars)"
    else
        bad "vector $i mismatch: go=${GO_OUT:0:24}... c=${C_OUT:0:24}... plugin=${PLUGIN_OUT:0:24}..."
        CROSS_OK=0
    fi
done

echo "== [8] link smoke test (full static ELF with stub config) =="
mkdir -p "$WORK/link"
for src in "$AGENT"/*.c; do
    base="$(basename "$src" .c)"
    musl-gcc $CFLAGS -DARCH_X86_64 -c "$src" -o "$WORK/link/$base.o" 2>/dev/null
done
# embedded mbedtls (H1) — compile + include in the link
for src in "$MBTLS/library"/*.c "$MBTLS/mbedtls_glue.c"; do
    base="$(basename "$src" .c)"
    musl-gcc $CFLAGS -DARCH_X86_64 -c "$src" -o "$WORK/link/$base.o" 2>/dev/null
done
if musl-gcc -static -nostdlib -nodefaultlibs -s -Wl,--build-id=none -lgcc -o "$WORK/link/agent_test" "$WORK"/link/*.o 2>"$WORK/link.err"; then
    ok "full agent links (static ELF)"
else
    bad "full agent link"; sed 's/^/      /' "$WORK/link.err" | head -10
fi

echo ""
echo "========================================"
echo " PASS=$PASS  FAIL=$FAIL"
[ "$FAIL" -eq 0 ] && echo " ALL CHECKS PASSED" || echo " FIX THE FAILURES ABOVE"
exit "$FAIL"

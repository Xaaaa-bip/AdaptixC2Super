#!/usr/bin/env bash
# gate3_matrix.sh — Phase 3 Gate: check BOF → resolver → autopush → 执行 全自动链实测。
# 前置: 插件已含 matrix_resolver autopush（make 后部署重启）。
# 步骤: 1) 用服务器真实 kver 构建良性载荷（打印 MATRIX_PAYLOAD_OK + 写标记文件）
#       2) 写入 ~/cc/run/matrix/ 矩阵布局 + manifest
#       3) e2e.py matrix 端到端断言（MATCH → sha256 → upload → execute → 输出标记）
set -euo pipefail
cd "$(dirname "$0")/.."
MATRIX=/root/cc/run/matrix
KVER=$(uname -r)
ARCH=x86_64
PAYLOAD=exploits/$KVER/axm_test

echo "== [1] build benign payload for kver=$KVER =="
mkdir -p "$MATRIX/$PAYLOAD.dir" 2>/dev/null || true
cat > /tmp/axm_payload.c <<'EOF'
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
int main(void) {
    int fd = open("/tmp/axm_marker", O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (fd >= 0) { const char m[] = "AXM_CHAIN_OK"; (void)!write(fd, m, sizeof(m)-1); close(fd); }
    puts("MATRIX_PAYLOAD_OK");
    return 0;
}
EOF
mkdir -p "$MATRIX/$(dirname "$PAYLOAD")"
gcc -static -Os -s -o "$MATRIX/$PAYLOAD" /tmp/axm_payload.c
SHA=$(sha256sum "$MATRIX/$PAYLOAD" | cut -d' ' -f1)
echo "payload: $MATRIX/$PAYLOAD sha256=$SHA"

echo "== [2] write manifest =="
python3 - "$MATRIX/matrix_manifest.json" "$PAYLOAD" "$SHA" "$KVER" "$ARCH" <<'EOF'
import json, sys
path, sha, kver, arch = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
try:
    m = json.load(open(sys.argv[1]))
except Exception:
    m = {}
m[path] = {"sha256": sha, "kver": kver, "arch": arch, "kind": "exploit", "feature": ""}
json.dump(m, open(sys.argv[1], "w"), indent=1)
print("manifest entries:", len(m))
EOF

echo "== [3] e2e matrix chain =="
python3 tools/e2e.py matrix --listener TestHTTP --timeout 150

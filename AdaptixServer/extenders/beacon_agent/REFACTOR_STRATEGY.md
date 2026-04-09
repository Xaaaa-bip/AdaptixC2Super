# AdaptixC2 Beacon 重构与免杀增强策略 (No-CRT & OpSec)

要将 AdaptixC2 的官方 C++ Beacon 重构为**不依赖 CRT（C Run-Time Library）**，并大幅增强其**免杀与隐蔽性（OpSec）**，同时**保持原有通讯协议不变**，这是一个非常经典且极具实战价值的工程。

当前 Adaptix 的 Beacon 虽然具备了基础的 API Hashing（`ApiLoader.cpp`）和可选的 IAT Hiding，但整体依然偏向“功能实现”，在高级对抗上还有很大提升空间。

---

## 〇、本仓库实现状态（与 `src_beacon` 代码同步）

| 策略章节 | 状态 | 说明 |
|---------|------|------|
| §一 No-CRT | **部分** | 已用 `-fno-exceptions`、`-fno-rtti`、无异步展开表、`crt.cpp`/`std.cpp` 桩、`pl_main` 可选 **无 CRT + IAT 隐藏** 链路；**未**做到全文所述的「全局 `-nostdlib` 且完全弃用 `new/delete`」等极端形态（见 `STAGE2_PLAN.md`）。 |
| §二.1 睡眠混淆 | **已做** | `OpsecSleep.cpp`：`CreateTimerQueueTimer` + `advapi32!SystemFunction032`（RC4）+ `WaitForSingleObject` 同步；`NtDelayExecution` 走 **`Indirect_NtDelayExecution`**（x64 间接 syscall）；失败回退 XOR + 直接 `ApiNt`。 |
| §二.2 间接 syscall | **已做** | `IndirectSyscall.cpp` + `beacon/asm/indirect_syscall_x64.asm`：Halo/Hell 式 SSN 解析 + `Opsec_FindNtdllSyscallGadget`；已覆盖 **`NtDelayExecution`、`NtProtectVirtualMemory`、`NtAllocateVirtualMemory`、`NtCreateThreadEx`**（x64）；x86 回退 `ApiNt` 导出。**BOF 加载**（`bof_loader.cpp` / `Boffer.cpp` 内 COFF 节区与 `mapFunctions`）**不**走 `Indirect_VirtualAllocSensitive`，保持 **`ApiWin->VirtualAlloc`**。 |
| §二.3 线程 / 栈 | **部分** | **线程**：x64 优先 **`Indirect_NtCreateThreadEx`**（挂起 + `ntdll!RtlUserThreadStart`）→ `GetThreadContext`/`SetThreadContext`/`ResumeThread`，失败再 `CreateThread` 同策略，最后直建线程；**x86** 仍为直建线程。**栈**：`Opsec_PreBlockingWinHttp` 在 `WinHttpSendRequest` 前 `SwitchToThread`（轻量过渡）；**未**做完整 ROP 伪造 `rpcrt4`/`kernel32` 栈。 |
| §三 协议不变 | **已做** | `Packer.cpp` / `Crypt.cpp` / `BuildBeat` 序列与加密逻辑未改；敏感能力走 `IndirectSyscall` / `ApiLoader` 扩展。 |
| §三.3 ApiLoader / PEB | **已做** | `ProcLoader::GetModuleAddress`：PEB（`GS:[0x60]` / `FS:[0x30]`）→ `InMemoryOrderModuleList` + 导出哈希；敏感路径用间接 syscall 包装（见上）。 |
| §四 路线图阶段 1 | **已做** | 见 `STAGE1_STATUS.md`（Connector/Agent 等 C 风格化等）。 |
| §四 路线图阶段 2–4 | **部分** | 无 CRT/IAT 隐藏、链接脚本等见 `STAGE2_PLAN.md`；阶段 4 中睡眠/间接 syscall 已在 §二落地，**完整 PIC/段合并等按构建格式仍有差异**。 |

---

以下是具体的**增强方向**与**重构实施路径**（原文档，保留作设计参考）：

---

## 一、 彻底剥离 CRT 依赖 (No-CRT C++)

C++ 剥离 CRT 比 C 语言难得多，因为 C++ 的面向对象特性（`new/delete`、虚函数、全局对象构造等）默认强依赖 CRT。

**1. 编译与链接参数调整**
*   **必须全局启用**：`-nostdlib -nodefaultlibs -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables`。
*   **入口点重写**：不能再使用 `main`，必须自定义入口点（如 `void WINAPI RawEntryPoint()`），并在链接时指定 `-Wl,-e,RawEntryPoint`。

**2. 补齐 C++ 运行时存根 (Stub)**
当前项目里有 `crt.cpp` 和 `std.cpp`，你需要确保它们覆盖了所有隐式调用：
*   **内存分配**：重载全局的 `void* operator new(size_t size)` 和 `delete`，底层直接调用 `HeapAlloc(GetProcessHeap(), ...)` 或 `NtAllocateVirtualMemory`。
*   **内存操作内置函数**：编译器会隐式插入 `memcpy`, `memset`, `memcmp`。必须用汇编或纯 C 手写这些函数，并加上 `extern "C"` 导出。
*   **纯虚函数支持**：由于 `Connector` 类使用了虚函数（`virtual void Exchange(...) = 0`），剥离 CRT 后如果发生纯虚函数调用崩溃，程序会寻找 `__cxa_pure_virtual` 或 `_purecall`。你需要定义一个空的存根函数防止链接报错。
*   **全局变量初始化**：C++ 的全局对象（如 `g_Agent`）在进入 `main` 之前需要调用构造函数（遍历 `.CRT$XCA` 到 `.CRT$XCZ` 段）。如果不依赖 CRT，最好**放弃全局对象的静态初始化**，改为在入口点手动 `g_Agent = new Agent();`。

---

## 二、 核心免杀能力增强 (OpSec Enhancements)

这是当前 Beacon 最欠缺的地方，也是 Kharon 等定制 Agent 的强项。

**1. 睡眠混淆 (Sleep Obfuscation) - 替代 `WaitMask.cpp`** — **【已做】**（见 §〇）
*   **现状**：当前使用 `WaitForSingleObject` 或 `Sleep`，休眠期间 Beacon 的内存是明文且属性为 `RWX` 或 `RX`，极易被 YARA 扫描（如 BeaconEye、Hunt-Sleeping-Beacons）。
*   **增强**：引入 **Ekko**、**Foliage** 或 **DeathSleep** 机制。
    *   在 `Connector::Sleep` 中，利用 `CreateTimerQueueTimer` 或 ROP 链，排队执行一系列 API。
    *   休眠前：将 Beacon 所在的内存段属性改为 `RW`，并使用 SystemFunction032 (RC4) 加密自身内存。
    *   休眠中：调用 `NtDelayExecution`。
    *   唤醒后：解密内存，恢复 `RX` 或 `RWX` 属性继续执行。

**2. 间接系统调用 (Indirect Syscalls)** — **【已做】**（见 §〇；线程见 §二.3）
*   **现状**：当前大量使用 `ApiWin->VirtualAlloc`、`ApiWin->CreateThread` 等，这些 API 在用户层（`ntdll.dll`）极易被 EDR Hook。
*   **增强**：实现 **Tartarus' Gate** 或 **Halo's Gate**。
    *   动态解析 `ntdll.dll`，提取关键 API（如 `NtAllocateVirtualMemory`, `NtProtectVirtualMemory`, `NtCreateThreadEx`）的系统调用号（SSN）。
    *   在 `ntdll.dll` 的干净代码段中寻找 `syscall; ret` 指令（Trampoline）。
    *   所有敏感的内存和线程操作，全部通过汇编跳转到 Trampoline 执行，完美绕过用户层 Hook。

**3. 线程与调用栈伪造 (Thread & Stack Spoofing)** — **【部分已做】**（见 §〇）
*   **现状**：`Boffer.cpp` (异步 BOF) 和 `JobsController` 会直接创建线程，线程起始地址指向 Beacon 内存，调用栈也是异常的。
*   **增强**：
    *   **线程起始地址伪造**：使用 `CreateThread` 创建挂起（Suspended）的线程，起始地址指向合法的 API（如 `ntdll!RtlUserThreadStart`），然后通过 `SetThreadContext` 或 `QueueUserAPC` 将实际的 RIP 指向你的 Payload，最后恢复线程。
    *   **调用栈伪造**：在执行网络请求（WinInet/WinHTTP）时，由于这些 API 耗时较长，EDR 经常会回溯调用栈。可以使用 ROP 链将调用栈伪造成来自 `kernel32.dll` 或 `rpcrt4.dll` 的合法调用。

---

## 三、 重构策略：如何不破坏通讯协议？ — **【已遵守】**（见 §〇）

Adaptix 的通讯协议是高度结构化的，重构时必须**严格保持数据包的序列化和加密逻辑不变**。

**1. 保持 `Packer` 和 `Crypt` 原封不动**
*   `Packer.cpp` 负责将数据打包成 TLV（Type-Length-Value）或特定数组格式。
*   `Crypt.cpp` 负责 RC4/AES 加密以及 Session Key 的协商。
*   **重构动作**：这两个文件只需确保它们内部的内存操作（`new`, `memcpy`）使用的是你重写的无 CRT 版本即可，逻辑一行都不要改。

**2. 保持 `BuildBeat` 逻辑一致**
*   在 `Agent.cpp` 中，`BuildBeat` 负责生成上线包（包含 OS、Arch、PID、IP 等）。
*   只要你收集这些信息的方式（如通过 PEB 获取 OS 版本，而不是通过 `GetVersionEx`）改变了，但最终填入 `Packer` 的数据结构和顺序不变，Teamserver 就能完美解析。

**3. 替换底层 API 调用 (ApiLoader 升级)** — **【已做】**（见 §〇）
*   当前的 `ApiLoader.cpp` 已经有了按 Hash 查找函数的功能。
*   你需要将其升级为：**绝不使用任何静态导入表（IAT）**。将 `kernel32.dll` 的基址获取改为纯汇编（读取 `GS:[0x60]` -> PEB -> Ldr -> InLoadOrderModuleList）。
*   把 `ApiWin->...` 结构体里的函数指针，部分替换为你的间接 Syscall 包装函数。

---

## 四、 推荐的重构与实施路线图 (Roadmap)

为了保证项目在重构过程中“始终可编译、可测试”，建议采用渐进式的重构策略。**将 C++ 降级为 C 风格是迈向纯 PIC 的最关键第一步。**

- [x] **阶段 1：C 语言化改造 (C++ to C-style)** — **详见 `STAGE1_STATUS.md`**
  *目标：消除 C++ 特性带来的绝对地址引用和隐藏的 CRT 依赖。*
  *   **去虚函数化**：将 `Connector` 等包含 `virtual` 的类，重构为 C 语言的结构体 + 函数指针表（类似 Linux 内核的面向对象实现）。
  *   **替换内存分配**：全局搜索并替换所有的 `new` 和 `delete`，改为使用 `ApiWin->LocalAlloc` 或自定义的 `MemAlloc` 宏。
  *   **消除全局对象**：将 `g_Agent` 等全局类实例改为指针，在入口点手动分配内存并初始化，避免 `.CRT` 段的静态构造函数。
  *   *测试点：此时代码仍用 `g++` 编译，但内部已经是纯 C 逻辑，确保通讯和任务执行正常。*

- [x] **阶段 2：字符串混淆与动态 API 解析** — **部分 / 与 IAT 隐藏链路重叠，见 `STAGE2_PLAN.md`**
  *目标：消除 `.rdata` 段的明文字符串和 `.idata` 段的导入表（IAT）。*
  *   将所有明文字符串替换为栈字符串（Stack Strings）或编译期 XOR 宏。
  *   确保所有 Windows API 调用都通过 `ApiLoader` 的 Hash 方式动态获取，彻底切断对 `kernel32.dll` 等的静态链接。

- [ ] **阶段 3：彻底剥离 CRT 与 PIC 提取 (No-CRT & PIC)** — **无 CRT 构建路径已具备；完整 PIC/壳代码提取因格式而异**
  *目标：生成真正的无文件落地的 Shellcode。*
  *   在 Makefile 中加入 `-nostdlib -nodefaultlibs -fno-exceptions -fno-rtti`。
  *   补充缺失的内置函数（如用汇编实现 `memset`, `memcpy`, `__chkstk`）。
  *   修改链接脚本（`Linker.ld`），合并所有段到 `.text`，并提取为 `.bin` 格式的 Shellcode。
  *   *测试点：使用 Shellcode Loader 加载 `.bin`，验证能否正常上线。*

- [ ] **阶段 4：高级对抗增强 (Advanced OpSec)** — **睡眠混淆 + 间接 syscall + 线程伪装已落地；完整栈 ROP 未做**
  *目标：对抗内存扫描和 EDR 行为监控。*
  *   **睡眠混淆**：重写 `WaitMask.cpp`，引入 Ekko/Foliage 机制，在休眠时加密 Beacon 内存。
  *   **间接 Syscall**：引入 Tartarus' Gate 等技术，将敏感的内存和线程操作替换为 Syscall。
  *   **调用栈伪造**：在进行网络通讯（WinHTTP）前伪造线程调用栈。

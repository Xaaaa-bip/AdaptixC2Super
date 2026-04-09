# Stage 2: 彻底移除 CRT 与底层特征规避 (No-CRT & OpSec)

## 核心目标：解决 360 QVM 等启发式引擎的静态查杀
根据对 Kharon 与 AdaptixC2 生成流程的对比分析，AdaptixC2 被 360 QVM 标记的核心原因在于其保留了大量传统编译器的特征（标准的 MinGW 编译、CRT 启动代码、可见的导入表）。为了达到 Kharon 的免杀效果，Stage 2 的**第一点实现**必须是彻底重构生成流程与底层加载机制。

### 1. 彻底剥离 C 运行库 (CRT) 与重写入口点
*   **现状问题**：即使代码改为了纯 C 风格，只要不彻底移除 CRT，编译器仍会自动插入一大段初始化代码（如 `mainCRTStartup`），这是杀软重点提取特征的地方。
*   **实现方案**：
    *   最终链接由 `pl_main.go` 在**无 CRT 模式**下注入 `-nostdlib -nostartfiles -nodefaultlibs`，配合 `beacon/crt.cpp` 中的桩与 RTTI 存根。
    *   Exe / Service：汇编入口 → `AxRawExeEntry` / `AxRawSvcEntry`（见 `beacon/asm/*.asm`）。
    *   DLL / Shellcode：仍从 `DllMain` 进入；无 CRT 时由同一套链接标志与自定义 CRT 支撑。

### 2. 零导入表设计 (Zero IAT) 与动态 API 解析
*   **现状问题**：当前虽然部分 API 动态加载，但基础 API（如 `LocalAlloc`、`GetProcAddress`）仍依赖操作系统的加载器，生成了可见的导入表（IAT），静态分析可直接看到敏感 API。
*   **实现方案**：
    *   实现完整的 PEB（进程环境块）遍历机制（如 `LdrLoad::Module` 和 `LdrLoad::_Api`）。
    *   通过 Hash 在内存中动态寻找 `ntdll.dll` 和 `kernel32.dll` 的基址，并解析所有需要的 API。
    *   确保编译出的 EXE/DLL 没有任何导入表，使静态分析引擎将其视为“无害”空壳。

### 3. 优化编译器与链接器特征 (Compiler & Linker Signatures)
*   **现状问题**：MinGW GCC 生成的 PE 文件具有非常明显的区段（Sections）特征和代码生成模式，QVM 对此有极高的基础“恶意评分”。
*   **实现方案**：
    *   引入自定义的链接脚本（如 `Linker.ld`），将 `.rdata`（只读数据段）等合并到 `.text`（代码段）中，打破传统的 PE 区段结构。
    *   **已做**：`src_beacon/Makefile` 使用 `clang++ -target *-w64-mingw32`，`COMMON_FLAGS` 含 `-fno-ident`、`-fpack-struct=8`、`-mrdrnd`、`-fms-extensions` 等（与 Kharon 思路对齐）；`crt.cpp` 单独加 `-masm=intel`、`-fno-ms-extensions`（避免 Clang 与 naked `_alloca` 冲突）。
    *   链接后使用 MinGW 的 `objcopy` 剥离 `.comment` / `.buildid` 等（`clang++` 时在 `pl_main.go` 中固定调用 `x86_64-w64-mingw32-objcopy` 或 `i686-w64-mingw32-objcopy`）。

---

## 实施步骤 (TODO)

- [x] 编写汇编入口点文件 (`beacon/asm/entry_exe_x64.asm`、`entry_exe_x86.asm`、`entry_svc_x64.asm`、`entry_svc_x86.asm`)。
- [x] 实现基于 PEB 的无 IAT 模块与 API 解析器：`ProcLoader` / `LdrResolve`，`ApiLoad()` 经哈希解析。
- [x] `Makefile`：NASM 编入口对象；Clang 交叉 + Kharon 风格 `COMMON_FLAGS`；`crt.cpp` 专用编译规则。
- [x] `pl_main.go`：默认**无 CRT**（`iat_hiding` 省略或为 `null` 时等同勾选）；注入 `-nostdlib` 与 `-DIAT_HIDING`（仅编译 `config.cpp`）。显式 `"iat_hiding": false` 时走 MinGW CRT + 常规 `main`/`WinMain` 链接。
- [x] IAT/无 CRT 且格式为 Exe 或 Service：追加 `entry_exe`/`entry_svc`、`Linker.ld`、`-Wl,-e,AxAsmEntry`（x86 `_AxAsmEntry`）。
- [x] `main.cpp`：`AxRawExeEntry` / `AxRawSvcEntry` 供汇编入口调用；CRT 构建路径仍保留。
- [x] 编译测试：`make pre && make x64 && make x86`；插件 `go build -buildmode=plugin`。

## 使用说明
- **默认生成（推荐）**：`iat_hiding` 未写或为 `true` → **无 CRT** + IAT 隐藏链路；UI 上「IAT Hiding (no CRT, …)」默认勾选。
- **传统 CRT**：配置中显式 **`"iat_hiding": false`**（或取消勾选）→ 标准 MinGW 启动与导入行为。
- **Exe / Service Exe + 无 CRT**：汇编入口 `AxAsmEntry` → `AxRawExeEntry` / `AxRawSvcEntry`，`Linker.ld` 合并 `.rdata` 到 `.text`。
- **DLL / Shellcode + 无 CRT**：`DllMain` 入口；部分格式会追加 `-lkernel32` 等以满足链接（仍可能产生少量导入，视符号解析而定）。

## OpSec 增强（已在 `src_beacon` 落地，与 `REFACTOR_STRATEGY.md` §〇 对照）

- [x] **睡眠混淆**：`OpsecSleep.cpp` — `CreateTimerQueueTimer` + `SystemFunction032`（RC4）+ 事件等待；`Indirect_NtDelayExecution`（x64 间接 syscall）。
- [x] **间接 syscall（x64）**：`IndirectSyscall.cpp` + `beacon/asm/indirect_syscall_x64.asm` — `NtDelayExecution`、`NtProtectVirtualMemory`、`NtAllocateVirtualMemory`、`NtCreateThreadEx`；Halo 式 SSN + ntdll `syscall;ret` gadget。
- [ ] **BOF 加载内存**：`Boffer.cpp` / `bof_loader.cpp` 仅使用 `ApiWin->VirtualAlloc`（**不按间接 syscall 增强 BOF**，与原版行为一致）。
- [x] **异步 BOF 线程（x64）**：优先 `Indirect_NtCreateThreadEx` + `RtlUserThreadStart` + `SetThreadContext`；`GetThreadId` 解析线程 ID；失败回退 `CreateThread` 同策略再回退直建线程。
- [x] **WinHTTP 轻量栈过渡**：`Opsec_PreBlockingWinHttp`（`SwitchToThread`）于 `ConnectorHTTP.cpp`。
- [ ] **完整调用栈 ROP 伪造**（文档所述 `kernel32`/`rpcrt4` 深度伪造）— 未实现。

---

## 仍可继续观察的点（未改代码，仅记录）
- **`ApiLoader.cpp`**：运行期仍可能对 **msvcrt** 做 `LoadLibraryA`（动态、非 IAT 直引）。若要连运行时也不碰 msvcrt，需逐调用审计并改为自实现或全走哈希解析。
- **`src_beacon/beacon_backup/`**：旧树副本（含已过时的 `crt` 编译说明），不参与正式构建；混淆检索时可忽略或日后删除。
- **`REFACTOR_STRATEGY.md`**：已增加 **§〇 实现状态表** 与各节 **【已做】** 标注；细节仍以本文件与仓库代码为准。

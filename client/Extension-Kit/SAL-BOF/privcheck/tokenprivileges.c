#include <windows.h>
#include <stdio.h>
#include "beacon.h"
#include "bofdefs.h"

BOOL IsVulnerable(char * privilegeNameBuffer) {
    // Define known vulnerable privileges
    static const char * KnownVulnerable[] = {
            "SeAssignPrimaryToken",
            "SeBackupPrivilege",
            "SeCreateTokenPrivilege",
            "SeRestorePrivilege",
            "SeDebugPrivilege",
            "SeImpersonatePrivilege",
            "SeLoadDriverPrivilege",
            "SeManageVolumePrivilege",
            "SeTcbPrivilege",
            "SeTakeOwnershipPrivilege",
            NULL
    };

    int i = 0;

    while (KnownVulnerable[i]) {
        if (MSVCRT$_stricmp(KnownVulnerable[i], privilegeNameBuffer) == 0) {
            return TRUE;
        }
        i++;
    }
    return FALSE;
}

void DisplayPrivileges(TOKEN_PRIVILEGES* tokenPrivileges) {
    for (DWORD i = 0; i < tokenPrivileges->PrivilegeCount; ++i) {
        LUID privilegeLuid = tokenPrivileges->Privileges[i].Luid;
        char privilegeNameBuffer[256];
        DWORD bufferSize = sizeof(privilegeNameBuffer);
        if (ADVAPI32$LookupPrivilegeNameA(NULL, &privilegeLuid, privilegeNameBuffer, &bufferSize)) {
            BOOL isEnabled = (tokenPrivileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) == SE_PRIVILEGE_ENABLED;
            BOOL isVulnerable = IsVulnerable(privilegeNameBuffer);
            BeaconPrintf(CALLBACK_OUTPUT,"[PRIVILEGE] %s: %s %s\n", privilegeNameBuffer, isEnabled ? "Enabled" : "Disabled", isVulnerable ? "- Vulnerable" : "");
        }
    }
}

void go() {
    HANDLE tokenHandle;
    if (!ADVAPI32$OpenProcessToken(KERNEL32$GetCurrentProcess(), TOKEN_QUERY, &tokenHandle)) {
        BeaconPrintf(CALLBACK_OUTPUT,"[PRIVILEGE] Failed to open process token. Error: %lu\n", KERNEL32$GetLastError());
        return;
    }

    DWORD tokenInfoSize = 0;
    ADVAPI32$GetTokenInformation(tokenHandle, TokenPrivileges, NULL, 0, &tokenInfoSize);
    if (tokenInfoSize == 0) {
        BeaconPrintf(CALLBACK_OUTPUT,"[PRIVILEGE] Failed to get token information size. Error: %lu\n", KERNEL32$GetLastError());
        goto cleanup;
    }

    PTOKEN_PRIVILEGES tokenPrivileges = (PTOKEN_PRIVILEGES)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, tokenInfoSize);
    if (!tokenPrivileges) {
        BeaconPrintf(CALLBACK_OUTPUT, "[PRIVILEGE] Memory allocation failed.\n");
        goto cleanup;
    }
    if (!ADVAPI32$GetTokenInformation(tokenHandle, TokenPrivileges, tokenPrivileges, tokenInfoSize, &tokenInfoSize)) {
        BeaconPrintf(CALLBACK_OUTPUT,"[PRIVILEGE] Failed to get token privileges. Error: %lu\n", KERNEL32$GetLastError());
        goto cleanup;
    }

    DisplayPrivileges(tokenPrivileges);

    cleanup:
        if (tokenPrivileges) {
            KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, tokenPrivileges);
        }
        if (tokenHandle) {
            KERNEL32$CloseHandle(tokenHandle);
        }
}
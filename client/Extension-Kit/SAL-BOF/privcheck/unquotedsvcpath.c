#include <windows.h>
#include <string.h>
#include <shlwapi.h>
#include "bofdefs.h"
#include "beacon.h"


void go() {
    HKEY servicesKey;
    BOOL foundVulnerablePath = FALSE;
    if (ADVAPI32$RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\", 0, KEY_READ, &servicesKey) == ERROR_SUCCESS) {
        char serviceSubkeyName[256];
        DWORD subkeyIndex = 0;
        DWORD subkeyNameSize = sizeof(serviceSubkeyName);
        while (TRUE) {
            subkeyNameSize = sizeof(serviceSubkeyName);
            if (ADVAPI32$RegEnumKeyExA(servicesKey, subkeyIndex++, serviceSubkeyName, &subkeyNameSize, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
                break;
            }
                HKEY imagePathKey;
                if (ADVAPI32$RegOpenKeyExA(servicesKey, serviceSubkeyName, 0, KEY_READ, &imagePathKey) == ERROR_SUCCESS) {
                    char imagePathValue[1024] = { 0 };
                    DWORD valueSize = sizeof(imagePathValue);
                    if (ADVAPI32$RegGetValueA(imagePathKey, NULL, "ImagePath", RRF_RT_REG_SZ, NULL, &imagePathValue, &valueSize) != ERROR_SUCCESS) {
                        ADVAPI32$RegCloseKey(imagePathKey);
                        continue;
                    }

                    if (SHLWAPI$StrStrIA(imagePathValue, " ") == NULL || SHLWAPI$StrStrIA(imagePathValue, "\"") != NULL) {
                        ADVAPI32$RegCloseKey(imagePathKey);
                        continue;
                    }

                    if (SHLWAPI$StrStrIA(imagePathValue, "System32") != NULL ||
                        SHLWAPI$StrStrIA(imagePathValue, "SysWow64") != NULL ||
                        SHLWAPI$StrStrIA(imagePathValue, ".sys") != NULL)
                    {
                        ADVAPI32$RegCloseKey(imagePathKey);
                        continue;
                    }

                    BeaconPrintf(CALLBACK_OUTPUT, "[SERVICE_PATH] Service '%s' has an unquoted executable path: %s\n", serviceSubkeyName, imagePathValue);
                    foundVulnerablePath = TRUE;

                    ADVAPI32$RegCloseKey(imagePathKey);
                }
        }
        ADVAPI32$RegCloseKey(servicesKey);
    }

    if (!foundVulnerablePath) {
        BeaconPrintf(CALLBACK_OUTPUT, "[SERVICE_PATH] No unquoted service paths found");
    }
}
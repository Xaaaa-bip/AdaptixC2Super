/*
 * BOF Definitions and Dynamic Function Resolution
 * IHxExec-BOF - Cross-Session Execution via COM
 */

#ifndef _BOFDEFS_H_
#define _BOFDEFS_H_

#include <windows.h>
#include "beacon.h"

/* ============================================================================
 * Dynamic Function Resolution for BOF
 * ============================================================================ */

#ifdef BOF

/* OLE32.dll - COM Functions */
DECLSPEC_IMPORT HRESULT WINAPI OLE32$CoInitializeEx(LPVOID, DWORD);
DECLSPEC_IMPORT void    WINAPI OLE32$CoUninitialize(void);
DECLSPEC_IMPORT HRESULT WINAPI OLE32$CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);

/* KERNEL32.dll */
DECLSPEC_IMPORT int     WINAPI KERNEL32$MultiByteToWideChar(UINT, DWORD, LPCCH, int, LPWSTR, int);
DECLSPEC_IMPORT void*   WINAPI KERNEL32$HeapAlloc(HANDLE, DWORD, SIZE_T);
DECLSPEC_IMPORT BOOL    WINAPI KERNEL32$HeapFree(HANDLE, DWORD, LPVOID);
DECLSPEC_IMPORT HANDLE  WINAPI KERNEL32$GetProcessHeap(void);
DECLSPEC_IMPORT DWORD   WINAPI KERNEL32$GetLastError(void);

/* MSVCRT.dll */
DECLSPEC_IMPORT size_t  __cdecl MSVCRT$wcslen(const wchar_t*);
DECLSPEC_IMPORT wchar_t* __cdecl MSVCRT$wcscpy(wchar_t*, const wchar_t*);
DECLSPEC_IMPORT wchar_t* __cdecl MSVCRT$wcscat(wchar_t*, const wchar_t*);
DECLSPEC_IMPORT int     __cdecl MSVCRT$wcsncmp(const wchar_t*, const wchar_t*, size_t);
DECLSPEC_IMPORT void*   __cdecl MSVCRT$memset(void*, int, size_t);

/* Macros for cleaner code */
#define CoInitializeEx      OLE32$CoInitializeEx
#define CoUninitialize      OLE32$CoUninitialize
#define CoCreateInstance    OLE32$CoCreateInstance
#define MultiByteToWideChar KERNEL32$MultiByteToWideChar
#define HeapAlloc           KERNEL32$HeapAlloc
#define HeapFree            KERNEL32$HeapFree
#define GetProcessHeap      KERNEL32$GetProcessHeap
#define GetLastError        KERNEL32$GetLastError
#define wcslen              MSVCRT$wcslen
#define wcscpy              MSVCRT$wcscpy
#define wcscat              MSVCRT$wcscat
#define wcsncmp             MSVCRT$wcsncmp
#define memset              MSVCRT$memset

#else /* Non-BOF compilation */

#pragma comment(lib, "ole32.lib")

#endif /* BOF */

/* ============================================================================
 * COM GUIDs for IHxExec
 * ============================================================================ */

// CLSID_IHxHelpPaneServer: {8cec58ae-07a1-11d9-b15e-000d56bfe6ee}
static const GUID CLSID_IHxHelpPaneServer = {
    0x8cec58ae, 0x07a1, 0x11d9, { 0xb1, 0x5e, 0x00, 0x0d, 0x56, 0xbf, 0xe6, 0xee }
};

// IID_IHxHelpPaneServer: {8cec592c-07a1-11d9-b15e-000d56bfe6ee}
static const GUID IID_IHxHelpPaneServer = {
    0x8cec592c, 0x07a1, 0x11d9, { 0xb1, 0x5e, 0x00, 0x0d, 0x56, 0xbf, 0xe6, 0xee }
};

// CLSID_ComActivator (Standard COM Activator): {0000033C-0000-0000-C000-000000000046}
static const GUID CLSID_ComActivator = {
    0x0000033C, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

// IID_IStandardActivator: {000001b8-0000-0000-C000-000000000046}
static const GUID IID_IStandardActivator = {
    0x000001b8, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

// IID_ISpecialSystemProperties: {000001b9-0000-0000-C000-000000000046}
static const GUID IID_ISpecialSystemProperties = {
    0x000001b9, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

/* ============================================================================
 * COM Interface: IHxHelpPaneServer
 * ============================================================================ */

typedef struct IHxHelpPaneServerVtbl {
    // IUnknown
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void* This, REFIID riid, void** ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void* This);
    ULONG   (STDMETHODCALLTYPE *Release)(void* This);
    // IHxHelpPaneServer
    HRESULT (STDMETHODCALLTYPE *DisplayTask)(void* This, LPWSTR pwszTask);
    HRESULT (STDMETHODCALLTYPE *DisplayContents)(void* This, LPWSTR pwszContents);
    HRESULT (STDMETHODCALLTYPE *DisplaySearchResults)(void* This, LPWSTR pwszSearch);
    HRESULT (STDMETHODCALLTYPE *Execute)(void* This, LPCWSTR pwszUrl);
} IHxHelpPaneServerVtbl;

typedef struct IHxHelpPaneServer {
    IHxHelpPaneServerVtbl* lpVtbl;
} IHxHelpPaneServer;

/* ============================================================================
 * COM Interface: IStandardActivator
 * ============================================================================ */

typedef struct IStandardActivatorVtbl {
    // IUnknown
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void* This, REFIID riid, void** ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void* This);
    ULONG   (STDMETHODCALLTYPE *Release)(void* This);
    // IStandardActivator
    HRESULT (STDMETHODCALLTYPE *StandardGetClassObject)(
        void* This, REFCLSID rclsid, DWORD dwClsCtx, COSERVERINFO* pServerInfo,
        REFIID riid, void** ppvClassObj);
    HRESULT (STDMETHODCALLTYPE *StandardCreateInstance)(
        void* This, REFCLSID rclsid, IUnknown* punkOuter, DWORD dwClsCtx,
        COSERVERINFO* pServerInfo, DWORD dwCount, MULTI_QI* pResults);
    HRESULT (STDMETHODCALLTYPE *StandardGetInstanceFromFile)(
        void* This, COSERVERINFO* pServerInfo, CLSID* pclsidOverride,
        IUnknown* punkOuter, DWORD dwClsCtx, DWORD grfMode, OLECHAR* pwszName,
        DWORD dwCount, MULTI_QI* pResults);
    HRESULT (STDMETHODCALLTYPE *StandardGetInstanceFromIStorage)(
        void* This, COSERVERINFO* pServerInfo, CLSID* pclsidOverride,
        IUnknown* punkOuter, DWORD dwClsCtx, IStorage* pstg,
        DWORD dwCount, MULTI_QI* pResults);
    HRESULT (STDMETHODCALLTYPE *Reset)(void* This);
} IStandardActivatorVtbl;

typedef struct IStandardActivator {
    IStandardActivatorVtbl* lpVtbl;
} IStandardActivator;

/* ============================================================================
 * COM Interface: ISpecialSystemProperties
 * ============================================================================ */

typedef struct ISpecialSystemPropertiesVtbl {
    // IUnknown
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void* This, REFIID riid, void** ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void* This);
    ULONG   (STDMETHODCALLTYPE *Release)(void* This);
    // ISpecialSystemProperties
    HRESULT (STDMETHODCALLTYPE *SetSessionId)(void* This, ULONG dwSessionId, BOOL bUseConsole, BOOL fRemoteThisSessionId);
    HRESULT (STDMETHODCALLTYPE *GetSessionId)(void* This, ULONG* pdwSessionId, BOOL* pbUseConsole);
    HRESULT (STDMETHODCALLTYPE *GetSessionId2)(void* This, ULONG* pdwSessionId, BOOL* pbUseConsole, BOOL* pfRemoteThisSessionId);
    HRESULT (STDMETHODCALLTYPE *SetClientImpersonating)(void* This, BOOL fClientImpersonating);
    HRESULT (STDMETHODCALLTYPE *GetClientImpersonating)(void* This, BOOL* pfClientImpersonating);
    HRESULT (STDMETHODCALLTYPE *SetPartitionId)(void* This, REFGUID guidPartitionId);
    HRESULT (STDMETHODCALLTYPE *GetPartitionId)(void* This, GUID* pguidPartitionId);
    HRESULT (STDMETHODCALLTYPE *SetProcessRequestType)(void* This, DWORD dwProcessRequestType);
    HRESULT (STDMETHODCALLTYPE *GetProcessRequestType)(void* This, DWORD* pdwProcessRequestType);
    HRESULT (STDMETHODCALLTYPE *SetOrigClsctx)(void* This, DWORD dwOrigClsctx);
    HRESULT (STDMETHODCALLTYPE *GetOrigClsctx)(void* This, DWORD* pdwOrigClsctx);
    HRESULT (STDMETHODCALLTYPE *SetDefaultAuthenticationLevel)(void* This, DWORD dwDefaultAuthenticationLevel);
    HRESULT (STDMETHODCALLTYPE *GetDefaultAuthenticationLevel)(void* This, DWORD* pdwDefaultAuthenticationLevel);
} ISpecialSystemPropertiesVtbl;

typedef struct ISpecialSystemProperties {
    ISpecialSystemPropertiesVtbl* lpVtbl;
} ISpecialSystemProperties;

#endif /* _BOFDEFS_H_ */

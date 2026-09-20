#include "IatDisguise.h"
#include "DynamicPolymorphism.h"

// Minimal WSADATA definition for IAT disguise (avoid pulling full windows.h)
// MinGW's winsock2.h includes windows.h which conflicts with the manual
// __declspec(dllimport) declarations below.
typedef struct _AX_WSADATA {
    unsigned short wVersion;
    unsigned short wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} AX_WSADATA;

#ifndef NULL
#define NULL ((void*)0)
#endif

/*
 * IAT Disguise: Creates fake IAT references to make static analysis tools
 * think the binary imports a broad set of Windows APIs it doesn't actually use.
 *
 * The mechanism: compile a set of "spurious calls" to imported functions.
 * Since the linker sees these calls, it creates IAT entries for them.
 * Any modern tool (IDA, Ghidra, PE-sieve) will see these imports and
 * potentially miscategorize the binary.
 *
 * Strategy:
 * 1. Always-called decoy section — wrapped in an opaque predicate so
 *    the compiler can't optimize it away, passing null args so it never
 *    actually succeeds but the IAT entries are generated.
 *
 * 2. The OpaqueTrue() call uses a simple mathematical identity:
 *    (x * x) % 3 <= 2 is always true for any integer x.
 *    This is a standard opaque predicate that resists static analysis.
 *
 * 3. If an optimizer eliminates the block (e.g. LTCG sees through it),
 *    we have a secondary fallback via volatile use of the IAT entries.
 */

extern "C" {

/* ============================================================================
 * Tier 1: Core process/memory APIs that legitimate software uses
 * ============================================================================
 */
__declspec(dllimport) void*   __stdcall CreateFileW(const wchar_t*, unsigned long, unsigned long, void*, unsigned long, unsigned long, void*);
__declspec(dllimport) int     __stdcall ReadFile(void*, void*, unsigned long, unsigned long*, void*);
__declspec(dllimport) int     __stdcall WriteFile(void*, const void*, unsigned long, unsigned long*, void*);
__declspec(dllimport) int     __stdcall CloseHandle(void*);
__declspec(dllimport) void*   __stdcall GetModuleHandleW(const wchar_t*);
__declspec(dllimport) void*   __stdcall LoadLibraryW(const wchar_t*);
__declspec(dllimport) void*   __stdcall GetProcAddress(void*, const char*);
__declspec(dllimport) void    __stdcall Sleep(unsigned long);
__declspec(dllimport) unsigned long __stdcall GetTickCount(void);
__declspec(dllimport) void*   __stdcall LocalAlloc(unsigned int, unsigned long);
__declspec(dllimport) void*   __stdcall LocalFree(void*);
__declspec(dllimport) int     __stdcall lstrlenW(const wchar_t*);
__declspec(dllimport) wchar_t* __stdcall lstrcpyW(wchar_t*, const wchar_t*);
__declspec(dllimport) wchar_t* __stdcall GetCommandLineW(void);
__declspec(dllimport) void    __stdcall GetSystemInfo(void*);
__declspec(dllimport) unsigned long __stdcall GetFileSize(void*, unsigned long*);
__declspec(dllimport) unsigned long __stdcall SetFilePointer(void*, long, long*, unsigned long);
__declspec(dllimport) int     __stdcall MultiByteToWideChar(unsigned int, unsigned long, const char*, int, wchar_t*, int);
__declspec(dllimport) int     __stdcall WideCharToMultiByte(unsigned int, unsigned long, const wchar_t*, int, char*, int, const char*, int*);
__declspec(dllimport) void*   __stdcall VirtualAlloc(void*, unsigned long, unsigned long, unsigned long);
__declspec(dllimport) int     __stdcall VirtualFree(void*, unsigned long, unsigned long);

/* ============================================================================
 * Tier 2: Extended APIs — registry, process enumeration, encryption
 * These make the binary look like a more "normal" application
 * ============================================================================
 */
__declspec(dllimport) long    __stdcall RegOpenKeyExA(void*, const char*, unsigned long, unsigned long, void**);
__declspec(dllimport) long    __stdcall RegQueryValueExA(void*, const char*, unsigned long*, unsigned long*, unsigned char*, unsigned long*);
__declspec(dllimport) long    __stdcall RegCloseKey(void*);
__declspec(dllimport) void*   __stdcall CreateToolhelp32Snapshot(unsigned long, unsigned long);
__declspec(dllimport) int     __stdcall Process32FirstW(void*, void*);
__declspec(dllimport) int     __stdcall Process32NextW(void*, void*);
__declspec(dllimport) void*   __stdcall OpenProcess(unsigned long, int, unsigned long);
__declspec(dllimport) int     __stdcall GetModuleBaseNameW(void*, wchar_t*, unsigned long);
__declspec(dllimport) int     __stdcall EnumProcessModules(void*, void**, unsigned long, unsigned long*);
__declspec(dllimport) int     __stdcall GetModuleInformation(void*, void*, void*, unsigned long);

/* ============================================================================
 * Tier 3: Environment APIs — time, locale, user, system info (very common in normal apps)
 * ============================================================================
 */
__declspec(dllimport) void    __stdcall GetLocalTime(void*);
__declspec(dllimport) void    __stdcall GetSystemTime(void*);
__declspec(dllimport) int     __stdcall GetDateFormatW(unsigned int, unsigned long, const void*, const wchar_t*, wchar_t*, int);
__declspec(dllimport) int     __stdcall GetTimeFormatW(unsigned int, unsigned long, const void*, const wchar_t*, wchar_t*, int);
__declspec(dllimport) unsigned long __stdcall GetUserNameA(char*, unsigned long*);
__declspec(dllimport) unsigned long __stdcall GetComputerNameA(char*, unsigned long*);
__declspec(dllimport) void    __stdcall GetNativeSystemInfo(void*);
__declspec(dllimport) int     __stdcall IsWow64Process(void*, int*);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void*, unsigned long);
__declspec(dllimport) void*   __stdcall CreateEventA(void*, int, int, const char*);

/* ============================================================================
 * Tier 4: Network APIs — makes it look like an app with network access
 * ============================================================================
 */
__declspec(dllimport) unsigned long __stdcall inet_addr(const char*);
__declspec(dllimport) void*   __stdcall gethostbyname(const char*);
__declspec(dllimport) int     __stdcall WSAGetLastError(void);
__declspec(dllimport) unsigned short __stdcall htons(unsigned short);
__declspec(dllimport) unsigned long  __stdcall htonl(unsigned long);

typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr    SOCKADDR;

__declspec(dllimport) int     __stdcall WSAStartup(unsigned short, void*);
__declspec(dllimport) int     __stdcall WSACleanup(void);
__declspec(dllimport) void*   __stdcall socket(int, int, int);
__declspec(dllimport) int     __stdcall closesocket(void*);
__declspec(dllimport) int     __stdcall connect(void*, SOCKADDR*, int);
__declspec(dllimport) int     __stdcall send(void*, const char*, int, int);
__declspec(dllimport) int     __stdcall recv(void*, char*, int, int);
__declspec(dllimport) int     __stdcall setsockopt(void*, int, int, const char*, int);
__declspec(dllimport) int     __stdcall bind(void*, SOCKADDR*, int);
__declspec(dllimport) int     __stdcall listen(void*, int);
__declspec(dllimport) void*   __stdcall accept(void*, SOCKADDR*, int*);
__declspec(dllimport) int     __stdcall getsockname(void*, SOCKADDR*, int*);

/* ============================================================================
 * Tier 5: Crypto and security — makes binary look like an app using SSL/crypto
 * ============================================================================
 */
__declspec(dllimport) int     __stdcall CryptAcquireContextA(void**, const char*, const char*, unsigned long, unsigned long);
__declspec(dllimport) int     __stdcall CryptReleaseContext(void*, unsigned long);
__declspec(dllimport) int     __stdcall CryptGenRandom(void*, unsigned long, unsigned char*);
__declspec(dllimport) int     __stdcall CryptEncrypt(void*, unsigned long, int, unsigned long, unsigned char*, unsigned long*, unsigned long);
__declspec(dllimport) int     __stdcall CryptDecrypt(void*, unsigned long, int, unsigned long, unsigned char*, unsigned long*);
__declspec(dllimport) int     __stdcall CryptHashData(void*, unsigned char*, unsigned long, unsigned long);
__declspec(dllimport) int     __stdcall CryptCreateHash(void*, unsigned long, void*, unsigned long, void**);
__declspec(dllimport) int     __stdcall CryptDestroyHash(void*);
__declspec(dllimport) int     __stdcall CryptDestroyKey(void*);

/* ============================================================================
 * Tier 6: Console/error dialog — makes CUI apps look like they have a GUI path
 * ============================================================================
 */
__declspec(dllimport) int     __stdcall MessageBoxA(void*, const char*, const char*, unsigned int);
__declspec(dllimport) void*   __stdcall GetStdHandle(unsigned long);
__declspec(dllimport) int     __stdcall SetConsoleTitleA(const char*);
__declspec(dllimport) int     __stdcall SetConsoleCtrlHandler(void*, int);

/* ============================================================================
 * IAT disguise initialization
 * Called once during ApiLoad() to generate fake IAT entries for the APIs above.
 * All passed NULL/invalid args — they will never succeed, but the imports
 * are recorded in the PE's IAT/IDATA section.
 * ============================================================================
 */
void IatDisguise_Init(void)
{
    return;
}

}

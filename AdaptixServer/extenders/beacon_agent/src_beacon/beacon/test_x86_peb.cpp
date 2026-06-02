/*
 * x86 PEB 访问诊断程序
 * 用法：
 *   1. 编译: clang++ -target i686-w64-mingw32 test_x86_peb.cpp -o test_x86.exe
 *   2. 在 32-bit Windows / WoW64 下运行 test_x86.exe
 *   3. 检查输出，确认 PEB 遍历是否正确
 */

#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "kernel32.lib")

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING;

// 1. 测试 PEB → Ldr → ModuleList 遍历 (AT&T 内联汇编)
void test_peb_at_t_asm() {
    printf("[1] AT&T inline asm PEB access (movl %%fs:0x30): ");

    void* peb;
    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(peb));

    if (!peb) {
        printf("FAIL - PEB is NULL\n");
        return;
    }

    // PEB+0x0C = Ldr (PPEB_LDR_DATA)
    void* ldr = *(void**)((BYTE*)peb + 0x0C);
    if (!ldr) {
        printf("FAIL - Ldr is NULL\n");
        return;
    }

    // Ldr+0x0C = InMemoryOrderModuleList (LIST_ENTRY)
    LIST_ENTRY* head = (LIST_ENTRY*)((BYTE*)ldr + 0x0C);
    LIST_ENTRY* entry = head->Flink;

    int count = 0;
    while (entry != head && count < 50) {
        // LDR_DATA_TABLE_ENTRY: InMemoryOrderLinks at offset sizeof(LIST_ENTRY)=8
        void* mod = (BYTE*)entry - 8;  // sizeof(LIST_ENTRY) on x86 = 8

        // DllBase at offset 0x18
        void* dllBase = *(void**)((BYTE*)mod + 0x18);

        // BaseDllName at offset 0x2C
        USHORT nameLen = *(USHORT*)((BYTE*)mod + 0x2C);
        WCHAR* nameBuf = *(WCHAR**)((BYTE*)mod + 0x30);

        if (nameBuf && nameLen > 0 && dllBase) {
            // Print first few chars
            char ansi[64] = {0};
            for (int i = 0; i < (int)(nameLen/2) && i < 30; i++) {
                WCHAR wc = nameBuf[i];
                ansi[i] = (wc >= 32 && wc <= 126) ? (char)wc : '?';
            }
            printf("\n    [%d] %-30s @ 0x%p", count, ansi, dllBase);
        }
        count++;
        entry = entry->Flink;
    }
    printf("\n    => %d modules found. PEB traversal: OK\n", count);
}

// 2. 测试 NtCurrentTeb() (系统头文件中的实现)
void test_nt_current_teb() {
    printf("[2] NtCurrentTeb(): ");
    void* teb = NtCurrentTeb();
    if (!teb) {
        printf("FAIL - TEB is NULL\n");
        return;
    }

    // TEB+0x30 = ProcessEnvironmentBlock (PPEB)
    void* pebFromTeb = *(void**)((BYTE*)teb + 0x30);
    printf("TEB=0x%p, PEB(from TEB)=0x%p - OK\n", teb, pebFromTeb);
}

// 3. 测试 LocalAlloc / LocalFree (基础内存分配)
void test_memory() {
    printf("[3] Memory allocation: ");
    void* p = LocalAlloc(LPTR, 4096);
    if (!p) {
        printf("FAIL - LocalAlloc returned NULL\n");
        return;
    }
    memset(p, 0xAB, 4096);
    LocalFree(p);
    printf("OK\n");
}

// 4. 测试 RC4 加密（核心加密算法）
void test_rc4() {
    printf("[4] RC4 encryption: ");
    unsigned char key[16] = "testkey12345678";
    unsigned char data[16] = "Hello World!!!!";
    unsigned char copy[16];

    memcpy(copy, data, 16);

    // RC4 encrypt
    {
        unsigned char S[256];
        int i, j = 0;
        unsigned char temp;

        for (i = 0; i < 256; i++) S[i] = (unsigned char)i;
        for (i = 0; i < 256; i++) {
            j = (j + S[i] + key[i % 16]) % 256;
            temp = S[i]; S[i] = S[j]; S[j] = temp;
        }
        i = 0; j = 0;
        for (int k = 0; k < 16; k++) {
            i = (i + 1) % 256;
            j = (j + S[i]) % 256;
            temp = S[i]; S[i] = S[j]; S[j] = temp;
            copy[k] ^= S[(S[i] + S[j]) % 256];
        }
    }

    // RC4 decrypt (same as encrypt)
    {
        unsigned char S[256];
        int i, j = 0;
        unsigned char temp;

        for (i = 0; i < 256; i++) S[i] = (unsigned char)i;
        for (i = 0; i < 256; i++) {
            j = (j + S[i] + key[i % 16]) % 256;
            temp = S[i]; S[i] = S[j]; S[j] = temp;
        }
        i = 0; j = 0;
        for (int k = 0; k < 16; k++) {
            i = (i + 1) % 256;
            j = (j + S[i]) % 256;
            temp = S[i]; S[i] = S[j]; S[j] = temp;
            copy[k] ^= S[(S[i] + S[j]) % 256];
        }
    }

    if (memcmp(copy, data, 16) == 0) {
        printf("OK (encrypt/decrypt round-trip matches)\n");
    } else {
        printf("FAIL - RC4 round-trip mismatch!\n");
    }
}

// 5. 测试 struct 对齐
void test_struct_layout() {
    printf("[5] Struct layouts:\n");
    printf("    sizeof(LIST_ENTRY)        = %zu (expected: 8 on x86, 16 on x64)\n", sizeof(LIST_ENTRY));
    printf("    sizeof(void*)             = %zu (expected: 4 on x86, 8 on x64)\n", sizeof(void*));
    printf("    sizeof(UNICODE_STRING)    = %zu (expected: 8 on x86, 16 on x64)\n", sizeof(UNICODE_STRING));
    printf("    sizeof(DWORD)             = %zu (expected: 4)\n", sizeof(DWORD));
    printf("    sizeof(ULONG)             = %zu (expected: 4)\n", sizeof(ULONG));
    printf("    sizeof(BOOL)              = %zu (expected: 4)\n", sizeof(BOOL));

    // Check that PEB offsets are correct
    void* peb;
    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(peb));

    void* teb = NtCurrentTeb();

    // TEB+0x30 should point to PEB
    void* pebFromTeb = *(void**)((BYTE*)teb + 0x30);
    printf("    PEB == TEB->PEB?          %s\n", peb == pebFromTeb ? "YES" : "NO!! BUG!");

    void* ldr = *(void**)((BYTE*)peb + 0x0C);
    printf("    PEB->Ldr                  = 0x%p %s\n", ldr, ldr ? "" : "(NULL!)");

    // Check heap
    void* processHeap = *(void**)((BYTE*)peb + 0x18);
    printf("    PEB->ProcessHeap          = 0x%p %s\n", processHeap, processHeap ? "" : "(NULL!)");
}

// 6. 测试 WinHTTP 库是否可加载
void test_winhttp() {
    printf("[6] WinHTTP availability: ");
    HMODULE h = LoadLibraryA("winhttp.dll");
    if (h) {
        printf("loaded @ 0x%p\n", (void*)h);
        FreeLibrary(h);
    } else {
        printf("FAIL - LoadLibraryA(\"winhttp.dll\") returned NULL (err=%lu)\n", GetLastError());
    }
}

int main() {
    printf("=== AdaptixC2 x86 PEB & Runtime Diagnostic ===\n");
    printf("Architecture: %d-bit\n", (int)(sizeof(void*) * 8));
    printf("\n");

    test_struct_layout();
    printf("\n");
    test_nt_current_teb();
    test_peb_at_t_asm();
    printf("\n");
    test_memory();
    test_rc4();
    test_winhttp();
    printf("\n");
    printf("=== All diagnostics complete ===\n");
    return 0;
}

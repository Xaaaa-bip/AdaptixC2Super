#include "IatDisguise.h"

extern "C" {

__declspec(dllimport) void* __stdcall CreateFileW(const wchar_t*, unsigned long, unsigned long, void*, unsigned long, unsigned long, void*);
__declspec(dllimport) int __stdcall ReadFile(void*, void*, unsigned long, unsigned long*, void*);
__declspec(dllimport) int __stdcall WriteFile(void*, const void*, unsigned long, unsigned long*, void*);
__declspec(dllimport) int __stdcall CloseHandle(void*);
__declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t*);
__declspec(dllimport) void* __stdcall LoadLibraryW(const wchar_t*);
__declspec(dllimport) void* __stdcall GetProcAddress(void*, const char*);
__declspec(dllimport) void __stdcall Sleep(unsigned long);
__declspec(dllimport) unsigned long __stdcall GetTickCount(void);
__declspec(dllimport) void* __stdcall LocalAlloc(unsigned int, unsigned long);
__declspec(dllimport) void* __stdcall LocalFree(void*);
__declspec(dllimport) int __stdcall lstrlenW(const wchar_t*);
__declspec(dllimport) wchar_t* __stdcall lstrcpyW(wchar_t*, const wchar_t*);
__declspec(dllimport) wchar_t* __stdcall GetCommandLineW(void);
__declspec(dllimport) void __stdcall GetSystemInfo(void*);
__declspec(dllimport) unsigned long __stdcall GetFileSize(void*, unsigned long*);
__declspec(dllimport) unsigned long __stdcall SetFilePointer(void*, long, long*, unsigned long);
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int, unsigned long, const char*, int, wchar_t*, int);
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int, unsigned long, const wchar_t*, int, char*, int, const char*, int*);
__declspec(dllimport) void* __stdcall VirtualAlloc(void*, unsigned long, unsigned long, unsigned long);
__declspec(dllimport) int __stdcall VirtualFree(void*, unsigned long, unsigned long);

void IatDisguise_Init(void)
{
    volatile void* h = (volatile void*)0;
    volatile unsigned long n = 0;
    volatile unsigned long s = 0;
    volatile int r = 0;
    wchar_t buf[2] = {0};

    if (((volatile unsigned long long)(unsigned long long)&IatDisguise_Init ^ (unsigned long long)0x4B1D5EADDEADBEEFull) == 0)
    {
        n  = (unsigned long)CreateFileW((const wchar_t*)0, 0, 0, (void*)0, 0, 0, (void*)0);
        r  = (int)ReadFile((void*)0, (void*)0, (unsigned long)0, (unsigned long*)0, (void*)0);
        r  = (int)WriteFile((void*)0, (const void*)0, (unsigned long)0, (unsigned long*)0, (void*)0);
        r  = (int)CloseHandle((void*)0);
        h  = (volatile void*)GetModuleHandleW((const wchar_t*)0);
        h  = (volatile void*)LoadLibraryW((const wchar_t*)0);
        h  = (volatile void*)GetProcAddress((void*)0, (const char*)0);
        Sleep((unsigned long)0);
        n  = (unsigned long)GetTickCount();
        h  = (volatile void*)LocalAlloc((unsigned int)0, (unsigned long)0);
        h  = (volatile void*)LocalFree((void*)0);
        r  = (int)lstrlenW((const wchar_t*)0);
        h  = (volatile void*)lstrcpyW((wchar_t*)0, (const wchar_t*)0);
        h  = (volatile void*)GetCommandLineW();
        GetSystemInfo((void*)0);
        n  = (unsigned long)GetFileSize((void*)0, (unsigned long*)0);
        n  = (unsigned long)SetFilePointer((void*)0, (long)0, (long*)0, (unsigned long)0);
        r  = (int)MultiByteToWideChar((unsigned int)0, (unsigned long)0, (const char*)0, (int)0, (wchar_t*)0, (int)0);
        r  = (int)WideCharToMultiByte((unsigned int)0, (unsigned long)0, (const wchar_t*)0, (int)0, (char*)0, (int)0, (const char*)0, (int*)0);
        h  = (volatile void*)VirtualAlloc((void*)0, (unsigned long)0, (unsigned long)0, (unsigned long)0);
        r  = (int)VirtualFree((void*)0, (unsigned long)0, (unsigned long)0);
    }

    (void)n; (void)s; (void)r; (void)buf;
}

}

#include "OpsecGadget.h"
#include "ApiLoader.h"

PVOID Opsec_FindNtdllSyscallGadget(void)
{
#if defined(_M_X64) || defined(__x86_64__)
	if (!SysModules || !SysModules->Ntdll)
		return NULL;

	HMODULE mod = SysModules->Ntdll;
	auto* dos = (PIMAGE_DOS_HEADER)mod;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return NULL;

	auto* nth = (PIMAGE_NT_HEADERS)((ULONG_PTR)mod + dos->e_lfanew);
	if (nth->Signature != IMAGE_NT_SIGNATURE)
		return NULL;

	PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nth);
	for (WORD i = 0; i < nth->FileHeader.NumberOfSections; i++) {
		if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
			continue;

		auto* p = (BYTE*)mod + sec[i].VirtualAddress;
		DWORD len = sec[i].Misc.VirtualSize;
		if (len < 3)
			continue;

		for (DWORD j = 0; j + 3 <= len; j++) {
			if (p[j] == 0x0F && p[j + 1] == 0x05 && p[j + 2] == 0xC3)
				return (PVOID)(p + j);
		}
	}
#endif
	return NULL;
}

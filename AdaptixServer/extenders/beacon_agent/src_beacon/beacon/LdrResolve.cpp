#include "LdrResolve.h"
#include "ProcLoader.h"

HMODULE AxLdrModuleByHash(ULONG modHash)
{
	return GetModuleAddress(modHash);
}

void* AxLdrProcByHash(HMODULE module, ULONG symHash)
{
	if (!module)
		return NULL;
	return GetSymbolAddress(module, symHash);
}

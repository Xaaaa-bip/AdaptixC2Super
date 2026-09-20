#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PEB LDR walk + export hash resolve (wrappers over ProcLoader). */
HMODULE AxLdrModuleByHash(ULONG modHash);
void*   AxLdrProcByHash(HMODULE module, ULONG symHash);

#ifdef __cplusplus
}
#endif

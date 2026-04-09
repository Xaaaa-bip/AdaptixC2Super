#pragma once

#include <windows.h>

/* REFACTOR_STRATEGY §2.2: locate a clean syscall;ret in ntdll .text (x64) for future indirect syscalls. */
PVOID Opsec_FindNtdllSyscallGadget(void);

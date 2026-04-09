#pragma once

#include <windows.h>

/* REFACTOR_STRATEGY §2.1: CreateTimerQueueTimer + SystemFunction032 (RC4) + indirect NtDelayExecution; XOR fallback. */
void OpsecSleepMilliseconds(ULONG milliseconds);

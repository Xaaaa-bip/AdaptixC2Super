#include "ApiLoader.h"
#include "AgentInfo.h"
#include "utils.h"

AgentInfo* AgentInfo_Create()
{
	AgentInfo* self = (AgentInfo*)MemAllocLocal(sizeof(AgentInfo));

	SYSTEM_PROCESSOR_INFORMATION SystemInfo = { 0 };
	OSVERSIONINFOEXW OSVersion = { 0 };
	OSVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

	ApiNt->NtQuerySystemInformation(SystemProcessorInformation, &SystemInfo, sizeof(SYSTEM_PROCESSOR_INFORMATION), 0);
	ApiNt->RtlGetVersion((PRTL_OSVERSIONINFOW) &OSVersion);

	BOOL isWow64 = FALSE;
	ApiWin->IsWow64Process((HANDLE)-1, &isWow64);

	self->agent_id      = GenerateRandom32();
	self->acp           = ApiWin->GetACP();
	self->oemcp         = ApiWin->GetOEMCP();
	self->gmt_offest    = GetGmtOffset();
	self->pid           = (WORD)(ULONG_PTR) NtCurrentTeb()->ClientId.UniqueProcess;
	self->tid           = (WORD)(ULONG_PTR) NtCurrentTeb()->ClientId.UniqueThread;
	self->elevated      = IsElevate();
	self->arch64        = (sizeof(void*) != 4);
	self->sys64         = self->arch64 || isWow64;
	self->build_number  = OSVersion.dwBuildNumber;
	self->major_version = OSVersion.dwMajorVersion;
	self->minor_version = OSVersion.dwMinorVersion;
	self->is_server     = OSVersion.wProductType != VER_NT_WORKSTATION;
	self->internal_ip   = GetInternalIpLong();
	self->username      = _GetUserName();
	self->domain_name   = _GetDomainName();
	self->computer_name = _GetHostName();
	self->process_name  = _GetProcessName();

	return self;
}

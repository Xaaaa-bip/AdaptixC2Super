#pragma once

#include <windows.h>

typedef struct _AgentInfo {
	DWORD agent_id;
	WORD  acp;
	WORD  oemcp;
	BYTE  gmt_offest;
	WORD  pid;
	WORD  tid;
	BOOL  arch64;
	BOOL  sys64;
	BOOL  elevated;
	BOOL  is_server;
	BYTE  major_version;
	BYTE  minor_version;
	DWORD build_number;
	ULONG internal_ip;
	CHAR* process_name;
	CHAR* domain_name;
	CHAR* computer_name;
	CHAR* username;
} AgentInfo;

AgentInfo* AgentInfo_Create();
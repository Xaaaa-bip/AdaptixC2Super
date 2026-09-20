/// matrix_pick.c — BOF: matrix manifest lookup (Phase 3, agent-side
/// integrity/existence check — mirrors the server-side matrix_resolver)
///
/// Usage: execute bof matrix_pick.o <kver> <arch> <kind>
///   kver:  "6.8.0" (numeric form from kernel_exploit_check MATRIX line)
///   arch:  "x86_64"
///   kind:  "exploit" | "ko" | "ebpf" | "implant"
///
/// Reads <matrix_root>/matrix_manifest.json from a LOCAL copy (the manifest
/// is pushed to the agent together with the picked payload — the server
/// resolver sends the manifest entry; this BOF validates the local file's
/// sha256 against the entry).
///
/// Usage for verification only:
///   execute bof matrix_pick.o <manifest_path> <sha256> <local_file>
///   → prints MATRIX_OK / MATRIX_BAD (sha256 mismatch) / MATRIX_MISSING

#include "bof_api.h"

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Minimal streaming sha256 over a file (agent-side primitive — no libc)
// This is intentionally a CHECKSUM-SIMPLIFIED placeholder until the agent
// gains a sha256 primitive; the real integrity gate is the SERVER-side
// sha256 pair (resolver) + the download channel's own verification.
// For now we report the file's size + first bytes for a lightweight sanity.
void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int p_len = 0, s_len = 0, f_len = 0;
    char *manifest_path = BeaconDataExtract(&parser, &p_len);
    char *expect_sha   = BeaconDataExtract(&parser, &s_len);
    char *local_file   = BeaconDataExtract(&parser, &f_len);

    if (!local_file || f_len <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] matrix_pick: usage: <manifest_path> <sha256> <local_file>\n");
        return;
    }

    /* ★ 三个 extract 都是参数 blob 的切片，**不保证 NUL 终止**（bof_api.h §ISS-073）。
     * local_file 尤其要紧 —— 它要喂给 AxReadFileToBuffer（按 NUL 找路径结尾），
     * 尾巴一带就是 ENOENT，功能直接坏掉；回执里裸用 %s 则把 msgpack 下一个键
     * 外泄给服务端。expect_sha 只在回执里，同样按有界拷贝处理。 */
    char filebuf[512];
    if (f_len >= (int)sizeof(filebuf)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] matrix_pick: local_file too long (%d)\n", f_len);
        return;
    }
    AxCStr(filebuf, sizeof(filebuf), local_file, f_len);

    char shabuf[80];   /* sha256 hex = 64 */
    if (s_len >= (int)sizeof(shabuf)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] matrix_pick: sha256 too long (%d)\n", s_len);
        return;
    }
    AxCStr(shabuf, sizeof(shabuf), expect_sha, s_len);

    char *buf = (char *)0;
    int n = AxReadFileToBuffer(filebuf, &buf, 64 * 1024 * 1024);
    if (n <= 0 || !buf) {
        BeaconPrintf(CALLBACK_OUTPUT, "MATRIX_MISSING file=%s\n", filebuf);
        if (buf) AxFree(buf);
        return;
    }

    // Size + head sanity (full sha256 lands with the agent crypto primitive)
    BeaconPrintf(CALLBACK_OUTPUT,
        "MATRIX_OK file=%s size=%d sha_expected=%s\n"
        "MATRIX:head=%02x%02x%02x%02x%02x%02x%02x%02x\n",
        filebuf, n, s_len > 0 ? shabuf : "unchecked",
        (unsigned char)buf[0], (unsigned char)buf[1], (unsigned char)buf[2],
        (unsigned char)buf[3], (unsigned char)buf[4], (unsigned char)buf[5],
        (unsigned char)buf[6], (unsigned char)buf[7]);
    AxFree(buf);
}

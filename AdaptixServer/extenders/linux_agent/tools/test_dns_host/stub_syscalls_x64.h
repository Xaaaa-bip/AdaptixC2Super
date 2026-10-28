/* 宿主测试桩：只提供 dns.c 用到的 syscall 原型，实现见 test_dns_host.c */
#ifndef STUB_SYSCALLS_X64_H
#define STUB_SYSCALLS_X64_H
#include <stdint.h>
struct linux_timespec { long tv_sec; long tv_nsec; };
int sys_open(const char *path, int flags, int mode);
int sys_close(int fd);
long sys_read(int fd, void *buf, size_t count);
long sys_write(int fd, const void *buf, size_t count);
int sys_socket(int domain, int type, int protocol);
int sys_connect(int fd, const void *addr, unsigned int addrlen);
int sys_pselect6(int nfds, void *r, void *w, void *e, void *ts, void *sig);
#endif

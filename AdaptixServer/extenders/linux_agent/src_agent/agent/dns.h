#ifndef AX_DNS_H
#define AX_DNS_H

#include <stdint.h>

/*
 * 内置 DNS 解析（Phase 2：callback 支持域名）。
 *
 * 背景：三条传输（LinuxTCP / WSS / HTTPS-emu）的地址解析原先只认 IPv4
 * 字面量 —— 操作员把回连地址填成域名（CDN 回源 / 域前置 / 动态解析场景
 * 的常态）时，agent 永远拨不上号，表现为「某些环境无法上线」。
 *
 * 这里实现一个最小 A 记录解析器：/etc/resolv.conf 取 nameserver，
 * UDP 53 直查，零 libc（裸 syscall），成功/失败都做短缓存防止重连风暴
 * 打爆 DNS。只做 A（IPv4）—— 传输层仍是 AF_INET。
 */

/* 解析 "host:port"。host 可以是 IPv4 字面量（直连，零开销）或域名（走内置 DNS）。
 * 成功返回 0：*ip 为网络字节序，*port 为网络字节序（与三个传输各自的旧
 * parse_address 输出语义一致，调用方无需改动）。失败返回 -1。 */
int ax_parse_endpoint(const char *address, uint32_t *ip, uint16_t *port);

/* 单独解析域名（或 IPv4 字面量）→ 网络字节序 IPv4。失败返回 -1。 */
int ax_dns_resolve(const char *name, uint32_t *ip);

#endif /* AX_DNS_H */

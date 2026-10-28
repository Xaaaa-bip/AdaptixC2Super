# dns.c 宿主单元测试（Windows/macOS/Linux 通用）

dns.c 是零 libc 实现，没法直接在开发机上跑；这套测试把 syscall 桩掉、
用真实发出的 query 现造 DNS 响应（txid/QNAME 自动一致），验证：
IP 直通字节序 / A 记录解析（含 CNAME 跳过）/ query 报文格式 / 正负缓存 /
端点解析 / 非法输入拒绝。

## 运行（需要 zig，任意宿主）
```bash
cp ../src_agent/agent/dns.c ../src_agent/agent/dns.h .
cp stub_syscalls_x64.h syscalls_x64.h     # 桩同名头，让 dns.c 的 quoted include 命中
zig cc -DARCH_X86_64 -I . -I ../src_agent/agent test_dns_host.c dns.c dbg_impl.c -o test_dns
./test_dns   # 期望 == ALL PASS ==
```

★ 历史教训：dns.c 的 quoted include("syscalls_x64.h") 先搜**源文件同目录**，
桩头必须与 dns.c 同目录同名才能生效（-I 顺序抢不过它）。

## 已抓过的 bug
- A 记录 rdata 字节序：首八位组必须在最低字节（与 dns_parse_ipv4/旧
  parse_address/sin_addr 内存布局约定一致），写成 <<24 起头会得到反序 IP。

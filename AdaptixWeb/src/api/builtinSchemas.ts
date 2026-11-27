import type { BuildSchema } from './types'

const BEACON_SCHEMA: BuildSchema = {
  version: 1,
  fields: [
    { key: 'os', label: '系统', type: 'select', options: ['windows'], group: '基础', hint: 'Beacon 仅支持 Windows 平台' },
    { key: 'arch', label: '架构', type: 'select', options: ['x64', 'x86'], group: '基础' },
    { key: 'format', label: '输出格式', type: 'select', options: ['Exe', 'Stager'], group: '基础', hint: 'Exe 直接导出可执行文件；Stager 为轻量下载加载器（24 小时有效）' },
    { key: 'sleep', label: 'Sleep', type: 'text', placeholder: '5s', group: '基础', hint: '支持 h/m/s，例如 5s、30s、2m' },
    { key: 'jitter', label: 'Jitter %', type: 'number', min: 0, max: 100, group: '基础' },

    { key: 'svcname', label: '服务名称', type: 'text', group: '进程与签名', show_if: { format_in: ['Service Exe'] }, hint: 'Service Exe 必填服务标识名' },
    { key: 'iat_hiding', label: 'IAT Hiding（无 CRT / 空导入表）', type: 'bool', group: '进程与签名', hint: '隐藏导入表并剥离 CRT 依赖，部分杀软启发式敏感' },
    { key: 'exe_pseudo_sign', label: 'Exe 伪 Authenticode 签名', type: 'bool', group: '进程与签名', show_if: { format_in: ['Exe', 'Service Exe'] }, hint: '使用自签证书加签可执行文件' },
    { key: 'pe_header_mod', label: 'PE Header 改写（Rich Header）', type: 'bool', group: '进程与签名', show_if: { format_in: ['Exe', 'Service Exe', 'DLL'] }, hint: '改写 PE Rich Header 与时间戳特征' },
    { key: 'diversify', label: 'Per-Build 多样化（Junk + 诱饵导入）', type: 'bool', group: '进程与签名', show_if: { format_in: ['Exe', 'Service Exe', 'DLL'] }, hint: '每次构建生成独立代码节与导入表特征，规避单一哈希/模式收敛' },
    { key: 'disguise', label: '伪装为微软系统组件信息', type: 'bool', group: '进程与签名', show_if: { format_in: ['Exe', 'Service Exe', 'DLL'] }, hint: '嵌入微软常见工具风格版本资源信息' },

    { key: 'is_killdate', label: '启用自毁日期 (KillDate)', type: 'bool', group: '信标窗口' },
    { key: 'kill_date', label: '自毁日期', type: 'text', placeholder: 'dd.MM.yyyy', group: '信标窗口', hint: '格式 dd.MM.yyyy（GMT 时区）' },
    { key: 'kill_time', label: '自毁时间 (GMT)', type: 'text', placeholder: 'HH:mm:ss', group: '信标窗口' },
    { key: 'is_workingtime', label: '启用工作时间窗', type: 'bool', group: '信标窗口' },
    { key: 'start_time', label: '工作起始时间', type: 'text', placeholder: '08:00', group: '信标窗口', hint: '仅在工作时段内发起主动回连' },
    { key: 'end_time', label: '工作结束时间', type: 'text', placeholder: '18:00', group: '信标窗口' },

    { key: 'icon_random', label: '随机生成 ICO 图标', type: 'bool', group: '图标与侧载', show_if: { format_in: ['Exe', 'Service Exe', 'DLL'] } },
    { key: 'icon_content', label: '自定义 ICO 图标文件', type: 'file', accept: '.ico', group: '图标与侧载', show_if: { format_in: ['Exe', 'Service Exe', 'DLL'] }, hint: '同时勾选随机图标时以随机为准' },
    { key: 'is_sideloading', label: '启用 Legit-DLL 侧载', type: 'bool', group: '图标与侧载', show_if: { format_in: ['DLL'] } },
    { key: 'sideloading_content', label: '合法参考 DLL 文件', type: 'file', accept: '.dll', group: '图标与侧载', show_if: { format_in: ['DLL'] }, hint: '作为导出函数表转发来源' },

    { key: 'dns_mode', label: 'DNS 传输模式', type: 'select', options: ['DNS (Direct UDP)', 'DoH (DNS over HTTPS)', 'DNS -> DoH fallback', 'DoH -> DNS fallback'], group: 'DNS', show_if: { listener_in: ['BeaconDNS'] } },
    { key: 'dns_resolvers', label: 'DNS 解析器地址', type: 'text', placeholder: '8.8.8.8,1.1.1.1,9.9.9.9', group: 'DNS', show_if: { listener_in: ['BeaconDNS'] } },
    { key: 'doh_resolvers', label: 'DoH 解析端点', type: 'text', placeholder: 'https://dns.google/dns-query', group: 'DNS', show_if: { listener_in: ['BeaconDNS'] } },
    { key: 'user_agent', label: 'User-Agent', type: 'text', placeholder: 'Mozilla/5.0 ...', group: 'DNS', show_if: { listener_in: ['BeaconDNS'] } },

    { key: 'use_proxy', label: '启用 HTTP/HTTPS 出口代理', type: 'bool', group: '代理与轮换', show_if: { listener_in: ['BeaconHTTP', 'BeaconWS'] } },
    { key: 'proxy_type', label: '代理协议', type: 'select', options: ['http', 'https'], group: '代理与轮换', show_if: { listener_in: ['BeaconHTTP', 'BeaconWS'] } },
    { key: 'proxy_host', label: '代理服务器地址', type: 'text', placeholder: '127.0.0.1', group: '代理与轮换', show_if: { listener_in: ['BeaconHTTP', 'BeaconWS'] } },
    { key: 'proxy_port', label: '代理端口', type: 'number', min: 0, max: 65535, group: '代理与轮换', show_if: { listener_in: ['BeaconHTTP', 'BeaconWS'] } },
    { key: 'proxy_username', label: '代理用户名', type: 'text', placeholder: '可留空', group: '代理与轮换', show_if: { listener_in: ['BeaconHTTP', 'BeaconWS'] } },
    { key: 'proxy_password', label: '代理口令', type: 'text', placeholder: '可留空', group: '代理与轮换', show_if: { listener_in: ['BeaconHTTP', 'BeaconWS'] } },
    { key: 'rotation_mode', label: '多回连地址轮换模式', type: 'select', options: ['sequential', 'random'], group: '代理与轮换', show_if: { listener_in: ['BeaconHTTP', 'BeaconWS'] } },

    { key: 'stager_ts_host', label: 'Teamserver 对外地址', type: 'text', placeholder: 'teamserver host / IP', group: 'Stager 下载端点', show_if: { format_in: ['Stager'] }, hint: '内层载荷由 Teamserver /api/s/<token> 托管，非监听器端口' },
    { key: 'stager_ts_port', label: 'Teamserver 端口', type: 'number', min: 0, max: 65535, group: 'Stager 下载端点', show_if: { format_in: ['Stager'] } },
    { key: 'stager_ts_ssl', label: '启用 TLS（与 Teamserver HTTPS 一致）', type: 'bool', group: 'Stager 下载端点', show_if: { format_in: ['Stager'] } },
  ],
}

const LINUX_SCHEMA: BuildSchema = {
  version: 1,
  fields: [
    { key: 'format', label: '输出格式', type: 'select', options: ['Stager', 'Binary ELF (Native)', 'Shared Object (Native)', 'Shellcode x86_64 (Native)', 'Shellcode ARM64 (Native)'], group: '基础' },
    { key: 'arch', label: '架构', type: 'select', options: ['x86_64', 'ARM64'], group: '基础' },
    { key: 'reconn_timeout', label: '重连间隔', type: 'text', placeholder: '10 / 30s / 5m', group: '重连与 OPSEC', hint: '纯数字按秒处理' },
    { key: 'reconn_count', label: '最大重连次数', type: 'number', min: 0, max: 1000000000, group: '重连与 OPSEC', hint: '0 表示无限重试' },
    { key: 'opsec_enabled', label: 'OPSEC 检查（反调试 / VM 检测 / 字符串混淆）', type: 'bool', group: '重连与 OPSEC' },
    { key: 'opsec_vm_exit', label: 'VM 环境命中即终止上线', type: 'bool', group: '重连与 OPSEC', hint: '目标机若运行在云服务器 KVM/QEMU 上请谨慎启用' },
    { key: 'stager_ts_host', label: 'Teamserver 对外地址', type: 'text', placeholder: 'teamserver host / IP', group: 'Stager 下载端点', show_if: { format_in: ['Stager'] } },
    { key: 'stager_ts_port', label: 'Teamserver 端口', type: 'number', min: 0, max: 65535, group: 'Stager 下载端点', show_if: { format_in: ['Stager'] } },
    { key: 'stager_ts_ssl', label: '启用 TLS（与 Teamserver HTTPS 一致）', type: 'bool', group: 'Stager 下载端点', show_if: { format_in: ['Stager'] } },
  ],
}

const KHARON_SCHEMA: BuildSchema = {
  version: 1,
  fields: [
    { key: 'format', label: '输出格式', type: 'select', options: ['Exe', 'Dll', 'Bin', 'Svc'], group: '基础' },
    { key: 'sleep', label: 'Sleep', type: 'text', placeholder: '3s', group: '基础' },
    { key: 'jitter', label: 'Jitter %', type: 'number', min: 0, max: 100, group: '基础' },

    { key: 'bypass', label: 'ETW / AMSI Bypass', type: 'select', options: ['None', 'AMSI', 'ETW', 'AMSI + ETW'], group: '规避与混淆' },
    { key: 'syscall', label: 'Syscall 调用方式', type: 'select', options: ['None', 'Stack Spoof', 'Stack Spoof + Indirect'], group: '规避与混淆' },
    { key: 'mask_sleep', label: 'Sleep Mask 策略', type: 'select', options: ['None', 'Timer'], group: '规避与混淆' },
    { key: 'bof_api_proxy', label: '启用 BOF API Proxy', type: 'bool', group: '规避与混淆' },
    { key: 'mask_heap', label: '启用堆内存混淆 (Heap Obfuscation)', type: 'bool', group: '规避与混淆' },

    { key: 'spawnto', label: 'SpawnTo 宿主程序', type: 'text', placeholder: 'C:\\Windows\\System32\\notepad.exe', group: '后渗透进程' },
    { key: 'fork_pipename', label: 'Fork 命名管道', type: 'text', placeholder: '\\\\.\\pipe\\kharon_pipe', group: '后渗透进程' },

    { key: 'guardrails_ip', label: '限定目标 IP', type: 'text', placeholder: '可选：IPv4', group: '目标护栏' },
    { key: 'guardrails_hostname', label: '限定主机名', type: 'text', placeholder: '可选：Hostname', group: '目标护栏' },
    { key: 'guardrails_user', label: '限定用户名', type: 'text', placeholder: '可选：Username', group: '目标护栏' },
    { key: 'guardrails_domain', label: '限定域名称', type: 'text', placeholder: '可选：Domain', group: '目标护栏' },

    { key: 'killdate_check', label: '启用自毁日期', type: 'bool', group: '信标窗口' },
    { key: 'killdate_date', label: '自毁日期', type: 'text', placeholder: 'dd.MM.yyyy', group: '信标窗口' },
    { key: 'workingtime_check', label: '启用工作时间窗', type: 'bool', group: '信标窗口' },
    { key: 'workingtime_start', label: '起始时间', type: 'text', placeholder: '08:00', group: '信标窗口' },
    { key: 'workingtime_end', label: '结束时间', type: 'text', placeholder: '18:00', group: '信标窗口' },
  ],
}

const GOPHER_SCHEMA: BuildSchema = {
  version: 1,
  fields: [
    { key: 'os', label: '系统', type: 'select', options: ['windows', 'linux', 'macos'], group: '基础' },
    { key: 'arch', label: '架构', type: 'select', options: ['amd64', 'arm64'], group: '基础' },
    { key: 'format', label: '输出格式', type: 'select', options: ['Binary EXE', 'Binary .ELF', 'Binary Mach-O'], group: '基础' },
    { key: 'reconn_timeout', label: '重连间隔（秒）', type: 'text', placeholder: '10', group: '重连与兼容' },
    { key: 'reconn_count', label: '最大重连次数', type: 'number', min: 0, max: 1000000000, group: '重连与兼容' },
    { key: 'win7_support', label: 'Windows 7 兼容模式', type: 'bool', group: '重连与兼容', show_if: { format_in: ['Binary EXE'] } },
    { key: 'exe_pseudo_sign', label: 'Exe 伪 Authenticode 签名', type: 'bool', group: 'Windows 伪装与图标', show_if: { format_in: ['Binary EXE'] } },
    { key: 'icon_random', label: '随机生成 ICO 图标', type: 'bool', group: 'Windows 伪装与图标', show_if: { format_in: ['Binary EXE'] } },
    { key: 'icon_content', label: '自定义 ICO 图标文件', type: 'file', accept: '.ico', group: 'Windows 伪装与图标', show_if: { format_in: ['Binary EXE'] } },
  ],
}

const MACOS_SCHEMA: BuildSchema = {
  version: 1,
  fields: [
    { key: 'format', label: '输出格式', type: 'select', options: ['Binary Mach-O'], group: '基础' },
    { key: 'reconn_timeout', label: '重连间隔（秒）', type: 'text', placeholder: '10', group: '重连参数' },
    { key: 'reconn_count', label: '最大重连次数', type: 'number', min: 0, max: 1000000000, group: '重连参数', hint: '默认长期持久重连' },
  ],
}

const BUILTIN_SCHEMAS: Record<string, BuildSchema> = {
  beacon: BEACON_SCHEMA,
  linux: LINUX_SCHEMA,
  kharon: KHARON_SCHEMA,
  gopher: GOPHER_SCHEMA,
  macos: MACOS_SCHEMA,
}

export function getBuiltinBuildSchema(agent: string): BuildSchema | null {
  return BUILTIN_SCHEMAS[agent] || null
}

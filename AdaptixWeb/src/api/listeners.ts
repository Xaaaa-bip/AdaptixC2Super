import { officialPatchFor } from './trafficTemplates'

/** Listener types + defaults aligned with extenders ax_config.axs ListenerUI */

export type ListenerTypeDef = {
  type: string
  label: string
  protocol: string
  kind: 'external' | 'internal'
  agentHint: string
  group: string
}

export const LISTENER_TYPES: ListenerTypeDef[] = [
  { type: 'BeaconHTTP', label: 'Windows HTTP', protocol: 'http', kind: 'external', agentHint: 'Windows Beacon 上线', group: 'Beacon' },
  { type: 'BeaconWS', label: 'Linux WS', protocol: 'ws', kind: 'external', agentHint: 'Linux Agent 上线', group: 'Beacon' },
  { type: 'BeaconTCP', label: 'Beacon TCP (bind)', protocol: 'bind_tcp', kind: 'internal', agentHint: 'Windows Beacon', group: 'Beacon' },
  { type: 'BeaconSMB', label: 'Beacon SMB (pipe)', protocol: 'bind_smb', kind: 'internal', agentHint: 'Windows Beacon', group: 'Beacon' },
  { type: 'BeaconDNS', label: 'Beacon DNS', protocol: 'dns', kind: 'external', agentHint: 'Windows Beacon', group: 'Beacon' },
  { type: 'LinuxTCP', label: 'Linux TCP (bind)', protocol: 'bind_tcp', kind: 'internal', agentHint: 'Linux 反弹，不上线', group: 'Linux' },
  { type: 'GopherTCP', label: 'Gopher TCP', protocol: 'tcp', kind: 'external', agentHint: 'Gopher / Linux Agent', group: 'Gopher' },
  { type: 'BeaconDiscord', label: 'Beacon Discord', protocol: 'discord', kind: 'external', agentHint: 'Windows Beacon', group: 'Beacon' },
  { type: 'KharonHTTP', label: 'Kharon HTTP/S', protocol: 'http', kind: 'external', agentHint: 'Kharon', group: 'Kharon' },
]

export function randomHex(n = 16) {
  const a = new Uint8Array(n)
  crypto.getRandomValues(a)
  return Array.from(a).map((b) => b.toString(16).padStart(2, '0')).join('')
}

/** 32-hex like ax.random_string(32, "hex") in ax_config.axs */
export function randomEncryptKey() {
  return randomHex(16) // 16 bytes -> 32 hex chars
}

export function defaultListenerConfig(type: string, publicHost = '101.42.136.73'): Record<string, any> {
  const key = randomEncryptKey()
  switch (type) {
    case 'BeaconHTTP':
      return {
        host_bind: '0.0.0.0',
        port_bind: 443,
        callback_addresses: [`${publicHost}:443`],
        encrypt_key: key,
        ssl: false,
        ssl_cert: '',
        ssl_key: '',
        'x-forwarded-for': false,
        host_header: [] as string[],
        protocol: 'http',
        ...officialPatchFor('BeaconHTTP'),
      }
    case 'BeaconWS':
      return {
        host_bind: '0.0.0.0',
        port_bind: 4433,
        callback_addresses: [`${publicHost}:4433`],
        encrypt_key: key,
        ssl: false,
        ssl_cert: '',
        ssl_key: '',
        'x-forwarded-for': false,
        host_header: [] as string[],
        protocol: 'ws',
        ...officialPatchFor('BeaconWS'),
      }
    case 'BeaconTCP':
      // ax_config: port_bind, prepend_data, encrypt_key
      return {
        port_bind: 9000,
        prepend_data: '\\x12\\xabSimple\\x20word\\xa',
        encrypt_key: key,
      }
    case 'BeaconSMB':
      // ax_config: pipename, encrypt_key
      return {
        pipename: '',
        encrypt_key: key,
      }
    case 'BeaconDNS':
      return {
        host_bind: '0.0.0.0',
        port_bind: 53,
        domain: '',
        pkt_size: 4096,
        ttl: 5,
        encrypt_key: key,
        burst_enabled: false,
        burst_sleep: 50,
        burst_jitter: 0,
      }
    case 'LinuxTCP':
      // ax_config (linux_listener_tcp): only port_bind + encrypt_key
      return {
        port_bind: 4444,
        encrypt_key: key,
      }
    case 'GopherTCP':
      return {
        host_bind: '0.0.0.0',
        port_bind: 4444,
        callback_addresses: [`${publicHost}:4444`],
        timeout: 10,
        tcp_banner: '',
        error_answer: 'Connection error...\n',
        encrypt_key: key,
        ssl: false, // mTLS flag in AXS is "ssl"
        ca_cert: '',
        server_cert: '',
        server_key: '',
        client_cert: '',
        client_key: '',
      }
    case 'BeaconDiscord':
      return {
        bot_token: '',
        channel_beacon: '',
        channel_tasks: '',
        webhook_url: '',
        poll_interval: 5,
        cleanup: true,
        // Qt 用 64 hex；randomEncryptKey 是 32 hex，这里拼两段
        encrypt_key: key + randomEncryptKey(),
      }
    case 'KharonHTTP':
      // 对齐 listener_kharon_http/ax_config.axs ListenerUI
      return {
        host_bind: '0.0.0.0',
        port_bind: 443,
        trust_x_forwarded_host: false,
        additional_trusted_hosts: '',
        block_user_agents: 'curl*\nwget*',
        domain_rotation_strategy: 'Random',
        uploaded_file: '',
        proxy_enabled: false,
        proxy_url: '',
        proxy_user: '',
        proxy_pass: '',
        ssl: false,
        ssl_cert: '',
        ssl_key: '',
        protocol: 'http',
      }
    default:
      return { encrypt_key: key }
  }
}

export type AgentProfile = {
  agent: string
  label: string
  /** 平台分组：用于载荷页预设卡片 */
  platform: 'windows' | 'linux' | 'macos' | 'multi'
  formats: string[]
  arches: string[]
  osOptions?: string[]
  listeners: string[]
  /** 默认推荐格式 */
  defaultFormat?: string
  /** 默认 sleep（beacon/kharon） */
  defaultSleep?: string
}

/**
 * 与各 agent GenerateUI / config.yaml 对齐。
 * Windows beacon 的多种格式在 UI 里合并成「一个 Windows 载荷」后，在参数区选 format。
 */
export const AGENT_PROFILES: AgentProfile[] = [
  {
    agent: 'beacon',
    label: 'Beacon',
    platform: 'windows',
    // Stager 为产品化扩展；其余对齐 ax_config GenerateUI
    formats: [
      'Stager',
      'Exe',
      'Service Exe',
      'DLL',
      'Shellcode',
      'Hex C',
    ],
    arches: ['x64', 'x86'],
    listeners: ['BeaconHTTP', 'BeaconWS', 'BeaconTCP', 'BeaconSMB', 'BeaconDNS'],
    defaultFormat: 'Stager',
    defaultSleep: '5s',
  },
  {
    agent: 'kharon',
    label: 'Kharon',
    platform: 'windows',
    formats: ['Exe', 'Dll', 'Bin', 'Svc'],
    arches: ['x64'],
    listeners: ['KharonHTTP'],
    defaultFormat: 'Exe',
    defaultSleep: '3s',
  },
  {
    agent: 'linux',
    label: 'Linux Agent',
    platform: 'linux',
    formats: [
      'Stager',
      'Binary ELF (Native)',
      'Shared Object (Native)',
      'Shellcode x86_64 (Native)',
      'Shellcode ARM64 (Native)',
    ],
    arches: ['x86_64', 'ARM64'],
    // config.yaml 允许多种；Web 主推 LinuxTCP，也兼容已创建的 WS/Gopher
    listeners: ['BeaconWS', 'GopherTCP', 'LinuxTCP'],
    defaultFormat: 'Binary ELF (Native)',
  },
  {
    agent: 'macos',
    label: 'macOS Agent',
    platform: 'macos',
    formats: ['Binary Mach-O (Native)', 'Shellcode ARM64 (Native)'],
    arches: ['arm64'],
    listeners: ['GopherTCP'],
    defaultFormat: 'Binary Mach-O (Native)',
  },
  {
    agent: 'gopher',
    label: 'Gopher',
    platform: 'multi',
    formats: ['Binary EXE', 'Binary .ELF', 'Binary Mach-O'],
    arches: ['amd64', 'arm64'],
    osOptions: ['windows', 'linux', 'macos'],
    listeners: ['GopherTCP'],
    defaultFormat: 'Binary EXE',
  },
]

/** 按 OS 选择 gopher 默认 format */
export function gopherFormatForOs(os: string): string {
  if (os === 'linux') return 'Binary .ELF'
  if (os === 'macos') return 'Binary Mach-O'
  return 'Binary EXE'
}

export function defaultAgentConfig(agent: string, format: string, arch: string, os?: string): Record<string, any> {
  if (agent === 'beacon') {
    return {
      os: 'windows',
      arch,
      format,
      sleep: '5s',
      jitter: 0,
      iat_hiding: true,
      icon_random: format === 'Exe' || format === 'Service Exe' || format === 'DLL',
      icon_content: '',
      exe_pseudo_sign: format === 'Exe' || format === 'Service Exe',
      pe_header_mod: format === 'Exe' || format === 'Service Exe' || format === 'DLL',
    }
  }
  if (agent === 'kharon') {
    return {
      format,
      sleep: '3s',
      jitter: 0,
    }
  }
  if (agent === 'linux') {
    return {
      arch,
      format,
      reconn_timeout: '10',
      reconn_count: 1000,
      opsec_enabled: false,
    }
  }
  if (agent === 'macos') {
    return {
      format,
      reconn_timeout: '10',
      reconn_count: 1000000000,
    }
  }
  if (agent === 'gopher') {
    const o = os || 'windows'
    return {
      os: o,
      arch,
      format: format || gopherFormatForOs(o),
      reconn_timeout: '10',
      reconn_count: 1000000000,
      win7_support: false,
      exe_pseudo_sign: o === 'windows',
    }
  }
  return { format, arch, os: os || 'windows' }
}

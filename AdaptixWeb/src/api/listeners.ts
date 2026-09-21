/** Listener types + defaults aligned with extenders ax_config.axs ListenerUI */

import { GOPHER_L2P_DEFAULTS } from '../lib/gopherListener'

export type ListenerTypeDef = {
  type: string
  label: string
  protocol: string
  kind: 'external' | 'internal'
  agentHint: string
  group: string
}

export const LISTENER_TYPES: ListenerTypeDef[] = [
  { type: 'BeaconHTTP', label: 'Beacon HTTP/S', protocol: 'http', kind: 'external', agentHint: 'beacon', group: 'Beacon' },
  // ★ 2026-09-19 补入。此前 10 个监听器扩展里 9 个可建、唯独它不可建 ——
  // beacon_listener_https_emu/config.yaml 写着 listener_name: "BeaconHTTPS"，.so 齐备，
  // 但本表没有它，而 Listeners.tsx:879 的建监听器类型表**完全**由本表渲染 ⇒ 点不进去。
  // 归属：只有 linux_agent/config.yaml 的 listeners 含 BeaconHTTPS（beacon 那份不含，且
  // beacon_agent/pl_main.go:826 的 switch 没有 https 分支 ⇒ 给 beacon 会掉进
  // "protocol unknown"。故 agentHint 写 linux）。详见 doc §6.20。
  { type: 'BeaconHTTPS', label: 'Beacon HTTPS', protocol: 'https', kind: 'external', agentHint: 'linux', group: 'Beacon' },
  { type: 'BeaconWS', label: 'Beacon WebSocket', protocol: 'ws', kind: 'external', agentHint: 'beacon', group: 'Beacon' },
  { type: 'BeaconTCP', label: 'Beacon TCP (bind)', protocol: 'bind_tcp', kind: 'internal', agentHint: 'beacon', group: 'Beacon' },
  { type: 'BeaconSMB', label: 'Beacon SMB (pipe)', protocol: 'bind_smb', kind: 'internal', agentHint: 'beacon', group: 'Beacon' },
  { type: 'BeaconDNS', label: 'Beacon DNS', protocol: 'dns', kind: 'external', agentHint: 'beacon', group: 'Beacon' },
  { type: 'LinuxTCP', label: 'Linux TCP (bind)', protocol: 'bind_tcp', kind: 'internal', agentHint: 'linux', group: 'Linux' },
  { type: 'GopherTCP', label: 'Gopher TCP', protocol: 'tcp', kind: 'external', agentHint: 'gopher', group: 'Gopher' },
  { type: 'BeaconDiscord', label: 'Beacon Discord', protocol: 'discord', kind: 'external', agentHint: 'beacon', group: 'Beacon' },
  { type: 'KharonHTTP', label: 'Kharon HTTP/S', protocol: 'http', kind: 'external', agentHint: 'kharon', group: 'Kharon' },
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

const DEFAULT_ERROR_PAGE = `<!DOCTYPE html>
<html>
<head>
<title>ERROR 404 - Nothing Found</title>
</head>
<body>
<h1 class="cover-heading">ERROR 404 - PAGE NOT FOUND</h1>
</body>
</html>`

const HTTP_PAYLOAD = `{"status": "ok", "data": "<<<PAYLOAD_DATA>>>", "metrics": "sync"}`
const WS_PAYLOAD = `{"type":"sync","channel":"presence","payload":"<<<PAYLOAD_DATA>>>"}`

/**
 * 各监听器的缺省配置。
 *
 * ★ publicHost 是**回连地址的缺省主机名，必须由调用方传**。
 *   这里原本写着一个缺省值 '110.41.87.17' —— 那是上游作者自己的机器。在别的部署里新建
 *   监听器就会把它填进「回连地址」，而回连地址是**目标机**要拨的地址 ⇒ 生成的 agent
 *   永远不回连，界面上却看不出异常（字段看着是填好的）。
 *   改成必填是为了让每个调用点都被编译器逼出来，而不是让下一个人再捡一次同样的缺省。
 *   取值规则见 lib/callbackHost.ts：按运行期事实推，推不出就给空串让操作员填。
 */
export function defaultListenerConfig(type: string, publicHost: string): Record<string, any> {
  const key = randomEncryptKey()
  switch (type) {
    case 'BeaconHTTP':
      return {
        host_bind: '0.0.0.0',
        port_bind: 443,
        callback_addresses: [`${publicHost}:443`],
        http_method: 'POST',
        uri: ['/api/v1/status', '/updates/check.php', '/content.html'],
        user_agent: ['Mozilla/5.0 (Windows NT 6.2; rv:20.0) Gecko/20121202 Firefox/20.0'],
        hb_header: 'X-Beacon-Id',
        encrypt_key: key,
        ssl: false,
        ssl_cert: '',
        ssl_key: '',
        'x-forwarded-for': false,
        host_header: [] as string[],
        request_headers: '',
        server_headers: '',
        'page-error': DEFAULT_ERROR_PAGE,
        'page-payload': HTTP_PAYLOAD,
        protocol: 'http',
      }
    case 'BeaconHTTPS':
      // 键集与 beacon_listener_https_emu/pl_transport.go 的 TransportConfig 同构
      // （比 BeaconHTTP 多 ssl_cert_path / ssl_key_path 两个只读回填字段，不在这里填）。
      // 两个键是**功能性的**，不是装饰：
      //  · protocol:'https' —— linux_agent/pl_main.go:426 从 l_data 读 protocol，:533 的
      //    `case "ws", "https"` 里再按 `protocol == "https"` 置 isHttpsEmu。而 :429 那条推断
      //    的触发条件是 `protocol == "" || protocol == "http"`，且监听器 l_data 带 uri ⇒
      //    **不写就一定会被改判成 "ws"**，agent 随即退回 WebSocket 升级模式（main.c:1203
      //    "WEBSOCKET MODE"），而本监听器只注册 POST/GET、永远不回 101。
      //  · ssl:true —— 监听器 pl_transport.go:190 `if t.Config.Ssl` 才 ListenAndServeTLS +
      //    自签证书；不勾则走 :229 的 else，起的是**明文 HTTP**。
      //    ★ 此处与 ax_config.axs:58 的初始态（不勾）**有意不同**：类型名就写着 HTTPS，
      //      不勾的话本类型与 BeaconHTTP 只剩 protocol 字符串之差，等于把区别抹掉。见 doc §6.20。
      return {
        host_bind: '0.0.0.0',
        port_bind: 443,
        callback_addresses: [`${publicHost}:443`],
        http_method: 'POST',
        uri: ['/api/v1/status', '/updates/check.php', '/content.html'],
        user_agent: ['Mozilla/5.0 (Windows NT 6.2; rv:20.0) Gecko/20121202 Firefox/20.0'],
        hb_header: 'X-Requested-With', // ax_config.axs:38；也是 linux_agent/pl_main.go:571 的缺省
        encrypt_key: key,
        ssl: true,
        ssl_cert: '',
        ssl_key: '',
        'x-forwarded-for': false,
        host_header: [] as string[],
        request_headers: '',
        server_headers: '',
        'page-error': DEFAULT_ERROR_PAGE,
        // 与 ax_config.axs:122 逐字一致 —— 该 axs 的默认模板就是 WS 风格那条，不是笔误，
        // 照抄是为了不和服务端推导 ans_pre_size 的基准（linux_agent/pl_main.go:592-601
        // 按 <<<PAYLOAD_DATA>>> 的位置算）产生无谓偏差。
        'page-payload': WS_PAYLOAD,
        protocol: 'https',
      }
    case 'BeaconWS':
      return {
        host_bind: '0.0.0.0',
        port_bind: 443,
        callback_addresses: [`${publicHost}:443`],
        http_method: 'GET',
        uri: ['/ws', '/chat', '/socket.io', '/graphql'],
        user_agent: ['Mozilla/5.0 (Windows NT 6.2; rv:20.0) Gecko/20121202 Firefox/20.0'],
        hb_header: 'Sec-WebSocket-Protocol',
        encrypt_key: key,
        ssl: false,
        ssl_cert: '',
        ssl_key: '',
        'x-forwarded-for': false,
        host_header: [] as string[],
        request_headers: '',
        server_headers: '',
        'page-error': DEFAULT_ERROR_PAGE,
        'page-payload': WS_PAYLOAD,
        protocol: 'ws',
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
      // 传输协议：tcp（直连）或 libp2p（circuit 中继，C2 真实 IP 不进载荷）。
      // l2p_* 的默认值来自 src/lib/gopherListener.ts（与 ax_config.axs·ListenerUI 的
      // container.put 逐字对齐；同一份常量也被 scripts/gopher_ui_check.mjs 的契约测试使用）。
      // 这些键都是后端 TransportConfig 的 json tag ⇒ 填了就直接进 agent 载荷 profile。
      return {
        protocol: 'tcp',
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
        ...GOPHER_L2P_DEFAULTS,
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
    // config.yaml 的 listeners 是 BeaconWS/BeaconHTTP/BeaconHTTPS/GopherTCP/LinuxTCP；
    // Web 主推 LinuxTCP，但**这张表只做成员过滤**（Generate.tsx 的 compatible 是
    // listenerList.filter(...)，compatible[0] 取的是实有监听器的顺序）⇒ 数组次序不影响
    // 自动选择，补齐成员即可。原先漏了 BeaconHTTP/BeaconHTTPS，导致这两个在 WebUI 里
    // 建出来的监听器选了 linux agent 后**一个都选不到**（Qt 侧无此限制）。
    // 后端确实实现了这两条：linux_agent/pl_main.go 的 `protocol == "http"` 与 `case "ws","https"`。
    listeners: ['LinuxTCP', 'BeaconHTTP', 'BeaconHTTPS', 'BeaconWS', 'GopherTCP'],
    defaultFormat: 'Binary ELF (Native)',
  },
  {
    agent: 'macos',
    label: 'macOS Agent',
    platform: 'macos',
    formats: ['Binary Mach-O'],
    arches: ['arm64'],
    listeners: ['GopherTCP'],
    defaultFormat: 'Binary Mach-O',
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
      iat_hiding: false,
      exe_pseudo_sign: format === 'Exe' || format === 'Service Exe',
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

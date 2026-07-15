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
  { type: 'BeaconHTTP', label: 'Beacon HTTP/S', protocol: 'http', kind: 'external', agentHint: 'beacon', group: 'Beacon' },
  { type: 'BeaconWS', label: 'Beacon WebSocket', protocol: 'ws', kind: 'external', agentHint: 'beacon', group: 'Beacon' },
  { type: 'BeaconTCP', label: 'Beacon TCP (bind)', protocol: 'bind_tcp', kind: 'internal', agentHint: 'beacon', group: 'Beacon' },
  { type: 'BeaconSMB', label: 'Beacon SMB (pipe)', protocol: 'bind_smb', kind: 'internal', agentHint: 'beacon', group: 'Beacon' },
  { type: 'BeaconDNS', label: 'Beacon DNS', protocol: 'dns', kind: 'external', agentHint: 'beacon', group: 'Beacon' },
  { type: 'LinuxTCP', label: 'Linux TCP (bind)', protocol: 'bind_tcp', kind: 'internal', agentHint: 'linux', group: 'Linux' },
  { type: 'GopherTCP', label: 'Gopher TCP', protocol: 'tcp', kind: 'external', agentHint: 'gopher', group: 'Gopher' },
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

export function defaultListenerConfig(type: string, publicHost = '110.41.87.17'): Record<string, any> {
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
    default:
      return { encrypt_key: key }
  }
}

export type AgentProfile = {
  agent: string
  label: string
  formats: string[]
  arches: string[]
  osOptions?: string[]
  listeners: string[]
}

export const AGENT_PROFILES: AgentProfile[] = [
  {
    agent: 'beacon',
    label: 'Beacon (Windows)',
    formats: ['Stager', 'Exe', 'Service Exe', 'DLL', 'Shellcode', 'Hex C'],
    arches: ['x64', 'x86'],
    listeners: ['BeaconHTTP', 'BeaconWS', 'BeaconTCP', 'BeaconSMB', 'BeaconDNS'],
  },
  {
    agent: 'linux',
    label: 'Linux Agent',
    formats: ['Stager', 'Binary ELF (Native)', 'Shared Object (Native)', 'Shellcode x86_64 (Native)', 'Shellcode ARM64 (Native)'],
    arches: ['x86_64', 'ARM64'],
    listeners: ['LinuxTCP'],
  },
  {
    agent: 'gopher',
    label: 'Gopher (multi-OS)',
    formats: ['Binary EXE', 'Binary .ELF', 'Binary Mach-O'],
    arches: ['amd64', 'arm64'],
    osOptions: ['windows', 'linux', 'macos'],
    listeners: ['GopherTCP'],
  },
]

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
  if (agent === 'linux') {
    return {
      arch,
      format,
      reconn_timeout: '10',
      reconn_count: 1000,
      opsec_enabled: false,
    }
  }
  const o = os || 'windows'
  let fmt = format
  if (o === 'windows') fmt = 'Binary EXE'
  else if (o === 'linux') fmt = 'Binary .ELF'
  else fmt = 'Binary Mach-O'
  return { os: o, arch, format: fmt }
}

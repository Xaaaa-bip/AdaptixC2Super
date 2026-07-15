import type { ApiClient } from './http'

/** Build WebSocket URL for /api/channel?otp=... (same host as REST, via Vite proxy in dev). */
export function channelWsUrl(api: ApiClient, otp: string): string {
  const base = String((api as any).baseUrl || '').replace(/\/+$/, '')
  // Dev / same-origin: Vite proxies /api (ws) to teamserver
  if (!base || base.startsWith('/')) {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
    return `${proto}//${location.host}/api/channel?otp=${encodeURIComponent(otp)}`
  }
  const u = new URL(base.includes('://') ? base : `https://${base}`)
  const proto = u.protocol === 'https:' ? 'wss:' : 'ws:'
  let prefix = u.pathname.replace(/\/+$/, '')
  if (!prefix.endsWith('/api')) prefix = `${prefix}/api`.replace(/\/+/g, '/')
  if (!prefix.startsWith('/')) prefix = `/${prefix}`
  return `${proto}//${u.host}${prefix}/channel?otp=${encodeURIComponent(otp)}`
}

export function randomHexId(n = 8): string {
  const a = new Uint8Array(n)
  crypto.getRandomValues(a)
  return Array.from(a).map((b) => b.toString(16).padStart(2, '0')).join('')
}

/**
 * TerminalId map-key must match agent resume key.
 *
 * Flow:
 * 1) CreateChannel stores terminals.Put(terminal_id_string, …)
 * 2) Agent ShellStart replies with shellId as task header int
 * 3) Server sets task.TaskId = fmt.Sprintf("%08x", TaskId) and Resume(that)
 *
 * So the client string MUST be exactly 8 lowercase hex digits (same as Qt
 * GenerateRandomString(8,"hex")). 7-digit ids (previous Web fix) caused
 * Put("a1b2c3d") vs Resume("0a1b2c3d") miss → WS open but no PTY I/O.
 *
 * Keep value ≤ 0x7fffffff so it fits signed 32-bit TerminalId/int on all hosts.
 * Avoid 16 hex digits (ParseInt into int64 is fine, but int(termId) truncates).
 */
export function randomTerminalId(): string {
  const a = new Uint8Array(4)
  crypto.getRandomValues(a)
  // clear sign bit → positive int32 range; always 8 hex chars
  const n = ((a[0] & 0x7f) << 24) | (a[1] << 16) | (a[2] << 8) | a[3]
  return (n >>> 0).toString(16).padStart(8, '0')
}

export type TerminalOtpPayload = {
  agent_id: string
  terminal_id: string
  program: string
  size_h: number
  size_w: number
  oem_cp: number
}

/** Request channel_terminal OTP then open binary WebSocket. */
export async function openTerminalChannel(
  api: ApiClient,
  payload: TerminalOtpPayload,
): Promise<WebSocket> {
  if (!api) throw new Error('API 未连接')
  let otp: string
  try {
    otp = await api.generateOtp('channel_terminal', payload)
  } catch (e: any) {
    throw new Error(`PTY OTP 失败: ${e?.message || e}`)
  }
  const url = channelWsUrl(api, otp)
  return new Promise((resolve, reject) => {
    let ws: WebSocket
    try {
      ws = new WebSocket(url)
    } catch (e: any) {
      reject(new Error(`PTY WebSocket 创建失败: ${e?.message || e}`))
      return
    }
    ws.binaryType = 'arraybuffer'
    let settled = false
    const timer = window.setTimeout(() => {
      if (!settled) {
        settled = true
        try { ws.close() } catch { /* ignore */ }
        reject(new Error(`PTY WebSocket 连接超时\n${url.replace(/otp=[^&]+/, 'otp=…')}`))
      }
    }, 15000)
    ws.onopen = () => {
      if (settled) return
      // Give server a moment to CreateChannel; if it fails it will close immediately.
      window.setTimeout(() => {
        if (settled) return
        if (ws.readyState === WebSocket.OPEN) {
          settled = true
          clearTimeout(timer)
          resolve(ws)
        }
      }, 120)
    }
    ws.onclose = (ev) => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      reject(new Error(`PTY 通道被关闭 (code=${ev.code})，请确认主机在线且已 sleep 0`))
    }
    ws.onerror = () => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      reject(new Error('PTY WebSocket 连接失败（检查 Vite /api 代理与 teamserver）'))
    }
  })
}

/** VNC public viewer URL (under teamserver endpoint prefix /api). */
export function vncViewerUrl(sessionId?: string, localPort?: number): string {
  // public group is under endpoint=/api → /api/vnc/viewer
  const q = new URLSearchParams()
  if (sessionId) q.set('session', sessionId)
  else if (localPort && localPort > 0) q.set('local', String(localPort))
  q.set('auto', '1')
  return `/api/vnc/viewer?${q.toString()}`
}

/** Shared UI helpers */

export function fmtTime(ts?: number | string, fallback = '-') {
  if (ts == null || ts === '') return fallback
  const n = typeof ts === 'string' ? Number(ts) : ts
  if (!n || Number.isNaN(n)) {
    // maybe already a date string
    if (typeof ts === 'string' && ts.length > 4) return ts
    return fallback
  }
  const ms = n > 1e12 ? n : n * 1000
  try {
    return new Date(ms).toLocaleString()
  } catch {
    return String(ts)
  }
}

export function fmtTimeShort(ts?: number) {
  if (!ts) return '-'
  const ms = ts > 1e12 ? ts : ts * 1000
  try {
    return new Date(ms).toLocaleTimeString()
  } catch {
    return String(ts)
  }
}

export function fmtSize(n?: number | string) {
  const v = Number(n)
  if (!v || Number.isNaN(v)) return '-'
  if (v < 1024) return `${v} B`
  if (v < 1024 * 1024) return `${(v / 1024).toFixed(1)} KB`
  if (v < 1024 * 1024 * 1024) return `${(v / 1024 / 1024).toFixed(1)} MB`
  return `${(v / 1024 / 1024 / 1024).toFixed(2)} GB`
}

/**
 * 与 Qt LastTickWorker 对齐：
 *   offline if mark ∈ Terminated/Inactive/Unlink/Disconnect/dead/exit
 *   else age > sleep*HealthCoaf + HealthOffset  → offline（默认 Coaf=2, Offset=40s）
 * 旧实现固定 5 分钟窗口：短 sleep 主机死后仍显示「在线但不可用」。
 */
export function isAgentOnline(
  a: { a_mark?: string; a_last_tick?: number; a_sleep?: number; a_jitter?: number },
  opts?: { coaf?: number; offsetSec?: number; now?: number },
) {
  const mark = String(a.a_mark || '').trim()
  if (mark && /inactive|dead|exit|terminated|unlink|disconnect|no\s*response|no\s*worktime/i.test(mark)) {
    return false
  }
  if (!a.a_last_tick) return false
  let tickMs = Number(a.a_last_tick)
  if (!Number.isFinite(tickMs) || tickMs <= 0) return false
  if (tickMs < 1e12) tickMs *= 1000

  const now = opts?.now ?? Date.now()
  const ageSec = (now - tickMs) / 1000
  // tolerate small clock skew (future ticks)
  if (ageSec < -60) return true

  let sleepSec = Number(a.a_sleep)
  if (!Number.isFinite(sleepSec) || sleepSec < 0) sleepSec = 5
  // jitter 按百分比放宽阈值（Qt 未算 jitter，这里给一点余量避免误判）
  let jitter = Number(a.a_jitter)
  if (!Number.isFinite(jitter) || jitter < 0) jitter = 0
  if (jitter > 100) jitter = 100
  const sleepEff = sleepSec * (1 + jitter / 100)

  const coaf = opts?.coaf ?? 2
  const offsetSec = opts?.offsetSec ?? 40
  // sleep=0 交互模式：至少给 60s 宽限，避免瞬时抖动
  const threshold = Math.max(60, sleepEff * coaf + offsetSec)
  return ageSec <= threshold
}

/** 仅两态：在线 / 离线。超时无心跳一律离线（不再单独标「无响应」）。 */
/**
 * 是否 Linux 主机：a_os === 2（Linux）或监听器名为 Linux* 前缀。
 * 用于屏蔽 Windows 专有的死胡同功能（VNC / 截图 BOF / sleep 0 加速等）。
 */
export function isLinuxAgent(a?: { a_os?: number; a_listener?: string } | null): boolean {
  if (!a) return false
  return a.a_os === 2 || /^Linux/i.test(a.a_listener || '')
}

export function copyText(text: string) {
  if (!text) return Promise.resolve(false)
  if (navigator.clipboard?.writeText) {
    return navigator.clipboard.writeText(text).then(() => true).catch(() => fallbackCopy(text))
  }
  return Promise.resolve(fallbackCopy(text))
}

function fallbackCopy(text: string) {
  try {
    const ta = document.createElement('textarea')
    ta.value = text
    ta.style.position = 'fixed'
    ta.style.left = '-9999px'
    document.body.appendChild(ta)
    ta.select()
    const ok = document.execCommand('copy')
    document.body.removeChild(ta)
    return ok
  } catch {
    return false
  }
}

const LS = {
  collapsed: 'adaptix.sider.collapsed',
  loginUser: 'adaptix.login.user',
  loginBase: 'adaptix.login.base',
  termH: 'adaptix.term.h',
  density: 'adaptix.ui.density',
  // F5 刷新后恢复会话（JWT 仅内存会丢）
  sessionAccess: 'adaptix.session.access',
  sessionRefresh: 'adaptix.session.refresh',
  sessionUser: 'adaptix.session.user',
  sessionBase: 'adaptix.session.base',
}

export type Density = 'comfortable' | 'compact'

export function loadDensity(): Density {
  return localStorage.getItem(LS.density) === 'compact' ? 'compact' : 'comfortable'
}

export function saveDensity(d: Density) {
  localStorage.setItem(LS.density, d)
  document.documentElement.setAttribute('data-density', d)
}

/** Relative last-seen for operator tables. */
export function fmtRelative(ts?: number | string, fallback = '-') {
  if (ts == null || ts === '') return fallback
  let n = typeof ts === 'string' ? Number(ts) : ts
  if (!n || Number.isNaN(n)) return fallback
  if (n < 1e12) n *= 1000
  const diff = Date.now() - n
  if (diff < -60_000) return '刚刚'
  const sec = Math.floor(diff / 1000)
  if (sec < 15) return '刚刚'
  if (sec < 60) return `${sec}s 前`
  const min = Math.floor(sec / 60)
  if (min < 60) return `${min}m 前`
  const hr = Math.floor(min / 60)
  if (hr < 48) return `${hr}h 前`
  const day = Math.floor(hr / 24)
  if (day < 14) return `${day}d 前`
  return fmtTime(ts, fallback)
}

/** 对齐 Qt FormatSecToStr：3h 2m 1s */
export function formatSecToStr(seconds: number) {
  let s = Math.max(0, Math.floor(Number(seconds) || 0))
  const hours = Math.floor(s / 3600)
  const minutes = Math.floor((s % 3600) / 60)
  const secs = s % 60
  let result = ''
  if (hours > 0) result += `${hours}h `
  if (minutes > 0) result += `${minutes}m `
  if (secs > 0 || !result) result += `${secs}s`
  return result.trim()
}

/**
 * 动态心跳文案（对齐 Qt LastTickWorker + Sleep 列）：
 * - 在线：`12s` 或 `12s / 5s`（距上次回连 / 配置 sleep）
 * - sleep=0 交互：`3s · interact`
 * - Linux：beacon 为编译期固定节拍（约 20~45s），a_sleep 恒为 0，不显示 interact
 * - 离线：相对时间
 */
export function formatHeartbeat(
  a: { a_last_tick?: number; a_sleep?: number; a_jitter?: number; a_mark?: string },
  opts?: { now?: number },
  isLinux = false,
): { text: string; sub?: string; live: boolean } {
  const live = isAgentOnline(a, opts)
  if (!a.a_last_tick) return { text: '-', live: false }
  let tickMs = Number(a.a_last_tick)
  if (!Number.isFinite(tickMs) || tickMs <= 0) return { text: '-', live: false }
  if (tickMs < 1e12) tickMs *= 1000
  const now = opts?.now ?? Date.now()
  const ageSec = Math.max(0, Math.floor((now - tickMs) / 1000))
  let sleepSec = Number(a.a_sleep)
  if (!Number.isFinite(sleepSec) || sleepSec < 0) sleepSec = 5
  const jitter = Math.max(0, Math.min(100, Number(a.a_jitter) || 0))

  if (!live) {
    return { text: fmtRelative(a.a_last_tick), sub: sleepSec === 0 ? 'sleep 0' : `sleep ${sleepSec}s`, live: false }
  }
  // Linux beacon：无 sleep 配置（a_sleep === 0），节拍由编译期决定（≈20~45s）
  if (isLinux && sleepSec === 0) {
    return { text: formatSecToStr(ageSec), sub: '≈30s beacon', live: true }
  }
  if (sleepSec === 0) {
    return { text: formatSecToStr(ageSec), sub: 'interact · sleep 0', live: true }
  }
  const sleepLabel = jitter > 0 ? `${sleepSec}s±${jitter}%` : `${sleepSec}s`
  return {
    text: `${formatSecToStr(ageSec)} / ${formatSecToStr(sleepSec)}`,
    sub: `jitter ${jitter}% · conf ${sleepLabel}`,
    live: true,
  }
}

export function loadTheme(): 'light' | 'dark' {
  // 黑客风：纯深色，无主题切换
  return 'dark'
}

export function loadCollapsed(): boolean {
  return localStorage.getItem(LS.collapsed) === '1'
}

export function saveCollapsed(v: boolean) {
  localStorage.setItem(LS.collapsed, v ? '1' : '0')
}

export function loadLoginPrefs() {
  return {
    username: localStorage.getItem(LS.loginUser) || 'admin',
    baseUrl: localStorage.getItem(LS.loginBase) || '',
  }
}

export function saveLoginPrefs(username: string, baseUrl: string) {
  localStorage.setItem(LS.loginUser, username)
  localStorage.setItem(LS.loginBase, baseUrl)
}

export type SessionSnap = {
  accessToken: string
  refreshToken: string
  username: string
  baseUrl: string
}

/** 登录成功后写入；F5 后 tryRestoreSession 读取 */
export function saveSession(s: SessionSnap) {
  try {
    localStorage.setItem(LS.sessionAccess, s.accessToken || '')
    localStorage.setItem(LS.sessionRefresh, s.refreshToken || '')
    localStorage.setItem(LS.sessionUser, s.username || '')
    localStorage.setItem(LS.sessionBase, s.baseUrl || '')
  } catch { /* quota / private mode */ }
}

export function loadSession(): SessionSnap | null {
  try {
    const accessToken = localStorage.getItem(LS.sessionAccess) || ''
    const refreshToken = localStorage.getItem(LS.sessionRefresh) || ''
    const username = localStorage.getItem(LS.sessionUser) || ''
    const baseUrl = localStorage.getItem(LS.sessionBase) || ''
    if (!accessToken && !refreshToken) return null
    if (!username) return null
    return { accessToken, refreshToken, username, baseUrl }
  } catch {
    return null
  }
}

export function clearSession() {
  try {
    localStorage.removeItem(LS.sessionAccess)
    localStorage.removeItem(LS.sessionRefresh)
    localStorage.removeItem(LS.sessionUser)
    localStorage.removeItem(LS.sessionBase)
  } catch { /* ignore */ }
}

export function loadTermHeight(defaultH = 280) {
  const n = Number(localStorage.getItem(LS.termH))
  if (Number.isFinite(n) && n >= 160 && n <= 560) return n
  return defaultH
}

export function saveTermHeight(h: number) {
  localStorage.setItem(LS.termH, String(Math.round(h)))
}

export function downloadBytes(filename: string, bytes: Uint8Array) {
  const blob = new Blob([bytes], { type: 'application/octet-stream' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  a.click()
  URL.revokeObjectURL(url)
}

export function clamp(n: number, min: number, max: number) {
  return Math.max(min, Math.min(max, n))
}


/** Runtime config served by adaptixserver -web at {Web.path}/config.json */
export type RuntimeConfig = {
  api_base?: string
  endpoint?: string
  path?: string
}

export async function loadRuntimeConfig(): Promise<RuntimeConfig | null> {
  try {
    // import.meta.env.BASE_URL ends with / in Vite (e.g. / or /cnmbzxc/)
    const base = (import.meta as any).env?.BASE_URL || '/'
    const url = new URL('config.json', window.location.origin + base).toString()
    const res = await fetch(url, { cache: 'no-store' })
    if (!res.ok) return null
    return (await res.json()) as RuntimeConfig
  } catch {
    return null
  }
}

/** Uint8Array → base64。分块是为了绕开 String.fromCharCode 的实参个数上限
 *  （几 MB 的载荷一次性展开会 stack overflow）。 */
export function bytesToB64(buf: Uint8Array): string {
  let s = ''
  const CH = 0x8000
  for (let i = 0; i < buf.length; i += CH) {
    s += String.fromCharCode(...buf.subarray(i, i + CH))
  }
  return btoa(s)
}

/** File → base64（不带 data: 前缀）。服务端按 base64.StdEncoding 解。 */
export async function fileToB64(f: File): Promise<string> {
  return bytesToB64(new Uint8Array(await f.arrayBuffer()))
}

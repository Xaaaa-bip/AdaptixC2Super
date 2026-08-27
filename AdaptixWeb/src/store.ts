import { create } from 'zustand'
import { ApiClient } from './api/http'
import { SyncSocket } from './api/ws'
import {
  Agent, AppLog, ChatMessage, ConsoleLine, DEFAULT_SUBSCRIPTIONS, DownloadItem,
  Listener, P, Packet, Pivot, Task,
} from './api/types'
import { clearSession, isAgentOnline, loadSession, saveLoginPrefs, saveSession } from './utils'

function uid() { return Math.random().toString(36).slice(2, 10) + Date.now().toString(36) }
function now() { return Date.now() }

/** 静态菜单页 + 动态工作页签（files:/procs:/pty:/vnc: 按主机停靠，非弹窗） */
export type StaticTabId =
  | 'dashboard' | 'sessions' | 'listeners' | 'tasks' | 'downloads' | 'logs'
  | 'generate' | 'delivery' | 'tunnels' | 'screens' | 'creds' | 'targets' | 'chat' | 'graph'
export type TabId = StaticTabId | `files:${string}` | `procs:${string}` | `pty:${string}` | `vnc:${string}`

/** 文件/进程管理产生的控制台噪音（listing / cat / ps 等） */
export function isBrowserConsoleNoise(text: string, kind?: string): boolean {
  const t = String(text || '').trim()
  if (!t) return true
  // help / 命令目录回显：绝不当噪音（含 Process-BOF / "Show process list" 等字样）
  if (/^\s*Group\s*-/im.test(t) || /^\s*Command\s+:/im.test(t) || /^\s*help\s*\[/im.test(t)) return false
  if (/\bSubCommand\b/i.test(t) && /\bDescription\b/i.test(t)) return false
  // local echo of browser-driven commands
  if (kind === 'local' || t.startsWith('>')) {
    const body = t.replace(/^>\s*/, '')
    // 仅单行本地回显（> ls …），多行 help 正文不要滤
    if (!/\n/.test(body) && /^(ls|disks|cat|download|rm|cp|mv|mkdir|ps\b|pwd)\b/i.test(body)) return true
  }
  if (/^Listing\s+'/i.test(t)) return true
  // 仅匹配任务标题行，避免误杀 help 里的 "Show process list" / Process-BOF
  if (/^Process list\b/i.test(t) && !/\n/.test(t)) return true
  if (/file content:/i.test(t)) return true
  if (/directory is EMPTY/i.test(t)) return true
  if (/^Status:\s*OK\b/i.test(t)) return true
  if (/^Task:\s*(list files|show process|show mounted|cat file|kill process|download file|remove path)/i.test(t)) return true
  if (/^Process \d+ killed/i.test(t)) return true
  if (/^File (moved|copied|removed)|Directory '.+' created|removed successfully/i.test(t)) return true
  // large table dumps (ls/ps) without user typing them
  if (/\bType\b.+\bSize\b.+\bName\b/i.test(t) && /\bdir\b/i.test(t)) return true
  if (/^\s*PID\b.+\bPPID\b/im.test(t) && /^\s*\d+\s+\d+/m.test(t) && !/^\s*Group\s*-/im.test(t)) return true
  return false
}

export function isBrowserCmdline(cmd: string): boolean {
  const c = String(cmd || '').trim().toLowerCase()
  if (!c) return false
  return /^(ls|disks|cat|download|rm|cp|mv|mkdir|pwd|ps)\b/.test(c)
}

export function isFilesTab(id: string | null | undefined): id is `files:${string}` {
  return !!id && id.startsWith('files:')
}
export function isProcsTab(id: string | null | undefined): id is `procs:${string}` {
  return !!id && id.startsWith('procs:')
}
export function isPtyTab(id: string | null | undefined): id is `pty:${string}` {
  return !!id && id.startsWith('pty:')
}
export function isVncTab(id: string | null | undefined): id is `vnc:${string}` {
  return !!id && id.startsWith('vnc:')
}
export function isInteractTab(id: string | null | undefined) {
  return isFilesTab(id) || isProcsTab(id) || isPtyTab(id) || isVncTab(id)
}
export function tabAgentId(id: string | null | undefined): string | null {
  if (!id) return null
  if (isFilesTab(id) || isProcsTab(id) || isPtyTab(id) || isVncTab(id)) {
    return id.slice(id.indexOf(':') + 1) || null
  }
  return null
}
export function filesTabId(agentId: string): TabId { return `files:${agentId}` }
export function procsTabId(agentId: string): TabId { return `procs:${agentId}` }
export function ptyTabId(agentId: string): TabId { return `pty:${agentId}` }
export function vncTabId(agentId: string): TabId { return `vnc:${agentId}` }

export type ToastItem = {
  id: string
  level: 'info' | 'ok' | 'err' | 'warn'
  text: string
  ttl?: number
}

type State = {
  api: ApiClient
  socket: SyncSocket
  connected: boolean
  wsConnected: boolean
  syncing: boolean
  username: string
  tab: TabId
  agents: Record<string, Agent>
  listeners: Record<string, Listener>
  tasks: Task[]
  downloads: Record<string, DownloadItem>
  selectedAgentId: string | null
  consoleLines: ConsoleLine[]
  logs: AppLog[]
  toasts: ToastItem[]
  filter: string
  onlyActive: boolean
  openConsoleTabs: string[]
  activeConsoleTab: string | null
  uiTabs: TabId[]
  activeUiTab: TabId | null
  lastError: string
  browserFiles: Record<string, { path: string; items: any[]; message: string; _ts?: number }>
  browserProcs: Record<string, { items: any[]; message: string; _ts?: number }>
  /** 打开文件/进程页签时自动 sleep 0 的恢复信息 */
  interactBoost: Record<string, { sleep: number; jitter: number; ref: number }>
  chatMessages: ChatMessage[]
  pivots: Record<string, Pivot>

  setTab: (t: TabId) => void
  openConsole: (id: string) => void
  closeConsoleTab: (id: string) => void
  setActiveConsoleTab: (id: string | null) => void
  openUiTab: (id: TabId) => void
  closeUiTab: (id: TabId) => void
  setActiveUiTab: (id: TabId | null) => void
  openFilesBrowser: (agentId: string) => void
  openProcsBrowser: (agentId: string) => void
  openPty: (agentId: string) => void
  openVnc: (agentId: string) => void
  /** 打开交互型页签时加速心跳；同一主机多页签共享 refcount */
  acquireInteractBoost: (agentId: string) => Promise<void>
  /** 关闭交互型页签时减引用；归零后恢复原 sleep */
  releaseInteractBoost: (agentId: string) => Promise<void>
  setFilter: (s: string) => void
  setOnlyActive: (v: boolean) => void
  selectAgent: (id: string | null) => void
  pushLog: (level: AppLog['level'], text: string, toast?: boolean) => void
  pushToast: (level: ToastItem['level'], text: string, ttl?: number) => void
  dismissToast: (id: string) => void
  pushConsole: (agentId: string, kind: ConsoleLine['kind'], text: string) => void
  clearConsole: (agentId?: string) => void
  pushChat: (m: Omit<ChatMessage, 'id'> & { id?: string }) => void
  loginAndConnect: (opts: { baseUrl: string; username: string; password: string }) => Promise<void>
  /** F5 后用 localStorage JWT 恢复（无密码） */
  tryRestoreSession: () => Promise<boolean>
  finishConnect: (api: ApiClient, username: string) => Promise<void>
  disconnect: () => void
  handlePacket: (pkt: Packet) => void
  refreshRest: () => Promise<void>
  sendCommand: (cmdline: string) => Promise<void>
  generateBeacon: (cfg: {
    listener: string; format: string; arch: string; sleep: string; jitter: number
    iat_hiding: boolean; icon_random?: boolean; icon_content?: string; exe_pseudo_sign?: boolean; pe_header_mod?: boolean
  }) => Promise<{ filename: string; bytes: Uint8Array }>
  createHttpListener: (name: string, port: number, callback: string) => Promise<void>
  removeSelected: () => Promise<void>
  setBrowserPath: (agentId: string, path: string) => void
  setBrowserFiles: (agentId: string, path: string, items: any[], message?: string) => void
  setBrowserProcs: (agentId: string, items: any[], message?: string) => void
}


export const useStore = create<State>((set, get) => ({
  api: new ApiClient(''),
  socket: new SyncSocket(),
  connected: false,
  wsConnected: false,
  syncing: false,
  username: '',
  tab: 'dashboard',
  agents: {},
  listeners: {},
  tasks: [],
  downloads: {},
  selectedAgentId: null,
  consoleLines: [],
  logs: [],
  filter: '',
  onlyActive: false,
  openConsoleTabs: [],
  activeConsoleTab: null,
  uiTabs: ['dashboard'],
  activeUiTab: 'dashboard',
  lastError: '',
  toasts: [],
  browserFiles: {},
  browserProcs: {},
  interactBoost: {},
  chatMessages: [],
  pivots: {},

  setTab: (t) => set({ tab: t, activeUiTab: t }),
  openConsole: (id) => set((s) => {
    const tabs = s.openConsoleTabs.includes(id) ? s.openConsoleTabs : [...s.openConsoleTabs, id]
    return { openConsoleTabs: tabs, activeConsoleTab: id, selectedAgentId: id }
  }),
  closeConsoleTab: (id) => set((s) => {
    const tabs = s.openConsoleTabs.filter((x) => x !== id)
    const active = s.activeConsoleTab === id ? (tabs[tabs.length - 1] || null) : s.activeConsoleTab
    // 关闭终端页签时清掉该主机的控制台输出，避免下次点开仍残留历史
    const consoleLines = s.consoleLines.filter((l) => l.agentId !== id)
    return { openConsoleTabs: tabs, activeConsoleTab: active, consoleLines }
  }),
  setActiveConsoleTab: (id) => set({ activeConsoleTab: id, selectedAgentId: id }),
  openUiTab: (id) => set((s) => {
    const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
    return { uiTabs: tabs, activeUiTab: id, tab: id }
  }),
  closeUiTab: (id) => {
    const agentId = tabAgentId(id)
    const needRelease = isInteractTab(id)
    set((s) => {
      let tabs = s.uiTabs.filter((x) => x !== id)
      if (!tabs.length) tabs = ['dashboard']
      const active = (s.activeUiTab === id ? tabs[tabs.length - 1] : s.activeUiTab) || 'dashboard'
      return { uiTabs: tabs, activeUiTab: active, tab: active }
    })
    // 关闭文件/进程/PTY/VNC 页签后尝试恢复 sleep（仍有同主机其他交互页签则不恢复）
    if (needRelease && agentId) {
      // fire-and-forget; errors toasted inside
      get().releaseInteractBoost(agentId).catch(() => {})
    }
  },
  setActiveUiTab: (id) => set({ activeUiTab: id || 'dashboard', tab: id || 'dashboard' }),
  openFilesBrowser: (agentId) => {
    if (!agentId) return
    const agent = get().agents[agentId]
    // 离线主机禁止打开文件管理（任务下发无意义且会堆积）
    if (!agent || !isAgentOnline(agent)) {
      get().pushToast('warn', '主机离线，无法打开文件管理')
      return
    }
    const id = filesTabId(agentId)
    const already = get().uiTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      return { uiTabs: tabs, activeUiTab: id, tab: id, selectedAgentId: agentId }
    })
    // 页签组件挂载时会 acquire；这里仅保证切换到已有页签时也维持加速
    if (already) get().acquireInteractBoost(agentId).catch(() => {})
  },
  openProcsBrowser: (agentId) => {
    if (!agentId) return
    const agent = get().agents[agentId]
    if (!agent || !isAgentOnline(agent)) {
      get().pushToast('warn', '主机离线，无法打开进程管理')
      return
    }
    const id = procsTabId(agentId)
    const already = get().uiTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      return { uiTabs: tabs, activeUiTab: id, tab: id, selectedAgentId: agentId }
    })
    if (already) get().acquireInteractBoost(agentId).catch(() => {})
  },
  openPty: (agentId) => {
    if (!agentId) return
    const agent = get().agents[agentId]
    if (!agent || !isAgentOnline(agent)) {
      get().pushToast('warn', '主机离线，无法打开交互 PTY')
      return
    }
    const id = ptyTabId(agentId)
    const already = get().uiTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      return { uiTabs: tabs, activeUiTab: id, tab: id, selectedAgentId: agentId }
    })
    if (already) get().acquireInteractBoost(agentId).catch(() => {})
  },
  openVnc: (agentId) => {
    if (!agentId) return
    const agent = get().agents[agentId]
    if (!agent || !isAgentOnline(agent)) {
      get().pushToast('warn', '主机离线，无法打开 VNC')
      return
    }
    const id = vncTabId(agentId)
    const already = get().uiTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      return { uiTabs: tabs, activeUiTab: id, tab: id, selectedAgentId: agentId }
    })
    if (already) get().acquireInteractBoost(agentId).catch(() => {})
  },
  acquireInteractBoost: async (agentId) => {
    if (!agentId) return
    const { api, agents, interactBoost, pushLog, pushToast } = get()
    const cur = interactBoost[agentId]
    if (cur && cur.ref > 0) {
      set((s) => ({
        interactBoost: {
          ...s.interactBoost,
          [agentId]: { ...cur, ref: cur.ref + 1 },
        },
      }))
      return
    }
    const agent = agents[agentId]
    // 记录恢复值：优先 agent 上报；缺省 5s
    let sleepSec = Number(agent?.a_sleep ?? 5)
    let jitter = Number(agent?.a_jitter ?? 0)
    if (!Number.isFinite(sleepSec) || sleepSec < 0) sleepSec = 5
    if (!Number.isFinite(jitter) || jitter < 0) jitter = 0
    // 若当前已是 0，恢复时仍用 5s 作为安全默认（避免永久 0）
    const restoreSleep = sleepSec > 0 ? sleepSec : 5
    const restoreJitter = sleepSec > 0 ? jitter : 0

    set((s) => ({
      interactBoost: {
        ...s.interactBoost,
        [agentId]: { sleep: restoreSleep, jitter: restoreJitter, ref: 1 },
      },
    }))

    if (sleepSec === 0) {
      pushLog('info', `主机 ${agentId} 已是 sleep 0`, false)
      return
    }
    try {
      const { browserBoost } = await import('./api/browser')
      await browserBoost(api, agentId)
      set((s) => {
        const a = s.agents[agentId]
        if (!a) return {}
        return { agents: { ...s.agents, [agentId]: { ...a, a_sleep: 0, a_jitter: 0 } } }
      })
      pushLog('ok', `已加速主机 ${agentId}（sleep 0），关闭文件/进程页签后恢复 ${restoreSleep}s`, false)
      pushToast('ok', `交互加速：sleep 0（关闭后恢复 ${restoreSleep}s）`)
    } catch (e: any) {
      set((s) => {
        const n = { ...s.interactBoost }
        delete n[agentId]
        return { interactBoost: n }
      })
      pushLog('err', `加速失败: ${e?.message || e}`)
    }
  },
  releaseInteractBoost: async (agentId) => {
    if (!agentId) return
    const state = get()
    const cur = state.interactBoost[agentId]
    if (!cur) return

    // 关闭后仍开着的同主机交互页签数
    const still = state.uiTabs.filter((t) => {
      const aid = tabAgentId(t)
      return aid === agentId && isInteractTab(t)
    }).length

    if (still > 0) {
      set((s) => ({
        interactBoost: {
          ...s.interactBoost,
          [agentId]: { ...cur, ref: Math.max(1, still) },
        },
      }))
      return
    }

    set((s) => {
      const n = { ...s.interactBoost }
      delete n[agentId]
      return { interactBoost: n }
    })
    if (cur.sleep <= 0) return
    try {
      const { browserRestoreSleep } = await import('./api/browser')
      await browserRestoreSleep(state.api, agentId, cur.sleep, cur.jitter)
      set((s) => {
        const a = s.agents[agentId]
        if (!a) return {}
        return {
          agents: {
            ...s.agents,
            [agentId]: { ...a, a_sleep: cur.sleep, a_jitter: cur.jitter },
          },
        }
      })
      state.pushLog('ok', `已恢复主机 ${agentId} sleep ${cur.sleep}s` + (cur.jitter ? ` jitter ${cur.jitter}%` : ''), false)
      state.pushToast('info', `已恢复 sleep ${cur.sleep}s`)
    } catch (e: any) {
      state.pushLog('err', `恢复 sleep 失败: ${e?.message || e}`)
    }
  },
  setFilter: (s) => set({ filter: s }),
  setOnlyActive: (v) => set({ onlyActive: v }),
  selectAgent: (id) => set({ selectedAgentId: id }),
  pushToast: (level, text, ttl = 3200) => set((s) => ({
    toasts: [...s.toasts, { id: uid(), level, text, ttl }].slice(-6),
  })),
  dismissToast: (id) => set((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) })),
  pushLog: (level, text, toast) => {
    set((s) => ({ logs: [{ id: uid(), level, text, ts: now() }, ...s.logs].slice(0, 500) }))
    // Toast important ops by default for err/ok/warn, or when explicitly requested
    const shouldToast = toast === true || (toast !== false && (level === 'err' || level === 'ok' || level === 'warn'))
    if (shouldToast) {
      // skip noisy low-value messages
      if (!/REST 数据已加载|sync 已启动|正在建立|login /.test(text)) {
        get().pushToast(level, text)
      }
    }
  },
  pushConsole: (agentId, kind, text) => {
    const t = String(text || '')
    // 文件管理 / 进程管理 / 静默浏览操作：绝不进命令控制台
    if (isBrowserConsoleNoise(t, kind)) return
    set((s) => ({
      consoleLines: [...s.consoleLines, { id: uid(), agentId, kind, text: t, ts: now() }].slice(-2000),
    }))
  },
  clearConsole: (agentId) => set((s) => ({
    consoleLines: agentId ? s.consoleLines.filter((l) => l.agentId !== agentId) : [],
  })),
  pushChat: (m) => set((s) => {
    const next: ChatMessage = {
      id: m.id || uid(),
      username: m.username || '?',
      message: m.message || '',
      date: m.date || Math.floor(now() / 1000),
    }
    // 去重：同用户+同内容+近 2 秒内视为同一条（乐观更新 vs WS 回包）
    const exists = s.chatMessages.some((x) =>
      x.username === next.username
      && x.message === next.message
      && Math.abs((x.date || 0) - (next.date || 0)) <= 2,
    )
    if (exists) return s
    return { chatMessages: [...s.chatMessages, next].slice(-500) }
  }),
  setBrowserPath: (agentId, path) => set((s) => ({
    browserFiles: {
      ...s.browserFiles,
      [agentId]: {
        path,
        items: s.browserFiles[agentId]?.items || [],
        message: s.browserFiles[agentId]?.message || '',
        _ts: s.browserFiles[agentId]?._ts,
      },
    },
  })),
  setBrowserFiles: (agentId, path, items, message = '') => set((s) => ({
    browserFiles: { ...s.browserFiles, [agentId]: { path, items, message, _ts: Date.now() } },
  })),
  setBrowserProcs: (agentId, items, message = '') => set((s) => ({
    browserProcs: { ...s.browserProcs, [agentId]: { items, message, _ts: Date.now() } },
  })),

  loginAndConnect: async ({ baseUrl, username, password }) => {
    const api = new ApiClient(baseUrl || '')
    get().pushLog('info', `login ${username}`, false)
    await api.login(username, password)
    get().pushLog('ok', 'JWT 登录成功', false)
    saveLoginPrefs(username, baseUrl || '')
    // F5 恢复：持久化 JWT
    saveSession({
      accessToken: api.accessToken,
      refreshToken: api.refreshToken,
      username,
      baseUrl: baseUrl || '',
    })
    api.onTokensChange = (access, refresh) => {
      saveSession({ accessToken: access, refreshToken: refresh, username, baseUrl: baseUrl || '' })
    }
    await get().finishConnect(api, username)
  },

  tryRestoreSession: async () => {
    const snap = loadSession()
    if (!snap) return false
    const api = new ApiClient(snap.baseUrl || '')
    api.setTokens(snap.accessToken, snap.refreshToken)
    api.onTokensChange = (access, refresh) => {
      saveSession({
        accessToken: access,
        refreshToken: refresh,
        username: snap.username,
        baseUrl: snap.baseUrl || '',
      })
    }
    get().pushLog('info', `恢复会话 ${snap.username}…`, false)
    // 校验 token：先 listAgents；失败则 refresh；再失败清会话
    try {
      try {
        await api.listAgents()
      } catch {
        await api.refreshAccess()
        await api.listAgents()
      }
    } catch (e: any) {
      clearSession()
      get().pushLog('warn', `会话已失效，请重新登录: ${e?.message || e}`, false)
      return false
    }
    await get().finishConnect(api, snap.username)
    get().pushLog('ok', '会话已恢复（F5）', false)
    return true
  },

  // 登录 / 恢复会话共用：进入主界面 + REST + WS
  finishConnect: async (api: ApiClient, username: string) => {
    set({
      api,
      connected: true,
      wsConnected: false,
      syncing: true,
      username,
      lastError: '',
      browserFiles: {},
      browserProcs: {},
      agents: {},
      listeners: {},
      tasks: [],
      downloads: {},
      chatMessages: [],
      pivots: {},
      openConsoleTabs: [],
      activeConsoleTab: null,
      selectedAgentId: null,
      uiTabs: ['dashboard'],
      activeUiTab: 'dashboard',
      tab: 'dashboard',
    })

    try {
      await get().refreshRest()
      get().pushLog('ok', 'REST 数据已加载', false)
    } catch (e: any) {
      get().pushLog('err', `REST 加载失败: ${e.message || e}`)
    }

    try {
      const otp = await api.generateOtp('connect', {
        client_type: 2,
        console_team_mode: true,
        subscriptions: DEFAULT_SUBSCRIPTIONS,
      })
      get().pushLog('info', '正在建立实时通道…', false)
      const wsUrl = api.connectWsUrl(otp)
      const socket = new SyncSocket()
      await new Promise<void>((resolve, reject) => {
        let settled = false
        const timer = setTimeout(() => {
          if (!settled) {
            settled = true
            reject(new Error('websocket timeout'))
          }
        }, 8000)
        socket.connect(wsUrl, {
          onOpen: async () => {
            set({ socket, wsConnected: true })
            get().pushLog('ok', 'WebSocket 已连接', false)
            try {
              await api.sync()
              get().pushLog('info', 'sync 已启动', false)
            } catch (e: any) {
              get().pushLog('warn', `sync 调用失败: ${e.message || e}`, false)
            }
            if (!settled) {
              settled = true
              clearTimeout(timer)
              resolve()
            }
          },
          onClose: (ev) => {
            set({ wsConnected: false })
            get().pushLog('warn', `WebSocket 关闭 code=${ev.code}`, false)
            if (!settled) {
              settled = true
              clearTimeout(timer)
              reject(new Error(`websocket closed ${ev.code}`))
            }
          },
          onError: () => {
            get().pushLog('err', 'WebSocket 错误', false)
          },
          onPacket: (pkt) => get().handlePacket(pkt),
        })
      })
    } catch (e: any) {
      const msg = e?.message || String(e)
      set({ lastError: `实时通道失败（REST 仍可用）: ${msg}`, syncing: false, wsConnected: false })
      get().pushLog('warn', `实时通道失败，继续使用 REST: ${msg}`)
    } finally {
      if (get().syncing && !get().wsConnected) set({ syncing: false })
    }
  },

  disconnect: () => {
    try { get().socket.close() } catch { /* ignore */ }
    clearSession()
    set({
      connected: false,
      wsConnected: false,
      syncing: false,
      username: '',
      agents: {},
      listeners: {},
      tasks: [],
      downloads: {},
      chatMessages: [],
      pivots: {},
      openConsoleTabs: [],
      activeConsoleTab: null,
      selectedAgentId: null,
      consoleLines: [],
      browserFiles: {},
      browserProcs: {},
      uiTabs: ['dashboard'],
      activeUiTab: 'dashboard',
      tab: 'dashboard',
    })
  },

  handlePacket: (pkt) => {
    const type = pkt.type ?? pkt.Type
    const { pushLog, pushConsole } = get()
    if ((type === P.SYNC_CATEGORY_BATCH || type === P.SYNC_BATCH) && Array.isArray(pkt.packets)) {
      for (const p of pkt.packets) get().handlePacket(p)
      return
    }
    switch (type) {
      case P.SYNC_START:
        set({ syncing: true })
        break
      case P.SYNC_FINISH:
        set({ syncing: false })
        get().pushLog('ok', '实时同步完成')
        // re-hydrate REST after sync to ensure lists filled
        get().refreshRest().catch(() => {})
        break
      case P.NOTIFICATION:
        pushLog('info', String(pkt.message || pkt.Message || ''))
        break
      case P.LISTENER_START:
      case P.LISTENER_EDIT: {
        const l = normListener(pkt)
        if (l.l_name) set((s) => ({ listeners: { ...s.listeners, [l.l_name]: { ...s.listeners[l.l_name], ...l } } }))
        break
      }
      case P.LISTENER_STOP: {
        const name = pkt.l_name || pkt.name
        if (name) set((s) => { const n = { ...s.listeners }; delete n[name]; return { listeners: n } })
        break
      }
      case P.AGENT_NEW:
      case P.AGENT_UPDATE: {
        const a = normAgent(pkt)
        if (a.a_id) set((s) => ({ agents: { ...s.agents, [a.a_id]: { ...s.agents[a.a_id], ...a } } }))
        break
      }
      case P.AGENT_TICK: {
        const ids: string[] = pkt.a_id_array || (pkt.a_id ? [pkt.a_id] : [])
        const tick = pkt.a_last_tick || Math.floor(Date.now() / 1000)
        set((s) => {
          const agents = { ...s.agents }
          for (const id of ids) if (agents[id]) agents[id] = { ...agents[id], a_last_tick: tick }
          return { agents }
        })
        break
      }
      case P.AGENT_REMOVE: {
        const ids: string[] = pkt.a_id_array || (pkt.a_id ? [pkt.a_id] : [])
        set((s) => { const agents = { ...s.agents }; for (const id of ids) delete agents[id]; return { agents } })
        break
      }
      case P.AGENT_TASK_SYNC:
      case P.AGENT_TASK_UPDATE:
      case P.AGENT_TASK_SEND: {
        const t = normTask(pkt)
        set((s) => ({ tasks: [t, ...s.tasks.filter((x) => x.a_task_id !== t.a_task_id)].slice(0, 500) }))
        break
      }
      case P.AGENT_TASK_REMOVE:
        if (pkt.a_task_id) set((s) => ({ tasks: s.tasks.filter((t) => t.a_task_id !== pkt.a_task_id) }))
        break
      case P.DOWNLOAD_CREATE:
      case P.DOWNLOAD_UPDATE: {
        const d = normDl(pkt)
        if (d.d_file_id) set((s) => ({ downloads: { ...s.downloads, [d.d_file_id!]: { ...s.downloads[d.d_file_id!], ...d } } }))
        break
      }
      case P.DOWNLOAD_DELETE: {
        const ids: string[] = pkt.d_file_id_array || (pkt.d_file_id ? [pkt.d_file_id] : [])
        set((s) => { const downloads = { ...s.downloads }; for (const id of ids) delete downloads[id]; return { downloads } })
        break
      }
      case P.BROWSER_DISKS: {
        const agentId = pkt.b_agent_id || pkt.a_id || ''
        const message = pkt.b_message || ''
        let items: any[] = []
        try {
          const arr = JSON.parse(pkt.b_data || '[]')
          if (Array.isArray(arr)) {
            // disks: { b_name, b_type }
            items = arr.map((x: any) => ({
              name: x.b_name || x.name || '',
              isDir: true,
              size: 0,
              date: 0,
              diskType: x.b_type || x.type || '',
            })).filter((x: any) => x.name)
          }
        } catch {}
        if (agentId) get().setBrowserFiles(agentId, '', items, message || 'Disks')
        // browser packets must NOT spam agent console
        break
      }
      case P.BROWSER_FILES: {
        const agentId = pkt.b_agent_id || pkt.a_id || ''
        const path = pkt.b_path || '.'
        const message = pkt.b_message || ''
        let items: any[] = []
        try {
          const raw = pkt.b_data || '[]'
          const arr = JSON.parse(raw)
          if (Array.isArray(arr)) {
            // Qt / server JSON keys: b_filename, b_is_dir, b_size, b_date
            items = arr.map((x: any) => {
              const rawName = x.b_filename || x.filename || x.Filename || x.name || ''
              return {
              name: rawName,
              isDir: !!(x.b_is_dir ?? x.is_dir ?? x.IsDir ?? x.dir),
              size: Number(x.b_size ?? x.size ?? x.Size ?? 0),
              date: Number(x.b_date ?? x.date ?? x.Date ?? 0),
              dateText: x.b_date
                ? new Date(Number(x.b_date) * (Number(x.b_date) < 1e12 ? 1000 : 1)).toLocaleString()
                : undefined,
              sizeText: x.b_size != null ? String(x.b_size) : undefined,
              mode: x.b_mode || x.mode || '',
              user: x.b_user || x.user || '',
              group: x.b_group || x.group || '',
            }
            }).filter((x: any) => x.name && x.name !== '.' && x.name !== '..')
          }
        } catch {
          // ignore malformed payload
        }
        if (agentId) get().setBrowserFiles(agentId, path, items, message)
        break
      }
      case P.BROWSER_FILES_STATUS: {
        const agentId = pkt.b_agent_id || pkt.a_id || ''
        const message = pkt.b_message || ''
        if (agentId && message) {
          // keep existing items, update status message only (no console)
          const cur = get().browserFiles[agentId]
          if (cur) get().setBrowserFiles(agentId, cur.path, cur.items, message)
        }
        break
      }
      case P.BROWSER_PROCESS: {
        const agentId = pkt.b_agent_id || pkt.a_id || ''
        const message = pkt.b_message || ''
        let items: any[] = []
        try {
          const raw = pkt.b_data || '[]'
          const arr = JSON.parse(raw)
          if (Array.isArray(arr)) {
            // Qt keys: b_pid, b_ppid, b_session_id, b_arch, b_context, b_process_name
            items = arr.map((x: any) => ({
              pid: Number(x.b_pid ?? x.pid ?? x.Pid ?? 0),
              ppid: Number(x.b_ppid ?? x.ppid ?? x.Ppid ?? 0),
              arch: x.b_arch ?? x.arch ?? x.Arch,
              session: x.b_session_id ?? x.session_id ?? x.SessionId,
              context: x.b_context ?? x.context ?? x.Context,
              name: x.b_process_name || x.process_name || x.ProcessName || x.name || '',
              tty: x.b_tty || x.tty || '',
            })).filter((x: any) => x.pid && x.name)
          }
        } catch {}
        if (agentId) get().setBrowserProcs(agentId, items, message)
        break
      }
      case P.CONSOLE_LOCAL: {
        // help 等本地命令：正文在 a_text；a_message 常为空；a_cmdline 只是输入行
        const aid = String(pkt.a_id || '')
        const text = String(pkt.a_text || '')
        const message = String(pkt.a_message || '')
        const cmdline = String(pkt.a_cmdline || '')
        const body = (text || message).trim()
        // 有正文（help 列表）→ 按 ok 原文输出，勿走 local 的 `> ` 前缀逻辑
        if (body) {
          if (!isBrowserConsoleNoise(body, 'ok')) {
            pushConsole(aid, 'ok', body)
          }
        } else if (cmdline && !isBrowserCmdline(cmdline) && !isBrowserConsoleNoise(cmdline, 'local')) {
          pushConsole(aid, 'local', cmdline.startsWith('>') ? cmdline : `> ${cmdline}`)
        }
        break
      }
      case P.CONSOLE_ERROR: {
        const errMsg = String(pkt.a_message || pkt.a_text || '')
        // 文件/进程浏览器错误只 toast，不刷控制台
        if (isBrowserConsoleNoise(errMsg, 'err') || isBrowserCmdline(String(pkt.a_cmdline || ''))) {
          if (errMsg) get().pushToast('err', errMsg)
        } else {
          pushConsole(pkt.a_id || '', 'err', errMsg)
        }
        break
      }
      case P.CONSOLE_OUT:
      case P.CONSOLE_TASK_SYNC:
      case P.CONSOLE_TASK_UPD: {
        const message = String(pkt.a_message || pkt.message || '')
        const text = String(pkt.a_text || pkt.text || '')
        const cmdline = String(pkt.a_cmdline || pkt.cmdline || pkt.a_command_line || '')
        const msg = [message, text].filter(Boolean).join('\n')
        const kind = pkt.a_completed === false ? 'task' : 'ok'
        const aid = pkt.a_id || ''
        // 文件/进程浏览器相关任务：只更新任务列表，绝不 push 到命令控制台
        const browserCmd = isBrowserCmdline(cmdline)
        const browserMsg = isBrowserConsoleNoise(msg, kind) || isBrowserConsoleNoise(message, kind)
        if (msg && aid && !browserCmd && !browserMsg) {
          pushConsole(aid, kind as any, msg)
        }
        // 仍写入 tasks（任务中心可见），但不刷控制台
        if (pkt.a_task_id) {
          const t = normTask(pkt)
          set((s) => ({ tasks: [t, ...s.tasks.filter((x) => x.a_task_id !== t.a_task_id)].slice(0, 500) }))
        }
        break
      }
      case P.CHAT_MESSAGE: {
        get().pushChat({
          username: String(pkt.c_username || pkt.username || '?'),
          message: String(pkt.c_message || pkt.message || ''),
          date: Number(pkt.c_date || pkt.date || Date.now()),
        })
        break
      }
      case P.PIVOT_CREATE: {
        const id = String(pkt.p_pivot_id || '')
        if (id) set((s) => ({
          pivots: {
            ...s.pivots,
            [id]: {
              p_pivot_id: id,
              p_pivot_name: pkt.p_pivot_name,
              p_parent_agent_id: pkt.p_parent_agent_id,
              p_child_agent_id: pkt.p_child_agent_id,
            },
          },
        }))
        break
      }
      case P.PIVOT_DELETE: {
        const id = String(pkt.p_pivot_id || '')
        if (id) set((s) => {
          const pivots = { ...s.pivots }
          delete pivots[id]
          return { pivots }
        })
        break
      }

      default:
        break
    }
  },

  refreshRest: async () => {
    const { api } = get()
    const [agents, listeners, downloads] = await Promise.all([
      api.listAgents().catch(() => []),
      api.listListeners().catch(() => []),
      api.listDownloads().catch(() => []),
    ])
    // tasks API needs agent_id — pull for known agents (cap concurrent)
    const agentArr = Array.isArray(agents) ? agents : []
    const agentIds = agentArr.map((a: any) => a.a_id || a.id).filter(Boolean).slice(0, 30)
    // also include currently known agents not in REST response
    for (const id of Object.keys(get().agents)) if (!agentIds.includes(id)) agentIds.push(id)
    const taskLists = await Promise.all(
      agentIds.slice(0, 20).map((id) => api.listTasks(id).catch(() => [])),
    )
    const tasks = taskLists.flat().filter(Boolean)

    const agentMap: Record<string, Agent> = {}
    for (const a of agentArr) {
      const n = normAgent(a)
      if (n.a_id) agentMap[n.a_id] = n
    }
    const listenerMap: Record<string, Listener> = {}
    for (const l of (Array.isArray(listeners) ? listeners : [])) {
      const n = normListener(l)
      if (n.l_name) listenerMap[n.l_name] = n
    }
    const dlMap: Record<string, DownloadItem> = {}
    for (const d of (Array.isArray(downloads) ? downloads : [])) {
      const n = normDl(d)
      if (n.d_file_id) dlMap[n.d_file_id] = n
    }
    set({
      // REST is source of truth for agents/listeners lists
      agents: agentMap,
      listeners: listenerMap,
      tasks: (Array.isArray(tasks) ? tasks : []).map(normTask).slice(0, 500),
      downloads: dlMap,
    })
  },

  sendCommand: async (cmdline) => {
    const { api, selectedAgentId, pushConsole, pushLog } = get()
    if (!selectedAgentId) throw new Error('未选择主机')
    const line = cmdline.trim()
    if (!line) return
    pushConsole(selectedAgentId, 'local', `> ${line}`)
    try { await api.commandRaw(selectedAgentId, line) }
    catch (e: any) {
      pushConsole(selectedAgentId, 'err', e.message || String(e))
      pushLog('err', `命令失败: ${e.message || e}`)
      throw e
    }
  },

  generateBeacon: async (cfg) => {
    const { api, pushLog } = get()
    pushLog('info', `生成 beacon format=${cfg.format} listener=${cfg.listener}`)
    const res = await api.generateAgent('beacon', [cfg.listener], {
      os: 'windows', arch: cfg.arch, format: cfg.format,
      sleep: cfg.sleep, jitter: cfg.jitter, iat_hiding: cfg.iat_hiding,
      icon_random: !!cfg.icon_random,
      icon_content: cfg.icon_content || '',
      exe_pseudo_sign: cfg.exe_pseudo_sign ?? (cfg.format === 'Exe' || cfg.format === 'Service Exe'),
      pe_header_mod: cfg.pe_header_mod ?? (cfg.format === 'Exe' || cfg.format === 'Service Exe' || cfg.format === 'DLL'),
    })
    const decoded = ApiClient.decodeGeneratePayload(res.message)
    pushLog('ok', `已生成 ${decoded.filename} (${decoded.bytes.length} bytes)`)
    return decoded
  },

  createHttpListener: async (name, port, callback) => {
    const { api, pushLog } = get()
    const config = {
      host_bind: '0.0.0.0', port_bind: port,
      callback_addresses: [callback],
      encrypt_key: randomHex(16),
      http_method: 'POST', hb_header: 'X-Beacon-Id',
      host_header: [], uri: ['/api/v1/status'],
      user_agent: ['Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'],
      ssl: false, 'page-error': '<html>404</html>',
      'page-payload': '<html><!--<<<PAYLOAD_DATA>>>--></body></html>', protocol: 'http',
    }
    await api.createListener(name, 'BeaconHTTP', config)
    pushLog('ok', `监听器 ${name} 已创建`)
    await get().refreshRest()
  },

  removeSelected: async () => {
    const { api, selectedAgentId, pushLog } = get()
    if (!selectedAgentId) return
    await api.removeAgents([selectedAgentId])
    pushLog('warn', `已删除主机 ${selectedAgentId}`)
    set((s) => {
      const agents = { ...s.agents }
      delete agents[selectedAgentId]
      const consoleLines = s.consoleLines.filter((l) => l.agentId !== selectedAgentId)
      const openConsoleTabs = s.openConsoleTabs.filter((x) => x !== selectedAgentId)
      const activeConsoleTab = s.activeConsoleTab === selectedAgentId
        ? (openConsoleTabs[openConsoleTabs.length - 1] || null)
        : s.activeConsoleTab
      // 关掉该主机的文件/进程页签
      const uiTabs = s.uiTabs.filter((t) => tabAgentId(t) !== selectedAgentId)
      const safeTabs = uiTabs.length ? uiTabs : (['dashboard'] as TabId[])
      const activeUiTab = safeTabs.includes(s.activeUiTab as TabId)
        ? s.activeUiTab
        : safeTabs[safeTabs.length - 1]
      return {
        agents,
        selectedAgentId: null,
        consoleLines,
        openConsoleTabs,
        activeConsoleTab,
        uiTabs: safeTabs,
        activeUiTab: activeUiTab || 'dashboard',
        tab: (activeUiTab || 'dashboard') as TabId,
      }
    })
  },
}))

function normAgent(p: any): Agent {
  return {
    a_id: p.a_id || p.id || '',
    a_name: p.a_name || p.name,
    a_listener: p.a_listener || p.listener,
    a_external_ip: p.a_external_ip || p.external,
    a_internal_ip: p.a_internal_ip || p.internal,
    a_domain: p.a_domain || p.domain,
    a_computer: p.a_computer || p.computer,
    a_username: p.a_username || p.username,
    a_os: p.a_os,
    a_os_desc: p.a_os_desc || p.os,
    a_process: p.a_process || p.process,
    a_pid: String(p.a_pid ?? p.pid ?? ''),
    a_tid: String(p.a_tid ?? p.tid ?? ''),
    a_tags: p.a_tags || p.tags || '',
    a_mark: p.a_mark || p.mark || '',
    a_create_time: p.a_create_time || p.create_time,
    a_last_tick: p.a_last_tick || p.last_tick,
    a_sleep: p.a_sleep ?? p.sleep,
    a_jitter: p.a_jitter ?? p.jitter,
    a_elevated: p.a_elevated,
    a_arch: p.a_arch || p.arch,
    a_acp: p.a_acp ?? p.acp,
    a_oemcp: p.a_oemcp ?? p.oem_cp ?? p.OemCP,
  }
}
function normListener(p: any): Listener {
  let data = p.l_data || p.config
  // keep raw JSON string for edit form; stringify objects
  if (data && typeof data === 'object') {
    try { data = JSON.stringify(data) } catch { /* keep as-is */ }
  }
  return {
    l_name: p.l_name || p.name || '',
    l_reg_name: p.l_reg_name || p.type || p.reg_name,
    l_protocol: p.l_protocol || p.protocol,
    l_type: p.l_type || p.type,
    l_bind_host: p.l_bind_host || p.bind_host,
    l_bind_port: String(p.l_bind_port ?? p.bind_port ?? p.port_bind ?? ''),
    l_agent_addr: p.l_agent_addr || p.agent_addr,
    l_status: p.l_status || p.status,
    a_create_time: p.a_create_time || p.create_time,
    l_data: data,
  }
}
function normTask(p: any): Task {
  return {
    a_task_id: p.a_task_id || p.task_id,
    a_id: p.a_id || p.agent_id,
    a_client: p.a_client || p.client,
    a_user: p.a_user || p.user,
    a_computer: p.a_computer || p.computer,
    a_cmdline: p.a_cmdline || p.cmdline || p.command,
    a_start_time: p.a_start_time || p.start_time,
    a_finish_time: p.a_finish_time || p.finish_time,
    a_message: p.a_message || p.message,
    a_text: p.a_text || p.text,
    a_completed: p.a_completed,
  }
}
function normDl(p: any): DownloadItem {
  return {
    d_file_id: p.d_file_id || p.file_id || p.id,
    d_agent_id: p.d_agent_id || p.agent_id,
    d_user: p.d_user || p.user,
    d_computer: p.d_computer || p.computer,
    d_filename: p.d_filename || p.filename || p.file,
    d_size: p.d_size || p.size,
  }
}
function randomHex(n: number) {
  const a = new Uint8Array(n)
  crypto.getRandomValues(a)
  return Array.from(a).map((b) => b.toString(16).padStart(2, '0')).join('')
}

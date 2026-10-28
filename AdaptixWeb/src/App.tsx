import React, { useEffect, useMemo, useState } from 'react'
import { Login } from './components/Login'
import { Dashboard } from './components/Dashboard'
import { Hosts } from './components/Hosts'
import { CmdExec } from './components/CmdExec'
import { Term } from './components/Term'
import { Listeners } from './components/Listeners'
import { Generate } from './components/Generate'
import { WebDelivery } from './components/WebDelivery'
import { Tasks } from './components/Tasks'
import { Downloads } from './components/Downloads'
import { Tunnels } from './components/Tunnels'
import { Screenshots } from './components/Screenshots'
import { Credentials } from './components/Credentials'
import { Targets } from './components/Targets'
import { Logs } from './components/Logs'
import { Chat } from './components/Chat'
import { AICollab } from './components/AICollab'
import { Graph } from './components/Graph'
import { Relay } from './components/Relay'
import { Bofs } from './components/Bofs'
import { ArgFuscator } from './components/ArgFuscator'
import { ExtKit } from './components/ExtKit'
import { FileManager } from './components/FileManager'
import { ProcessManager } from './components/ProcessManager'
import { PtyTerm } from './components/PtyTerm'
import { VncPanel } from './components/VncPanel'
import { EventsDock } from './components/EventsDock'
import { ToastHost } from './components/Toast'
import { ConnStatus } from './components/StatusPill'
import { CommandPalette } from './components/CommandPalette'
import { ShortcutHelp } from './components/ui'
import { useStore, type TabId, isFilesTab, isProcsTab, isPtyTab, isVncTab, isInteractTab, isCmdTab, tabAgentId } from './store'
import { isAgentOnline, loadDensity, loadTheme, saveDensity, saveTheme, type Density } from './utils'

type Menu = { id: TabId; label: string; ico: string; group?: string; shortcut?: string }

/** 统一 stroke 图标 — 显式 currentColor，任何底色上都可见 */
function Icon({ children }: { children: React.ReactNode }) {
  return (
    <svg
      viewBox="0 0 24 24"
      width="18"
      height="18"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      stroke="currentColor"
      strokeWidth={1.75}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
      focusable="false"
      style={{ display: 'block', color: 'inherit', flexShrink: 0 }}
    >
      {children}
    </svg>
  )
}

const ICO: Record<string, React.ReactNode> = {
  dashboard: (
    <Icon>
      <path d="M4 11l8-7 8 7" />
      <path d="M6 10.5V20h4v-5h4v5h4v-9.5" />
    </Icon>
  ),
  sessions: (
    <Icon>
      <rect x="3.5" y="4.5" width="17" height="12" rx="1.5" />
      <path d="M8 20h8M12 16.5V20" />
    </Icon>
  ),
  listeners: (
    <Icon>
      <circle cx="12" cy="12" r="2.5" />
      <path d="M6.2 6.2a8.2 8.2 0 0 1 0 11.6M17.8 6.2a8.2 8.2 0 0 0 0 11.6" />
    </Icon>
  ),
  generate: (
    <Icon>
      <path d="M13 3L5 13h6l-1 8 9-12h-6l0-6z" />
    </Icon>
  ),
  delivery: (
    <Icon>
      <path d="M4 7h16v10H4z" />
      <path d="M8 11h8M8 14h5" />
      <path d="M12 3v4M9 5l3-2 3 2" />
    </Icon>
  ),
  tasks: (
    <Icon>
      <path d="M9 6h11M9 12h11M9 18h11" />
      <circle cx="4.5" cy="6" r="1" fill="currentColor" stroke="none" />
      <circle cx="4.5" cy="12" r="1" fill="currentColor" stroke="none" />
      <circle cx="4.5" cy="18" r="1" fill="currentColor" stroke="none" />
    </Icon>
  ),
  bofs: (
    <Icon>
      <rect x="3.5" y="3.5" width="17" height="17" rx="1.5" />
      <path d="M8 3.5v17M16 3.5v17M3.5 8h17M3.5 16h17" />
    </Icon>
  ),
  argfuscator: (
    <Icon>
      <path d="M3.5 8h4l9 8h4" />
      <path d="M3.5 16h4l9-8h4" />
      <path d="M17.5 5 20.5 8l-3 3" />
      <path d="M17.5 13 20.5 16l-3 3" />
    </Icon>
  ),
  extkit: (
    <Icon>
      <path d="M3.5 4.5h17v15h-17z" />
      <path d="M3.5 9.5h17M3.5 14.5h17" />
      <path d="M9 4.5v15" />
    </Icon>
  ),
  downloads: (
    <Icon>
      <path d="M12 4v11" />
      <path d="M7.5 11.5 12 16l4.5-4.5" />
      <path d="M5 20h14" />
    </Icon>
  ),
  tunnels: (
    <Icon>
      <rect x="7" y="6.5" width="10" height="4" rx="1" />
      <rect x="7" y="13.5" width="10" height="4" rx="1" />
      <path d="M4 8.5H2.5v7H4M20 8.5h1.5v7H20" />
    </Icon>
  ),
  screens: (
    <Icon>
      <rect x="3.5" y="5" width="17" height="13" rx="1.5" />
      <circle cx="12" cy="11.5" r="2.5" />
    </Icon>
  ),
  creds: (
    <Icon>
      <rect x="4" y="10" width="16" height="9" rx="1.5" />
      <path d="M8 10V8a4 4 0 0 1 8 0v2" />
    </Icon>
  ),
  targets: (
    <Icon>
      <circle cx="12" cy="12" r="7.5" />
      <circle cx="12" cy="12" r="2.5" />
      <path d="M12 3v2.2M12 18.8V21M3 12h2.2M18.8 12H21" />
    </Icon>
  ),
  logs: (
    <Icon>
      <path d="M8 6h12M8 12h12M8 18h12" />
      <path d="M4 6h.01M4 12h.01M4 18h.01" strokeWidth={2.4} />
    </Icon>
  ),
  chat: (
    <Icon>
      <path d="M5 6h14v9H8l-3 3V6z" />
      <path d="M9 10h6M9 13h4" />
    </Icon>
  ),
  ai: (
    <Icon>
      <path d="M12 3.5 14.2 9l5.3 1.3-5.3 1.4L12 17l-2.2-5.3-5.3-1.4L9.8 9 12 3.5z" />
      <path d="M18.5 16.5v4M16.5 18.5h4" />
    </Icon>
  ),
  graph: (
    <Icon>
      <circle cx="7" cy="8" r="2.5" />
      <circle cx="17" cy="7" r="2.5" />
      <circle cx="12" cy="17" r="2.5" />
      <path d="M9 9.2 15 8.2M8.2 10.2 10.8 15M15.8 9.2 13.2 15" />
    </Icon>
  ),
  relay: (
    <Icon>
      <circle cx="12" cy="12" r="2.6" />
      <path d="M6.8 6.8a7.4 7.4 0 0 0 0 10.4M17.2 6.8a7.4 7.4 0 0 1 0 10.4" />
      <path d="M4 4a11.3 11.3 0 0 0 0 16M20 4a11.3 11.3 0 0 1 0 16" />
    </Icon>
  ),
  events: (
    <Icon>
      <path d="M4 12h4l2-5 3 10 2-5h5" />
    </Icon>
  ),
  help: (
    <Icon>
      <circle cx="12" cy="12" r="8.5" />
      <path d="M9.6 9.6a2.4 2.4 0 1 1 3.3 2.2c-.7.35-1.1.85-1.1 1.7V14" />
      <circle cx="12" cy="17" r="0.8" fill="currentColor" stroke="none" />
    </Icon>
  ),
}

/**
 * 工具条菜单。★ 客户端布局里「主机」不再是中央页面而是**常驻顶部 dock**，
 * 所以 MENUS 里没有 sessions —— go('sessions') 在 go() 里被拦截成开 dock。
 */
const MENUS: Menu[] = [
  { id: 'dashboard', label: '概览', ico: 'dashboard', group: '总览', shortcut: 'G D' },
  { id: 'delivery', label: '投递', ico: 'delivery', group: '作战', shortcut: 'G W' },
  { id: 'listeners', label: '监听器', ico: 'listeners', group: '作战', shortcut: 'G L' },
  { id: 'generate', label: '载荷', ico: 'generate', group: '作战', shortcut: 'G P' },
  { id: 'tasks', label: '任务', ico: 'tasks', group: '作战', shortcut: 'G T' },
  { id: 'bofs', label: 'BOF 模块', ico: 'bofs', group: '作战', shortcut: 'G B' },
  { id: 'argfuscator', label: '参数混淆', ico: 'argfuscator', group: '作战', shortcut: 'G A' },
  { id: 'extkit', label: '扩展命令', ico: 'extkit', group: '作战', shortcut: 'G X' },
  { id: 'downloads', label: '下载', ico: 'downloads', group: '资产' },
  { id: 'tunnels', label: '隧道', ico: 'tunnels', group: '资产' },
  { id: 'screens', label: '截图', ico: 'screens', group: '资产' },
  { id: 'creds', label: '凭据', ico: 'creds', group: '资产' },
  { id: 'targets', label: '目标', ico: 'targets', group: '资产' },
  { id: 'graph', label: '拓扑', ico: 'graph', group: '协作' },
  { id: 'chat', label: '聊天', ico: 'chat', group: '协作' },
  { id: 'ai', label: 'AI 协作', ico: 'ai', group: '协作', shortcut: 'G I' },
  { id: 'relay', label: '中转', ico: 'relay', group: '系统' },
  { id: 'logs', label: '日志', ico: 'logs', group: '系统', shortcut: 'G O' },
]

const PRIMARY_MENU_IDS = ['dashboard', 'delivery', 'listeners', 'generate', 'tasks', 'ai'] as const

/** 主机 dock 高度拖动的钳位（与 store 初始校验一致） */
const HOSTS_H_MIN = 160, HOSTS_H_MAX = 640
const EVENTS_H_MIN = 100, EVENTS_H_MAX = 480

/** 分隔条键盘调整（ARIA APG 语义：方向键**移动分隔条本身**）。
 *  hosts 条在面板**下沿**：↓=条下移=面板增高，↑=减；
 *  events 条在面板**上沿**：↑=条上移=面板增高，↓=减。Home/End 到边界。 */
function resizerKeyDown(which: 'hosts' | 'events', e: React.KeyboardEvent) {
  const s = useStore.getState()
  const grow = which === 'hosts' ? e.key === 'ArrowDown' : e.key === 'ArrowUp'
  const shrink = which === 'hosts' ? e.key === 'ArrowUp' : e.key === 'ArrowDown'
  if (!grow && !shrink && e.key !== 'Home' && e.key !== 'End') return
  e.preventDefault()
  if (which === 'hosts') {
    const h = e.key === 'Home' ? HOSTS_H_MAX : e.key === 'End' ? HOSTS_H_MIN
      : s.hostsDockH + (grow ? 24 : -24)
    s.setDockHeights(Math.min(HOSTS_H_MAX, Math.max(HOSTS_H_MIN, h)), s.eventsDockH)
  } else {
    const h = e.key === 'Home' ? EVENTS_H_MAX : e.key === 'End' ? EVENTS_H_MIN
      : s.eventsDockH + (grow ? 24 : -24)
    s.setDockHeights(s.hostsDockH, Math.min(EVENTS_H_MAX, Math.max(EVENTS_H_MIN, h)))
  }
}

export default function App() {
  // ALL hooks must be above any conditional return
  const connected = useStore((s) => s.connected)
  const wsConnected = useStore((s) => s.wsConnected)
  const syncing = useStore((s) => s.syncing)
  const username = useStore((s) => s.username)
  const tab = useStore((s) => s.tab)
  const setTab = useStore((s) => s.setTab)
  const agents = useStore((s) => s.agents) || {}
  const listeners = useStore((s) => s.listeners) || {}
  const tasks = useStore((s) => s.tasks) || []
  const selected = useStore((s) => s.selectedAgentId)
  const disconnect = useStore((s) => s.disconnect)
  const refreshRest = useStore((s) => s.refreshRest)
  const tryRestoreSession = useStore((s) => s.tryRestoreSession)
  const [restoring, setRestoring] = useState(true)
  const openTabs = useStore((s) => s.uiTabs) || ['sessions']
  const activeUiTab = useStore((s) => s.activeUiTab)
  const openUiTab = useStore((s) => s.openUiTab)
  const closeUiTab = useStore((s) => s.closeUiTab)
  const setActiveUiTab = useStore((s) => s.setActiveUiTab)
  const lastError = useStore((s) => s.lastError)
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)

  // 客户端 dock 状态
  const eventsDockOpen = useStore((s) => s.eventsDockOpen)
  const eventsDockH = useStore((s) => s.eventsDockH)
  const toggleEventsDock = useStore((s) => s.toggleEventsDock)
  const setDockHeights = useStore((s) => s.setDockHeights)
  /** 'hosts' | 'events' | null —— 正在拖哪条分隔条 */
  const [dragging, setDragging] = useState<'hosts' | 'events' | null>(null)

  const [density, setDensity] = useState<Density>(loadDensity)
  // 客户端主题：默认 Adaptix Dark（暗色）。
  // ★ 迁移：上一代（山水太极）把用户强制留在浅色；客户端布局以暗色为准，
  //   没打过 'client-dark' 标记的老用户一次性迁到暗色，之后 ☀/☾ 随意切。
  const [dark, setDark] = useState(() => {
    if (localStorage.getItem('adaptix.theme.migrated') !== 'client-dark') {
      saveTheme('dark')
      localStorage.setItem('adaptix.theme.migrated', 'client-dark')
      return true
    }
    return loadTheme() === 'dark'
  })
  useEffect(() => {
    saveTheme(dark ? 'dark' : 'light')
  }, [dark])
  const [paletteOpen, setPaletteOpen] = useState(false)
  const [helpOpen, setHelpOpen] = useState(false)
  const [chord, setChord] = useState<string | null>(null)

  // 首页保持干净，主机表和事件流由工具条按需展开。
  // 迁移一次旧版默认布局，避免已有浏览器把概览夹在两个 dock 中间。
  useEffect(() => {
    const key = 'adaptix.layout.simple-v1'
    if (localStorage.getItem(key)) return
    useStore.setState({ hostsDockOpen: false, eventsDockOpen: false })
    try {
      localStorage.setItem('adaptix.dock.hosts', '0')
      localStorage.setItem('adaptix.dock.events', '0')
      localStorage.setItem(key, '1')
    } catch {
      /* localStorage 不可用时保留内存态 */
    }
  }, [])

  const agentCount = Object.keys(agents || {}).length
  const listenerCount = Object.keys(listeners || {}).length
  const onlineCount = Object.values(agents || {}).filter((a: any) => isAgentOnline(a)).length

  useEffect(() => {
    saveDensity(density)
  }, [density])

  // F5：从 localStorage 恢复 JWT 会话，避免刷新后必须重新登录
  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        if (!useStore.getState().connected) {
          await tryRestoreSession()
        }
      } catch {
        /* tryRestoreSession 内部已清会话 */
      } finally {
        if (!cancelled) setRestoring(false)
      }
    })()
    return () => { cancelled = true }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // periodic REST refresh fallback when WS is down
  useEffect(() => {
    if (!connected) return
    const tick = () => {
      if (!useStore.getState().wsConnected) {
        refreshRest().catch(() => {})
      }
    }
    const id = window.setInterval(tick, 15000)
    return () => clearInterval(id)
  }, [connected, refreshRest])

  // dock 分隔条拖动：mousemove 全局挂，松手落 localStorage（setDockHeights）
  useEffect(() => {
    if (!dragging) return
    const onMove = (e: MouseEvent) => {
      const s = useStore.getState()
      if (dragging === 'hosts') {
        // hosts dock 在视口顶部（工具条+标签条之下），向下拖增高
        const top = (document.querySelector('.cl-bar')?.getBoundingClientRect().bottom || 0)
          + (document.querySelector('.cl-tabs')?.getBoundingClientRect().height || 0)
        const h = Math.round(Math.min(HOSTS_H_MAX, Math.max(HOSTS_H_MIN, e.clientY - top - 14)))
        if (h !== s.hostsDockH) useStore.setState({ hostsDockH: h })
      } else {
        // events dock 贴底（状态栏之上），向上拖增高
        const bottom = document.querySelector('.ops-status')?.getBoundingClientRect().top || window.innerHeight
        const h = Math.round(Math.min(EVENTS_H_MAX, Math.max(EVENTS_H_MIN, bottom - e.clientY - 14)))
        if (h !== s.eventsDockH) useStore.setState({ eventsDockH: h })
      }
    }
    const onUp = () => {
      const s = useStore.getState()
      setDockHeights(s.hostsDockH, s.eventsDockH)
      setDragging(null)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [dragging, setDockHeights])

  const taskCount = tasks.length

  function focusHostsSearch() {
    requestAnimationFrame(() => {
      const el = document.querySelector<HTMLInputElement>('.sess-search')
      if (el) { el.focus(); el.select() }
    })
  }

  /** 主机是默认工作区；导航后直接聚焦搜索（G H / 命令面板共用此路径） */
  function go(id: TabId) {
    if (id === 'sessions') {
      useStore.setState({ hostsDockOpen: false, eventsDockOpen: false })
      setTab('sessions')
      openUiTab?.('sessions')
      focusHostsSearch()
      return
    }
    if (id === 'dashboard') {
      useStore.setState({ hostsDockOpen: false, eventsDockOpen: false })
      try {
        localStorage.setItem('adaptix.dock.hosts', '0')
        localStorage.setItem('adaptix.dock.events', '0')
      } catch { /* ignore */ }
    }
    try {
      setTab(id)
      openUiTab?.(id)
    } catch (e) {
      console.error(e)
      setTab(id)
    }
  }

  // Global shortcuts (only when connected shell is mounted)
  useEffect(() => {
    if (!connected) return

    const isTyping = (t: EventTarget | null) => {
      const el = t as HTMLElement | null
      if (!el) return false
      const tag = el.tagName
      return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || el.isContentEditable
    }

    const onKey = (e: KeyboardEvent) => {
      const mod = e.ctrlKey || e.metaKey

      // Ctrl/Cmd+K command palette
      if (mod && (e.key === 'k' || e.key === 'K')) {
        e.preventDefault()
        setPaletteOpen((v) => !v)
        setHelpOpen(false)
        setChord(null)
        return
      }

      // ? open shortcut help (when not typing)
      if (!mod && e.key === '?' && !isTyping(e.target)) {
        e.preventDefault()
        setHelpOpen((v) => !v)
        setChord(null)
        return
      }

      // Ctrl/Cmd+Shift+F / P for selected agent tools (online only; store also guards)
      if (mod && e.shiftKey && selected) {
        const a = useStore.getState().agents[selected]
        const live = a ? isAgentOnline(a) : false
        if (e.key === 'F' || e.key === 'f') {
          e.preventDefault()
          if (!live) return
          openFilesBrowser(selected)
          return
        }
        if (e.key === 'P' || e.key === 'p') {
          e.preventDefault()
          if (!live) return
          openProcsBrowser(selected)
          return
        }
      }

      if (paletteOpen) return
      if (isTyping(e.target)) return

      // Esc close overlays / dynamic tab
      if (e.key === 'Escape') {
        if (helpOpen) { e.preventDefault(); setHelpOpen(false); return }
        if (paletteOpen) { e.preventDefault(); setPaletteOpen(false); return }
        const cur = (useStore.getState().activeUiTab || useStore.getState().tab) as TabId
        if (isInteractTab(cur)) {
          e.preventDefault()
          closeUiTab(cur)
        }
        setChord(null)
        return
      }

      // G then letter navigation chord
      if (chord === 'g') {
        e.preventDefault()
        const k = e.key.toLowerCase()
        const map: Record<string, TabId> = {
          d: 'dashboard',
          h: 'sessions',
          l: 'listeners',
          p: 'generate',
          w: 'delivery',
          t: 'tasks',
          b: 'bofs',
          a: 'argfuscator',
          x: 'extkit',
          o: 'logs',
          i: 'ai',
        }
        const dest = map[k]
        if (dest) go(dest)
        setChord(null)
        return
      }

      if (!mod && (e.key === 'g' || e.key === 'G')) {
        e.preventDefault()
        setChord('g')
        window.setTimeout(() => setChord((c) => (c === 'g' ? null : c)), 1200)
        return
      }

      if (!mod && e.key === '/') {
        // 主机工作区：直接聚焦搜索
        e.preventDefault()
        useStore.setState({ hostsDockOpen: false, eventsDockOpen: false })
        setTab('sessions')
        openUiTab?.('sessions')
        focusHostsSearch()
      }
    }

    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
    // ★ `chord` 必须在依赖里：onKey 读闭包值，setChord 不会让本 effect 重跑
  }, [connected, paletteOpen, helpOpen, selected, openFilesBrowser, openProcsBrowser, closeUiTab, chord, openUiTab, setTab])

  // early return AFTER all hooks
  if (restoring) {
    return (
      <>
        <div className="lx" style={{ display: 'grid', placeItems: 'center', minHeight: '100dvh' }}>
          <div className="dim">恢复会话…</div>
        </div>
        <ToastHost />
      </>
    )
  }

  if (!connected) {
    return (
      <>
        <Login />
        <ToastHost />
      </>
    )
  }

  const current = (activeUiTab || tab || 'sessions') as TabId
  const tabs = openTabs?.length ? openTabs : [current]

  function tabLabel(id: string) {
    if (id === 'sessions') return '主机'
    if (id === 'terminal') return '命令终端'
    const staticLabel = MENUS.find((m) => m.id === id)?.label
    if (staticLabel) return staticLabel
    if (isFilesTab(id)) {
      const aid = tabAgentId(id)
      const a = aid ? agents[aid] : null
      return `文件 · ${a?.a_computer || aid || '?'}`
    }
    if (isProcsTab(id)) {
      const aid = tabAgentId(id)
      const a = aid ? agents[aid] : null
      return `进程 · ${a?.a_computer || aid || '?'}`
    }
    if (isPtyTab(id)) {
      const aid = tabAgentId(id)
      const a = aid ? agents[aid] : null
      return `PTY · ${a?.a_computer || aid || '?'}`
    }
    if (isCmdTab(id)) {
      const aid = tabAgentId(id)
      const a = aid ? agents[aid] : null
      return `命令执行 · ${a?.a_computer || aid || '?'}`
    }
    if (isVncTab(id)) {
      const aid = tabAgentId(id)
      const a = aid ? agents[aid] : null
      return `VNC · ${a?.a_computer || aid || '?'}`
    }
    return id
  }

  const filesAgent = isFilesTab(current) ? tabAgentId(current) : null
  const procsAgent = isProcsTab(current) ? tabAgentId(current) : null
  // Keep ALL open PTY/VNC sessions mounted so switching tabs does not kill WS
  const openPtyTabs = tabs.filter((id) => isPtyTab(id))
  const openVncTabs = tabs.filter((id) => isVncTab(id))

  return (
    <div className="ops" data-theme={dark ? 'dark' : 'light'} data-density={density}>
      {/* ── 顶部工具条（Qt 客户端 topHLayout 的 Web 形态） ── */}
      <header className="cl-bar">
        <button type="button" className="cl-brand" onClick={() => go('dashboard')} title="Console">
          <span className="cl-mark" aria-hidden>{'>_'}</span>
          <span className="cl-brand-name">Console</span>
        </button>
        <div className="cl-tools">
          {/* 高频工作区：保持浩劫式的菜单密度，低频功能走命令面板 */}
          <button
            type="button"
            className={`cl-btn ${current === 'sessions' ? 'active' : ''}`}
            onClick={() => go('sessions')}
            title={`主机 (${onlineCount}/${agentCount}) — G H`}
          >
            {ICO.sessions}
            <span className="cl-btn-label">主机</span>
            <span className={`cl-badge ${onlineCount === 0 ? 'muted' : ''}`}>{agentCount}</span>
          </button>
          <span className="cl-sep" />
          {PRIMARY_MENU_IDS.map((id) => {
            const m = MENUS.find((item) => item.id === id)
            if (!m) return null
            return (
              <button
                key={m.id}
                type="button"
                className={`cl-btn ${current === m.id ? 'active' : ''}`}
                onClick={() => go(m.id)}
                title={m.shortcut ? `${m.label} (${m.shortcut})` : m.label}
              >
                {ICO[m.ico] || null}
                <span className="cl-btn-label">{m.label}</span>
              </button>
            )
          })}
          <span className="cl-sep" />
          <button
            type="button"
            className={`cl-btn ${eventsDockOpen ? 'active' : ''}`}
            onClick={toggleEventsDock}
            title="事件日志"
          >
            {ICO.events}
            <span className="cl-btn-label">事件</span>
          </button>
          {chord === 'g' && <span className="cl-chord">G …</span>}
        </div>
        <div className="cl-right">
          <span className="cl-pill">
            <span className="signal">{onlineCount}</span>
            <span>/{agentCount}</span>
          </span>
          <span className="cl-pill">{listenerCount} 监听</span>
          <button type="button" className="cl-ghost" onClick={() => setPaletteOpen(true)} title="更多功能与命令面板 ⌘K">更多</button>
          <button type="button" className="cl-ghost" title="刷新" onClick={() => refreshRest().catch(() => {})}>↻</button>
          <button type="button" className="cl-ghost" title={dark ? '浅色' : '深色'} onClick={() => setDark((v) => !v)}>
            {dark ? '☀' : '☾'}
          </button>
          <button type="button" className="cl-ghost" title="密度" onClick={() => setDensity((d) => d === 'compact' ? 'comfortable' : 'compact')}>
            {density === 'compact' ? '▣' : '▢'}
          </button>
          <span className="cl-user" title={username}>{username}</span>
          <button type="button" className="cl-ghost" onClick={() => disconnect()}>退出</button>
        </div>
      </header>

      {/* ── 中央工作区：标签条 + 面板 ── */}
      <div className="cl-main">
        <div className="cl-tabs">
          {tabs.map((id) => {
            const label = tabLabel(id)
            return (
              <button
                key={id}
                type="button"
                className={`cl-tab ${current === id ? 'active' : ''}`}
                onClick={() => { setActiveUiTab?.(id as TabId); setTab(id as TabId) }}
                title={id}
              >
                {label}
                {tabs.length > 1 && (
                  <span className="x" onClick={(e) => { e.stopPropagation(); closeUiTab?.(id as TabId) }}>×</span>
                )}
              </button>
            )
          })}
        </div>

        <div className="ops-body content">
          {current === 'sessions' && <Hosts />}
          {current === 'terminal' && <Term />}
          {current === 'dashboard' && <Dashboard online={onlineCount} total={agentCount} listeners={listenerCount} />}
          {current === 'listeners' && <Listeners />}
          {current === 'generate' && <Generate />}
          {current === 'delivery' && <WebDelivery />}
          {current === 'tasks' && <Tasks />}
          {current === 'downloads' && <Downloads />}
          {current === 'tunnels' && <Tunnels />}
          {current === 'screens' && <Screenshots />}
          {current === 'creds' && <Credentials />}
          {current === 'targets' && <Targets />}
          {current === 'graph' && <Graph />}
          {current === 'chat' && <Chat />}
          {current === 'ai' && <AICollab />}
          {current === 'bofs' && <Bofs />}
          {current === 'argfuscator' && <ArgFuscator />}
          {current === 'extkit' && <ExtKit />}
          {current === 'relay' && <Relay />}
          {current === 'logs' && <Logs />}
          {filesAgent && <FileManager agentId={filesAgent} />}
          {procsAgent && <ProcessManager agentId={procsAgent} />}
          {openPtyTabs.map((id) => {
            const aid = tabAgentId(id)
            if (!aid) return null
            return <PtyTerm key={id} tabId={id} agentId={aid} active={current === id} />
          })}
          {/* 命令执行台：全屏终端式，一台主机可多开（cmd:<id>#n）。
              与 VNC 一样 keep mounted —— 切走再切回来，草稿和滚动位置都还在。 */}
          {(openTabs || []).filter(isCmdTab).map((id) => {
            const aid = tabAgentId(id)
            if (!aid) return null
            return (
              <div key={id} style={{ display: current === id ? 'contents' : 'none', height: '100%', minHeight: 0 }}>
                <CmdExec agentId={aid} />
              </div>
            )
          })}
          {openVncTabs.map((id) => {
            const aid = tabAgentId(id)
            if (!aid) return null
            // VNC panel: keep mounted when tab open; hide when inactive
            return (
              <div key={id} style={{ display: current === id ? 'contents' : 'none', height: '100%', minHeight: 0 }}>
                <VncPanel agentId={aid} />
              </div>
            )
          })}
        </div>
      </div>

      {/* ── 事件日志 dock（底部常驻，Qt LogsDock 的 Web 形态） ── */}
      {eventsDockOpen && (
        <>
          <div
            className={`cl-resizer ${dragging === 'events' ? 'dragging' : ''}`}
            onMouseDown={() => setDragging('events')}
            onKeyDown={(e) => resizerKeyDown('events', e)}
            tabIndex={0}
            role="separator"
            aria-orientation="horizontal"
            aria-label="调整事件面板高度（方向键 ±24px）"
          />
          <section className="cl-dock events" style={{ height: eventsDockH }}>
            <div className="cl-dock-hd">
              <span className="cl-dock-title">事件日志</span>
              <div className="cl-dock-tools">
                <button type="button" className="cl-dock-btn" title="收起事件面板" onClick={toggleEventsDock}>▴</button>
              </div>
            </div>
            <div className="cl-dock-bd">
              <EventsDock />
            </div>
          </section>
        </>
      )}

      {/* ── 状态栏（连接态与计数，客户端底部状态行的 Web 形态） ── */}
      <footer className="ops-status">
        <ConnStatus connected={connected} wsConnected={wsConnected} syncing={syncing} />
        {lastError && !wsConnected ? <span className="ops-status-note warn">通道降级</span> : null}
        <span className="vsep" />
        <span className="ops-status-counts">{onlineCount} 在线 · {listenerCount} 监听 · {taskCount} 任务</span>
        <span className="spacer" />
        <span className="dim">⌘K 命令 · G H 主机 · ? 帮助</span>
      </footer>

      <CommandPalette open={paletteOpen} onClose={() => setPaletteOpen(false)} go={go} />
      <ShortcutHelp open={helpOpen} onClose={() => setHelpOpen(false)} />
      <ToastHost />
    </div>
  )
}

import React, { useEffect, useMemo, useState } from 'react'
import { Login } from './components/Login'
import { Dashboard } from './components/Dashboard'
import { Hosts } from './components/Hosts'
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
import { Graph } from './components/Graph'
import { FileManager } from './components/FileManager'
import { ProcessManager } from './components/ProcessManager'
import { PtyTerm } from './components/PtyTerm'
import { VncPanel } from './components/VncPanel'
import { ToastHost } from './components/Toast'
import { ConnStatus } from './components/StatusPill'
import { CommandPalette } from './components/CommandPalette'
import { ShortcutHelp } from './components/ui'
import { useStore, type TabId, isFilesTab, isProcsTab, isPtyTab, isVncTab, isInteractTab, tabAgentId } from './store'
import {
  clamp, isAgentOnline, loadDensity, loadTermHeight, loadTheme,
  saveDensity, saveTermHeight, saveTheme, type Density,
} from './utils'

type Menu = { id: TabId; label: string; ico: string; group?: string; shortcut?: string }

/** 统一 stroke 图标 — 显式 currentColor，保证墨轨上可见 */
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
      <path d="M4 6h.01M4 12h.01M4 18h.01" strokeWidth="2.4" />
    </Icon>
  ),
  chat: (
    <Icon>
      <path d="M5 6h14v9H8l-3 3V6z" />
      <path d="M9 10h6M9 13h4" />
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
  command: (
    <Icon>
      <path d="M8 8H6.5A2.5 2.5 0 1 1 9 5.5V8h6V5.5A2.5 2.5 0 1 1 17.5 8H16" />
      <path d="M8 16H6.5A2.5 2.5 0 1 0 9 18.5V16h6v2.5a2.5 2.5 0 1 0 2.5-2.5H16" />
      <path d="M9 8h6M9 16h6" />
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

const MENUS: Menu[] = [
  { id: 'dashboard', label: '概览', ico: 'dashboard', group: '总览', shortcut: 'G D' },
  { id: 'sessions', label: '主机', ico: 'sessions', group: '作战', shortcut: 'G H' },
  { id: 'delivery', label: '投递', ico: 'delivery', group: '作战', shortcut: 'G W' },
  { id: 'listeners', label: '监听器', ico: 'listeners', group: '作战', shortcut: 'G L' },
  { id: 'generate', label: '载荷', ico: 'generate', group: '作战', shortcut: 'G P' },
  { id: 'tasks', label: '任务', ico: 'tasks', group: '作战', shortcut: 'G T' },
  { id: 'downloads', label: '下载', ico: 'downloads', group: '资产' },
  { id: 'tunnels', label: '隧道', ico: 'tunnels', group: '资产' },
  { id: 'screens', label: '截图', ico: 'screens', group: '资产' },
  { id: 'creds', label: '凭据', ico: 'creds', group: '资产' },
  { id: 'targets', label: '目标', ico: 'targets', group: '资产' },
  { id: 'graph', label: '拓扑', ico: 'graph', group: '协作' },
  { id: 'chat', label: '聊天', ico: 'chat', group: '协作' },
  { id: 'logs', label: '日志', ico: 'logs', group: '系统', shortcut: 'G O' },
]

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
  const openTabs = useStore((s) => s.uiTabs) || ['dashboard']
  const activeUiTab = useStore((s) => s.activeUiTab)
  const openUiTab = useStore((s) => s.openUiTab)
  const closeUiTab = useStore((s) => s.closeUiTab)
  const setActiveUiTab = useStore((s) => s.setActiveUiTab)
  const lastError = useStore((s) => s.lastError)
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)
  const openPty = useStore((s) => s.openPty)
  const openVnc = useStore((s) => s.openVnc)

  const [dark, setDark] = useState(() => {
    // 强制默认阳面：旧 localStorage dark 从 Signal Deck 残留时清掉
    const t = loadTheme()
    if (t === 'dark' && localStorage.getItem('adaptix.theme.migrated') !== 'shanshui') {
      saveTheme('light')
      localStorage.setItem('adaptix.theme.migrated', 'shanshui')
      return false
    }
    return t === 'dark'
  })
  const [density, setDensity] = useState<Density>(loadDensity)
  const [termH, setTermH] = useState(loadTermHeight)
  const [dragging, setDragging] = useState(false)
  const [paletteOpen, setPaletteOpen] = useState(false)
  const [helpOpen, setHelpOpen] = useState(false)
  const [chord, setChord] = useState<string | null>(null)

  const agentCount = Object.keys(agents || {}).length
  const listenerCount = Object.keys(listeners || {}).length
  const onlineCount = Object.values(agents || {}).filter((a: any) => isAgentOnline(a)).length

  const badges = useMemo(() => ({
    sessions: agentCount,
    listeners: listenerCount,
  }), [agentCount, listenerCount])

  useEffect(() => {
    const theme = dark ? 'dark' : 'light'
    saveTheme(theme)
    // 壳与 html 双写，避免 token 只挂在 html 时 ops 不同步
    document.documentElement.setAttribute('data-theme', theme)
  }, [dark])

  useEffect(() => {
    saveDensity(density)
  }, [density])

  useEffect(() => {
    document.documentElement.style.setProperty('--term-h', `${termH}px`)
  }, [termH])

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

  useEffect(() => {
    if (!dragging) return
    const onMove = (e: MouseEvent) => {
      const layout = document.querySelector('.content.with-term') as HTMLElement | null
      if (!layout) return
      const rect = layout.getBoundingClientRect()
      const h = clamp(rect.bottom - e.clientY, 160, 560)
      setTermH(h)
    }
    const onUp = () => {
      setDragging(false)
      setTermH((h) => { saveTermHeight(h); return h })
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [dragging])

  const taskCount = tasks.length

  function go(id: TabId) {
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
          o: 'logs',
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
        // focus host search if on sessions, else open palette
        if ((useStore.getState().activeUiTab || useStore.getState().tab) === 'sessions') {
          const el = document.querySelector<HTMLInputElement>('.sess-search')
          if (el) { e.preventDefault(); el.focus(); el.select(); return }
        }
        e.preventDefault()
        setPaletteOpen(true)
      }
    }

    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [connected, paletteOpen, helpOpen, selected, openFilesBrowser, openProcsBrowser, closeUiTab])

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

  const current = (activeUiTab || tab || 'dashboard') as TabId
  const tabs = openTabs?.length ? openTabs : [current]
  const selectedAgent = selected ? agents[selected] : null

  function tabLabel(id: string) {
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
    if (isVncTab(id)) {
      const aid = tabAgentId(id)
      const a = aid ? agents[aid] : null
      return `VNC · ${a?.a_computer || aid || '?'}`
    }
    return id
  }

  const title = tabLabel(current)
  const filesAgent = isFilesTab(current) ? tabAgentId(current) : null
  const procsAgent = isProcsTab(current) ? tabAgentId(current) : null
  // Keep ALL open PTY/VNC sessions mounted so switching tabs does not kill WS
  const openPtyTabs = tabs.filter((id) => isPtyTab(id))
  const openVncTabs = tabs.filter((id) => isVncTab(id))
  const vncAgent = isVncTab(current) ? tabAgentId(current) : null

  const primaryMenus = MENUS.filter((m) => m.group === '总览' || m.group === '作战')
  // 协作（拓扑/聊天）与资产、系统一并进侧栏
  const assetMenus = MENUS.filter((m) => m.group === '资产' || m.group === '协作' || m.group === '系统')

  return (
    <div
      className="ops"
      data-theme={dark ? 'dark' : 'light'}
      data-density={density}
    >
      <div className="ink-scape" aria-hidden>
        <div className="ink-wash ink-wash-a" />
        <div className="ink-wash ink-wash-b" />
        <div className="ink-ridge" />
        <div className="ink-mist" />
      </div>
      <nav className="rail" aria-label="主导航" data-rail="ink">
        <button type="button" className="rail-brand" onClick={() => go('dashboard')} title="Adaptix">
          <span className="rail-mark" aria-hidden><i /></span>
          <span className="rail-brand-txt">Adaptix</span>
        </button>
        <div className="rail-section">
          {primaryMenus.map((m) => (
            <button
              key={m.id}
              type="button"
              className={`rail-item ${current === m.id || (m.id === 'sessions' && isInteractTab(current)) ? 'active' : ''}`}
              onClick={() => go(m.id)}
              title={m.shortcut ? `${m.label} (${m.shortcut})` : m.label}
            >
              <span className="rail-ico" aria-hidden>{ICO[m.ico] || null}</span>
              <span className="rail-label">{m.label}</span>
              {(m.id === 'sessions' || m.id === 'listeners') && (
                <span className="rail-badge">{(badges as any)[m.id] ?? 0}</span>
              )}
            </button>
          ))}
        </div>
        <div className="rail-gap" />
        <div className="rail-section muted-sec">
          {assetMenus.map((m) => (
            <button
              key={m.id}
              type="button"
              className={`rail-item ${current === m.id ? 'active' : ''}`}
              onClick={() => go(m.id)}
              title={m.label}
            >
              <span className="rail-ico" aria-hidden>{ICO[m.ico] || null}</span>
              <span className="rail-label">{m.label}</span>
            </button>
          ))}
        </div>
        <div className="rail-foot">
          <button type="button" className="rail-item" onClick={() => setPaletteOpen(true)} title="命令面板 ⌘K">
            <span className="rail-ico" aria-hidden>{ICO.command}</span>
            <span className="rail-label">命令</span>
          </button>
          <button type="button" className="rail-item" onClick={() => setHelpOpen(true)} title="快捷键">
            <span className="rail-ico" aria-hidden>{ICO.help}</span>
            <span className="rail-label">帮助</span>
          </button>
        </div>
      </nav>

      <div className="ops-main">
        <header className="ops-top">
          <div className="ops-top-left">
            <div className="ops-title">
              <span className="ops-page">{title}</span>
              {selectedAgent ? (
                <span className="ops-target">
                  <span className={`sdot ${isAgentOnline(selectedAgent) ? 'on' : 'off'}`} />
                  <span className="mono">{selectedAgent.a_computer || selected}</span>
                  {selectedAgent.a_internal_ip ? <span className="mono dim"> · {selectedAgent.a_internal_ip}</span> : null}
                </span>
              ) : null}
              {chord === 'g' && <span className="chord-hint">G …</span>}
            </div>
          </div>
          <div className="ops-top-center">
            <button type="button" className="ops-search" onClick={() => setPaletteOpen(true)}>
              <span>搜索</span>
              <kbd className="kbd">⌘K</kbd>
            </button>
          </div>
          <div className="ops-top-right">
            <div className="ops-pills">
              <span className="ops-pill">
                <span className="signal">{onlineCount}</span>
                <span className="dim">/{agentCount}</span>
              </span>
              <span className="ops-pill dim">{listenerCount} 监听</span>
              <ConnStatus connected={connected} wsConnected={wsConnected} syncing={syncing} />
            </div>
            <span className="ops-user">{username}</span>
            <button type="button" className="ghost ico" title="刷新" onClick={() => refreshRest().catch(() => {})}>↻</button>
            <button type="button" className="ghost ico" title={dark ? '浅色' : '深色'} onClick={() => setDark((v) => !v)}>
              {dark ? '☀' : '☾'}
            </button>
            <button type="button" className="ghost ico" title="密度" onClick={() => setDensity((d) => d === 'compact' ? 'comfortable' : 'compact')}>
              {density === 'compact' ? '▣' : '▢'}
            </button>
            <button type="button" className="ghost" onClick={() => disconnect()}>退出</button>
          </div>
        </header>

        <div className="ops-tabs">
          {tabs.map((id) => {
            const label = tabLabel(id)
            return (
              <button
                key={id}
                type="button"
                className={`ops-tab ${current === id ? 'active' : ''}`}
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

        <div className={`ops-body content ${current === 'sessions' ? 'with-term' : ''}`}>
          {current === 'dashboard' && <Dashboard online={onlineCount} total={agentCount} listeners={listenerCount} />}
          <Hosts active={current === 'sessions'} />
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
          {current === 'logs' && <Logs />}
          {filesAgent && <FileManager agentId={filesAgent} />}
          {procsAgent && <ProcessManager agentId={procsAgent} />}
          {openPtyTabs.map((id) => {
            const aid = tabAgentId(id)
            if (!aid) return null
            return <PtyTerm key={id} agentId={aid} active={current === id} />
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
          {/* legacy single vncAgent unused when openVncTabs covers it */}
          {false && vncAgent && <VncPanel agentId={vncAgent} />}
          {current === 'sessions' && (
            <div
              className={`term-resizer ${dragging ? 'active' : ''}`}
              onMouseDown={(e) => { e.preventDefault(); setDragging(true) }}
              title="拖动调整终端高度"
            />
          )}
          <Term active={current === 'sessions'} />
        </div>

        <footer className="ops-status">
          <ConnStatus connected={connected} wsConnected={wsConnected} syncing={syncing} />
          {lastError && !wsConnected ? <span className="ops-status-note warn">通道降级</span> : null}
          <span className="vsep" />
          <span className="ops-status-counts">{onlineCount} 在线 · {listenerCount} 监听 · {taskCount} 任务</span>
          <span className="spacer" />
          <span className="dim">⌘K 命令 · G H 主机 · ? 帮助</span>
        </footer>
      </div>

      <CommandPalette open={paletteOpen} onClose={() => setPaletteOpen(false)} go={go} />
      <ShortcutHelp open={helpOpen} onClose={() => setHelpOpen(false)} />
      <ToastHost />
    </div>
  )
}

import React, { memo, useEffect, useRef, useState, useSyncExternalStore } from 'react'
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
import {
  useStore,
  type TabId,
  isConsoleTab,
  isFilesTab,
  isProcsTab,
  isPtyTab,
  isVncTab,
  isCmdTab,
  tabAgentId,
} from './store'
import { t, setLang, LANG } from './i18n'
import { isAgentOnline, loadDensity, saveDensity, type Density } from './utils'
import {
  applyQtTheme,
  DEFAULT_SESSIONS_COLS,
  loadQtSettings,
  QT_CONSOLE_THEMES,
  QT_THEMES,
  qtAsset,
  saveQtSettings,
  SESSIONS_COL_META,
  type QtClientSettings,
  type QtConsoleThemeId,
  type QtFontFamily,
  type QtThemeId,
} from './design/qt-themes'

function QtImg({ src, cls = 'qt-ico qt-ico-mono' }: { src: string; cls?: string }) {
  return <img className={cls} src={qtAsset(`icons/${src}`)} alt="" draggable={false} />
}

const ICO = {
  logs: <QtImg src="news_64dp.png" />,
  listeners: <QtImg src="headphones_64dp.png" />,
  ext: <QtImg src="extension_64dp.png" />,
  sessions: <QtImg src="format_list_bulleted_64dp.png" />,
  graph: <QtImg src="hub_64dp.png" />,
  tasks: <QtImg src="job_64dp.png" />,
  chat: <QtImg src="forum_64dp.png" />,
  tunnels: <QtImg src="vpn_64dp.png" />,
  downloads: <QtImg src="downloads_64.png" />,
  targets: <QtImg src="devices_64dp.png" />,
  creds: <QtImg src="key_64dp.png" />,
  screens: <QtImg src="picture_64dp.png" />,
  refresh: <QtImg src="autorenew_64dp.png" />,
  search: <QtImg src="code_blocks_64dp.png" />,
  settings: <QtImg src="settings_account_64dp.png" />,
}

function tabIconFile(id: TabId): string {
  if (id === 'sessions') return 'format_list_bulleted_64dp.png'
  if (id === 'graph') return 'hub_64dp.png'
  if (id === 'tasks') return 'job_64dp.png'
  if (id === 'logs') return 'news_64dp.png'
  if (id === 'listeners') return 'headphones_64dp.png'
  if (id === 'chat') return 'forum_64dp.png'
  if (id === 'tunnels') return 'vpn_64dp.png'
  if (id === 'downloads') return 'downloads_64.png'
  if (id === 'targets') return 'devices_64dp.png'
  if (id === 'creds') return 'key_64dp.png'
  if (id === 'screens') return 'picture_64dp.png'
  if (isConsoleTab(id) || id === 'terminal' || isPtyTab(id) || isCmdTab(id)) return 'code_blocks_64dp.png'
  if (isFilesTab(id)) return 'folder_64dp.png'
  if (isProcsTab(id)) return 'storage_64dp.png'
  if (isVncTab(id)) return 'devices_64dp.png'
  return 'extension_64dp.png'
}

const EXTENSION_DOCKS: { id: TabId; label: string; sc?: string }[] = [
  { id: 'generate', label: '载荷生成', sc: 'G G' },
  { id: 'delivery', label: '一键投递', sc: 'G W' },
  { id: 'relay', label: '中转服务', sc: 'G R' },
  { id: 'ai', label: 'AI 协作', sc: 'G I' },
  { id: 'bofs', label: 'BOF 模块', sc: 'G B' },
  { id: 'argfuscator', label: '参数混淆', sc: 'G A' },
  { id: 'extkit', label: '扩展命令', sc: 'G X' },
  { id: 'dashboard', label: '状态概览', sc: 'G O' },
]

const STATIC_LABELS: Record<string, string> = {
  sessions: 'Sessions table',
  graph: 'Sessions graph',
  tasks: 'Jobs & Tasks',
  logs: 'Logs',
  listeners: 'Listeners & Sites',
  chat: 'Chat',
  tunnels: 'Tunnels',
  downloads: 'Downloads',
  screens: 'Screenshots',
  creds: 'Credentials',
  targets: 'Targets',
  dashboard: '概览',
  generate: '载荷生成',
  delivery: '一键投递',
  relay: '中转',
  ai: 'AI 协作',
  bofs: 'BOF 模块',
  argfuscator: '参数混淆',
  extkit: '扩展命令',
  terminal: 'Console',
}

let appClock5s = Date.now()
const appClock5sListeners = new Set<() => void>()
let appClock5sTimer: number | null = null
function subscribeAppClock5s(cb: () => void) {
  appClock5sListeners.add(cb)
  if (appClock5sTimer === null) {
    appClock5sTimer = window.setInterval(() => {
      appClock5s = Date.now()
      for (const fn of appClock5sListeners) fn()
    }, 5000)
  }
  return () => {
    appClock5sListeners.delete(cb)
    if (appClock5sListeners.size === 0 && appClock5sTimer !== null) {
      window.clearInterval(appClock5sTimer)
      appClock5sTimer = null
    }
  }
}
function getAppClock5s() { return appClock5s }

function computeOnlineCount(agents: Record<string, any>, ticks: Record<string, number>, now: number): number {
  let n = 0
  for (const a of Object.values(agents)) {
    if (isAgentOnline(a, { now, lastTick: ticks[a.a_id] })) n++
  }
  return n
}

const ListenersToolbarBtn = memo(function ListenersToolbarBtn({ active, onClick }: { active: boolean; onClick: () => void }) {
  const listenerCount = useStore((s) => Object.keys(s.listeners || {}).length)
  return (
    <button
      type="button"
      className={`qt-tb-btn ${active ? 'active' : ''}`}
      title="Listeners & Sites (G L)"
      onClick={onClick}
    >
      {ICO.listeners}
      <span className="qt-tb-lbl">{LANG === 'en' ? 'Listeners' : '监听器'}</span>
      {listenerCount > 0 ? <span className="qt-tb-badge">{listenerCount}</span> : null}
    </button>
  )
})

const SessionsToolbarBtn = memo(function SessionsToolbarBtn({ active, onClick }: { active: boolean; onClick: () => void }) {
  const agentCount = useStore((s) => Object.keys(s.agents || {}).length)
  const tickOnline = useStore((s) => computeOnlineCount(s.agents || {}, s.agentTicks || {}, Date.now()))
  const clockOnline = useSyncExternalStore(
    subscribeAppClock5s,
    () => {
      const st = useStore.getState()
      return computeOnlineCount(st.agents || {}, st.agentTicks || {}, getAppClock5s())
    },
  )
  void tickOnline
  void clockOnline
  const st = useStore.getState()
  const onlineCount = computeOnlineCount(st.agents || {}, st.agentTicks || {}, Date.now())
  return (
    <button
      type="button"
      className={`qt-tb-btn ${active ? 'active' : ''}`}
      title={`Sessions table (${onlineCount}/${agentCount}) — G H`}
      onClick={onClick}
    >
      {ICO.sessions}
      <span className="qt-tb-lbl">{LANG === 'en' ? 'Sessions' : '主机'}</span>
      <span className={`qt-tb-badge ${onlineCount === 0 ? 'muted' : ''}`}>{agentCount}</span>
    </button>
  )
})

const DockAgentDot = memo(function DockAgentDot({ agentId }: { agentId: string }) {
  const liveByTick = useStore((s) => {
    const a = s.agents?.[agentId]
    return a ? isAgentOnline(a, { lastTick: s.agentTicks?.[agentId] }) : false
  })
  const liveByClock = useSyncExternalStore(
    subscribeAppClock5s,
    () => {
      const a = useStore.getState().agents?.[agentId]
      return a ? isAgentOnline(a, { now: getAppClock5s() }) : false
    },
  )
  void liveByTick
  void liveByClock
  const a = useStore.getState().agents?.[agentId]
  const live = a ? isAgentOnline(a) : false
  return <span className={`qt-tab-dot ${live ? 'on' : 'off'}`} />
})

const DockTabText = memo(function DockTabText({ id }: { id: TabId }) {
  const aid = tabAgentId(id)
  const host = useStore((s) => (aid ? (s.agents?.[aid]?.a_computer || aid) : ''))
  if (STATIC_LABELS[id]) return <>{LANG === 'en' ? STATIC_LABELS[id] : t(STATIC_LABELS[id])}</>
  const h = host || '?'
  if (isConsoleTab(id)) return <>{`Console [${h}]`}</>
  if (isFilesTab(id)) return <>{`Files [${h}]`}</>
  if (isProcsTab(id)) return <>{`Processes [${h}]`}</>
  if (isPtyTab(id)) return <>{`Terminal [${h}]`}</>
  if (isCmdTab(id)) return <>{`Cmd [${h}]`}</>
  if (isVncTab(id)) return <>{`VNC [${h}]`}</>
  return <>{id}</>
})

const SouthStatusCounts = memo(function SouthStatusCounts() {
  const agentCount = useStore((s) => Object.keys(s.agents || {}).length)
  const listenerCount = useStore((s) => Object.keys(s.listeners || {}).length)
  const taskCount = useStore((s) => (s.tasks || []).length)
  const tickOnline = useStore((s) => computeOnlineCount(s.agents || {}, s.agentTicks || {}, Date.now()))
  const clockOnline = useSyncExternalStore(
    subscribeAppClock5s,
    () => {
      const st = useStore.getState()
      return computeOnlineCount(st.agents || {}, st.agentTicks || {}, getAppClock5s())
    },
  )
  void tickOnline
  void clockOnline
  const st = useStore.getState()
  const onlineCount = computeOnlineCount(st.agents || {}, st.agentTicks || {}, Date.now())
  return (
    <div className="qt-south-stat">
      <span><b>{onlineCount}</b> / {agentCount} {t('在线')}</span>
      <span>{listenerCount} {t('监听')}</span>
      <span>{taskCount} {t('任务')}</span>
    </div>
  )
})

const DashboardConnected = memo(function DashboardConnected() {
  const agentCount = useStore((s) => Object.keys(s.agents || {}).length)
  const listenerCount = useStore((s) => Object.keys(s.listeners || {}).length)
  const tickOnline = useStore((s) => computeOnlineCount(s.agents || {}, s.agentTicks || {}, Date.now()))
  const clockOnline = useSyncExternalStore(
    subscribeAppClock5s,
    () => {
      const st = useStore.getState()
      return computeOnlineCount(st.agents || {}, st.agentTicks || {}, getAppClock5s())
    },
  )
  void tickOnline
  void clockOnline
  const st = useStore.getState()
  return <Dashboard online={computeOnlineCount(st.agents || {}, st.agentTicks || {}, Date.now())} total={agentCount} listeners={listenerCount} />
})

export default function App() {
  const api = useStore((s) => s.api)
  const connected = useStore((s) => s.connected)
  const wsConnected = useStore((s) => s.wsConnected)
  const syncing = useStore((s) => s.syncing)
  const username = useStore((s) => s.username)
  const selected = useStore((s) => s.selectedAgentId)
  const disconnect = useStore((s) => s.disconnect)
  const finishConnect = useStore((s) => s.finishConnect)
  const refreshRest = useStore((s) => s.refreshRest)
  const tryRestoreSession = useStore((s) => s.tryRestoreSession)
  const openUiTab = useStore((s) => s.openUiTab)
  const closeUiTab = useStore((s) => s.closeUiTab)
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)

  const topTabs = useStore((s) => s.topTabs)
  const activeTopTab = useStore((s) => s.activeTopTab)
  const bottomTabs = useStore((s) => s.bottomTabs)
  const activeBottomTab = useStore((s) => s.activeBottomTab)
  const dockTopPct = useStore((s) => s.dockTopPct)
  const dockMax = useStore((s) => s.dockMax)
  const setActiveTopTab = useStore((s) => s.setActiveTopTab)
  const setActiveBottomTab = useStore((s) => s.setActiveBottomTab)
  const setDockTopPct = useStore((s) => s.setDockTopPct)
  const setDockMax = useStore((s) => s.setDockMax)
  const resetDockLayout = useStore((s) => s.resetDockLayout)

  const [restoring, setRestoring] = useState(true)
  const [qtSettings, setQtSettings] = useState<QtClientSettings>(() => loadQtSettings())
  const qtTheme = qtSettings.theme
  const [density, setDensity] = useState<Density>(() => loadDensity())
  const [openMenu, setOpenMenu] = useState<'projects' | 'extensions' | 'settings' | null>(null)
  const [extPopupOpen, setExtPopupOpen] = useState(false)
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [settingsTab, setSettingsTab] = useState<'main' | 'sessions' | 'console' | 'layout'>('main')
  const [paletteOpen, setPaletteOpen] = useState(false)
  const [helpOpen, setHelpOpen] = useState(false)
  const [chord, setChord] = useState<string | null>(null)
  const [draggingSplit, setDraggingSplit] = useState(false)
  const workspaceRef = useRef<HTMLDivElement>(null)
  const topTabsRef = useRef<HTMLDivElement>(null)
  const bottomTabsRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    const el = topTabsRef.current?.querySelector('.qt-tab.active') as HTMLElement | null
    el?.scrollIntoView({ inline: 'nearest', block: 'nearest' })
  }, [activeTopTab, topTabs.length])

  useEffect(() => {
    const el = bottomTabsRef.current?.querySelector('.qt-tab.active') as HTMLElement | null
    el?.scrollIntoView({ inline: 'nearest', block: 'nearest' })
  }, [activeBottomTab, bottomTabs.length])

  useEffect(() => {
    const onExtSettings = (e: Event) => {
      const detail = (e as CustomEvent<QtClientSettings>).detail
      if (detail) setQtSettings(detail)
    }
    window.addEventListener('adaptix-qt-settings', onExtSettings)
    return () => window.removeEventListener('adaptix-qt-settings', onExtSettings)
  }, [])

  function setQtTheme(id: QtThemeId) {
    applyQtTheme(id, true)
    setQtSettings(loadQtSettings())
  }

  function updateQtSettings(patch: Partial<QtClientSettings>) {
    const next: QtClientSettings = { ...qtSettings, ...patch }
    saveQtSettings(next)
    setQtSettings(next)
  }

  useEffect(() => {
    saveDensity(density)
  }, [density])

  // Close menus/popups on outside click
  useEffect(() => {
    if (!openMenu && !extPopupOpen) return
    const onDown = (e: MouseEvent) => {
      const target = e.target as HTMLElement | null
      if (target?.closest('.qt-menubar') || target?.closest('.qt-ext-wrap')) return
      setOpenMenu(null)
      setExtPopupOpen(false)
    }
    window.addEventListener('mousedown', onDown)
    return () => window.removeEventListener('mousedown', onDown)
  }, [openMenu, extPopupOpen])

  // F5 session restore
  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        if (!useStore.getState().connected) {
          await tryRestoreSession()
        }
      } catch {
        /* tryRestoreSession already cleared session */
      } finally {
        if (!cancelled) setRestoring(false)
      }
    })()
    return () => { cancelled = true }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // Periodic REST fallback when WS is down, stuck syncing, or silent > 20s
  useEffect(() => {
    if (!connected) return
    const tick = () => {
      const st = useStore.getState()
      const wsSilent = st.lastWsPacketAt === 0 || (Date.now() - st.lastWsPacketAt > 20000)
      if (!st.wsConnected || st.syncing || wsSilent) {
        refreshRest().catch(() => {})
      }
    }
    const id = window.setInterval(tick, 15000)
    return () => clearInterval(id)
  }, [connected, refreshRest])

  // KDDockWidgets horizontal splitter drag
  useEffect(() => {
    if (!draggingSplit) return
    const onMove = (e: MouseEvent) => {
      const box = workspaceRef.current?.getBoundingClientRect()
      if (!box || box.height <= 0) return
      const pct = ((e.clientY - box.top) / box.height) * 100
      setDockTopPct(pct)
    }
    const onUp = () => setDraggingSplit(false)
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [draggingSplit, setDockTopPct])

  function go(id: TabId) {
    setOpenMenu(null)
    setExtPopupOpen(false)
    openUiTab(id)
  }

  // Global keyboard shortcuts
  useEffect(() => {
    if (!connected) return
    const isTyping = (el: EventTarget | null) => {
      const node = el as HTMLElement | null
      if (!node) return false
      const tag = node.tagName
      return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || node.isContentEditable
    }
    const onKey = (e: KeyboardEvent) => {
      const mod = e.ctrlKey || e.metaKey
      if (mod && (e.key === 'k' || e.key === 'K')) {
        e.preventDefault()
        setPaletteOpen((v) => !v)
        setHelpOpen(false)
        setSettingsOpen(false)
        setChord(null)
        return
      }
      if (!mod && e.key === '?' && !isTyping(e.target)) {
        e.preventDefault()
        setHelpOpen((v) => !v)
        setChord(null)
        return
      }
      if (mod && e.shiftKey && selected) {
        const a = useStore.getState().agents[selected]
        const live = a ? isAgentOnline(a) : false
        if (e.key === 'F' || e.key === 'f') {
          e.preventDefault()
          if (live) openFilesBrowser(selected)
          return
        }
        if (e.key === 'P' || e.key === 'p') {
          e.preventDefault()
          if (live) openProcsBrowser(selected)
          return
        }
      }
      if (e.key === 'Escape') {
        if (settingsOpen) { setSettingsOpen(false); return }
        if (helpOpen) { setHelpOpen(false); return }
        if (paletteOpen) { setPaletteOpen(false); return }
        if (openMenu || extPopupOpen) { setOpenMenu(null); setExtPopupOpen(false); return }
        return
      }
      if (paletteOpen || settingsOpen || isTyping(e.target)) return
      if (chord === 'g') {
        setChord(null)
        const k = e.key.toLowerCase()
        const map: Record<string, TabId> = {
          h: 'sessions', o: 'dashboard', l: 'listeners', g: 'generate',
          w: 'delivery', t: 'tasks', d: 'downloads', u: 'tunnels',
          s: 'screens', c: 'creds', k: 'targets', p: 'graph',
          m: 'chat', i: 'ai', r: 'relay', b: 'bofs', a: 'argfuscator',
          x: 'extkit', e: 'logs',
        }
        if (map[k]) { e.preventDefault(); go(map[k]) }
        return
      }
      if (!mod && !e.altKey && e.key.toLowerCase() === 'g') {
        setChord('g')
        window.setTimeout(() => setChord((c) => (c === 'g' ? null : c)), 1200)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connected, paletteOpen, helpOpen, settingsOpen, openMenu, extPopupOpen, selected, chord])

  function tabLabel(id: TabId): string {
    if (STATIC_LABELS[id]) return LANG === 'en' ? STATIC_LABELS[id] : t(STATIC_LABELS[id])
    const aid = tabAgentId(id)
    const a = aid ? useStore.getState().agents?.[aid] : null
    const host = a?.a_computer || aid || '?'
    if (isConsoleTab(id)) return `Console [${host}]`
    if (isFilesTab(id)) return `Files [${host}]`
    if (isProcsTab(id)) return `Processes [${host}]`
    if (isPtyTab(id)) return `Terminal [${host}]`
    if (isCmdTab(id)) return `Cmd [${host}]`
    if (isVncTab(id)) return `VNC [${host}]`
    return id
  }

  function renderDockTabContent(id: TabId) {
    if (id === 'sessions') return <Hosts />
    if (id === 'graph') return <Graph />
    if (id === 'tasks') return <Tasks />
    if (id === 'logs') return <div className="qt-ev-wrap"><EventsDock /></div>
    if (id === 'listeners') return <Listeners />
    if (id === 'dashboard') return <DashboardConnected />
    if (id === 'generate') return <Generate />
    if (id === 'delivery') return <WebDelivery />
    if (id === 'downloads') return <Downloads />
    if (id === 'tunnels') return <Tunnels />
    if (id === 'screens') return <Screenshots />
    if (id === 'creds') return <Credentials />
    if (id === 'targets') return <Targets />
    if (id === 'chat') return <Chat />
    if (id === 'ai') return <AICollab />
    if (id === 'bofs') return <Bofs />
    if (id === 'argfuscator') return <ArgFuscator />
    if (id === 'extkit') return <ExtKit />
    if (id === 'relay') return <Relay />
    if (id === 'terminal') return <Term />
    const aid = tabAgentId(id)
    if (aid && isFilesTab(id)) return <FileManager agentId={aid} />
    if (aid && isProcsTab(id)) return <ProcessManager agentId={aid} />
    return null
  }

  if (restoring) {
    return (
      <div className="qt-login-screen">
        <div className="qt-splash">
          <img src={qtAsset('SyncLogo.png')} alt="Adaptix" />
          <div style={{ fontWeight: 700, fontSize: 13, color: 'var(--qt-text-press)' }}>Adaptix Client</div>
          <div style={{ fontSize: 11.5, color: 'var(--qt-text-muted)' }}>{t('正在恢复会话…')}</div>
          <div className="qt-splash-bar"><i /></div>
        </div>
        <ToastHost />
      </div>
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

  // Keep mounted stateful bottom tabs (Console, Files, Processes, PTY, Cmd, VNC) across tab switches
  const openConsoleDockTabs = bottomTabs.filter(isConsoleTab)
  const openFilesDockTabs = bottomTabs.filter(isFilesTab)
  const openProcsDockTabs = bottomTabs.filter(isProcsTab)
  const openPtyDockTabs = bottomTabs.filter(isPtyTab)
  const openCmdDockTabs = bottomTabs.filter(isCmdTab)
  const openVncDockTabs = bottomTabs.filter(isVncTab)

  const hasBottom = bottomTabs.length > 0 && dockMax !== 'top'
  const hasTop = topTabs.length > 0 && dockMax !== 'bottom'
  const showSplitter = hasTop && hasBottom

  const topFlexStyle: React.CSSProperties = !hasBottom
    ? { flex: '1 1 100%' }
    : { flex: `0 0 ${dockTopPct}%` }

  const activeThemeMeta = QT_THEMES.find((x) => x.id === qtTheme) || QT_THEMES[0]
  const hostLabel = api?.baseUrl ? api.baseUrl.replace(/^https?:\/\//, '') : (window.location.host || 'local')
  const isBottomActive = (id: TabId) => activeBottomTab === id && hasBottom
  const isTopActive = (id: TabId) => activeTopTab === id && hasTop

  function handleReconnect() {
    if (!api || !username) return
    void finishConnect(api, username, Object.values(useStore.getState().agents || {}))
  }

  return (
    <div className="qt-shell">
      {/* ── 1. Qt QMenuBar (Projects | Extensions | Settings) ── */}
      <nav className="qt-menubar" aria-label="Menu bar">
        <img className="qt-menubar-logo" src={qtAsset('Logo.png')} alt="" draggable={false} />
        <div className="qt-menu-item">
          <button
            type="button"
            className={`qt-menu-trigger ${openMenu === 'projects' ? 'open' : ''}`}
            onClick={() => { setOpenMenu(openMenu === 'projects' ? null : 'projects'); setExtPopupOpen(false) }}
            onMouseEnter={() => { if (openMenu) setOpenMenu('projects') }}
          >
            {LANG === 'en' ? 'Projects' : '项目'}
          </button>
          {openMenu === 'projects' && (
            <div className="qt-menu-dropdown">
              <button type="button" className="qt-menu-action" onClick={() => { setOpenMenu(null); handleReconnect() }}>
                <span>{t('重连 WebSocket')}</span>
              </button>
              <button type="button" className="qt-menu-action" onClick={() => { setOpenMenu(null); refreshRest({ includeTasks: true }).catch(() => {}) }}>
                <span>{t('立即刷新数据')}</span>
              </button>
              <button type="button" className="qt-menu-action" onClick={() => go('dashboard')}>
                <span>{t('状态概览')}</span>
                <span className="qt-menu-sc">G O</span>
              </button>
              <div className="qt-menu-sep" />
              <button type="button" className="qt-menu-action" onClick={() => { setOpenMenu(null); resetDockLayout() }}>
                <span>{t('重置分屏布局')}</span>
              </button>
              <div className="qt-menu-sep" />
              <button type="button" className="qt-menu-action" onClick={() => { setOpenMenu(null); disconnect() }}>
                <span>{t('断开 / 切换项目')}</span>
              </button>
            </div>
          )}
        </div>

        <div className="qt-menu-item">
          <button
            type="button"
            className={`qt-menu-trigger ${openMenu === 'extensions' ? 'open' : ''}`}
            onClick={() => { setOpenMenu(openMenu === 'extensions' ? null : 'extensions'); setExtPopupOpen(false) }}
            onMouseEnter={() => { if (openMenu) setOpenMenu('extensions') }}
          >
            {LANG === 'en' ? 'Extensions' : '扩展'}
          </button>
          {openMenu === 'extensions' && (
            <div className="qt-menu-dropdown">
              <div className="qt-menu-head">Extension Docks</div>
              {EXTENSION_DOCKS.map((ext) => (
                <button key={ext.id} type="button" className="qt-menu-action" onClick={() => go(ext.id)}>
                  <span>{t(ext.label)}</span>
                  {ext.sc ? <span className="qt-menu-sc">{ext.sc}</span> : null}
                </button>
              ))}
            </div>
          )}
        </div>

        <div className="qt-menu-item">
          <button
            type="button"
            className={`qt-menu-trigger ${openMenu === 'settings' ? 'open' : ''}`}
            onClick={() => { setOpenMenu(openMenu === 'settings' ? null : 'settings'); setExtPopupOpen(false) }}
            onMouseEnter={() => { if (openMenu) setOpenMenu('settings') }}
          >
            {LANG === 'en' ? 'Settings' : '设置'}
          </button>
          {openMenu === 'settings' && (
            <div className="qt-menu-dropdown">
              <button type="button" className="qt-menu-action" onClick={() => { setOpenMenu(null); setSettingsOpen(true) }}>
                <span>{t('打开设置…')}</span>
              </button>
              <div className="qt-menu-sep" />
              <div className="qt-menu-head">Qt Theme</div>
              {QT_THEMES.map((th) => (
                <button
                  key={th.id}
                  type="button"
                  className="qt-menu-action"
                  onClick={() => { setQtTheme(th.id); setOpenMenu(null) }}
                >
                  <span>{qtTheme === th.id ? '✓ ' : '   '}{th.name}</span>
                  <span className="qt-swatches">
                    <i style={{ background: th.swatches[1] }} />
                    <i style={{ background: th.swatches[2] }} />
                  </span>
                </button>
              ))}
              <div className="qt-menu-sep" />
              <button type="button" className="qt-menu-action" onClick={() => { setOpenMenu(null); setLang(LANG === 'zh' ? 'en' : 'zh') }}>
                <span>{LANG === 'zh' ? 'Switch to English' : '切换到中文'}</span>
              </button>
              <button type="button" className="qt-menu-action" onClick={() => { setOpenMenu(null); setHelpOpen(true) }}>
                <span>{t('快捷键帮助')}</span>
                <span className="qt-menu-sc">?</span>
              </button>
            </div>
          )}
        </div>

        <div className="qt-menubar-spacer" />
        <div className="qt-menubar-meta">
          {chord === 'g' ? <span className="qt-menubar-chip" style={{ color: 'var(--qt-primary-hover)' }}>G …</span> : null}
          <button type="button" className="qt-menubar-chip" onClick={() => setSettingsOpen(true)} title="Qt Theme">
            {activeThemeMeta.name}
          </button>
          <button
            type="button"
            className="qt-menubar-chip"
            onClick={() => setLang(LANG === 'zh' ? 'en' : 'zh')}
            title={LANG === 'zh' ? 'Switch to English' : '切换到中文'}
          >
            {LANG === 'zh' ? 'EN' : '中'}
          </button>
        </div>
      </nav>

      {/* ── 2. Qt topHLayout Icon + Label Toolbar (AdaptixWidget::createUI) ── */}
      <header className="qt-toolbar">
        {/* Group 1: Logs · Listeners · Extensions */}
        <button
          type="button"
          className={`qt-tb-btn ${isBottomActive('logs') ? 'active' : ''}`}
          title="Notifications / Logs (G E)"
          onClick={() => go('logs')}
        >
          {ICO.logs}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Logs' : '日志'}</span>
        </button>
        <ListenersToolbarBtn active={isBottomActive('listeners')} onClick={() => go('listeners')} />
        <div className="qt-ext-wrap">
          <button
            type="button"
            className={`qt-tb-btn ${extPopupOpen ? 'active' : ''}`}
            title="Extensions Docks (载荷 / 投递 / 中转 / AI / BOF)"
            onClick={() => { setExtPopupOpen((v) => !v); setOpenMenu(null) }}
          >
            {ICO.ext}
            <span className="qt-tb-lbl">{LANG === 'en' ? 'Ext' : '扩展'}</span>
          </button>
          {extPopupOpen && (
            <div className="qt-ext-popup">
              <div className="qt-ext-title">Extensions Docks</div>
              <div className="qt-ext-list">
                {EXTENSION_DOCKS.map((ext) => (
                  <button key={ext.id} type="button" className="qt-ext-item" onClick={() => go(ext.id)}>
                    <span>{t(ext.label)}</span>
                    {ext.sc ? <span className="qt-ext-sub">{ext.sc}</span> : null}
                  </button>
                ))}
              </div>
            </div>
          )}
        </div>

        <span className="qt-tb-vline" />

        {/* Group 2 (dockTop): Sessions · Graph · Tasks */}
        <SessionsToolbarBtn active={isTopActive('sessions')} onClick={() => go('sessions')} />
        <button
          type="button"
          className={`qt-tb-btn ${isTopActive('graph') ? 'active' : ''}`}
          title="Sessions graph (G P)"
          onClick={() => go('graph')}
        >
          {ICO.graph}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Graph' : '拓扑'}</span>
        </button>
        <button
          type="button"
          className={`qt-tb-btn ${isTopActive('tasks') ? 'active' : ''}`}
          title="Jobs & Tasks (G T)"
          onClick={() => go('tasks')}
        >
          {ICO.tasks}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Tasks' : '任务'}</span>
        </button>

        <span className="qt-tb-vline" />

        {/* Group 3: Chat · Tunnels */}
        <button
          type="button"
          className={`qt-tb-btn ${isBottomActive('chat') ? 'active' : ''}`}
          title="Chat (G M)"
          onClick={() => go('chat')}
        >
          {ICO.chat}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Chat' : '聊天'}</span>
        </button>
        <button
          type="button"
          className={`qt-tb-btn ${isBottomActive('tunnels') ? 'active' : ''}`}
          title="Tunnels table (G U)"
          onClick={() => go('tunnels')}
        >
          {ICO.tunnels}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Tunnels' : '隧道'}</span>
        </button>

        <span className="qt-tb-vline" />

        {/* Group 4: Downloads · Targets · Credentials · Screenshots */}
        <button
          type="button"
          className={`qt-tb-btn ${isBottomActive('downloads') ? 'active' : ''}`}
          title="Downloads (G D)"
          onClick={() => go('downloads')}
        >
          {ICO.downloads}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Downloads' : '下载'}</span>
        </button>
        <button
          type="button"
          className={`qt-tb-btn ${isBottomActive('targets') ? 'active' : ''}`}
          title="Targets table (G K)"
          onClick={() => go('targets')}
        >
          {ICO.targets}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Targets' : '目标'}</span>
        </button>
        <button
          type="button"
          className={`qt-tb-btn ${isBottomActive('creds') ? 'active' : ''}`}
          title="Credentials (G C)"
          onClick={() => go('creds')}
        >
          {ICO.creds}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Creds' : '凭据'}</span>
        </button>
        <button
          type="button"
          className={`qt-tb-btn ${isBottomActive('screens') ? 'active' : ''}`}
          title="Screenshots (G S)"
          onClick={() => go('screens')}
        >
          {ICO.screens}
          <span className="qt-tb-lbl">{LANG === 'en' ? 'Screens' : '截图'}</span>
        </button>

        <span className="qt-tb-vline" />

        {/* Group 5: Reconnect Link Button (NeonGreen when live, Yellow syncing, Red offline) */}
        <button
          type="button"
          className="qt-tb-btn"
          title={syncing ? 'Syncing… (Click to reconnect)' : wsConnected ? 'Connected — Click to reconnect' : 'Disconnected — Click to reconnect'}
          onClick={handleReconnect}
        >
          <QtImg
            src={wsConnected || syncing ? 'link_64dp.png' : 'link_off_64dp.png'}
            cls={`qt-ico ${syncing ? 'qt-ico-link-sync' : wsConnected ? 'qt-ico-link-on' : 'qt-ico-link-off'}`}
          />
          <span className="qt-tb-lbl">
            {syncing ? (LANG === 'en' ? 'Sync' : '同步') : wsConnected ? (LANG === 'en' ? 'Online' : '已连') : (LANG === 'en' ? 'Link' : '重连')}
          </span>
        </button>

        <div className="qt-tb-spacer" />

        <div className="qt-tb-right">
          <button
            type="button"
            className="qt-tb-pill"
            title={t('刷新')}
            onClick={() => refreshRest().catch(() => {})}
          >
            {ICO.refresh}
            <span>{t('刷新')}</span>
          </button>
          <button
            type="button"
            className="qt-tb-pill"
            title={t('命令面板') + ' (Ctrl+K)'}
            onClick={() => setPaletteOpen(true)}
          >
            {ICO.search}
            <span>⌘K</span>
          </button>
          <button
            type="button"
            className="qt-tb-pill"
            title={t('设置 / 切换 Qt 主题')}
            onClick={() => setSettingsOpen(true)}
          >
            {ICO.settings}
          </button>
        </div>
      </header>

      {/* ── 3. KDDockWidgets 2-Zone Split Workspace (dockTop + splitter + dockBottom) ── */}
      <main className="qt-workspace" ref={workspaceRef}>
        {hasTop && (
          <section className="qt-dock" style={topFlexStyle}>
            <div className="qt-dock-bar">
              <div
                className="qt-dock-tabs"
                ref={topTabsRef}
                onWheel={(e) => { if (Math.abs(e.deltaY) > 0 && topTabsRef.current) topTabsRef.current.scrollLeft += e.deltaY }}
              >
                {topTabs.map((id) => (
                  <button
                    key={id}
                    type="button"
                    className={`qt-tab ${activeTopTab === id ? 'active' : ''}`}
                    onClick={() => setActiveTopTab(id)}
                    onMouseDown={(e) => { if (e.button === 1 && topTabs.length > 1) { e.preventDefault(); closeUiTab(id) } }}
                    title={tabLabel(id)}
                  >
                    <img className="qt-ico qt-ico-mono qt-tab-ico" src={qtAsset(`icons/${tabIconFile(id)}`)} alt="" />
                    <span>{tabLabel(id)}</span>
                    {topTabs.length > 1 && (
                      <span
                        className="qt-tab-x"
                        onClick={(e) => { e.stopPropagation(); closeUiTab(id) }}
                        title={t('关闭')}
                      >×</span>
                    )}
                  </button>
                ))}
              </div>
              <div className="qt-dock-ctrl">
                <button
                  type="button"
                  className="qt-dock-btn"
                  title={dockMax === 'top' ? t('还原分屏') : t('最大化上面板')}
                  onClick={() => setDockMax(dockMax === 'top' ? 'none' : 'top')}
                >
                  {dockMax === 'top' ? '❐' : '□'}
                </button>
              </div>
            </div>
            <div className="qt-dock-body">
              {activeTopTab ? renderDockTabContent(activeTopTab) : <div className="qt-dock-empty">Sessions table</div>}
            </div>
          </section>
        )}

        {showSplitter && (
          <div
            className={`qt-splitter ${draggingSplit ? 'dragging' : ''}`}
            role="separator"
            aria-orientation="horizontal"
            title={t('拖拽调整上下分屏比例，双击还原')}
            onMouseDown={() => setDraggingSplit(true)}
            onDoubleClick={() => setDockTopPct(46)}
          />
        )}

        {hasBottom && (
          <section className="qt-dock" style={{ flex: '1 1 auto' }}>
            <div className="qt-dock-bar">
              <div
                className="qt-dock-tabs"
                ref={bottomTabsRef}
                onWheel={(e) => { if (Math.abs(e.deltaY) > 0 && bottomTabsRef.current) bottomTabsRef.current.scrollLeft += e.deltaY }}
              >
                {bottomTabs.map((id) => {
                  const aid = tabAgentId(id)
                  return (
                    <button
                      key={id}
                      type="button"
                      className={`qt-tab ${activeBottomTab === id ? 'active' : ''}`}
                      onClick={() => setActiveBottomTab(id)}
                      onMouseDown={(e) => { if (e.button === 1) { e.preventDefault(); closeUiTab(id) } }}
                      title={tabLabel(id)}
                    >
                      {aid
                        ? <DockAgentDot agentId={aid} />
                        : <img className="qt-ico qt-ico-mono qt-tab-ico" src={qtAsset(`icons/${tabIconFile(id)}`)} alt="" />}
                      <span><DockTabText id={id} /></span>
                      <span
                        className="qt-tab-x"
                        onClick={(e) => { e.stopPropagation(); closeUiTab(id) }}
                        title={t('关闭')}
                      >×</span>
                    </button>
                  )
                })}
              </div>
              <div className="qt-dock-ctrl">
                <button
                  type="button"
                  className="qt-dock-btn"
                  title={dockMax === 'bottom' ? t('还原分屏') : t('最大化下面板')}
                  onClick={() => setDockMax(dockMax === 'bottom' ? 'none' : 'bottom')}
                >
                  {dockMax === 'bottom' ? '❐' : '□'}
                </button>
                <button
                  type="button"
                  className="qt-dock-btn"
                  title={t('收起下面板')}
                  onClick={() => setDockMax('top')}
                >
                  —
                </button>
              </div>
            </div>
            <div className="qt-dock-body">
              {activeBottomTab &&
                !isConsoleTab(activeBottomTab) &&
                !isFilesTab(activeBottomTab) &&
                !isProcsTab(activeBottomTab) &&
                !isPtyTab(activeBottomTab) &&
                !isCmdTab(activeBottomTab) &&
                !isVncTab(activeBottomTab) &&
                renderDockTabContent(activeBottomTab)}

              {/* Keep interactive tabs mounted while open in dockBottom */}
              {openConsoleDockTabs.map((id) => {
                const aid = tabAgentId(id)
                if (!aid) return null
                return (
                  <div
                    key={id}
                    style={{ display: activeBottomTab === id ? 'flex' : 'none', flexDirection: 'column', flex: 1, minHeight: 0 }}
                  >
                    <Term agentId={aid} />
                  </div>
                )
              })}
              {openFilesDockTabs.map((id) => {
                const aid = tabAgentId(id)
                if (!aid) return null
                return (
                  <div
                    key={id}
                    style={{ display: activeBottomTab === id ? 'flex' : 'none', flexDirection: 'column', flex: 1, minHeight: 0 }}
                  >
                    <FileManager agentId={aid} />
                  </div>
                )
              })}
              {openProcsDockTabs.map((id) => {
                const aid = tabAgentId(id)
                if (!aid) return null
                return (
                  <div
                    key={id}
                    style={{ display: activeBottomTab === id ? 'flex' : 'none', flexDirection: 'column', flex: 1, minHeight: 0 }}
                  >
                    <ProcessManager agentId={aid} />
                  </div>
                )
              })}
              {openPtyDockTabs.map((id) => {
                const aid = tabAgentId(id)
                if (!aid) return null
                return <PtyTerm key={id} tabId={id} agentId={aid} active={activeBottomTab === id} />
              })}
              {openCmdDockTabs.map((id) => {
                const aid = tabAgentId(id)
                if (!aid) return null
                return (
                  <div
                    key={id}
                    style={{ display: activeBottomTab === id ? 'contents' : 'none', height: '100%', minHeight: 0 }}
                  >
                    <CmdExec agentId={aid} />
                  </div>
                )
              })}
              {openVncDockTabs.map((id) => {
                const aid = tabAgentId(id)
                if (!aid) return null
                return (
                  <div
                    key={id}
                    style={{ display: activeBottomTab === id ? 'contents' : 'none', height: '100%', minHeight: 0 }}
                  >
                    <VncPanel agentId={aid} />
                  </div>
                )
              })}
            </div>
          </section>
        )}
      </main>

      {/* ── 4. Qt South Project TabBar + Status Strip (QTabWidget::South) ── */}
      <footer className="qt-south">
        <div className="qt-project-tab" title={`${username}@${hostLabel}`}>
          <img src={qtAsset('Logo.png')} alt="" draggable={false} />
          <span>{localStorage.getItem('adaptix.qt.project') || 'Adaptix'}</span>
          <span style={{ color: 'var(--qt-text-muted)', fontWeight: 400 }}>({username || 'operator'}@{hostLabel})</span>
        </div>

        <ConnStatus connected={connected} wsConnected={wsConnected} syncing={syncing} />

        <span className="qt-south-sep" />
        <SouthStatusCounts />

        <div className="qt-south-spacer" />

        {dockMax === 'top' && (
          <button type="button" className="qt-south-btn" onClick={() => setDockMax('none')}>
            ▴ {t('展开下面板')} ({bottomTabs.length})
          </button>
        )}
        <button type="button" className="qt-south-btn" onClick={() => setSettingsOpen(true)}>
          {activeThemeMeta.name}
        </button>
        <button type="button" className="qt-south-btn" onClick={() => setHelpOpen(true)}>
          ?
        </button>
        <button type="button" className="qt-south-btn" onClick={() => disconnect()}>
          {t('退出')}
        </button>
      </footer>

      {/* ── 5. Adaptix Settings Dialog (1:1 DialogSettings.cpp split sidebar) ── */}
      {settingsOpen && (
        <div className="qt-modal-mask" onClick={() => setSettingsOpen(false)}>
          <div className="qt-dialog" onClick={(e) => e.stopPropagation()}>
            <div className="qt-dialog-hd">
              <span style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
                <img src={qtAsset('Logo.png')} alt="" style={{ width: 16, height: 16, borderRadius: 3 }} />
                <span>Adaptix Settings</span>
              </span>
              <button type="button" className="qt-dock-btn" onClick={() => setSettingsOpen(false)}>×</button>
            </div>

            <div className="qt-settings-split">
              <aside className="qt-settings-nav">
                <button type="button" className={`qt-settings-nav-btn ${settingsTab === 'main' ? 'active' : ''}`} onClick={() => setSettingsTab('main')}>
                  Main Settings
                </button>
                <button type="button" className={`qt-settings-nav-btn ${settingsTab === 'sessions' ? 'active' : ''}`} onClick={() => setSettingsTab('sessions')}>
                  Sessions Table
                </button>
                <button type="button" className={`qt-settings-nav-btn ${settingsTab === 'console' ? 'active' : ''}`} onClick={() => setSettingsTab('console')}>
                  Console
                </button>
                <button type="button" className={`qt-settings-nav-btn ${settingsTab === 'layout' ? 'active' : ''}`} onClick={() => setSettingsTab('layout')}>
                  Layout
                </button>
              </aside>

              <div className="qt-settings-pane">
                {settingsTab === 'main' && (
                  <>
                    <div className="qt-set-label">Main theme ({QT_THEMES.length} presets)</div>
                    <div className="qt-theme-grid">
                      {QT_THEMES.map((th) => (
                        <button
                          key={th.id}
                          type="button"
                          className={`qt-theme-card ${qtTheme === th.id ? 'active' : ''}`}
                          onClick={() => setQtTheme(th.id)}
                        >
                          <span>{th.name}</span>
                          <span className="qt-swatches">
                            {th.swatches.map((hex, i) => (
                              <i key={i} style={{ background: hex }} />
                            ))}
                          </span>
                        </button>
                      ))}
                    </div>

                    <div className="qt-set-row">
                      <span className="qt-set-label">Font family</span>
                      <select
                        value={qtSettings.fontFamily}
                        onChange={(e) => updateQtSettings({ fontFamily: e.target.value as QtFontFamily })}
                      >
                        <option value="JetBrains Mono">JetBrains Mono (Default)</option>
                        <option value="Hack">Hack</option>
                        <option value="System">System UI</option>
                      </select>
                    </div>

                    <div className="qt-set-row">
                      <span className="qt-set-label">Font size</span>
                      <select
                        value={qtSettings.fontSize}
                        onChange={(e) => updateQtSettings({ fontSize: Number(e.target.value) })}
                      >
                        {[11, 12, 13, 14].map((sz) => (
                          <option key={sz} value={sz}>{sz} px</option>
                        ))}
                      </select>
                    </div>

                    <div className="qt-set-row">
                      <span className="qt-set-label">Language</span>
                      <div style={{ display: 'flex', gap: 6 }}>
                        <button type="button" className={LANG === 'zh' ? 'primary' : ''} onClick={() => setLang('zh')}>中文</button>
                        <button type="button" className={LANG === 'en' ? 'primary' : ''} onClick={() => setLang('en')}>English</button>
                      </div>
                    </div>
                  </>
                )}

                {settingsTab === 'sessions' && (
                  <>
                    <div className="qt-set-row">
                      <span className="qt-set-label">Visible Columns (Sessions Table)</span>
                      <button
                        type="button"
                        onClick={() => updateQtSettings({ sessionsCols: { ...DEFAULT_SESSIONS_COLS } })}
                      >
                        Reset Default
                      </button>
                    </div>
                    <div className="qt-cols-grid">
                      {SESSIONS_COL_META.map((col) => (
                        <label key={col.key} className="qt-check-inline">
                          <input
                            type="checkbox"
                            checked={qtSettings.sessionsCols[col.key] !== false}
                            onChange={(e) =>
                              updateQtSettings({
                                sessionsCols: {
                                  ...qtSettings.sessionsCols,
                                  [col.key]: e.target.checked,
                                },
                              })
                            }
                          />
                          <span>{col.label}</span>
                        </label>
                      ))}
                    </div>
                  </>
                )}

                {settingsTab === 'console' && (
                  <>
                    <div className="qt-set-row">
                      <span className="qt-set-label">Console theme</span>
                      <select
                        value={qtSettings.consoleTheme}
                        onChange={(e) => updateQtSettings({ consoleTheme: e.target.value as QtConsoleThemeId })}
                      >
                        {QT_CONSOLE_THEMES.map((ct) => (
                          <option key={ct.id} value={ct.id}>{ct.name}</option>
                        ))}
                      </select>
                    </div>
                    <label className="qt-check-inline">
                      <input
                        type="checkbox"
                        checked={qtSettings.consoleShowBg}
                        onChange={(e) => updateQtSettings({ consoleShowBg: e.target.checked })}
                      />
                      <span>Show Console Background Image (adaptix_background.png)</span>
                    </label>
                    <label className="qt-check-inline">
                      <input
                        type="checkbox"
                        checked={qtSettings.consoleShowTime}
                        onChange={(e) => updateQtSettings({ consoleShowTime: e.target.checked })}
                      />
                      <span>Print timestamp prefix</span>
                    </label>
                    <label className="qt-check-inline">
                      <input
                        type="checkbox"
                        checked={qtSettings.consoleAutoScroll}
                        onChange={(e) => updateQtSettings({ consoleAutoScroll: e.target.checked })}
                      />
                      <span>Auto scroll to bottom</span>
                    </label>
                    <label className="qt-check-inline">
                      <input
                        type="checkbox"
                        checked={qtSettings.consoleNoWrap}
                        onChange={(e) => updateQtSettings({ consoleNoWrap: e.target.checked })}
                      />
                      <span>Disable line wrapping (NoWrap)</span>
                    </label>
                  </>
                )}

                {settingsTab === 'layout' && (
                  <>
                    <div className="qt-set-row">
                      <span className="qt-set-label">Table row density</span>
                      <div style={{ display: 'flex', gap: 6 }}>
                        <button type="button" className={density === 'compact' ? 'primary' : ''} onClick={() => setDensity('compact')}>
                          Compact (Qt)
                        </button>
                        <button type="button" className={density === 'comfortable' ? 'primary' : ''} onClick={() => setDensity('comfortable')}>
                          Comfortable
                        </button>
                      </div>
                    </div>
                    <div className="qt-set-row">
                      <span className="qt-set-label">KDDockWidgets Split</span>
                      <button type="button" onClick={() => resetDockLayout()}>
                        Reset Dock Layout
                      </button>
                    </div>
                  </>
                )}
              </div>
            </div>

            <div className="qt-dialog-foot">
              <button type="button" className="primary" onClick={() => setSettingsOpen(false)}>Close</button>
            </div>
          </div>
        </div>
      )}

      <CommandPalette open={paletteOpen} onClose={() => setPaletteOpen(false)} go={go} />
      <ShortcutHelp open={helpOpen} onClose={() => setHelpOpen(false)} />
      <ToastHost />
    </div>
  )
}

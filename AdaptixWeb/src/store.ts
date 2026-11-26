import { create } from 'zustand'
import { t } from './i18n'
import { ApiClient } from './api/http'
import { SyncSocket } from './api/ws'
import {
  Agent, AppLog, ChatMessage, ConsoleLine, DEFAULT_SUBSCRIPTIONS, DownloadItem,
  Listener, P, Packet, Pivot, Task,
} from './api/types'
import { clearLiveAgentTicks, clearSession, getServerNowMs, isAgentOnline, isLinuxAgent, loadSession, saveLoginPrefs, saveSession, setLiveAgentTick, syncServerClock } from './utils'
import { emptySnapshot, GraphBoard, GraphSnapshot, isSnapshot, normSnapshot } from './graphBoard'

function uid() { return Math.random().toString(36).slice(2, 10) + Date.now().toString(36) }
function now() { return Date.now() }

/** 静态菜单页 + 动态工作页签（files:/procs:/pty:/vnc: 按主机停靠，非弹窗） */
export type StaticTabId =
  | 'dashboard' | 'sessions' | 'listeners' | 'tasks' | 'downloads' | 'logs'
  | 'generate' | 'delivery' | 'tunnels' | 'screens' | 'creds' | 'targets' | 'chat' | 'graph' | 'bofs'
  | 'argfuscator' | 'extkit' | 'relay' | 'terminal' | 'ai'
export type TabId =
  | StaticTabId
  | `console:${string}`
  | `files:${string}` | `procs:${string}` | `pty:${string}` | `vnc:${string}`
  // 命令执行台（全屏终端式，走命令通道不是 PTY）。`#n` 是多开的会话序号。
  | `cmd:${string}`

/**
 * ★ 这里曾经有 isBrowserConsoleNoise() / isBrowserCmdline()，按**命令拼写**与
 *   **输出形状**（`> ls`、`Listing '…'`、`file content:`、`Process list`、
 *   `Task: …`…）猜「这条是不是文件管理器发的」，命中即静默丢弃。
 *   **已整批删除，不要再加回来。** 理由（2026-09-20 实测，两条独立证据）：
 *
 *   1. 服务端已经分流，前端这份猜测是纯冗余：
 *      `ts_agent.go:162` 把 `ui:true` 的任务改成 TASK_TYPE_BROWSER，
 *      `mgr_task.go:78/:98` 的 `!= TASK_TYPE_BROWSER` 同时挡掉 TsSyncConsole 与
 *      DbConsoleInsert，`ts_browser.go` 四条完成路径只发 BROWSER_* 包
 *      （且都不调 completeTask ⇒ browser 任务连 Tasks 表都不落）。
 *      前端的 BROWSER_FILES/DISKS/PROCESS 是**独立 case 分支**，从不进 pushConsole。
 *      ⇒ 四个 CONSOLE_* 分支**结构上不可能**收到文件管理器的输出。
 *   2. 那些规则会**误杀操作员自己的命令**，而且是双向的：
 *      拼写表让手敲 `ls`/`cat`/`ps`/`pwd`/`rm`/`cp`/`mv`/`mkdir` 的
 *      **回显和输出一起消失**；`/^Task:/` 吞掉 `sleep` 的回执
 *      （`beacon_agent/pl_main.go:2181` 就返回 "Task: sleep to …"，
 *      与文件管理器那句 "Task: list files" 同形，**拼写无法区分**）。
 *
 *   判据（可复现）：文件面板发出的请求体是
 *   `{"ui":true,"cmdline":"ls .","data":"{\"message\":\"Task: list files\"}"}`，
 *   返回 ok:true，而 Consoles / Tasks **两张表都不增行**。
 */

export function isConsoleTab(id: string | null | undefined): id is `console:${string}` {
  return !!id && id.startsWith('console:')
}
export function isTopDockTab(id: string | null | undefined): boolean {
  return id === 'sessions' || id === 'graph' || id === 'tasks'
}
export function isFilesTab(id: string | null | undefined): id is `files:${string}` {
  return !!id && id.startsWith('files:')
}
export function isProcsTab(id: string | null | undefined): id is `procs:${string}` {
  return !!id && id.startsWith('procs:')
}
export function isCmdTab(id: string | null | undefined) {
  return !!id && id.startsWith('cmd:')
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
  if (isConsoleTab(id) || isFilesTab(id) || isProcsTab(id) || isPtyTab(id) || isVncTab(id) || isCmdTab(id)) {
    // 多个终端/命令台会话用 `pty:<agentId>#<n>`、`cmd:<agentId>#<n>`，主机 id 是 `#` 前面那段
    const rest = id.slice(id.indexOf(':') + 1).split('#')[0]
    return rest || null
  }
  return null
}
export function consoleTabId(agentId: string): TabId { return `console:${agentId}` }
export function filesTabId(agentId: string): TabId { return `files:${agentId}` }
export function procsTabId(agentId: string): TabId { return `procs:${agentId}` }
/**
 * 交互 PTY 页签 id。seq=0 是「该主机的常驻会话」（右键『交互 PTY』复用它），
 * seq>0 是**额外新开的会话**（右键『执行命令』每次都新开一个，见 openPty）。
 * ★ 一台主机能开多个终端是刚需（同时盯 shell / 日志 / 另一个 shell）；
 *   原来 id 只有 `pty:<agentId>`，第二个开出来只是把第一个切到前台。
 */
/**
 * 命令执行台页签 id。seq=0 是第一个会话，seq>0 是额外新开的 —— 一台主机能同时开几个
 * （跟 shell / 跟日志各一个），每个自己开关。★ 同 ptyTabId：id 不唯一就开不出第二个。
 */
export function cmdTabId(agentId: string, seq = 0): TabId {
  return seq > 0 ? `cmd:${agentId}#${seq}` : `cmd:${agentId}`
}
export function ptyTabId(agentId: string, seq = 0): TabId {
  return seq > 0 ? `pty:${agentId}#${seq}` : `pty:${agentId}`
}
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
  lastWsPacketAt: number
  /**
   * 服务端自报的非回环网卡地址，来自同步起始包（SyncPackerStart.interfaces，与 Qt 侧
   * ax.interfaces() 同源）。只用于给「回连地址」的缺省值做旁证 —— 见 lib/callbackHost.ts。
   */
  serverInterfaces: string[]
  username: string
  tab: TabId
  agents: Record<string, Agent>
  /** Decoupled high-frequency heartbeat timestamps (epoch sec) so AGENT_TICK never invalidates s.agents */
  agentTicks: Record<string, number>
  listeners: Record<string, Listener>
  tasks: Task[]
  downloads: Record<string, DownloadItem>
  selectedAgentId: string | null
  consoleLines: ConsoleLine[]
  /** Per-agent console buckets so Console tabs only re-render on their own output */
  consoleByAgent: Record<string, ConsoleLine[]>
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
  /**
   * 主机页批量删除的勾选集合。
   *
   * 用 `Set` 而不是数组：线上见过 454 台主机，每行都要判一次「我勾了没」，
   * 数组就是 454×454。用 Set 是 O(1)。
   *
   * ★ 两条 zustand v5 的硬约束（违反任一条都是「点了没反应」）：
   *   1. **永远替换、绝不原地改**。v5 用 `Object.is` 比引用，`s.checkedIds.add(x)`
   *      之后引用没变 ⇒ 订阅者永不重渲染。
   *   2. **绝不在 selector 里 new**。`useStore(s => new Set(s.checkedIds))`
   *      每次比较都是新对象 ⇒ 无限重渲染。
   */
  checkedIds: Set<string>

  /**
   * 客户端壳（client.css 布局）：主机表与事件日志是**常驻 dock**，
   * 不再是普通页面 —— 开合状态与高度由操作员拖出，落 localStorage。
   */
  hostsDockOpen: boolean
  eventsDockOpen: boolean
  hostsDockH: number
  eventsDockH: number
  toggleHostsDock: () => void
  toggleEventsDock: () => void
  setDockHeights: (hosts: number, events: number) => void

  /** Qt KDDockWidgets 2-zone split workspace (dockTop + dockBottom) */
  topTabs: TabId[]
  activeTopTab: TabId | null
  bottomTabs: TabId[]
  activeBottomTab: TabId | null
  dockTopPct: number
  dockMax: 'none' | 'top' | 'bottom'
  setActiveTopTab: (id: TabId) => void
  setActiveBottomTab: (id: TabId) => void
  setDockTopPct: (pct: number) => void
  setDockMax: (m: 'none' | 'top' | 'bottom') => void
  resetDockLayout: () => void

  setTab: (t: TabId) => void
  openConsole: (id: string) => void
  closeConsoleTab: (id: string) => void
  setActiveConsoleTab: (id: string | null) => void
  openUiTab: (id: TabId) => void
  closeUiTab: (id: TabId) => void
  setActiveUiTab: (id: TabId | null) => void
  openFilesBrowser: (agentId: string) => void
  openProcsBrowser: (agentId: string) => void
  openPty: (agentId: string, opts?: { newSession?: boolean }) => void
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
  finishConnect: (api: ApiClient, username: string, preloadedAgents?: any[]) => Promise<void>
  disconnect: () => void
  handlePacket: (pkt: Packet) => void
  handlePackets: (pkts: Packet[]) => void
  refreshRest: (opts?: { includeTasks?: boolean; skipAgents?: boolean }) => Promise<void>
  sendCommand: (cmdline: string) => Promise<void>
  /** 删一批主机的**唯一配方**。removeSelected / removeChecked 都是它的薄包装。 */
  removeAgents: (ids: string[]) => Promise<void>
  removeSelected: () => Promise<void>
  removeChecked: () => Promise<void>
  toggleChecked: (id: string, on?: boolean) => void
  /** 全选 / 全不选当前筛选结果（不是全部主机 —— 操作员看到什么就勾什么） */
  setChecked: (ids: string[], on: boolean) => void
  clearChecked: () => void
  setBrowserFiles: (agentId: string, path: string, items: any[], message?: string) => void
  setBrowserProcs: (agentId: string, items: any[], message?: string) => void

  /**
   * 拓扑图上「操作员自己画的那部分」。全局一张图，所有操作员共享，
   * 存在服务端（extenders/topology 插件 + ExtenderData 表），不在 localStorage。
   *
   * ★ 放在 store 而不是 Graph.tsx 的 local state：实时通道的
   *   SERVICE_DATA 推送要能写进来，而推送处理器在 handlePacket 里。
   */
  graph: GraphSnapshot
  /** 进拓扑页时拉一次全量。失败只记日志、不阻断渲染（画空图也比白屏强）。 */
  loadGraph: () => Promise<void>
  /** 只换本地快照。WS 推送与保存成功都走它。 */
  applyGraph: (snap: GraphSnapshot) => void
  /**
   * 保存一块 board。成功 → true。
   * 撞上乐观锁（别人先改了）→ 吃下服务端回的当前版本、提示、返回 false。
   */
  saveGraph: (board: GraphBoard) => Promise<boolean>
}


/**
 * 删掉一批主机时该做的**全部**本地清理。
 *
 * 抽成一份是因为有两条路径要删主机（单选 removeSelected / 批量 removeChecked），
 * 而**重复配方是本项目的铁律违反**（`pl_bofs_api.go:381` 原话：「避免出现第二份
 * 配方（本项目铁律）」）。两份配方必然漂 —— 原来那一份就漏了 browserFiles /
 * browserProcs / interactBoost / downloads / tasks / pivots 六项，全是泄漏。
 *
 * 两个容易写错的地方：
 *
 * 1. **`selectedAgentId` 只在「焦点主机自己也被删」时才清空。** 批量场景下
 *    焦点主机经常不在勾选集合里，无条件清空会把操作员的控制台从脚底下抽走。
 *
 * 2. **`interactBoost` 要删、不是 release。** 对已经不存在的主机走
 *    releaseInteractBoost 会**给不存在的 agent 下发 sleep 恢复任务**。
 *    直接删是安全的：`acquireInteractBoost` 里对恢复值有钳制
 *    （`restoreSleep = sleepSec > 0 ? sleepSec : 5`，注释明写「避免永久 0」），
 *    所以主机若真回来，会以 ≥5s 重新注册，不会永远停在 sleep 0。
 */
function purgeAgents(s: State, ids: Set<string>): Partial<State> {
  const agents = { ...s.agents }
  const browserFiles = { ...s.browserFiles }
  const browserProcs = { ...s.browserProcs }
  const interactBoost = { ...s.interactBoost }
  for (const id of ids) {
    delete agents[id]
    delete browserFiles[id]
    delete browserProcs[id]
    delete interactBoost[id]
  }

  const openConsoleTabs = s.openConsoleTabs.filter((x) => !ids.has(x))
  const activeConsoleTab = s.activeConsoleTab && ids.has(s.activeConsoleTab)
    ? (openConsoleTabs[openConsoleTabs.length - 1] || null)
    : s.activeConsoleTab

  // 文件/进程/PTY/VNC 页签（`files:<id>` 这种）随主机一起关
  const kept = s.uiTabs.filter((t) => { const a = tabAgentId(t); return !a || !ids.has(a) })
  // 终端页本身没有 agent id；删掉最后一个主机终端后不让空壳页签残留。
  const visibleTabs = openConsoleTabs.length ? kept : kept.filter((t) => t !== 'terminal')
  const uiTabs = visibleTabs.length ? visibleTabs : (['sessions'] as TabId[])
  const activeUiTab = uiTabs.includes(s.activeUiTab as TabId) ? s.activeUiTab : uiTabs[uiTabs.length - 1]

  const topTabs = s.topTabs.filter((t) => { const a = tabAgentId(t); return !a || !ids.has(a) })
  const nextTopTabs = topTabs.length ? topTabs : (['sessions'] as TabId[])
  const activeTopTab = s.activeTopTab && nextTopTabs.includes(s.activeTopTab)
    ? s.activeTopTab
    : nextTopTabs[nextTopTabs.length - 1]

  const bottomTabs = s.bottomTabs.filter((t) => { const a = tabAgentId(t); return !a || !ids.has(a) })
  const nextBottomTabs = bottomTabs.length ? bottomTabs : (['logs'] as TabId[])
  const activeBottomTab = s.activeBottomTab && nextBottomTabs.includes(s.activeBottomTab)
    ? s.activeBottomTab
    : nextBottomTabs[nextBottomTabs.length - 1]

  const downloads: Record<string, DownloadItem> = {}
  for (const [k, d] of Object.entries(s.downloads)) {
    if (!ids.has(String((d as any)?.d_agent_id || ''))) downloads[k] = d
  }

  const pivots: Record<string, Pivot> = {}
  for (const [k, p] of Object.entries(s.pivots)) {
    const parent = String((p as any)?.p_parent_agent_id || '')
    const child = String((p as any)?.p_child_agent_id || '')
    if (!ids.has(parent) && !ids.has(child)) pivots[k] = p
  }

  const agentTicks: Record<string, number> = {}
  for (const [k, v] of Object.entries(s.agentTicks)) {
    if (!ids.has(k)) agentTicks[k] = v
  }
  clearLiveAgentTicks(ids)
  const consoleByAgent: Record<string, ConsoleLine[]> = {}
  for (const [k, v] of Object.entries(s.consoleByAgent)) {
    if (!ids.has(k)) consoleByAgent[k] = v
  }

  return {
    agents,
    agentTicks,
    browserFiles,
    browserProcs,
    interactBoost,
    consoleLines: s.consoleLines.filter((l) => !ids.has(l.agentId)),
    consoleByAgent,
    openConsoleTabs,
    activeConsoleTab,
    uiTabs,
    activeUiTab: (activeUiTab || 'sessions') as TabId,
    tab: (activeUiTab || 'sessions') as TabId,
    topTabs: nextTopTabs,
    activeTopTab: activeTopTab || 'sessions',
    bottomTabs: nextBottomTabs,
    activeBottomTab: activeBottomTab || 'logs',
    selectedAgentId: s.selectedAgentId && ids.has(s.selectedAgentId) ? null : s.selectedAgentId,
    tasks: s.tasks.filter((t) => !ids.has(String((t as any)?.a_id || ''))),
    // 勾选集合跟着剪枝：留着已删主机的 id，下次批量删就会把它再发给服务端
    // ⇒ 服务端报「找不到」⇒ 整个请求 ok:false（见 removeAgents 的注释）。
    checkedIds: new Set([...s.checkedIds].filter((x) => !ids.has(x))),
  }
}

let connectEpoch = 0
let refreshRestSeq = 0
let refreshTasksSeq = 0
let syncStartSeenForEpoch = 0
let syncWatchdogTimer: ReturnType<typeof setTimeout> | null = null
let syncFinishTimer: ReturnType<typeof setTimeout> | null = null
let restoreInFlight: Promise<boolean> | null = null
let pendingWsQueue: Packet[] = []
let pendingWsTimer: ReturnType<typeof setTimeout> | null = null

function clearPendingWsQueue() {
  pendingWsQueue = []
  if (pendingWsTimer) { clearTimeout(pendingWsTimer); pendingWsTimer = null }
}

function agentMetaEqual(a: Agent, b: Agent): boolean {
  return (
    a.a_id === b.a_id &&
    a.a_name === b.a_name &&
    a.a_listener === b.a_listener &&
    a.a_os === b.a_os &&
    a.a_os_desc === b.a_os_desc &&
    a.a_arch === b.a_arch &&
    a.a_computer === b.a_computer &&
    a.a_username === b.a_username &&
    a.a_domain === b.a_domain &&
    a.a_process === b.a_process &&
    a.a_pid === b.a_pid &&
    a.a_tid === b.a_tid &&
    a.a_internal_ip === b.a_internal_ip &&
    a.a_external_ip === b.a_external_ip &&
    a.a_tags === b.a_tags &&
    a.a_mark === b.a_mark &&
    a.a_elevated === b.a_elevated &&
    a.a_sleep === b.a_sleep &&
    a.a_jitter === b.a_jitter &&
    a.a_create_time === b.a_create_time
  )
}

function flattenPackets(pkts: Packet[], out: Packet[] = []): Packet[] {
  for (const pkt of pkts) {
    const type = pkt.type ?? pkt.Type
    if ((type === P.SYNC_CATEGORY_BATCH || type === P.SYNC_BATCH) && Array.isArray(pkt.packets)) {
      flattenPackets(pkt.packets, out)
    } else {
      out.push(pkt)
    }
  }
  return out
}

function isUrgentPacket(pkt: Packet): boolean {
  const type = pkt.type ?? pkt.Type
  return (
    type === P.SYNC_START ||
    type === P.SYNC_FINISH ||
    type === P.BROWSER_DISKS ||
    type === P.BROWSER_FILES ||
    type === P.BROWSER_FILES_STATUS ||
    type === P.BROWSER_PROCESS ||
    type === P.SYNC_BATCH ||
    type === P.SYNC_CATEGORY_BATCH
  )
}
let cancelConnectWait: (() => void) | null = null

function clearSyncWatchdog() {
  if (syncWatchdogTimer !== null) {
    clearTimeout(syncWatchdogTimer)
    syncWatchdogTimer = null
  }
  if (syncFinishTimer !== null) {
    clearTimeout(syncFinishTimer)
    syncFinishTimer = null
  }
}

function maxTick(...vals: Array<number | undefined>): number | undefined {
  let bestVal: number | undefined
  let bestMs = 0
  for (const v of vals) {
    const n = Number(v) || 0
    if (n <= 0) {
      if (bestVal === undefined && v !== undefined) bestVal = v
      continue
    }
    const ms = n < 1e12 ? n * 1000 : n
    if (ms >= bestMs) {
      bestMs = ms
      bestVal = v
    }
  }
  return bestVal
}

export const useStore = create<State>((set, get) => ({
  api: new ApiClient(''),
  socket: new SyncSocket(),
  connected: false,
  wsConnected: false,
  syncing: false,
  lastWsPacketAt: 0,
  serverInterfaces: [],
  username: '',
  tab: 'sessions',
  agents: {},
  agentTicks: {},
  listeners: {},
  tasks: [],
  downloads: {},
  selectedAgentId: null,
  consoleLines: [],
  consoleByAgent: {},
  logs: [],
  filter: '',
  onlyActive: false,
  openConsoleTabs: [],
  activeConsoleTab: null,
  uiTabs: ['sessions'],
  activeUiTab: 'sessions',
  lastError: '',
  toasts: [],
  browserFiles: {},
  browserProcs: {},
  interactBoost: {},
  chatMessages: [],
  pivots: {},
  graph: emptySnapshot(),
  checkedIds: new Set<string>(),

  // dock 初始值从 localStorage 读（utils 里没有专用封装，这里就地读）
  hostsDockOpen: ((): boolean => {
    try { return localStorage.getItem('adaptix.dock.hosts') !== '0' } catch { return true }
  })(),
  eventsDockOpen: ((): boolean => {
    try { return localStorage.getItem('adaptix.dock.events') !== '0' } catch { return true }
  })(),
  hostsDockH: ((): number => {
    const v = Number(localStorage.getItem('adaptix.dock.hostsH'))
    return Number.isFinite(v) && v >= 160 && v <= 640 ? v : 232
  })(),
  eventsDockH: ((): number => {
    const v = Number(localStorage.getItem('adaptix.dock.eventsH'))
    return Number.isFinite(v) && v >= 100 && v <= 480 ? v : 148
  })(),
  toggleHostsDock: () => set((s) => {
    const open = !s.hostsDockOpen
    try { localStorage.setItem('adaptix.dock.hosts', open ? '1' : '0') } catch { /* 私密模式等 */ }
    return { hostsDockOpen: open }
  }),
  toggleEventsDock: () => set((s) => {
    const open = !s.eventsDockOpen
    try { localStorage.setItem('adaptix.dock.events', open ? '1' : '0') } catch { /* 私密模式等 */ }
    return { eventsDockOpen: open }
  }),
  setDockHeights: (hosts, events) => {
    try {
      localStorage.setItem('adaptix.dock.hostsH', String(Math.round(hosts)))
      localStorage.setItem('adaptix.dock.eventsH', String(Math.round(events)))
    } catch { /* 私密模式等 */ }
    set({ hostsDockH: hosts, eventsDockH: events })
  },

  topTabs: ['sessions'],
  activeTopTab: 'sessions',
  bottomTabs: ['logs'],
  activeBottomTab: 'logs',
  dockTopPct: ((): number => {
    try {
      const v = Number(localStorage.getItem('adaptix.qt.dockTopPct'))
      return Number.isFinite(v) && v >= 20 && v <= 80 ? v : 46
    } catch { return 46 }
  })(),
  dockMax: 'none',
  setActiveTopTab: (id) => set((s) => ({
    activeTopTab: id,
    tab: id,
    activeUiTab: id,
    dockMax: s.dockMax === 'bottom' ? 'none' : s.dockMax,
  })),
  setActiveBottomTab: (id) => set((s) => {
    const aid = tabAgentId(id)
    return {
      activeBottomTab: id,
      tab: id,
      activeUiTab: id,
      activeConsoleTab: isConsoleTab(id) && aid ? aid : s.activeConsoleTab,
      selectedAgentId: aid || s.selectedAgentId,
      dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
    }
  }),
  setDockTopPct: (pct) => {
    const clamped = Math.max(18, Math.min(82, Math.round(pct * 10) / 10))
    try { localStorage.setItem('adaptix.qt.dockTopPct', String(clamped)) } catch { /* ignore */ }
    set({ dockTopPct: clamped })
  },
  setDockMax: (m) => set({ dockMax: m }),
  resetDockLayout: () => {
    try { localStorage.setItem('adaptix.qt.dockTopPct', '46') } catch { /* ignore */ }
    set({
      topTabs: ['sessions'],
      activeTopTab: 'sessions',
      bottomTabs: ['logs'],
      activeBottomTab: 'logs',
      dockTopPct: 46,
      dockMax: 'none',
    })
  },

  setTab: (t) => set((s) => {
    if (isTopDockTab(t)) {
      const topTabs = s.topTabs.includes(t) ? s.topTabs : [...s.topTabs, t]
      return {
        tab: t,
        activeUiTab: t,
        topTabs,
        activeTopTab: t,
        dockMax: s.dockMax === 'bottom' ? 'none' : s.dockMax,
      }
    }
    const bottomTabs = s.bottomTabs.includes(t) ? s.bottomTabs : [...s.bottomTabs, t]
    return {
      tab: t,
      activeUiTab: t,
      bottomTabs,
      activeBottomTab: t,
      dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
    }
  }),
  openConsole: (id) => set((s) => {
    const ctab = consoleTabId(id)
    const tabs = s.openConsoleTabs.includes(id) ? s.openConsoleTabs : [...s.openConsoleTabs, id]
    const uiTabs: TabId[] = s.uiTabs.includes(ctab) ? s.uiTabs : [...s.uiTabs, ctab]
    const bottomTabs: TabId[] = s.bottomTabs.includes(ctab) ? s.bottomTabs : [...s.bottomTabs, ctab]
    return {
      openConsoleTabs: tabs,
      activeConsoleTab: id,
      selectedAgentId: id,
      uiTabs,
      activeUiTab: ctab,
      tab: ctab,
      bottomTabs,
      activeBottomTab: ctab,
      dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
    }
  }),
  closeConsoleTab: (id) => set((s) => {
    const ctab = consoleTabId(id)
    const tabs = s.openConsoleTabs.filter((x) => x !== id)
    const active = s.activeConsoleTab === id ? (tabs[tabs.length - 1] || null) : s.activeConsoleTab
    // 关闭终端页签时清掉该主机的控制台输出，避免下次点开仍残留历史
    const consoleLines = s.consoleLines.filter((l) => l.agentId !== id)
    const consoleByAgent = { ...s.consoleByAgent }
    delete consoleByAgent[id]
    const bottomTabs = s.bottomTabs.filter((t) => t !== ctab)
    const nextBottomTabs: TabId[] = bottomTabs.length ? bottomTabs : ['logs']
    const activeBottomTab = s.activeBottomTab === ctab
      ? nextBottomTabs[nextBottomTabs.length - 1]
      : s.activeBottomTab
    const uiTabs = s.uiTabs.filter((t) => t !== ctab && (tabs.length ? true : t !== 'terminal'))
    const nextUiTabs: TabId[] = uiTabs.length ? uiTabs : ['sessions']
    const next = nextUiTabs[nextUiTabs.length - 1]
    return {
      openConsoleTabs: tabs,
      activeConsoleTab: active,
      consoleLines,
      consoleByAgent,
      bottomTabs: nextBottomTabs,
      activeBottomTab,
      uiTabs: nextUiTabs,
      activeUiTab: s.activeUiTab === ctab ? next : s.activeUiTab,
      tab: s.tab === ctab ? next : s.tab,
    }
  }),
  setActiveConsoleTab: (id) => set((s) => {
    if (!id) return { activeConsoleTab: null }
    const ctab = consoleTabId(id)
    const bottomTabs = s.bottomTabs.includes(ctab) ? s.bottomTabs : [...s.bottomTabs, ctab]
    return { activeConsoleTab: id, selectedAgentId: id, bottomTabs, activeBottomTab: ctab }
  }),
  openUiTab: (id) => set((s) => {
    const tabs: TabId[] = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
    if (isTopDockTab(id)) {
      const topTabs: TabId[] = s.topTabs.includes(id) ? s.topTabs : [...s.topTabs, id]
      return {
        uiTabs: tabs,
        activeUiTab: id,
        tab: id,
        topTabs,
        activeTopTab: id,
        dockMax: s.dockMax === 'bottom' ? 'none' : s.dockMax,
      }
    }
    const bottomTabs: TabId[] = s.bottomTabs.includes(id) ? s.bottomTabs : [...s.bottomTabs, id]
    return {
      uiTabs: tabs,
      activeUiTab: id,
      tab: id,
      bottomTabs,
      activeBottomTab: id,
      dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
    }
  }),
  closeUiTab: (id) => {
    const agentId = tabAgentId(id)
    const needRelease = isInteractTab(id)
    if (isConsoleTab(id) && agentId) {
      get().closeConsoleTab(agentId)
      return
    }
    set((s) => {
      let tabs = s.uiTabs.filter((x) => x !== id)
      if (!tabs.length) tabs = ['sessions']
      const active = (s.activeUiTab === id ? tabs[tabs.length - 1] : s.activeUiTab) || 'sessions'

      if (isTopDockTab(id)) {
        const idx = s.topTabs.indexOf(id)
        let topTabs = s.topTabs.filter((x) => x !== id)
        if (!topTabs.length) topTabs = ['sessions']
        const activeTopTab = s.activeTopTab === id
          ? (topTabs[Math.min(idx, topTabs.length - 1)] || topTabs[0])
          : s.activeTopTab
        return { uiTabs: tabs, activeUiTab: active, tab: active, topTabs, activeTopTab }
      }

      const idx = s.bottomTabs.indexOf(id)
      const bottomTabs = s.bottomTabs.filter((x) => x !== id)
      const activeBottomTab = s.activeBottomTab === id
        ? (bottomTabs.length ? bottomTabs[Math.min(idx, bottomTabs.length - 1)] : null)
        : s.activeBottomTab
      return { uiTabs: tabs, activeUiTab: active, tab: active, bottomTabs, activeBottomTab }
    })
    // 关闭文件/进程/PTY/VNC 页签后尝试恢复 sleep（仍有同主机其他交互页签则不恢复）
    if (needRelease && agentId) {
      // fire-and-forget; errors toasted inside
      get().releaseInteractBoost(agentId).catch(() => {})
    }
  },
  setActiveUiTab: (id) => {
    if (!id) { set({ activeUiTab: 'sessions', tab: 'sessions' }); return }
    get().openUiTab(id)
  },
  openFilesBrowser: (agentId) => {
    if (!agentId) return
    const agent = get().agents[agentId]
    // 离线主机禁止打开文件管理（任务下发无意义且会堆积）
    if (!agent || !isAgentOnline(agent)) {
      get().pushToast('warn', '主机离线，无法打开文件管理')
      return
    }
    const id = filesTabId(agentId)
    const already = get().uiTabs.includes(id) || get().bottomTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      const bottomTabs = s.bottomTabs.includes(id) ? s.bottomTabs : [...s.bottomTabs, id]
      return {
        uiTabs: tabs,
        activeUiTab: id,
        tab: id,
        bottomTabs,
        activeBottomTab: id,
        selectedAgentId: agentId,
        dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
      }
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
    const already = get().uiTabs.includes(id) || get().bottomTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      const bottomTabs = s.bottomTabs.includes(id) ? s.bottomTabs : [...s.bottomTabs, id]
      return {
        uiTabs: tabs,
        activeUiTab: id,
        tab: id,
        bottomTabs,
        activeBottomTab: id,
        selectedAgentId: agentId,
        dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
      }
    })
    if (already) get().acquireInteractBoost(agentId).catch(() => {})
  },
  openPty: (agentId, opts) => {
    if (!agentId) return
    const agent = get().agents[agentId]
    if (!agent || !isAgentOnline(agent)) {
      get().pushToast('warn', '主机离线，无法打开交互 PTY')
      return
    }
    // 每次都新开的调用方（右键『执行命令』）要找一个没被占用的序号 ——
    // 复用已有的那个 tab id 只会把旧会话切到前台，开不出第二个终端。
    let id = ptyTabId(agentId)
    if (opts?.newSession) {
      let n = 1
      while (get().uiTabs.includes(ptyTabId(agentId, n)) || get().bottomTabs.includes(ptyTabId(agentId, n))) n++
      id = ptyTabId(agentId, n)
    }
    const already = get().uiTabs.includes(id) || get().bottomTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      const bottomTabs = s.bottomTabs.includes(id) ? s.bottomTabs : [...s.bottomTabs, id]
      return {
        uiTabs: tabs,
        activeUiTab: id,
        tab: id,
        bottomTabs,
        activeBottomTab: id,
        selectedAgentId: agentId,
        dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
      }
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
    // linux_agent 无桌面捕获能力，VNC 面板是死胡同：直接拦住，不开页签
    if (isLinuxAgent(agent)) {
      get().pushToast('warn', 'Linux 主机不支持 VNC')
      return
    }
    const id = vncTabId(agentId)
    const already = get().uiTabs.includes(id) || get().bottomTabs.includes(id)
    set((s) => {
      const tabs = s.uiTabs.includes(id) ? s.uiTabs : [...s.uiTabs, id]
      const bottomTabs = s.bottomTabs.includes(id) ? s.bottomTabs : [...s.bottomTabs, id]
      return {
        uiTabs: tabs,
        activeUiTab: id,
        tab: id,
        bottomTabs,
        activeBottomTab: id,
        selectedAgentId: agentId,
        dockMax: s.dockMax === 'top' ? 'none' : s.dockMax,
      }
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
    // Linux 主机：sleep 0 交互加速无意义（linux_agent 不解析该命令），
    // 仅登记占位防止误下发，并在关闭页签时由 releaseInteractBoost 直接清理
    if (isLinuxAgent(agent)) {
      set((s) => ({
        interactBoost: {
          ...s.interactBoost,
          [agentId]: { sleep: 0, jitter: 0, ref: 1 },
        },
      }))
      pushLog('info', 'Linux 主机无需交互加速（不发送 sleep 0）', false)
      return
    }
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
    // 唯一的过滤：空行不携带信息。**不按内容/拼写判断**——见本文件顶部那段说明。
    if (!t.trim()) return
    const item: ConsoleLine = { id: uid(), agentId, kind, text: t, ts: now() }
    set((s) => {
      const prev = s.consoleByAgent[agentId] || []
      const nextBucket = prev.length >= 600 ? [...prev.slice(-599), item] : [...prev, item]
      return {
        consoleLines: [...s.consoleLines, item].slice(-2000),
        consoleByAgent: { ...s.consoleByAgent, [agentId]: nextBucket },
      }
    })
  },
  clearConsole: (agentId) => set((s) => {
    if (!agentId) return { consoleLines: [], consoleByAgent: {} }
    const consoleByAgent = { ...s.consoleByAgent }
    delete consoleByAgent[agentId]
    return {
      consoleLines: s.consoleLines.filter((l) => l.agentId !== agentId),
      consoleByAgent,
    }
  }),
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
  setBrowserFiles: (agentId, path, items, message = '') => set((s) => ({
    browserFiles: { ...s.browserFiles, [agentId]: { path, items, message, _ts: Date.now() } },
  })),
  setBrowserProcs: (agentId, items, message = '') => set((s) => ({
    browserProcs: { ...s.browserProcs, [agentId]: { items, message, _ts: Date.now() } },
  })),

  applyGraph: (snap) => set({ graph: snap }),

  loadGraph: async () => {
    try {
      const res = await get().api.topologyGet()
      // ★ 只有真拿到快照才覆盖本地。服务端答 200 但空体时 request() 返回 null，
      //   直接 normSnapshot(null) 会把操作员手上这张图变成空的 —— 而他下一步
      //   一保存，空图就盖掉了服务端上所有人画的东西。见 graphBoard.isSnapshot。
      if (!isSnapshot(res)) {
        get().pushLog('warn', '拓扑图加载失败: 服务端未返回有效数据', false)
        return
      }
      set({ graph: normSnapshot(res) })
    } catch (e: any) {
      // 不弹 toast：进页面自动拉一次就弹错，操作员会以为是自己点坏了。
      get().pushLog('warn', `拓扑图加载失败: ${e?.message || e}`, false)
    }
  },

  saveGraph: async (board) => {
    const { api, graph } = get()
    try {
      const res = await api.topologySave(graph.rev, board)
      // ★ 判成功必须看 rev，不能看「没抛异常」。同样那条 200+空体路径会让
      //   request() 返回 null，而 `Number(undefined) || rev+1` 恰好算出一个
      //   漂亮的 rev ⇒ 界面报告保存成功、实际什么都没落库。宁可误报失败，
      //   也不能让操作员以为存住了。
      if (!isSnapshot(res)) {
        get().pushLog('err', '拓扑图保存失败: 服务端未确认（未返回 rev）')
        get().pushToast('err', '拓扑图保存失败：服务端未确认，刷新后会丢失')
        return false
      }
      // 本地立即生效，不等 WS 推送回来：推送是尽力而为的，而"拖完就按 F5"
      // 一定会发生。等推送的写法在这条路径上会丢操作。
      set({ graph: { rev: Number(res.rev), board } })
      return true
    } catch (e: any) {
      const p = e?.payload
      if (p && p.conflict) {
        // 别人先改了。服务端把当前版本连同 board 一起回给我们，直接吃下重画 ——
        // 只回一句「图已被他人修改」的话，操作员只能靠猜现在是什么样。
        set({ graph: normSnapshot(p) })
        get().pushToast('warn', '拓扑图已被他人修改，已载入最新版本')
        return false
      }
      get().pushLog('err', `拓扑图保存失败: ${e?.message || e}`)
      get().pushToast('err', `拓扑图保存失败: ${e?.message || e}`)
      return false
    }
  },

  loginAndConnect: async ({ baseUrl, username, password }) => {
    const api = new ApiClient(baseUrl || '')
    get().pushLog('info', `login ${username}`, false)
    await api.login(username, password)
    get().pushLog('ok', t('JWT 登录成功'), false)
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
    if (restoreInFlight) return restoreInFlight
    const run = (async () => {
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
      const epochAtStart = connectEpoch
      // 校验 token 并直接复用这次 listAgents() 返回的新鲜主机列表，避免 F5 重复拉取和阻塞
      let initialAgents: any[] = []
      try {
        const res = await api.listAgents()
        if (Array.isArray(res)) initialAgents = res
      } catch (e: any) {
        if (epochAtStart !== connectEpoch) return false
        clearSession()
        get().pushLog('warn', `会话已失效，请重新登录: ${e?.message || e}`, false)
        return false
      }
      if (epochAtStart !== connectEpoch) return false
      // finishConnect 会立即置 connected=true 并写入 initialAgents，REST 补充和 WS 建连在后台并行，
      // 不再把用户卡在「恢复会话…」长达数十秒导致时间戳过期变离线。
      void get().finishConnect(api, snap.username, initialAgents)
      get().pushLog('ok', '会话已恢复（F5）', false)
      return true
    })()
    restoreInFlight = run
    try {
      return await run
    } finally {
      if (restoreInFlight === run) restoreInFlight = null
    }
  },

  // 登录 / 恢复会话共用：进入主界面 + 并行 REST 与 WS（不阻塞在 20 路 listTasks 上）
  finishConnect: async (api: ApiClient, username: string, preloadedAgents?: any[]) => {
    const epoch = ++connectEpoch
    clearSyncWatchdog()
    clearPendingWsQueue()
    clearLiveAgentTicks()
    cancelConnectWait?.()
    cancelConnectWait = null
    try { get().socket.close() } catch { /* ignore */ }

    const initAgentMap: Record<string, Agent> = {}
    const initTicks: Record<string, number> = {}
    const hasPreloaded = Array.isArray(preloadedAgents)
    if (hasPreloaded) {
      for (const a of preloadedAgents!) {
        const n = normAgent(a)
        if (n.a_id) {
          initAgentMap[n.a_id] = n
          if (n.a_last_tick) {
            const tickVal = Number(n.a_last_tick)
            initTicks[n.a_id] = tickVal
            setLiveAgentTick(n.a_id, tickVal)
          }
        }
      }
    }

    set({
      api,
      connected: true,
      wsConnected: false,
      syncing: true,
      lastWsPacketAt: 0,
      username,
      lastError: '',
      browserFiles: {},
      browserProcs: {},
      agents: initAgentMap,
      agentTicks: initTicks,
      consoleByAgent: {},
      listeners: {},
      tasks: [],
      downloads: {},
      chatMessages: [],
      pivots: {},
      // 换账号/换服务端连着进来时不能带着上一条连接的图渲染。
      graph: emptySnapshot(),
      openConsoleTabs: [],
      activeConsoleTab: null,
      selectedAgentId: null,
      checkedIds: new Set<string>(),
      uiTabs: ['sessions'],
      activeUiTab: 'sessions',
      tab: 'sessions',
      topTabs: ['sessions'],
      activeTopTab: 'sessions',
      bottomTabs: ['logs'],
      activeBottomTab: 'logs',
      dockMax: 'none',
    })

    // 并行拉取轻量 REST（listeners/downloads，必要时 agents），绝不阻塞 WS 建连
    void get().refreshRest({ includeTasks: false, skipAgents: hasPreloaded })
      .then(() => {
        if (epoch === connectEpoch) get().pushLog('ok', t('REST 数据已加载'), false)
      })
      .catch((e: any) => {
        if (epoch === connectEpoch) get().pushLog('err', `REST 加载失败: ${e?.message || e}`)
      })

    let socket: SyncSocket | null = null
    try {
      const otp = await api.generateOtp('connect', {
        client_type: 2,
        console_team_mode: true,
        subscriptions: DEFAULT_SUBSCRIPTIONS,
      })
      if (epoch !== connectEpoch) return
      get().pushLog('info', '正在建立实时通道…', false)
      const wsUrl = api.connectWsUrl(otp)
      socket = new SyncSocket()
      set({ socket })
      await new Promise<void>((resolve, reject) => {
        let settled = false
        const finish = (err?: Error) => {
          if (settled) return
          settled = true
          clearTimeout(timer)
          if (cancelConnectWait === abort) cancelConnectWait = null
          if (err) reject(err)
          else resolve()
        }
        const abort = () => finish()
        cancelConnectWait = abort
        const timer = setTimeout(() => {
          if (!settled) {
            try { socket?.close() } catch { /* ignore */ }
            finish(new Error('websocket timeout'))
          }
        }, 8000)
        socket!.connect(wsUrl, {
          onOpen: async () => {
            if (epoch !== connectEpoch) {
              try { socket?.close() } catch { /* ignore */ }
              finish()
              return
            }
            set({ socket: socket!, wsConnected: true })
            get().pushLog('ok', 'WebSocket 已连接', false)
            try {
              await api.sync()
              if (epoch === connectEpoch) {
                get().pushLog('info', 'sync 已启动', false)
              }
            } catch (e: any) {
              if (epoch === connectEpoch) {
                get().pushLog('warn', `sync 调用失败: ${e.message || e}`, false)
              }
            }
            // 兜底看门狗：若发出 /api/sync 后 2.5s 仍未收到 SYNC_START，自动重发，
            // 防止连接竞争或瞬态抖动导致永久停留在「同步 拉取中」并停推心跳。
            const scheduleSyncRetry = (attempt: number) => {
              clearSyncWatchdog()
              syncWatchdogTimer = setTimeout(async () => {
                syncWatchdogTimer = null
                if (epoch !== connectEpoch || !get().wsConnected || !get().syncing) return
                if (syncStartSeenForEpoch === epoch) return
                if (attempt >= 3) {
                  set({ syncing: false })
                  get().refreshRest().catch(() => {})
                  return
                }
                try {
                  await api.sync()
                } catch { /* ignore */ }
                if (epoch === connectEpoch && get().wsConnected && get().syncing && syncStartSeenForEpoch !== epoch) {
                  scheduleSyncRetry(attempt + 1)
                }
              }, 2500)
            }
            if (epoch === connectEpoch && get().wsConnected && get().syncing && syncStartSeenForEpoch !== epoch) {
              scheduleSyncRetry(1)
            }
            finish()
          },
          onClose: (ev) => {
            if (epoch === connectEpoch) {
              clearSyncWatchdog()
              clearPendingWsQueue()
              set({ wsConnected: false, syncing: false })
              get().pushLog('warn', `WebSocket 关闭 code=${ev.code}`, false)
            }
            finish(epoch === connectEpoch ? new Error(`websocket closed ${ev.code}`) : undefined)
          },
          onError: () => {
            if (epoch === connectEpoch) {
              get().pushLog('err', 'WebSocket 错误', false)
            }
          },
          onPacket: (pkt) => {
            if (epoch === connectEpoch) {
              const ts = Date.now()
              if (ts - get().lastWsPacketAt > 2000) set({ lastWsPacketAt: ts })
              get().handlePackets([pkt])
            }
          },
          onPackets: (pkts) => {
            if (epoch !== connectEpoch || !pkts.length) return
            const ts = Date.now()
            if (ts - get().lastWsPacketAt > 2000) set({ lastWsPacketAt: ts })
            pendingWsQueue.push(...pkts)
            if (pkts.some(isUrgentPacket) || pendingWsQueue.length >= 64) {
              if (pendingWsTimer) { clearTimeout(pendingWsTimer); pendingWsTimer = null }
              const batch = pendingWsQueue
              pendingWsQueue = []
              get().handlePackets(batch)
              return
            }
            if (!pendingWsTimer) {
              pendingWsTimer = setTimeout(() => {
                pendingWsTimer = null
                if (epoch !== connectEpoch || !pendingWsQueue.length) {
                  pendingWsQueue = []
                  return
                }
                const batch = pendingWsQueue
                pendingWsQueue = []
                get().handlePackets(batch)
              }, 32)
            }
          },
        })
      })
    } catch (e: any) {
      if (epoch !== connectEpoch) return
      try { socket?.close() } catch { /* ignore */ }
      const msg = e?.message || String(e)
      set({ lastError: `实时通道失败（REST 仍可用）: ${msg}`, syncing: false, wsConnected: false })
      // 降级状态已经显示在底部状态栏；连接失败只写日志，避免登录后弹窗
      // 挡住主机表。操作员需要详情时可打开事件流或系统日志查看原文。
      get().pushLog('warn', `实时通道失败，继续使用 REST: ${msg}`, false)
    } finally {
      if (epoch === connectEpoch && get().syncing && !get().wsConnected) set({ syncing: false })
    }
  },

  disconnect: () => {
    connectEpoch++
    clearSyncWatchdog()
    clearPendingWsQueue()
    clearLiveAgentTicks()
    cancelConnectWait?.()
    cancelConnectWait = null
    try { get().socket.close() } catch { /* ignore */ }
    clearSession()
    set({
      connected: false,
      wsConnected: false,
      syncing: false,
      lastWsPacketAt: 0,
      serverInterfaces: [],
      username: '',
      agents: {},
      agentTicks: {},
      consoleByAgent: {},
      listeners: {},
      tasks: [],
      downloads: {},
      chatMessages: [],
      pivots: {},
      // ★ 只清本地缓存。服务端那份是所有人共享的，退出登录绝不能顺手把它抹了。
      graph: emptySnapshot(),
      openConsoleTabs: [],
      activeConsoleTab: null,
      selectedAgentId: null,
      checkedIds: new Set<string>(),
      consoleLines: [],
      browserFiles: {},
      browserProcs: {},
      uiTabs: ['sessions'],
      activeUiTab: 'sessions',
      tab: 'sessions',
      topTabs: ['sessions'],
      activeTopTab: 'sessions',
      bottomTabs: ['logs'],
      activeBottomTab: 'logs',
      dockMax: 'none',
    })
  },

  handlePackets: (pkts) => {
    const flat = flattenPackets(pkts)
    if (!flat.length) return
    if (flat.length === 1) {
      get().handlePacket(flat[0])
      return
    }

    let pendingTicks: Record<string, number> | null = null
    let pendingAgents: Record<string, { pkt: Packet; norm: Agent }> | null = null
    let pendingTasks: Task[] | null = null
    let pendingConsole: Array<{ agentId: string; kind: ConsoleLine['kind']; text: string }> | null = null

    const flushAccumulated = () => {
      if (!pendingTicks && !pendingAgents && !pendingTasks && !pendingConsole) return
      const ticksBatch = pendingTicks
      const agentsBatch = pendingAgents
      const tasksBatch = pendingTasks
      const consoleBatch = pendingConsole
      pendingTicks = null
      pendingAgents = null
      pendingTasks = null
      pendingConsole = null

      set((s) => {
        const patch: Partial<State> = {}
        let nextAgents: Record<string, Agent> | null = null
        let nextTicks: Record<string, number> | null = null

        if (agentsBatch) {
          for (const [id, { pkt, norm }] of Object.entries(agentsBatch)) {
            const old = (nextAgents ?? s.agents)[id]
            const next = old && (pkt.type ?? pkt.Type) === P.AGENT_UPDATE ? normAgent({ ...old, ...pkt }) : { ...old, ...norm }
            const curTick = (nextTicks ?? s.agentTicks)[id]
            const bestTick = maxTick(curTick, old?.a_last_tick, next.a_last_tick)
            if (bestTick !== undefined) {
              next.a_last_tick = bestTick
              setLiveAgentTick(id, bestTick)
              if (bestTick !== curTick) {
                if (!nextTicks) nextTicks = { ...s.agentTicks }
                nextTicks[id] = bestTick
              }
            }
            if (!old || !agentMetaEqual(old, next)) {
              if (!nextAgents) nextAgents = { ...s.agents }
              nextAgents[id] = next
            }
          }
        }

        if (ticksBatch) {
          const activeAgents = nextAgents ?? s.agents
          for (const [id, tick] of Object.entries(ticksBatch)) {
            if (!activeAgents[id]) continue
            const curTick = (nextTicks ?? s.agentTicks)[id]
            const best = maxTick(curTick, activeAgents[id]?.a_last_tick, tick)
            if (best !== undefined) {
              setLiveAgentTick(id, best)
              if (best !== curTick) {
                if (!nextTicks) nextTicks = { ...s.agentTicks }
                nextTicks[id] = best
              }
            }
          }
        }

        if (nextAgents) patch.agents = nextAgents
        if (nextTicks) patch.agentTicks = nextTicks

        if (tasksBatch && tasksBatch.length) {
          const byId = new Map<string, Task>()
          for (const existing of s.tasks) {
            if (existing.a_task_id) byId.set(existing.a_task_id, existing)
          }
          const touchedOrder: string[] = []
          for (const t of tasksBatch) {
            if (!t.a_task_id) continue
            const prev = byId.get(t.a_task_id)
            byId.set(t.a_task_id, prev ? mergeTask(prev, t) : t)
            touchedOrder.push(t.a_task_id)
          }
          const touchedSet = new Set(touchedOrder)
          const head: Task[] = []
          const seen = new Set<string>()
          for (let i = touchedOrder.length - 1; i >= 0; i--) {
            const tid = touchedOrder[i]
            if (seen.has(tid)) continue
            seen.add(tid)
            const item = byId.get(tid)
            if (item) head.push(item)
          }
          const tail = s.tasks.filter((x) => !x.a_task_id || !touchedSet.has(x.a_task_id))
          patch.tasks = [...head, ...tail].slice(0, 500)
        }

        if (consoleBatch && consoleBatch.length) {
          const now = Date.now()
          const addedAll: ConsoleLine[] = []
          const byAgent = { ...s.consoleByAgent }
          for (let i = 0; i < consoleBatch.length; i++) {
            const c = consoleBatch[i]
            const line: ConsoleLine = { id: uid(), ts: now + i, agentId: c.agentId, kind: c.kind, text: c.text }
            addedAll.push(line)
            if (c.agentId) {
              const prev = byAgent[c.agentId] || []
              byAgent[c.agentId] = [...prev, line].slice(-600)
            }
          }
          patch.consoleLines = [...s.consoleLines, ...addedAll].slice(-1200)
          patch.consoleByAgent = byAgent
        }

        return Object.keys(patch).length ? patch : s
      })
    }

    for (const pkt of flat) {
      const type = pkt.type ?? pkt.Type
      switch (type) {
        case P.AGENT_TICK: {
          const ids: string[] = Array.isArray(pkt.a_id) ? pkt.a_id : (pkt.a_id ? [pkt.a_id] : [])
          if (pkt.a_last_tick) syncServerClock(pkt.a_last_tick)
          const tick = pkt.a_last_tick || Math.floor(getServerNowMs() / 1000)
          if (ids.length) {
            if (!pendingTicks) pendingTicks = {}
            for (const id of ids) {
              const best = maxTick(pendingTicks[id], tick)
              if (best !== undefined) pendingTicks[id] = best
            }
          }
          break
        }
        case P.AGENT_NEW:
        case P.AGENT_UPDATE: {
          const a = normAgent(pkt)
          if (!a.a_id) break
          if (!pendingAgents) pendingAgents = {}
          const prev = pendingAgents[a.a_id]
          if (prev && type === P.AGENT_UPDATE) {
            const mergedPkt = { ...prev.pkt, ...pkt }
            pendingAgents[a.a_id] = { pkt: mergedPkt, norm: normAgent(mergedPkt) }
          } else {
            pendingAgents[a.a_id] = { pkt, norm: a }
          }
          break
        }
        case P.AGENT_TASK_SYNC:
        case P.AGENT_TASK_UPDATE:
        case P.AGENT_TASK_SEND: {
          if (pkt.a_task_id) {
            if (!pendingTasks) pendingTasks = []
            pendingTasks.push(normTask(pkt))
          }
          break
        }
        case P.CONSOLE_LOCAL: {
          const aid = String(pkt.a_id || '')
          const text = String(pkt.a_text || '')
          const message = String(pkt.a_message || '')
          const cmdline = String(pkt.a_cmdline || '')
          const body = (text || message).trim()
          if (body) {
            if (!pendingConsole) pendingConsole = []
            pendingConsole.push({ agentId: aid, kind: 'ok', text: body })
          } else if (cmdline) {
            if (!pendingConsole) pendingConsole = []
            pendingConsole.push({ agentId: aid, kind: 'local', text: cmdline.startsWith('>') ? cmdline : `> ${cmdline}` })
          }
          break
        }
        case P.CONSOLE_ERROR: {
          if (!pendingConsole) pendingConsole = []
          pendingConsole.push({ agentId: pkt.a_id || '', kind: 'err', text: String(pkt.a_message || pkt.a_text || '') })
          break
        }
        case P.CONSOLE_OUT:
        case P.CONSOLE_TASK_SYNC:
        case P.CONSOLE_TASK_UPD: {
          const message = String(pkt.a_message || pkt.message || '')
          const text = String(pkt.a_text || pkt.text || '')
          const msg = [message, text].filter(Boolean).join('\n')
          const kind: ConsoleLine['kind'] = pkt.a_completed === false ? 'task' : 'ok'
          const aid = pkt.a_id || ''
          if (msg && aid) {
            if (!pendingConsole) pendingConsole = []
            pendingConsole.push({ agentId: aid, kind, text: msg })
          }
          if (pkt.a_task_id) {
            if (!pendingTasks) pendingTasks = []
            pendingTasks.push(normTask(pkt))
          }
          break
        }
        default: {
          flushAccumulated()
          get().handlePacket(pkt)
          break
        }
      }
    }
    flushAccumulated()
  },

  handlePacket: (pkt) => {
    const type = pkt.type ?? pkt.Type
    const { pushLog, pushConsole } = get()
    if ((type === P.SYNC_CATEGORY_BATCH || type === P.SYNC_BATCH) && Array.isArray(pkt.packets)) {
      get().handlePackets(pkt.packets)
      return
    }
    switch (type) {
      case P.SYNC_START:
        // 服务端在同步起始包里带上了自己的网卡地址（SyncPackerStart.interfaces，
        // 与 Qt 侧 ax.interfaces() 同源）。此前这个字段收到就丢。
        // Web 这边只拿它给「回连地址」的缺省值做旁证 —— 见 lib/callbackHost.ts。
        syncStartSeenForEpoch = connectEpoch
        clearSyncWatchdog()
        {
          const syncEpoch = connectEpoch
          // 防止 SYNC_START 到达后因大包/代理丢帧导致 SYNC_FINISH 迟迟不到而永久卡死在「同步 拉取中」
          syncFinishTimer = setTimeout(() => {
            syncFinishTimer = null
            if (syncEpoch === connectEpoch && get().syncing) {
              set({ syncing: false })
              get().refreshRest().catch(() => {})
            }
          }, 10000)
        }
        if (pkt.server_time) syncServerClock(pkt.server_time)
        set({ syncing: true, serverInterfaces: Array.isArray(pkt.interfaces) ? pkt.interfaces : [] })
        break
      case P.SYNC_FINISH:
        syncStartSeenForEpoch = connectEpoch
        clearSyncWatchdog()
        set({ syncing: false })
        get().pushLog('ok', '实时同步完成')
        // 同步完成后只做轻量 REST 对账（不触发 20 路 SQLite listTasks 阻塞服务端），
        // 并在后台低并发补齐最近任务列表。
        get().refreshRest({ includeTasks: true }).catch(() => {})
        break
      case P.NOTIFICATION:
        pushLog('info', String(pkt.message || pkt.Message || ''))
        break
      case P.SERVICE_DATA: {
        // 插件自定义服务的广播（TsServiceSendDataAll）。
        // ★ SyncPackerServiceData.Data 是 **string**，里面才是 JSON ——
        //   少一次 parse 就会把 "{\"rev\":3}" 整个字面量当成 board。
        if (String(pkt.service || '') !== 'topology') break
        let inner: any = null
        try { inner = JSON.parse(String(pkt.data ?? '')) } catch { break }
        // 解析出来还得**是**一份快照。这条守卫与 loadGraph/saveGraph 那两处同源：
        // 插件只推 rev+board，缺了 rev 就说明这不是本轮认识的格式。
        if (!isSnapshot(inner)) break
        // 服务端是唯一权威，收到就吃下，不做 rev 比较。
        // 加"只接受更新的 rev"这道守卫看似稳妥，实际会造出一个死局：
        // 一旦有人手工删掉 ExtenderData 里那行（回退文档里就是这么写的），
        // rev 从 0 重来，所有还开着的浏览器就会永远拒绝更新。
        set({ graph: normSnapshot(inner) })
        break
      }
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
        if (!a.a_id) break
        set((s) => {
          const old = s.agents[a.a_id]
          // ★ AGENT_UPDATE 是**部分更新**：服务端 SyncPackerAgentUpdate 除 a_id 外
          // 每个字段都是 `*T` + omitempty（core/server/utils.go:335-363），
          // 一次「设置标签」只带 a_id + a_tags 两个字段，其余一概不出现。
          // 而 normAgent 是「字段齐全」的归一化器，包里没有的字段会填 '' / undefined，
          // 于是 {...old, ...normAgent(pkt)} 会把 a_os / a_computer / a_username /
          // a_arch / a_elevated 这些**包里根本没提**的字段一起冲成空。
          // （长期没被发现，是因为 Hosts 改完标签会 refreshRest() 重拉全量补回来；
          //   但拓扑图这类不重拉的路径上，空白会一直留到下次全量同步。）
          //
          // 修法：先把包里的**原始键**盖到旧值上，再整体归一化 ——
          // 「这次到底带了哪些字段」由包自己说了算，不必再维护一张
          // a_x → x 的别名表（那张表在 a_external_ip→external、
          // a_os_desc→os、a_oemcp→oem_cp 这几处并不规则，通用规则一定写错）。
          const next = old && pkt.type === P.AGENT_UPDATE ? normAgent({ ...old, ...pkt }) : { ...old, ...a }
          // 单调保留较新的心跳时间，避免预同步 AGENT_NEW 覆盖刚到达的实时 AGENT_TICK
          const bestTick = maxTick(s.agentTicks[a.a_id], old?.a_last_tick, next.a_last_tick)
          if (bestTick !== undefined) {
            next.a_last_tick = bestTick
            setLiveAgentTick(a.a_id, bestTick)
          }
          const patch: Partial<State> = {}
          if (bestTick !== undefined && bestTick !== s.agentTicks[a.a_id]) {
            patch.agentTicks = { ...s.agentTicks, [a.a_id]: bestTick }
          }
          if (!old || !agentMetaEqual(old, next)) {
            patch.agents = { ...s.agents, [a.a_id]: next }
          }
          return Object.keys(patch).length ? patch : s
        })
        break
      }
      case P.AGENT_TICK: {
        // 服务端 SyncPackerAgentTick 把 id 列表序列化为数组放在 a_id（json:"a_id"）
        const ids: string[] = Array.isArray(pkt.a_id) ? pkt.a_id : (pkt.a_id ? [pkt.a_id] : [])
        if (pkt.a_last_tick) syncServerClock(pkt.a_last_tick)
        const tick = pkt.a_last_tick || Math.floor(getServerNowMs() / 1000)
        if (!ids.length) break
        set((s) => {
          let nextTicks: Record<string, number> | null = null
          for (const id of ids) {
            if (!s.agents[id]) continue
            const cur = s.agentTicks[id]
            const best = maxTick(cur, s.agents[id]?.a_last_tick, tick)
            if (best !== undefined) {
              setLiveAgentTick(id, best)
              if (best !== cur) {
                if (!nextTicks) nextTicks = { ...s.agentTicks }
                nextTicks[id] = best
              }
            }
          }
          return nextTicks ? { agentTicks: nextTicks } : s
        })
        break
      }
      case P.AGENT_REMOVE: {
        const ids: string[] = pkt.a_id_array || (pkt.a_id ? [pkt.a_id] : [])
        // 走同一份清理配方，而不是只删 agents 里的键：别的主机下线时，
        // 这边开着的控制台页签 / 文件页签 / 勾选状态同样是悬挂的。
        // （没删干净的话，下一次批量删除会把这些幽灵 id 发给服务端。）
        set((s) => purgeAgents(s, new Set(ids)))
        break
      }
      case P.AGENT_TASK_SYNC:
      case P.AGENT_TASK_UPDATE:
      case P.AGENT_TASK_SEND: {
        // ★ 这里是「任务跑完，命令/开始/操作员全变 `-`」的**真凶**，别再只盯
        //   下面的 CONSOLE_* 分支（那一处是同一个 bug 的第二实例，但不是
        //   产生现象的那一次写入）。服务端对每个任务发两条通道的包：
        //     mgr_task.go:88 → AGENT_TASK_UPDATE，0x4a，字段只有
        //       a_id/a_task_id/a_handler_id/a_task_type/a_finish_time/
        //       a_msg_type/a_message/a_text/a_completed
        //       —— **没有 a_cmdline / a_start_time / a_client**（utils.go:401）
        //     mgr_task.go:99 → CONSOLE_TASK_UPD，同样不带这三样
        //   而建行的 AGENT_TASK_SYNC（utils.go:383）**带全**五样。
        //   于是整条替换 = 用残缺包覆盖完整行。改成合并（见 mergeTask）。
        if (pkt.a_task_id) {
          const t = normTask(pkt)
          set((s) => {
            const prev = s.tasks.find((x) => x.a_task_id === t.a_task_id)
            const merged = prev ? mergeTask(prev, t) : t
            return { tasks: [merged, ...s.tasks.filter((x) => x.a_task_id !== t.a_task_id)].slice(0, 500) }
          })
        }
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
        // 服务端 SyncPackerDownloadDelete 的字段名是 d_files_id
        const ids: string[] = pkt.d_files_id || pkt.d_file_id_array || (pkt.d_file_id ? [pkt.d_file_id] : [])
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
              // linux_agent 的 b_date 是格式化字符串（"Jan 05 12:34"）而非 epoch：非数字则原样透传
              const num = Number(x.b_date)
              const date = Number.isFinite(num) && num > 0 ? new Date(num * 1000) : null
              return {
              name: rawName,
              isDir: !!(x.b_is_dir ?? x.is_dir ?? x.IsDir ?? x.dir),
              size: Number(x.b_size ?? x.size ?? x.Size ?? 0),
              date: date ? Math.floor(date.getTime() / 1000) : (Number(x.date ?? x.Date ?? 0) || 0),
              dateText: date ? date.toLocaleString() : (typeof x.b_date === 'string' ? x.b_date : ''),
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
          pushConsole(aid, 'ok', body)
        } else if (cmdline) {
          pushConsole(aid, 'local', cmdline.startsWith('>') ? cmdline : `> ${cmdline}`)
        }
        break
      }
      case P.CONSOLE_ERROR: {
        // 错误一律进控制台（原文）。此前按 isBrowserCmdline 把「像文件管理器」的
        // 报错降级成 toast ⇒ 手敲 ls/cat/ps… 失败时控制台一个字都没有。
        pushConsole(pkt.a_id || '', 'err', String(pkt.a_message || pkt.a_text || ''))
        break
      }
      case P.CONSOLE_OUT:
      case P.CONSOLE_TASK_SYNC:
      case P.CONSOLE_TASK_UPD: {
        const message = String(pkt.a_message || pkt.message || '')
        const text = String(pkt.a_text || pkt.text || '')
        const msg = [message, text].filter(Boolean).join('\n')
        const kind = pkt.a_completed === false ? 'task' : 'ok'
        const aid = pkt.a_id || ''
        if (msg && aid) {
          pushConsole(aid, kind as any, msg)
        }
        // 任务行必须**合并**，不能整条替换：type=107（完成包）不带
        // a_cmdline/a_start_time/a_client。★ 但**产生「跑完变 `-`」现象的那次
        // 写入不在这里**，在 P.AGENT_TASK_UPDATE 分支（它先到、先把行擦空，
        // 于是这里的 prev 已经是残缺的）。两处都要合并，缺一不可。
        if (pkt.a_task_id) {
          const t = normTask(pkt)
          set((s) => {
            const prev = s.tasks.find((x) => x.a_task_id === t.a_task_id)
            const merged = prev ? mergeTask(prev, t) : t
            return { tasks: [merged, ...s.tasks.filter((x) => x.a_task_id !== t.a_task_id)].slice(0, 500) }
          })
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

  refreshRest: async (opts) => {
    const includeTasks = !!opts?.includeTasks
    const skipAgents = !!opts?.skipAgents
    const epoch = connectEpoch
    const seq = ++refreshRestSeq
    const { api } = get()
    const [agentsRes, listenersRes, downloadsRes] = await Promise.allSettled([
      skipAgents ? Promise.resolve(null) : api.listAgents(),
      api.listListeners(),
      api.listDownloads(),
    ])
    if (epoch !== connectEpoch || seq !== refreshRestSeq) return

    const agentsOk = !skipAgents && agentsRes.status === 'fulfilled' && Array.isArray(agentsRes.value)
    const listenersOk = listenersRes.status === 'fulfilled' && Array.isArray(listenersRes.value)
    const downloadsOk = downloadsRes.status === 'fulfilled' && Array.isArray(downloadsRes.value)

    const agentArr = agentsOk ? (agentsRes.value as any[]) : []
    const agentMap: Record<string, Agent> = {}
    for (const a of agentArr) {
      const n = normAgent(a)
      if (n.a_id) agentMap[n.a_id] = n
    }
    const listenerMap: Record<string, Listener> = {}
    if (listenersOk) {
      for (const l of listenersRes.value) {
        const n = normListener(l)
        if (n.l_name) listenerMap[n.l_name] = n
      }
    }
    const dlMap: Record<string, DownloadItem> = {}
    if (downloadsOk) {
      for (const d of downloadsRes.value) {
        const n = normDl(d)
        if (n.d_file_id) dlMap[n.d_file_id] = n
      }
    }

    // 立即落盘 agents/listeners/downloads：失败时不清空已有列表，且单调保留较新的 WS a_last_tick。
    set((s) => {
      const patch: Partial<State> = {}
      if (agentsOk) {
        const merged: Record<string, Agent> = {}
        let nextTicks: Record<string, number> | null = null
        const prevKeys = Object.keys(s.agents)
        const nextEntries = Object.entries(agentMap)
        let agentsChanged = prevKeys.length !== nextEntries.length
        for (const [id, n] of nextEntries) {
          const old = s.agents[id]
          const bestTick = maxTick(s.agentTicks[id], old?.a_last_tick, n.a_last_tick)
          if (bestTick !== undefined) {
            setLiveAgentTick(id, bestTick)
            if (bestTick !== s.agentTicks[id]) {
              if (!nextTicks) nextTicks = { ...s.agentTicks }
              nextTicks[id] = bestTick
            }
          }
          if (old && agentMetaEqual(old, n)) {
            merged[id] = old
          } else {
            agentsChanged = true
            merged[id] = bestTick !== undefined ? { ...n, a_last_tick: bestTick } : n
          }
        }
        // 同步清理已不存在的 agentTicks 键
        const staleIds: string[] = []
        for (const id of Object.keys(s.agentTicks)) {
          if (!merged[id]) {
            if (!nextTicks) nextTicks = { ...s.agentTicks }
            delete nextTicks[id]
            staleIds.push(id)
          }
        }
        if (staleIds.length) clearLiveAgentTicks(staleIds)
        if (nextTicks) patch.agentTicks = nextTicks
        if (agentsChanged) {
          patch.agents = merged
          // 勾选集合跟 REST 名单求交：批量条上显示的必须是**真的还在**的主机数。
          const filteredChecked = [...s.checkedIds].filter((id) => !!merged[id])
          if (filteredChecked.length !== s.checkedIds.size) {
            patch.checkedIds = new Set(filteredChecked)
          }
        }
      }
      if (listenersOk) patch.listeners = listenerMap
      if (downloadsOk) patch.downloads = dlMap
      return Object.keys(patch).length ? patch : s
    })

    if (!includeTasks) return

    // 任务历史改为后台低并发（最多 2 路、limit=50）分批拉取，不阻塞调用方也不打满服务端 SQLite 单连接。
    const tSeq = ++refreshTasksSeq
    const agentIds = agentArr.map((a: any) => a.a_id || a.id).filter(Boolean).slice(0, 20)
    for (const id of Object.keys(get().agents)) {
      if (agentIds.length >= 20) break
      if (!agentIds.includes(id)) agentIds.push(id)
    }
    void (async () => {
      const collected: any[] = []
      const CONCURRENCY = 2
      let idx = 0
      const worker = async () => {
        while (idx < agentIds.length) {
          if (epoch !== connectEpoch || tSeq !== refreshTasksSeq) return
          const id = agentIds[idx++]
          const list = await api.listTasks(id, 50).catch(() => [])
          if (Array.isArray(list)) collected.push(...list)
        }
      }
      await Promise.all(Array.from({ length: Math.min(CONCURRENCY, agentIds.length) }, () => worker()))
      if (epoch !== connectEpoch || tSeq !== refreshTasksSeq) return
      const normalized = collected.filter(Boolean).map(normTask)
      set((s) => {
        // 合并实时 WS 任务与历史任务，避免覆盖刚通过 WS 到达的新任务状态
        const byId = new Map<string, Task>()
        for (const t of normalized) {
          if (t.a_task_id) byId.set(t.a_task_id, t)
        }
        for (const existing of s.tasks) {
          if (!existing.a_task_id) continue
          const prev = byId.get(existing.a_task_id)
          byId.set(existing.a_task_id, prev ? mergeTask(prev, existing) : existing)
        }
        const merged = Array.from(byId.values())
          .sort((a, b) => Number(b.a_start_time || 0) - Number(a.a_start_time || 0))
          .slice(0, 500)
        return { tasks: merged }
      })
    })()
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

  /**
   * 删一批主机。单选与批量走**同一条**路径。
   *
   * ★ 这里的 catch 是必须的，而且是本功能最容易写错的一处：
   * `tc_agents.go:311-331` 会遍历所有 id 收集错误，**只要有一台失败**就整体
   * 返回 `{message: <编号列表>, ok: false}`（HTTP 200），而 `http.ts:92` 在
   * `ok === false` 时抛 `data.message`。如果照直 await 出去，结果是
   * 「批量删 50 台、其中 1 台删不掉 ⇒ 抛错 ⇒ 本地清理根本不执行 ⇒ 50 台
   * 全都还在，日志里只有一行错误」。所以：出错也照清，再 refreshRest 对账，
   * 服务端真的没删掉的会自己重新出现。
   */
  removeAgents: async (ids) => {
    const { api, pushLog } = get()
    const list = [...new Set(ids)].filter((id) => !!get().agents[id])
    if (!list.length) return
    let failed = false
    try {
      await api.removeAgents(list)
    } catch (e: any) {
      failed = true
      pushLog('err', `删除未全部成功（本地已清理，稍后对账）：${e?.message || e}`)
    }
    set((s) => purgeAgents(s, new Set(list)))
    pushLog('warn', failed
      ? `已请求删除 ${list.length} 台主机（服务端报告部分失败）`
      : `已删除 ${list.length} 台主机`)
    await get().refreshRest().catch(() => {})
  },

  removeSelected: async () => {
    const { selectedAgentId } = get()
    if (selectedAgentId) await get().removeAgents([selectedAgentId])
  },

  removeChecked: async () => {
    const { checkedIds } = get()
    if (checkedIds.size) await get().removeAgents([...checkedIds])
  },

  toggleChecked: (id, on) => set((s) => {
    const n = new Set(s.checkedIds)
    if (on ?? !n.has(id)) n.add(id); else n.delete(id)
    return { checkedIds: n }
  }),

  setChecked: (ids, on) => set((s) => {
    const n = new Set(s.checkedIds)
    for (const id of ids) { if (on) n.add(id); else n.delete(id) }
    return { checkedIds: n }
  }),

  clearChecked: () => set({ checkedIds: new Set<string>() }),
}))

/**
 * `0.0.0.0` / `::` 是"没取到地址"的哨兵值，不是地址。
 *
 * 原样留着会一路伪装成真地址。实测 454 台主机里 448 台的 a_internal_ip 是
 * `0.0.0.0`，于是所有 `a_internal_ip || a_external_ip || '-'` 的回退链全断了：
 * `'0.0.0.0'` 是非空字符串（真值），永远赢过真实的外网 IP，也永远显示不出 `-`。
 * 界面上就成了 448 张卡片齐刷刷写着 `root@0.0.0.0` —— 一个看着像地址、
 * 实际意思是"不知道"的东西，操作员按它去定位主机只会扑空。
 *
 * 在唯一入口处归一成空串，让回退链恢复本来的意思。两侧都要过一遍：
 * `blankIp(a) || blankIp(b)` 而不是 `blankIp(a || b)` —— 后者在 a 是 '0.0.0.0'
 * 时根本轮不到 b。
 */
const blankIp = (v: any): string => {
  const s = String(v ?? '').trim()
  return s === '0.0.0.0' || s === '::' || s === '::0' ? '' : s
}

function normAgent(p: any): Agent {
  return {
    a_id: p.a_id || p.id || '',
    a_name: p.a_name || p.name,
    a_listener: p.a_listener || p.listener,
    a_external_ip: blankIp(p.a_external_ip) || blankIp(p.external),
    a_internal_ip: blankIp(p.a_internal_ip) || blankIp(p.internal),
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
/**
 * 任务包是**增量**的，同一条任务会先后收到多个包：
 *   type=106 / CONSOLE_TASK_SYNC —— 带 a_cmdline + a_start_time + a_client，a_completed=false
 *   type=107 / CONSOLE_TASK_UPD / CONSOLE_OUT —— 只带 a_finish_time + a_message/a_text
 * 所以合并时**已知值优先**：新包里缺失（undefined/null/空串）的字段绝不覆盖已有的非空值。
 *
 * ★ a_completed 取**单调或**：断线重连会回放旧包，一个迟到的 106（completed=false）
 *   不该把已经完成的任务翻回未完成。
 */
function mergeTask(prev: Task, next: Task): Task {
  const out: any = { ...prev }
  for (const k of Object.keys(next) as (keyof Task)[]) {
    const v: any = (next as any)[k]
    if (v === undefined || v === null || v === '') continue
    if (k === 'a_completed') {
      out[k] = prev.a_completed === true || v === true
      continue
    }
    out[k] = v
  }
  return out as Task
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

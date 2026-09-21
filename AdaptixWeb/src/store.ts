import { create } from 'zustand'
import { ApiClient } from './api/http'
import { SyncSocket } from './api/ws'
import {
  Agent, AppLog, ChatMessage, ConsoleLine, DEFAULT_SUBSCRIPTIONS, DownloadItem,
  Listener, P, Packet, Pivot, Task,
} from './api/types'
import { clearSession, isAgentOnline, isLinuxAgent, loadSession, saveLoginPrefs, saveSession } from './utils'
import { emptySnapshot, GraphBoard, GraphSnapshot, isSnapshot, normSnapshot } from './graphBoard'

function uid() { return Math.random().toString(36).slice(2, 10) + Date.now().toString(36) }
function now() { return Date.now() }

/** 静态菜单页 + 动态工作页签（files:/procs:/pty:/vnc: 按主机停靠，非弹窗） */
export type StaticTabId =
  | 'dashboard' | 'sessions' | 'listeners' | 'tasks' | 'downloads' | 'logs'
  | 'generate' | 'delivery' | 'tunnels' | 'screens' | 'creds' | 'targets' | 'chat' | 'graph' | 'bofs'
  | 'argfuscator' | 'extkit' | 'relay'
export type TabId =
  | StaticTabId
  | `files:${string}` | `procs:${string}` | `pty:${string}` | `vnc:${string}`
  // 命令执行台（单机维度）。右键「执行命令」跳到这里 —— 弹窗挡着回显、留不下历史，
  // 命令执行是"敲一条看一眼"的事，得给它一个能常驻的页签。
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
export function isCmdTab(id: string | null | undefined) {
  return !!id && id.startsWith('cmd:')
}
export function isInteractTab(id: string | null | undefined) {
  return isFilesTab(id) || isProcsTab(id) || isPtyTab(id) || isVncTab(id)
}
export function tabAgentId(id: string | null | undefined): string | null {
  if (!id) return null
  if (isFilesTab(id) || isProcsTab(id) || isPtyTab(id) || isVncTab(id) || isCmdTab(id)) {
    return id.slice(id.indexOf(':') + 1) || null
  }
  return null
}
export function filesTabId(agentId: string): TabId { return `files:${agentId}` }
export function procsTabId(agentId: string): TabId { return `procs:${agentId}` }
export function ptyTabId(agentId: string): TabId { return `pty:${agentId}` }
export function vncTabId(agentId: string): TabId { return `vnc:${agentId}` }
export function cmdTabId(agentId: string): TabId { return `cmd:${agentId}` }

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
  /**
   * 服务端自报的非回环网卡地址，来自同步起始包（SyncPackerStart.interfaces，与 Qt 侧
   * ax.interfaces() 同源）。只用于给「回连地址」的缺省值做旁证 —— 见 lib/callbackHost.ts。
   */
  serverInterfaces: string[]
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
  const uiTabs = kept.length ? kept : (['dashboard'] as TabId[])
  const activeUiTab = uiTabs.includes(s.activeUiTab as TabId) ? s.activeUiTab : uiTabs[uiTabs.length - 1]

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

  return {
    agents,
    browserFiles,
    browserProcs,
    interactBoost,
    consoleLines: s.consoleLines.filter((l) => !ids.has(l.agentId)),
    openConsoleTabs,
    activeConsoleTab,
    uiTabs,
    activeUiTab: (activeUiTab || 'dashboard') as TabId,
    tab: (activeUiTab || 'dashboard') as TabId,
    selectedAgentId: s.selectedAgentId && ids.has(s.selectedAgentId) ? null : s.selectedAgentId,
    tasks: s.tasks.filter((t) => !ids.has(String((t as any)?.a_id || ''))),
    // 勾选集合跟着剪枝：留着已删主机的 id，下次批量删就会把它再发给服务端
    // ⇒ 服务端报「找不到」⇒ 整个请求 ok:false（见 removeAgents 的注释）。
    checkedIds: new Set([...s.checkedIds].filter((x) => !ids.has(x))),
  }
}

export const useStore = create<State>((set, get) => ({
  api: new ApiClient(''),
  socket: new SyncSocket(),
  connected: false,
  wsConnected: false,
  syncing: false,
  serverInterfaces: [],
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
  graph: emptySnapshot(),
  checkedIds: new Set<string>(),

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
    // linux_agent 无桌面捕获能力，VNC 面板是死胡同：直接拦住，不开页签
    if (isLinuxAgent(agent)) {
      get().pushToast('warn', 'Linux 主机不支持 VNC')
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
    // 校验 token：listAgents 内部自带一次 401→refresh 重试；
    // 失败即清会话，不再二次 refresh（避免打爆登录限流）
    try {
      await api.listAgents()
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
      // 换账号/换服务端连着进来时不能带着上一条连接的图渲染。
      graph: emptySnapshot(),
      openConsoleTabs: [],
      activeConsoleTab: null,
      selectedAgentId: null,
      checkedIds: new Set<string>(),
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
      serverInterfaces: [],
      username: '',
      agents: {},
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
        // 服务端在同步起始包里带上了自己的网卡地址（SyncPackerStart.interfaces，
        // 与 Qt 侧 ax.interfaces() 同源）。此前这个字段收到就丢。
        // Web 这边只拿它给「回连地址」的缺省值做旁证 —— 见 lib/callbackHost.ts。
        set({ syncing: true, serverInterfaces: Array.isArray(pkt.interfaces) ? pkt.interfaces : [] })
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
          return { agents: { ...s.agents, [a.a_id]: next } }
        })
        break
      }
      case P.AGENT_TICK: {
        // 服务端 SyncPackerAgentTick 把 id 列表序列化为数组放在 a_id（json:"a_id"）
        const ids: string[] = Array.isArray(pkt.a_id) ? pkt.a_id : (pkt.a_id ? [pkt.a_id] : [])
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
    set((s) => ({
      // REST is source of truth for agents/listeners lists
      agents: agentMap,
      listeners: listenerMap,
      tasks: (Array.isArray(tasks) ? tasks : []).map(normTask).slice(0, 500),
      downloads: dlMap,
      // 勾选集合跟 REST 名单求交：批量条上显示的必须是**真的还在**的主机数。
      // 少了一台没剪掉，操作员看到的「已选 50 台」里就有一台是幽灵。
      checkedIds: new Set([...s.checkedIds].filter((id) => !!agentMap[id])),
    }))
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

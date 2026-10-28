import { useEffect, useRef, useState } from 'react'
import { cmdTabId, useStore } from '../store'
import { copyText, isAgentOnline } from '../utils'
import { FALLBACK_BOFS, FALLBACK_COMMANDS, normType } from '../lib/completions'
import { useCompletion } from '../lib/useCompletion'
import { CmdSuggest } from './CmdSuggest'

function fmtClock(ts?: number): string {
  if (!ts) return ''
  const d = new Date(ts)
  const p = (n: number) => String(n).padStart(2, '0')
  return `[${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}] `
}

/**
 * 控制台**只做回显格式化，不做过滤**。
 *
 * ★ 这里原来还有一份按拼写丢弃的规则：
 *   `if (/^(ls|disks|cat|download|rm|cp|mv|mkdir|ps\b|pwd)\b/i.test(body)) return null`
 *   —— 而 `store.sendCommand` 推操作员回显用的正是 `kind:'local'`，于是
 *   **手敲 `ls`/`cat`/`ps`/`pwd`/`rm` 连自己那一行 `> ls` 都不显示**。
 *   已删除，不要再加回来：服务端（ts_agent.go:162 / mgr_task.go:78）已经按
 *   任务类型分流，前端不需要也不会正确地再判一次。详见 store.ts 顶部说明。
 */
function simplifyOutput(kind: string, text: string): string | null {
  const t = (text || '').trim()
  if (!t) return null
  if (kind === 'local') {
    return t.startsWith('>') ? t : `> ${t}`
  }
  return t
}

const QUICK_WIN = [
  // Extension-Kit SAL-BOF / PostEx-BOF：直接走 BOF，勿 shell 包装
  { label: 'whoami', cmd: 'whoami' },
  { label: 'pwd', cmd: 'pwd' },
  { label: 'ip配置', cmd: 'ipconfig' },
  { label: '截图', cmd: 'screenshot_bof' },
]

const QUICK_LINUX = [
  // linux_agent 内置命令。★ 原文写「无 BOF / 无截图」—— **已经不成立**：
  // linux_agent 现在有 BOF（服务端运行期把 .c 编成 .o，见 pl_bofs_api.go），
  // 而且它的命令集里确实没有 screenshot_bof（截图是 Windows 侧的 BOF）。
  { label: 'getuid', cmd: 'getuid' },
  { label: 'pwd', cmd: 'pwd' },
  { label: 'netstat', cmd: 'netstat' },
  { label: 'mounts', cmd: 'mounts' },
  { label: 'ps', cmd: 'ps' },
  { label: 'help', cmd: 'help' },
]

// Linux 上的 gopher：命令集来自它 ax_config.axs 的 commands_unix 组
// （cat/cd/cp/download/exit/job/kill/ls/mkdir/mv/ps/pwd/rm/run/screenshot/
//  shell/socks/upload/vnc/zip）—— **既不是** linux_agent 那套
// （没有 getuid/netstat/mounts），**也没有** `execute bof`（cmd_execute
// 只在 commands_win 里）。这四条的可用性已在 /api/axscript/commands 上实测。
const QUICK_GOPHER_UNIX = [
  { label: 'pwd', cmd: 'pwd' },
  { label: 'ls', cmd: 'ls' },
  { label: 'ps', cmd: 'ps' },
  { label: 'shell', cmd: 'shell' },
]

/**
 * 快捷按钮分组 —— 判据是 **agent 类型**（`a_name`），不是「主机是不是 Linux」。
 *
 * ★ 原来这里是 `isLinuxAgent(hintAgent)`，而它的定义是
 *   `a_os===2 || /^Linux/i.test(a_listener)`（utils.ts:82）—— 于是
 *   **Linux 上的 gopher 被当成 linux_agent**，拿到一份它根本没有的命令清单
 *   （getuid/netstat/mounts 在 gopher 的 unix 组里一个都没有），
 *   而 Windows 上的 gopher 反而拿不到它**确实支持**的 BOF 入口。
 *   两种情况正好判反，这就是缺陷 D 要区分的「同 OS 不同 agent」。
 *
 * 命令名归一复用 completions.ts 的 normType()，不另写一份。
 */
function quickGroupFor(a: { a_name?: string; a_os?: number } | null | undefined) {
  const t = normType(a?.a_name)
  if (t === 'linux') return QUICK_LINUX
  if (t === 'gopher' && (Number(a?.a_os) || 0) !== 1) return QUICK_GOPHER_UNIX
  return QUICK_WIN
}

export function Term() {
  const agents = useStore((s) => s.agents) || {}
  const lines = useStore((s) => s.consoleLines) || []
  const openTabs = useStore((s) => s.openConsoleTabs) || []
  const activeTab = useStore((s) => s.activeConsoleTab)
  const setActiveConsoleTab = useStore((s) => s.setActiveConsoleTab)
  const closeConsoleTab = useStore((s) => s.closeConsoleTab)
  const sendCommand = useStore((s) => s.sendCommand)
  const clearConsole = useStore((s) => s.clearConsole)
  const selectAgent = useStore((s) => s.selectAgent)
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)
  const openPty = useStore((s) => s.openPty)
  const openVnc = useStore((s) => s.openVnc)
  const openUiTab = useStore((s) => s.openUiTab)
  const pushToast = useStore((s) => s.pushToast)
  const selectedAgentId = useStore((s) => s.selectedAgentId)
  const tasks = useStore((s) => s.tasks) || []
  const uiTabs = useStore((s) => s.uiTabs) || []

  const [cmd, setCmd] = useState('')
  const [busy, setBusy] = useState(false)
  // 命令历史上行缓存：按主机隔离，关闭页签后随组件状态清理
  const [historyByAgent, setHistoryByAgent] = useState<Record<string, string[]>>({})
  const [histIdx, setHistIdx] = useState(-1)
  const bottomRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)
  const agentId = activeTab
  const agent = agentId ? agents[agentId] : null
  const live = agent ? isAgentOnline(agent) : false
  // 快捷键与提示文案按 **agent 类型** 判（原来用 isLinuxAgent，会把 Linux 上的
  // gopher 误判成 linux_agent —— 两种情况正好判反，见 quickGroupFor 的注释）。
  // 未开页签时用当前选中主机判断，保证提示文案可用。
  const hintAgent = agent || (selectedAgentId ? agents[selectedAgentId] : null)
  const hintType = normType(hintAgent?.a_name)
  const linux = hintType === 'linux'
  // 与 linux 同样只用于**文案**。命令集本身交给服务端那张表过滤（见下）。
  const quickAll = quickGroupFor(hintAgent)
  const history = agentId ? (historyByAgent[agentId] || []) : []

  // 补全按**页签绑定的这台主机**过滤（不是 selectedAgentId）：
  // 在这个控制台里敲的命令就是发给它的，候选自然该按它的 agent 类型 / os 出。
  const comp = useCompletion({
    value: cmd,
    agent,
    onAccept: setCmd,
    fallbackCommands: FALLBACK_COMMANDS,
    fallbackBofs: FALLBACK_BOFS,
  })

  /**
   * 快捷按钮 = 手抄分组 ∩ 服务端实际认得的命令。
   *
   * 手抄清单必然有点不动的按钮：实测 kharon 没有 `pwd`/`ls`/`ps`/`shell`
   * （它用 `fs`/`process`/`token`），Linux 上的 gopher 没有 `getuid`/`netstat`/
   * `mounts`。以前这些都照渲染，点下去才知道不行。
   * `help` 单独放行：它是核心内建（core/server/ts_axscript.go:285），
   * **不出现在任何 batch 里**，用表过滤会把它误杀。
   *
   * ★ 表还没到（或压根拿不到）时**不过滤** —— 否则命令表一失败，整条快捷栏
   *   就空了，而「空」和「这台主机没有可用命令」在界面上完全同形。
   *   宁可给一个可能失效的按钮（点下去控制台会明确报错），也不要一条都没有。
   */
  const quick = comp.availableCommands.size
    ? quickAll.filter((q) => q.cmd === 'help' || comp.availableCommands.has(q.cmd))
    : quickAll

  const agentTasks = agentId
    ? tasks.filter((t) => t.a_id === agentId).slice(0, 8)
    : []
  const runningTasks = agentTasks.filter((t) => !t.a_completed).length
  const latestTask = agentTasks[0]

  function openCommandExec() {
    if (!agentId) return
    let n = 0
    while (uiTabs.includes(cmdTabId(agentId, n))) n++
    openUiTab(cmdTabId(agentId, n))
  }

  const view = lines
    .filter((l) => l.agentId === agentId)
    .map((l) => {
      const text = simplifyOutput(l.kind, l.text)
      return text ? { ...l, text } : null
    })
    .filter(Boolean) as typeof lines

  useEffect(() => { bottomRef.current?.scrollIntoView({ behavior: 'smooth' }) }, [view.length, agentId])

  // 切换主机：清空输入框与历史索引，避免把 A 的命令/↑历史带到 B
  useEffect(() => {
    setCmd('')
    setHistIdx(-1)
    setBusy(false)
    if (agentId) inputRef.current?.focus()
  }, [agentId])

  // 页签被关掉的主机，同步丢掉其本地命令历史
  useEffect(() => {
    setHistoryByAgent((h) => {
      const next: Record<string, string[]> = {}
      for (const id of openTabs) {
        if (h[id]) next[id] = h[id]
      }
      return next
    })
  }, [openTabs.join('|')])

  async function run(line: string) {
    if (!agentId || !line.trim() || busy) return
    // 仅对没有 BOF/内置命令的常见系统工具补 shell；whoami/dir/ipconfig 已由 SAL-BOF 注册
    const finalCmd = /^(hostname|systeminfo)(\s|$)/i.test(line.trim()) && !line.trim().includes(' ')
      ? `shell ${line.trim()}`
      : line
    setCmd('')
    setHistoryByAgent((h) => {
      const prev = h[agentId] || []
      return {
        ...h,
        [agentId]: [finalCmd, ...prev.filter((x) => x !== finalCmd)].slice(0, 100),
      }
    })
    setHistIdx(-1)
    setBusy(true)
    try {
      selectAgent(agentId)
      await sendCommand(finalCmd)
    } finally {
      setBusy(false)
      // ★ 输入框不再随 busy 禁用（见下面 <input> 的注释），这里的 focus() 才真正生效；
      //   兜住"发完一条焦点被别处抢走"的情况。!agentId 时输入框是 disabled，跳过。
      if (agentId) inputRef.current?.focus()
    }
  }

  async function copyAll() {
    const text = view.map((l) => l.text).join('\n')
    const ok = await copyText(text)
    pushToast(ok ? 'ok' : 'err', ok ? '终端输出已复制' : '复制失败')
  }

  function onCloseTab(id: string) {
    // store.closeConsoleTab 会清 consoleLines；本地历史在 openTabs effect 里同步丢掉
    closeConsoleTab(id)
    setHistoryByAgent((h) => {
      const n = { ...h }
      delete n[id]
      return n
    })
  }

  return (
    <div className="term">
      <div className="term-tabs">
        {openTabs.length === 0 && <button className="t active">Terminal</button>}
        {openTabs.map((id) => {
          const a = agents[id]
          const title = a ? `${a.a_computer || id}` : id
          const on = a ? isAgentOnline(a) : false
          return (
            <button key={id} className={`t ${activeTab === id ? 'active' : ''}`}
              onClick={() => { setActiveConsoleTab(id); selectAgent(id) }} title={id}>
              <span className={`sdot ${on ? 'on' : 'off'}`} style={{ width: 6, height: 6, marginRight: 6 }} />
              {title}
              <span className="x" onClick={(e) => { e.stopPropagation(); onCloseTab(id) }}>×</span>
            </button>
          )
        })}
        <span style={{ flex: 1 }} />
        {agentId && (
          <div className="term-quick">
            <button className="ghost term-exec-link" disabled={busy} onClick={openCommandExec} title="打开任务式命令执行台">命令执行</button>
            {quick.map((q) => (
              <button key={q.label} className="ghost" disabled={busy || !live} onClick={() => run(q.cmd)}>{q.label}</button>
            ))}
            {/* BOF 入口按**能力**显示，不是按「是不是 Linux」。
                原来这里是 `linux &&`：于是 Windows 上的 gopher（**支持** BOF）
                看不到这个入口，Linux 上的 gopher（不支持）反而看得到 —— 正好判反。
                `bofCapable` 由服务端回答（/api/bofs/exec 的 capable），
                前端不复刻那份能力矩阵。拿不到就是 false：不给打不开的门。 */}
            {comp.bofCapable && (
              <button className="ghost" disabled={busy} onClick={() => openUiTab('bofs')} title="BOF 模块（execute bof 图形入口）">BOF</button>
            )}
            <button className="ghost" disabled={busy || !live} onClick={() => openPty(agentId)} title="交互 PTY">PTY</button>
            <button className="ghost" disabled={busy || !live} onClick={() => openFilesBrowser(agentId)} title="文件管理">文件</button>
            <button className="ghost" disabled={busy || !live} onClick={() => openProcsBrowser(agentId)} title="进程管理">进程</button>
            <button className="ghost" disabled={busy || !live} onClick={() => openVnc(agentId)} title="VNC">VNC</button>
          </div>
        )}
        <button className="ghost ico" style={{ color: '#94a3b8', border: 'none' }} disabled={!agentId || !view.length}
          title="复制输出" onClick={copyAll}>⧉</button>
        <button className="ghost ico" style={{ color: '#94a3b8', border: 'none' }} disabled={!agentId}
          title="清空 (Ctrl+L)" onClick={() => agentId && clearConsole(agentId)}>⌫</button>
      </div>
      <div className="term-context">
        {agentId ? (
          <>
            <div className="term-context-target">
              <span className={`sdot ${live ? 'on' : 'off'}`} />
              <strong>{agent?.a_computer || agentId}</strong>
              <span className="term-context-meta">{agent?.a_username || '未知用户'} · {agent?.a_name || 'agent'} · {agent?.a_internal_ip || agent?.a_external_ip || '无地址'}</span>
            </div>
            <div className="term-context-stats">
              <span className={runningTasks ? 'is-running' : ''}>{runningTasks ? `${runningTasks} 个任务执行中` : '无执行中任务'}</span>
              <span>{agentTasks.length} 条最近任务</span>
              {latestTask?.a_cmdline ? <span className="mono" title={latestTask.a_cmdline}>最近：{latestTask.a_cmdline}</span> : null}
            </div>
            <button className="ghost term-task-link" onClick={() => openUiTab('tasks')} title="查看任务中心">任务中心</button>
          </>
        ) : (
          <span className="term-context-empty">从主机列表选择一个目标，终端会保留每台主机自己的页签与历史。</span>
        )}
      </div>
      <div
        className="term-out"
        onClick={(e) => {
          // 仅在空白区域点击时聚焦输入框——点选/复制文本不抢焦点
          if (e.target === e.currentTarget) inputRef.current?.focus()
        }}
      >
        {!agentId && (
          <div className="muted">
            {/* 文案直接由上面那份**已过滤**的快捷组生成 —— 手抄一遍就会漂：
                原文写死了 getuid/netstat/mounts，而 Linux 上的 gopher 一个都没有。 */}
            选择主机后执行命令。快捷：{quick.map((q) => q.label).join(' · ') || '（该主机暂无快捷命令）'} · ↑↓ 历史
          </div>
        )}
        {agentId && view.length === 0 && (
          <div className="muted">
            <span className={`sdot ${live ? 'on' : 'off'}`} style={{ display: 'inline-block', width: 7, height: 7, borderRadius: '50%', marginRight: 8, verticalAlign: 'middle' }} />
            [{agent?.a_computer || agentId}] 就绪 · {agent?.a_username || '-'}@{agent?.a_internal_ip || '-'}
            {linux ? ' · beacon ≈30s' : (agent?.a_sleep != null ? ` · sleep ${agent.a_sleep}s` : '')}
            {!live ? ' · 离线（命令可排队，文件/进程不可用）' : ''}
          </div>
        )}
        {view.map((l) => (
          <div
            key={l.id}
            className={`ln ${l.kind}`}
            title="双击复制此行"
            onDoubleClick={async () => {
              const ok = await copyText(l.text)
              pushToast(ok ? 'ok' : 'err', ok ? '已复制该行' : '复制失败')
            }}
          >
            <span className="ln-time">{fmtClock((l as any).ts)}</span>
            {l.text}
          </div>
        ))}
        <div ref={bottomRef} />
      </div>
      {comp.open && (
        <CmdSuggest
          items={comp.items}
          index={comp.index}
          degraded={comp.degraded}
          notice={comp.notice}
          onPick={comp.pick}
          onHover={comp.hover}
          onRefresh={comp.refresh}
        />
      )}
      <div className="term-in">
        <span className="ps1">{agent?.a_username || 'op'}@{agent?.a_computer || 'host'}:~$</span>
        {/*
          ★ 只保留 !agentId（没选主机时确实不能敲）。**不能**再带 busy：
            disabled 的元素浏览器会立刻收回焦点，而 run() 要等 sendCommand 的
            往返，表现就是"执行一条之后必须再点一下输入框才能敲第二条"。
            连按回车不会重复下发 —— run() 开头就有 `if (... || busy) return`。
        */}
        <input
          ref={inputRef}
          disabled={!agentId}
          value={cmd}
          placeholder={agentId
            ? (live
              ? (linux ? '命令，如 getuid / ls / / netstat / ps / help' : '命令，如 whoami / ls C:\\ / screenshot_bof / ps list')
              : '主机离线，命令将排队')
            : '未选择主机'}
          onChange={(e) => setCmd(e.target.value)}
          onKeyDown={(e) => {
            // 补全先过一道：它只在 Tab、弹层已开时的 ↑/↓/Esc、以及**已高亮**时的
            // 回车上返回 true。返回 false 时下面这段照旧 —— 尤其弹层关着时的
            // ↑/↓ 是命令历史，绝不能被补全抢走（那等于把历史功能删了）。
            if (comp.onKeyDown(e)) return
            if (e.key === 'Enter') run(cmd)
            if (e.key === 'ArrowUp') {
              e.preventDefault()
              const next = Math.min(histIdx + 1, history.length - 1)
              if (next >= 0 && history[next]) { setHistIdx(next); setCmd(history[next]) }
            }
            if (e.key === 'ArrowDown') {
              e.preventDefault()
              const next = histIdx - 1
              if (next < 0) { setHistIdx(-1); setCmd('') }
              else { setHistIdx(next); setCmd(history[next] || '') }
            }
            if (e.key === 'l' && (e.ctrlKey || e.metaKey)) {
              e.preventDefault()
              if (agentId) clearConsole(agentId)
            }
          }}
        />
        <button className="primary" disabled={!agentId || busy || !cmd.trim()} onClick={() => run(cmd)}>
          {busy ? '…' : '执行'}
        </button>
      </div>
    </div>
  )
}

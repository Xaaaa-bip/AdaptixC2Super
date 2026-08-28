import { useEffect, useRef, useState } from 'react'
import { useStore } from '../store'
import { copyText, isAgentOnline } from '../utils'

function simplifyOutput(kind: string, text: string): string | null {
  const t = (text || '').trim()
  if (!t) return null
  // help / 命令目录：原样显示（含 Process-BOF 分组名，勿被下面过滤器误杀）
  if (/^\s*Group\s*-/im.test(t) || /^\s*Command\s+:/im.test(t) || (/\bSubCommand\b/i.test(t) && /\bDescription\b/i.test(t))) {
    return t
  }
  if (/^Task:\s*/i.test(t) && !/\n/.test(t)) return null
  if (/^Agent called server/i.test(t)) return null
  if (/^\[\*\]/i.test(t) && /sent \[/i.test(t)) return null
  if (/^Job \[[0-9a-f]+\] finished/i.test(t)) return null
  if (/^Job \[[0-9a-f]+\] output:\s*$/i.test(t)) return null
  if (/^Program .+ started with PID/i.test(t)) return null
  // 文件管理 / 进程管理：不在命令行显示
  if (/^Listing\s+'/i.test(t)) return null
  if (/^Process list\b/i.test(t) && !/\n/.test(t)) return null
  if (/file content:/i.test(t)) return null
  if (/directory is EMPTY/i.test(t)) return null
  if (/^Status:\s*OK\b/i.test(t)) return null
  if (/^Process \d+ killed/i.test(t)) return null
  if (/\bType\b.+\bSize\b.+\bName\b/i.test(t) && /\bdir\b/i.test(t)) return null
  if (/^\s*PID\b.+\bPPID\b/im.test(t) && /^\s*\d+\s+\d+/m.test(t)) return null
  if (kind === 'local') {
    const body = t.replace(/^>\s*/, '')
    // 多行本地输出（help 等）原样；单行浏览器命令回显隐藏
    if (/\n/.test(body)) return t
    if (/^(ls|disks|cat|download|rm|cp|mv|mkdir|ps\b)\b/i.test(body)) return null
    return t.startsWith('>') ? t : `> ${t}`
  }
  return t.replace(/^Job \[[0-9a-f]+\] output:\s*/i, '').trim() || null
}

const QUICK = [
  // Extension-Kit SAL-BOF / PostEx-BOF：直接走 BOF，勿 shell 包装
  { label: 'whoami', cmd: 'whoami' },
  { label: 'pwd', cmd: 'pwd' },
  { label: 'ip', cmd: 'ipconfig' },
  { label: '截图', cmd: 'screenshot_bof' },
]

export function Term({ active = true }: { active?: boolean }) {
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
  const pushToast = useStore((s) => s.pushToast)

  const [cmd, setCmd] = useState('')
  const [busy, setBusy] = useState(false)
  // 命令历史上行缓存：按主机隔离，关闭页签后随组件状态清理
  const [historyByAgent, setHistoryByAgent] = useState<Record<string, string[]>>({})
  const [histIdx, setHistIdx] = useState(-1)
  const bottomRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)
  const wasActiveRef = useRef(active)
  const viewLenRef = useRef(0)
  const agentId = activeTab
  const agent = agentId ? agents[agentId] : null
  const live = agent ? isAgentOnline(agent) : false
  const history = agentId ? (historyByAgent[agentId] || []) : []

  const view = lines
    .filter((l) => l.agentId === agentId)
    .map((l) => {
      const text = simplifyOutput(l.kind, l.text)
      return text ? { ...l, text } : null
    })
    .filter(Boolean) as typeof lines

  useEffect(() => {
    if (!active) {
      wasActiveRef.current = false
      viewLenRef.current = view.length
      return
    }
    const justShown = !wasActiveRef.current
    wasActiveRef.current = true
    const grew = view.length !== viewLenRef.current
    viewLenRef.current = view.length
    if (justShown || grew) {
      bottomRef.current?.scrollIntoView({ behavior: 'auto', block: 'end' })
    }
  }, [view.length, agentId, active])

  // 切换主机：清空输入框与历史索引，避免把 A 的命令/↑历史带到 B
  useEffect(() => {
    setCmd('')
    setHistIdx(-1)
    setBusy(false)
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
    try { selectAgent(agentId); await sendCommand(finalCmd) } finally { setBusy(false) }
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
    <div className={`term ${active ? '' : 'is-parked'}`} aria-hidden={!active}>
      <div className="term-tabs">
        <div className="term-tablist">
          {openTabs.length === 0 && <button className="t active">终端</button>}
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
        </div>
        {agentId && (
          <div className="term-quick">
            {QUICK.map((q) => (
              <button key={q.label} className="ghost" disabled={busy || !live} onClick={() => run(q.cmd)}>{q.label}</button>
            ))}
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
      <div className="term-out" onClick={() => inputRef.current?.focus()}>
        {!agentId && <div className="muted">选择主机后执行命令。快捷：whoami · pwd · ipconfig · screenshot_bof · ↑↓ 历史</div>}
        {agentId && view.length === 0 && (
          <div className="muted">
            <span className={`sdot ${live ? 'on' : 'off'}`} style={{ display: 'inline-block', width: 7, height: 7, borderRadius: '50%', marginRight: 8, verticalAlign: 'middle' }} />
            [{agent?.a_computer || agentId}] 就绪 · {agent?.a_username || '-'}@{agent?.a_internal_ip || '-'}
            {agent?.a_sleep != null ? ` · sleep ${agent.a_sleep}s` : ''}
            {!live ? ' · 离线（命令可排队，文件/进程不可用）' : ''}
          </div>
        )}
        {view.map((l) => <div key={l.id} className={`ln ${l.kind}`}>{l.text}</div>)}
        <div ref={bottomRef} />
      </div>
      <div className="term-in">
        <span className="ps1">{agent?.a_username || 'op'}@{agent?.a_computer || 'host'}:~$</span>
        <input
          ref={inputRef}
          disabled={!agentId || busy}
          value={cmd}
          placeholder={agentId ? (live ? '命令，如 whoami / ls C:\\ / screenshot_bof / ps list' : '主机离线，命令将排队') : '未选择主机'}
          onChange={(e) => setCmd(e.target.value)}
          onKeyDown={(e) => {
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

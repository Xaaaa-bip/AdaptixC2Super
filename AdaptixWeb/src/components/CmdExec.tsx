import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { copyText, fmtRelative, isAgentOnline, isLinuxAgent } from '../utils'
import { FALLBACK_BOFS, FALLBACK_COMMANDS } from '../lib/completions'
import { useCompletion } from '../lib/useCompletion'
import { CmdSuggest } from './CmdSuggest'

/**
 * 「命令执行」—— 一台主机的**全屏终端式**命令台（`cmd:<agentId>[#n]`）。
 *
 * ★ 长得像 PTY，但**不是 PTY**：底层走的是普通命令通道（store.sendCommand →
 *   /api/agent/command/raw），不是交互式 shell。区别很实在：
 *     · 命令通道：一条一条发，每条都是一个 Task，任何 agent 都支持；
 *     · 交互 PTY：在目标机上起真 shell（agent 侧要有 pty 能力），双向流。
 *   要交互 shell 用右键「交互 PTY」，那条路径没动。
 *
 * 为什么做成这个样子（用户要的 VShell 那种）：
 *   ① **全屏**：终端占满整个工作区，不是弹窗、也不是挤在主机表下面的一条；
 *   ② **底部提示符**：输入行钉在底部（像 shell 的 prompt），回显往上滚 ——
 *      输入框放在顶上时，回显一多就得往上翻才能看见刚敲的那条；
 *   ③ **可多开**：`cmd:<id>`、`cmd:<id>#1`、`cmd:<id>#2`… 一台主机能同时开几个
 *      （跟 shell / 跟日志 / 另一个窗口各一个），每个自己开关；
 *   ④ 终端配色：深色底 + 等宽字，与「命令终端」「交互 PTY」同一套观感。
 */
export function CmdExec({ agentId }: { agentId: string }) {
  const agents = useStore((s) => s.agents)
  const lines = useStore((s) => s.consoleLines) || []
  const sendCommand = useStore((s) => s.sendCommand)
  const selectAgent = useStore((s) => s.selectAgent)
  const clearConsole = useStore((s) => s.clearConsole)
  const pushToast = useStore((s) => s.pushToast)
  const tasks = useStore((s) => s.tasks) || []
  const openUiTab = useStore((s) => s.openUiTab)

  const agent = agents[agentId]
  const online = isAgentOnline(agent)
  const linux = isLinuxAgent(agent)

  const [cmd, setCmd] = useState('')
  const [hist, setHist] = useState<string[]>([])
  const [histIdx, setHistIdx] = useState(-1)
  const inputRef = useRef<HTMLInputElement>(null)
  const outRef = useRef<HTMLDivElement>(null)
  /**
   * 防重入用 ref 而**不是** state + `disabled`。
   *
   * ★ 原来这里是 `const [busy, setBusy] = useState(false)` 加 `disabled={busy}`，
   *   后果是**每发一条命令输入框就失焦一次**：disabled 的元素浏览器会立刻收回
   *   焦点，而 `finally` 里的 `inputRef.current?.focus()` 是在 React 把 disabled
   *   摘掉**之前**跑的 —— 对 disabled 元素调 focus() 是**静默无效**的。
   *   表现就是"执行一条之后必须再用鼠标点一下输入框才能打第二条"。
   *   改用 ref：输入框全程可用，敲完一条能接着敲下一条（命令通道本来就是
   *   一条一条排队发的），焦点也不会丢。
   */
  const inFlight = useRef(false)

  const comp = useCompletion({
    value: cmd,
    agent,
    onAccept: setCmd,
    fallbackCommands: FALLBACK_COMMANDS,
    fallbackBofs: FALLBACK_BOFS,
  })

  const mine = useMemo(() => lines.filter((l) => l.agentId === agentId), [lines, agentId])
  const mineTasks = useMemo(
    () => tasks.filter((t) => t.a_id === agentId).slice(0, 10),
    [tasks, agentId],
  )
  const runningTasks = mineTasks.filter((t) => !t.a_completed).length
  const latestTask = mineTasks[0]
  const quick = useMemo(() => {
    const preferred = linux
      ? ['getuid', 'pwd', 'ls', 'ps', 'netstat', 'help', 'shell']
      : ['whoami', 'pwd', 'ps', 'ipconfig', 'hostname', 'help', 'shell']
    if (!comp.availableCommands.size) return preferred
    const available = comp.availableCommands
    return preferred.filter((name) => name === 'help' || available.has(name))
  }, [comp.availableCommands, linux])

  // 新回显自动滚到底 —— 终端不跟着滚，就得手动往下拖才能看到刚跑那条的回报
  useEffect(() => {
    const el = outRef.current
    if (el) el.scrollTop = el.scrollHeight
  }, [mine.length])

  useEffect(() => { inputRef.current?.focus() }, [agentId])

  async function submitLine(raw: string) {
    const c = raw.trim()
    if (!c) return
    if (inFlight.current) return      // 上一条还在发（连按回车不会重复下发）
    if (!online) { pushToast('warn', '主机离线，命令无法执行'); return }
    setHist((h) => (h[h.length - 1] === c ? h : [...h, c]).slice(-200))
    setHistIdx(-1)
    inFlight.current = true
    // 先把这条回显出来（本地行），再发出去 —— 否则敲完到有回显之间是空白的
    try {
      selectAgent(agentId)
      await sendCommand(c)
      setCmd('')          // 成功才清空；失败保留原文方便改
    } catch (e: any) {
      pushToast('err', e?.message || String(e))
    } finally {
      inFlight.current = false
      // 输入框全程没有 disabled，这里的 focus() 是真正生效的
      //（也兜住"发送失败后光标被别处抢走"的情况）。
      inputRef.current?.focus()
    }
  }

  async function submit() {
    await submitLine(cmd)
  }

  function onKeyDown(e: React.KeyboardEvent<HTMLInputElement>) {
    if (comp.onKeyDown(e)) return
    if (e.key === 'Enter') { e.preventDefault(); void submit(); return }
    // ↑/↓ 翻本地历史（补全弹层开着时上面那行已经消费掉了）
    if (e.key === 'ArrowUp' && hist.length) {
      e.preventDefault()
      const i = histIdx < 0 ? hist.length - 1 : Math.max(0, histIdx - 1)
      setHistIdx(i); setCmd(hist[i])
    } else if (e.key === 'ArrowDown' && histIdx >= 0) {
      e.preventDefault()
      const i = histIdx + 1
      if (i >= hist.length) { setHistIdx(-1); setCmd('') } else { setHistIdx(i); setCmd(hist[i]) }
    }
  }

  async function copyAll() {
    const ok = await copyText(mine.map((l) => l.text).join('\n'))
    pushToast(ok ? 'ok' : 'err', ok ? '回显已复制' : '复制失败')
  }

  function taskLabel() {
    if (!latestTask) return '尚无任务'
    if (!latestTask.a_completed) return '执行中'
    return '已完成'
  }

  return (
    <div className="cmd-exec">
      <div className="cmd-exec-hd">
        <span className="cmd-exec-prompt">❯</span>
        <span className="cmd-exec-title">命令执行</span>
        <span className="cmd-exec-mode">RAW COMMAND</span>
        <span className="dim mono" style={{ fontSize: 12 }}>
          {String(agent?.a_computer || agentId)}
          {agent?.a_username ? ` · ${agent.a_username}` : ''}
        </span>
        <span className={`sdot ${online ? 'on' : 'off'}`} title={online ? '在线' : '离线'} />
        <span className="dim" style={{ fontSize: 12 }}>{online ? '在线' : '离线（命令可排队）'}</span>
        <span className="cmd-exec-host-meta mono">{agent?.a_name || 'agent'} · {agent?.a_internal_ip || agent?.a_external_ip || '无地址'}</span>
        <span style={{ flex: 1 }} />
        <button className="ghost" onClick={() => openUiTab('tasks')} title="查看这台主机的任务">任务 {mineTasks.length}</button>
        <button className="ghost" onClick={() => void copyAll()} disabled={!mine.length}>复制回显</button>
        <button className="ghost" onClick={() => clearConsole(agentId)} disabled={!mine.length}>清空回显</button>
      </div>

      <div className="cmd-exec-workbar">
        <div className="cmd-exec-state">
          <span className={`cmd-state-dot ${runningTasks ? 'running' : latestTask ? 'done' : ''}`} />
          <span className="cmd-state-main">{taskLabel()}</span>
          <span className="cmd-state-detail">
            {latestTask?.a_cmdline ? latestTask.a_cmdline : '每条命令都会形成独立任务并回流到任务中心'}
          </span>
        </div>
        <div className="cmd-exec-quick" aria-label="常用命令">
          {quick.slice(0, 6).map((name) => (
            <button key={name} className="cmd-chip" disabled={!online || inFlight.current} onClick={() => { setCmd(name); void submitLine(name) }}>
              {name}
            </button>
          ))}
        </div>
      </div>

      {/* 终端主体：回显在上、提示符钉在底部（shell 的读法） */}
      <div className="cmd-exec-workspace">
        <div className="cmd-exec-body" ref={outRef} onClick={() => inputRef.current?.focus()}>
          {mine.length === 0 ? (
            <div className="cmd-exec-tip">
              {online
                ? `输入命令开始执行 · ${linux ? 'linux_agent' : '当前 agent'} · ↑↓ 历史 · Tab 补全`
                : '主机离线；恢复在线后再发送命令'}
            </div>
          ) : (
            mine.map((l) => (
              <div key={l.id} className={`ln ${l.kind}`} title="双击复制此行" onDoubleClick={() => void copyText(l.text)}>
                <span className="cmd-line-time">{new Date(l.ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}</span>
                {l.text}
              </div>
            ))
          )}
        </div>
        <aside className="cmd-exec-history">
          <div className="cmd-side-hd">
            <span>最近命令</span>
            <button className="ghost" disabled={!hist.length} onClick={() => setHist([])}>清除</button>
          </div>
          {hist.length === 0 ? <div className="cmd-side-empty">提交过的命令会出现在这里，可点击重放。</div> : (
            <div className="cmd-side-list">
              {[...hist].reverse().slice(0, 12).map((item) => (
                <button key={item} className="cmd-history-item" disabled={!online} onClick={() => void submitLine(item)} title={`重放 ${item}`}>
                  <span className="mono">{item}</span><span>↗</span>
                </button>
              ))}
            </div>
          )}
          <div className="cmd-side-foot">
            <span>{mine.length} 条回显</span>
            {latestTask?.a_start_time ? <span>最近 {fmtRelative(latestTask.a_start_time)}</span> : null}
          </div>
        </aside>
      </div>

      <div className="cmd-exec-in">
        <span className="cmd-exec-caret">❯</span>
        <input
          ref={inputRef}
          className="mono"
          value={cmd}
          placeholder={linux ? 'getuid / shell id …' : 'whoami /all …'}
          onChange={(e) => { setCmd(e.target.value); setHistIdx(-1) }}
          onKeyDown={onKeyDown}
          spellCheck={false}
          autoComplete="off"
        />
        <span className="cmd-exec-shortcuts">Enter 执行 · ↑↓ 历史 · Tab 补全</span>
        <button className="primary cmd-exec-submit" disabled={!cmd.trim() || !online} onClick={() => void submit()}>
          执行
        </button>
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
      </div>
    </div>
  )
}

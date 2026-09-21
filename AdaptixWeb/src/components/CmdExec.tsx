import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { copyText, isAgentOnline, isLinuxAgent } from '../utils'
import { FALLBACK_BOFS, FALLBACK_COMMANDS } from '../lib/completions'
import { useCompletion } from '../lib/useCompletion'
import { CmdSuggest } from './CmdSuggest'
import { EmptyState } from './ui'

/**
 * 「命令执行」—— 一台主机的独立命令台页签（`cmd:<agentId>`）。
 *
 * 为什么要有这一页（而不是继续用弹窗）：
 *   主机页底部的常驻终端按需求去掉了，命令执行改从右键进。但**弹窗只够跑一条命令** ——
 *   看不到输出、翻不到历史、也留不下上下文。命令执行本来是一件"要盯着回显连着敲"
 *   的事，弹窗恰恰挡着回显。所以给它一个**独立页签**：上面输入、下面输出，
 *   和搜索/文件/进程那些页签一个层级，可以一边开着一边切去别的页看。
 *
 * 与「命令终端」页签（Term）的分工：
 *   Term 是**整机维度**的控制台（多台主机各自的页签 + 服务端回显流水），
 *   本页是**单机维度**的执行台（大输入框 + 补全 + 本地历史 + 只看这台的回显）。
 *   两者读的是同一份 `consoleLines`，所以输出不会分叉。
 */
export function CmdExec({ agentId }: { agentId: string }) {
  const agents = useStore((s) => s.agents)
  const lines = useStore((s) => s.consoleLines) || []
  const sendCommand = useStore((s) => s.sendCommand)
  const selectAgent = useStore((s) => s.selectAgent)
  const clearConsole = useStore((s) => s.clearConsole)
  const pushToast = useStore((s) => s.pushToast)

  const agent = agents[agentId]
  const online = isAgentOnline(agent)
  const linux = isLinuxAgent(agent)

  const [cmd, setCmd] = useState('')
  const [hist, setHist] = useState<string[]>([])
  const [histIdx, setHistIdx] = useState(-1)
  const [busy, setBusy] = useState(false)
  const inputRef = useRef<HTMLInputElement>(null)
  const outRef = useRef<HTMLDivElement>(null)

  const comp = useCompletion({
    value: cmd,
    agent,
    onAccept: setCmd,
    fallbackCommands: FALLBACK_COMMANDS,
    fallbackBofs: FALLBACK_BOFS,
  })

  const mine = useMemo(() => lines.filter((l) => l.agentId === agentId), [lines, agentId])

  // 新输出自动滚到底 —— 命令台不跟着滚，操作员就得手动往下拖才能看到刚跑的那条的回报
  useEffect(() => {
    const el = outRef.current
    if (el) el.scrollTop = el.scrollHeight
  }, [mine.length])

  useEffect(() => { inputRef.current?.focus() }, [agentId])

  async function submit() {
    const c = cmd.trim()
    if (!c) return
    if (!online) { pushToast('warn', '主机离线，命令无法执行'); return }
    setHist((h) => (h[h.length - 1] === c ? h : [...h, c]).slice(-200))
    setHistIdx(-1)
    setBusy(true)
    try {
      selectAgent(agentId)
      await sendCommand(c)
      setCmd('')           // 成功才清空；失败保留原文方便改
    } catch (e: any) {
      pushToast('err', e?.message || String(e))
    } finally {
      setBusy(false)
      inputRef.current?.focus()
    }
  }

  function onKeyDown(e: React.KeyboardEvent<HTMLInputElement>) {
    if (comp.onKeyDown(e)) return
    if (e.key === 'Enter') { e.preventDefault(); void submit(); return }
    // 历史：↑/↓ 在补全弹层没开时才走本地历史（弹层开着时上面那行已经消费掉了）
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
    pushToast(ok ? 'ok' : 'err', ok ? '输出已复制' : '复制失败')
  }

  return (
    <div className="page cmd-exec">
      <div className="toolbar">
        <h2>命令执行</h2>
        <span className="dim mono" style={{ fontSize: 12 }}>
          {String(agent?.a_computer || agentId)}
          {agent?.a_username ? ` · ${agent.a_username}` : ''}
          {agent?.a_process ? ` · ${agent.a_process}` : ''}
        </span>
        <span className={`sdot ${online ? 'on' : 'off'}`} title={online ? '在线' : '离线'} style={{ marginLeft: 8 }} />
        <span className="dim" style={{ fontSize: 12 }}>{online ? '在线' : '离线（命令可排队）'}</span>
        <span style={{ flex: 1 }} />
        <button className="ghost" onClick={() => void copyAll()} disabled={!mine.length}>复制输出</button>
        <button className="ghost" onClick={() => clearConsole(agentId)} disabled={!mine.length}>清除输出</button>
      </div>

      {/* 输入在上、输出在下：命令台是"敲一条看一眼"的节奏，输入框被输出顶下去就没法用 */}
      <div className="cmd-exec-in">
        <input
          ref={inputRef}
          className="mono"
          value={cmd}
          placeholder={linux ? '例如: getuid / shell id' : '例如: whoami /all'}
          onChange={(e) => { setCmd(e.target.value); setHistIdx(-1) }}
          onKeyDown={onKeyDown}
          disabled={busy}
        />
        <button className="primary" onClick={() => void submit()} disabled={busy || !cmd.trim()}>
          {busy ? '…' : '执行'}
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

      <div className="term-out cmd-exec-out" ref={outRef}>
        {mine.length === 0 ? (
          <EmptyState
            icon="❯"
            title="还没有输出"
            hint={online ? '在上面敲一条命令，回显会出现在这里' : '主机离线；命令可排队，上线后自动下发'}
          />
        ) : (
          mine.map((l) => (
            // 与 Term 同款：kind 直接当类名（local/info/ok/err/task），上色规则在 CSS 里
            <div key={l.id} className={`ln ${l.kind}`}>{l.text}</div>
          ))
        )}
      </div>
    </div>
  )
}

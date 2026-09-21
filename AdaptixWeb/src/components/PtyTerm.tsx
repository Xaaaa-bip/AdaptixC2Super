import { useEffect, useRef, useState } from 'react'
import { useStore } from '../store'
import { isAgentOnline } from '../utils'
import { ensureXterm } from '../lib/xtermLoader'
import { openTerminalChannel, randomTerminalId } from '../api/channel'
import { PageTips } from './PageTips'

function isNixAgent(agent: any): boolean {
  const os = String(agent?.a_os_desc || agent?.a_os || '').toLowerCase()
  return /linux|unix|darwin|mac|freebsd|bsd/.test(os)
}

function defaultProgram(agent: any): string {
  return isNixAgent(agent) ? '/bin/bash' : 'C:\\Windows\\System32\\cmd.exe'
}

const PRESETS_WIN = [
  { label: 'Cmd', path: 'C:\\Windows\\System32\\cmd.exe' },
  { label: 'PowerShell', path: 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe' },
]
const PRESETS_NIX = [
  { label: 'bash', path: '/bin/bash' },
  { label: 'sh', path: '/bin/sh' },
]

export function PtyTerm({ agentId, active = true }: { agentId: string; active?: boolean }) {
  const api = useStore((s) => s.api)
  const agents = useStore((s) => s.agents) || {}
  const closeUiTab = useStore((s) => s.closeUiTab)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  const acquireInteractBoost = useStore((s) => s.acquireInteractBoost)
  const agent = agents[agentId]
  const live = !!(agent && isAgentOnline(agent))
  const isWin = !isNixAgent(agent)
  // Windows interactive shell text is usually OEM code page; prefer oem then acp
  const oemCp = Number(agent?.a_oemcp || agent?.OemCP || agent?.a_acp || agent?.ACP || (isWin ? 936 : 0)) || 0

  const presets = isWin ? PRESETS_WIN : PRESETS_NIX
  const [program, setProgram] = useState(() => defaultProgram(agent))
  const [status, setStatus] = useState<'idle' | 'connecting' | 'running' | 'stopped' | 'error'>('idle')
  const [err, setErr] = useState('')
  const [geom, setGeom] = useState({ cols: 80, rows: 24 })
  const hostRef = useRef<HTMLDivElement>(null)
  const termRef = useRef<any>(null)
  const fitRef = useRef<any>(null)
  const wsRef = useRef<WebSocket | null>(null)
  const termIdRef = useRef('')
  const disposed = useRef(false)
  const dataDispRef = useRef<any>(null)
  const activeRef = useRef(active)

  useEffect(() => {
    activeRef.current = active
    if (active) {
      // re-fit when becoming visible again (display:none → block)
      try {
        fitRef.current?.fit()
        if (termRef.current) {
          setGeom({ cols: termRef.current.cols || 80, rows: termRef.current.rows || 24 })
          if (status === 'running') termRef.current.focus()
        }
      } catch { /* ignore */ }
    }
  }, [active, status])

  useEffect(() => {
    setProgram(defaultProgram(agent))
  }, [agentId])

  // Mount xterm once per agent tab; keep alive across page switches (parent keeps us mounted).
  useEffect(() => {
    disposed.current = false
    let ro: ResizeObserver | null = null
    ;(async () => {
      try { await acquireInteractBoost(agentId) } catch { /* store toast */ }
      try {
        const { Terminal, FitAddon } = await ensureXterm()
        if (disposed.current || !hostRef.current) return
        const term = new Terminal({
          cursorBlink: true,
          fontSize: 13,
          fontFamily: 'Consolas, "Cascadia Mono", "SF Mono", monospace',
          theme: {
            background: '#141414',
            foreground: '#ecece8',
            cursor: '#f2f2f0',
            selectionBackground: 'rgba(242,242,240,.22)',
          },
          convertEol: true,
          scrollback: 5000,
          allowProposedApi: true,
        })
        const fit = new FitAddon()
        term.loadAddon(fit)
        term.open(hostRef.current)
        fit.fit()
        termRef.current = term
        fitRef.current = fit
        setGeom({ cols: term.cols || 80, rows: term.rows || 24 })
        term.writeln('\x1b[90m# 交互 PTY · 选择程序后点「启动」\x1b[0m')
        term.writeln(`\x1b[90m# host=${agent?.a_computer || agentId} · ${live ? '在线' : '离线'} · oemcp=${oemCp}\x1b[0m`)
        term.writeln('\x1b[90m# 切换其它页签会保持会话；关闭 PTY 页签才会断开\x1b[0m')
        ro = new ResizeObserver(() => {
          if (!activeRef.current) return
          try {
            fit.fit()
            if (termRef.current) setGeom({ cols: termRef.current.cols || 80, rows: termRef.current.rows || 24 })
          } catch { /* ignore */ }
        })
        ro.observe(hostRef.current)
      } catch (e: any) {
        setErr(e?.message || String(e))
        setStatus('error')
      }
    })()
    return () => {
      disposed.current = true
      ro?.disconnect()
      try { dataDispRef.current?.dispose?.() } catch { /* ignore */ }
      dataDispRef.current = null
      try { wsRef.current?.close() } catch { /* ignore */ }
      wsRef.current = null
      try { termRef.current?.dispose() } catch { /* ignore */ }
      termRef.current = null
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [agentId])

  async function start() {
    if (!live) {
      pushToast('warn', '主机离线，无法启动 PTY')
      return
    }
    if (!api) {
      pushToast('err', '未连接 API')
      return
    }
    if (!program.trim()) {
      pushToast('warn', '请填写程序路径')
      return
    }
    const term = termRef.current
    const fit = fitRef.current
    if (!term) {
      pushToast('err', '终端组件未就绪（xterm 可能被 CDN 拦截）')
      return
    }
    stop(false)
    setStatus('connecting')
    setErr('')
    try { await acquireInteractBoost(agentId) } catch { /* */ }
    try { fit?.fit() } catch { /* ignore */ }
    const terminalId = randomTerminalId()
    termIdRef.current = terminalId
    const size_w = Math.max(20, term.cols || 80)
    const size_h = Math.max(10, term.rows || 24)
    setGeom({ cols: size_w, rows: size_h })
    term.reset()
    term.writeln(`\x1b[36m[*] connecting PTY terminal_id=${terminalId}\x1b[0m`)
    term.writeln(`\x1b[90m    program=${program}\x1b[0m`)
    term.writeln(`\x1b[90m    size=${size_w}x${size_h} oem_cp=${oemCp}\x1b[0m`)
    term.writeln('\x1b[90m    正在申请 OTP 并建立 WebSocket…\x1b[0m')
    try {
      const ws = await openTerminalChannel(api, {
        agent_id: agentId,
        terminal_id: terminalId,
        program: program.trim(),
        size_h,
        size_w,
        oem_cp: oemCp,
      })
      if (disposed.current) { ws.close(); return }
      wsRef.current = ws
      setStatus('running')
      // WS open only means channel registered; shell starts after next agent check-in.
      term.writeln('\x1b[32m[+] WebSocket 已建立，已下发 SHELL_START\x1b[0m')
      term.writeln('\x1b[90m    等待 agent 拉任务并 Resume（建议 sleep 0；有回显即可输入）\x1b[0m')
      pushLog('ok', `PTY 通道已建立 ${agentId} ${program}`, false)
      pushToast('ok', 'PTY 通道已建立，等待 agent 回显')

      ws.onmessage = (ev) => {
        if (!termRef.current) return
        try {
          if (typeof ev.data === 'string') termRef.current.write(ev.data)
          else if (ev.data instanceof ArrayBuffer) termRef.current.write(new Uint8Array(ev.data))
          else if (ev.data instanceof Blob) {
            ev.data.arrayBuffer().then((buf) => termRef.current?.write(new Uint8Array(buf)))
          }
        } catch { /* ignore */ }
      }
      ws.onclose = () => {
        if (wsRef.current === ws) {
          wsRef.current = null
          setStatus((s) => (s === 'connecting' ? 'error' : 'stopped'))
          termRef.current?.writeln('\r\n\x1b[90m# session closed\x1b[0m')
        }
      }
      ws.onerror = () => setErr('WebSocket 错误')

      try { dataDispRef.current?.dispose?.() } catch { /* ignore */ }
      dataDispRef.current = term.onData((data: string) => {
        const sock = wsRef.current
        if (!sock || sock.readyState !== WebSocket.OPEN) return
        // send as binary for agent shell write path
        sock.send(new TextEncoder().encode(data))
      })
      term.focus()
    } catch (e: any) {
      const msg = e?.message || String(e)
      setErr(msg)
      setStatus('error')
      term.writeln(`\x1b[31m[!] ${msg}\x1b[0m`)
      pushToast('err', msg)
    }
  }

  function stop(notify = true) {
    const ws = wsRef.current
    wsRef.current = null
    try { dataDispRef.current?.dispose?.() } catch { /* ignore */ }
    dataDispRef.current = null
    if (ws) {
      try { ws.close() } catch { /* ignore */ }
    }
    setStatus((s) => (s === 'running' || s === 'connecting' ? 'stopped' : s))
    if (notify) pushToast('info', 'PTY 已停止')
  }

  return (
    <div className="pty-page" style={{ display: active ? 'flex' : 'none' }} data-active={active ? '1' : '0'}>
      <div className="toolbar">
        <strong>交互 PTY</strong>
        <span className="mono dim">{agent?.a_computer || agentId}</span>
        <span className={`tag ${live ? 'online' : 'offline'}`}>{live ? '在线' : '离线'}</span>
        <span className={`tag ${status === 'running' ? 'listen' : status === 'error' ? 'offline' : 'warn'}`}>{status}</span>
        <div className="sep" />
        <select
          disabled={status === 'running' || status === 'connecting'}
          value={presets.some((p) => p.path === program) ? program : '__custom__'}
          onChange={(e) => {
            if (e.target.value === '__custom__') return
            setProgram(e.target.value)
          }}
          style={{ width: 140 }}
        >
          {presets.map((p) => <option key={p.path} value={p.path}>{p.label}</option>)}
          <option value="__custom__">自定义</option>
        </select>
        <input
          className="mono"
          style={{ minWidth: 260, flex: 1 }}
          disabled={status === 'running' || status === 'connecting'}
          value={program}
          onChange={(e) => setProgram(e.target.value)}
          placeholder="程序路径"
        />
        <button className="primary" disabled={!live || status === 'connecting' || status === 'running'} onClick={start}>
          {status === 'connecting' ? '连接中…' : '启动'}
        </button>
        <button disabled={status !== 'running' && status !== 'connecting'} onClick={() => stop()}>停止</button>
        <button className="ghost" onClick={() => {
          try {
            fitRef.current?.fit()
            termRef.current?.focus()
            if (termRef.current) setGeom({ cols: termRef.current.cols || 80, rows: termRef.current.rows || 24 })
          } catch { /* */ }
        }}>适配</button>
        <button className="ghost" onClick={() => closeUiTab(`pty:${agentId}` as any)}>关闭</button>
        <PageTips id="pty">
          <p>交互式 PTY 终端：预设 bash/sh 或自定义程序。</p>
          <p>WebSocket 建立后需等待 agent 下一次回连完成通道握手。</p>
          <p>关闭页签即结束会话；页签切换会保持会话存活。</p>
        </PageTips>
      </div>
      {err && <div className="banner warn">{err}</div>}
      <div className="pty-host" ref={hostRef} />
      <div className="pty-foot dim">
        binary WS · channel_terminal · id={termIdRef.current || '-'} · {geom.cols}x{geom.rows}
        {' '}· oem_cp={oemCp}
        {' '}· 切页保持会话 · 关页签才断开
      </div>
    </div>
  )
}

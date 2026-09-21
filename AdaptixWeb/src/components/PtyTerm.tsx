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
  const [line, setLine] = useState('')
  const hostRef = useRef<HTMLDivElement>(null)
  const termRef = useRef<any>(null)
  const fitRef = useRef<any>(null)
  const wsRef = useRef<WebSocket | null>(null)
  const cmdRef = useRef<HTMLInputElement>(null)
  const termIdRef = useRef('')
  const disposed = useRef(false)
  const dataDispRef = useRef<any>(null)
  const activeRef = useRef(active)

  function focusTerm() {
    try { termRef.current?.focus() } catch { /* ignore */ }
  }

  function focusCmd() {
    window.requestAnimationFrame(() => cmdRef.current?.focus())
  }

  function safeFit() {
    const el = hostRef.current
    if (!el || el.clientWidth < 40 || el.clientHeight < 20) return
    try {
      fitRef.current?.fit()
      if (termRef.current) {
        setGeom({ cols: termRef.current.cols || 80, rows: termRef.current.rows || 24 })
      }
    } catch { /* ignore */ }
  }

  function sendPty(data: string) {
    const sock = wsRef.current
    if (!sock || sock.readyState !== WebSocket.OPEN || data == null || data === '') return false
    try {
      // Binary frame, same as Qt. End with \n so server can expand to \r\n for oem_cp.
      sock.send(new TextEncoder().encode(data))
      return true
    } catch {
      try { sock.send(data); return true } catch { return false }
    }
  }

  function writeRemote(ev: MessageEvent) {
    const term = termRef.current
    if (!term) return
    try {
      if (typeof ev.data === 'string') term.write(ev.data)
      else if (ev.data instanceof ArrayBuffer) term.write(new Uint8Array(ev.data))
      else if (ev.data instanceof Blob) {
        ev.data.arrayBuffer().then((buf) => termRef.current?.write(new Uint8Array(buf)))
      }
    } catch { /* ignore */ }
  }

  function submitLine() {
    if (status !== 'running') return
    const raw = line
    setLine('')
    // Always terminate with \n. Windows oem_cp path turns \n into \r\n.
    // A lone \r is ignored by cmd.exe when stdin is a pipe — no echo, no run.
    const payload = `${raw}\n`
    const ok = sendPty(payload)
    try {
      if (raw) termRef.current?.writeln(`\x1b[90m→ ${raw}\x1b[0m`)
      if (ok) termRef.current?.writeln('\x1b[90m  已发送，等主机回连出结果\x1b[0m')
      else termRef.current?.writeln('\x1b[31m  发送失败：通道未开\x1b[0m')
    } catch { /* ignore */ }
    if (!ok) pushToast('warn', 'PTY 通道未就绪')
    focusCmd()
  }

  useEffect(() => {
    activeRef.current = active
    if (!active) return
    const t = window.setTimeout(() => {
      safeFit()
      if (status === 'running') focusCmd()
    }, 40)
    return () => window.clearTimeout(t)
  }, [active])

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
        try { term.options.disableStdin = false } catch { /* ignore */ }
        termRef.current = term
        fitRef.current = fit
        safeFit()
        dataDispRef.current = term.onData((data: string) => sendPty(data))
        const proc = String(agent?.a_process || '')
        term.writeln('\x1b[90m# 交互 PTY · 选择程序后点「启动」\x1b[0m')
        term.writeln(`\x1b[90m# host=${agent?.a_computer || agentId} · ${live ? '在线' : '离线'} · oemcp=${oemCp}\x1b[0m`)
        if (proc) term.writeln(`\x1b[90m# process=${proc}\x1b[0m`)
        term.writeln('\x1b[90m# 切换其它页签会保持会话；关闭 PTY 页签才会断开\x1b[0m')
        ro = new ResizeObserver(() => {
          if (!activeRef.current) return
          safeFit()
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
    if (!term) {
      pushToast('err', '终端组件未就绪（xterm 可能被 CDN 拦截）')
      return
    }
    stop(false)
    setStatus('connecting')
    setErr('')
    try { await acquireInteractBoost(agentId) } catch { /* */ }
    safeFit()
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
      term.writeln('\x1b[32m[+] WebSocket 已建立，已下发 SHELL_START\x1b[0m')
      term.writeln('\x1b[90m    等下面出现 D:\\... 提示符再输入；sleep 大则慢\x1b[0m')
      pushLog('ok', `PTY 通道已建立 ${agentId} ${program}`, false)
      pushToast('ok', 'PTY 通道已建立，等待 agent 回显')

      const drain = (ws as any).__drain as ((h: (ev: MessageEvent) => void) => void) | undefined
      if (drain) drain(writeRemote)
      else ws.onmessage = writeRemote
      ws.onclose = () => {
        if (wsRef.current === ws) {
          wsRef.current = null
          setStatus((s) => (s === 'connecting' ? 'error' : 'stopped'))
          termRef.current?.writeln('\r\n\x1b[90m# session closed\x1b[0m')
        }
      }
      ws.onerror = () => setErr('WebSocket 错误')

      if (!dataDispRef.current) {
        dataDispRef.current = term.onData((data: string) => sendPty(data))
      }
      window.setTimeout(() => focusCmd(), 50)
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
        {agent?.a_process ? <span className="mono dim" title={agent.a_process}>{agent.a_process}</span> : null}
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
        {status === 'running' || status === 'connecting' ? (
          <span className="mono dim pty-prog" title={program}>{program}</span>
        ) : (
          <input
            className="mono"
            style={{ minWidth: 260, flex: 1 }}
            value={program}
            onChange={(e) => setProgram(e.target.value)}
            placeholder="程序路径"
          />
        )}
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
      <div
        className="pty-host"
        ref={hostRef}
        onMouseDown={() => focusCmd()}
      />
      <div className="pty-in">
        <span className="ps1">{isWin ? '>' : '$'}</span>
        <input
          ref={cmdRef}
          className="pty-cmd mono"
          disabled={status !== 'running'}
          value={line}
          placeholder={status === 'running' ? (isWin ? '在此输入命令，回车发送，例如 dir / whoami' : '在此输入命令，回车发送') : '先点「启动」，通道就绪后再输入'}
          onChange={(e) => setLine(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault()
              submitLine()
              return
            }
            if (e.key === 'c' && (e.ctrlKey || e.metaKey)) {
              e.preventDefault()
              sendPty('\x03')
              try { termRef.current?.write('^C') } catch { /* ignore */ }
            }
          }}
        />
        <button className="primary" disabled={status !== 'running'} onClick={() => submitLine()}>发送</button>
      </div>
      <div className="pty-foot dim">
        底部输入框发送到 shell · 点终端也可聚焦输入框
        {' '}· id={termIdRef.current || '-'} · {geom.cols}x{geom.rows}
        {' '}· oem_cp={oemCp}
      </div>
    </div>
  )
}

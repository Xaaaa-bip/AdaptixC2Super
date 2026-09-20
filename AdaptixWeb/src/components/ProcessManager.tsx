import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { browserKillProc, browserListProcs } from '../api/browser'
import type { ProcItem } from '../utils/browserParse'
import { copyText, isLinuxAgent } from '../utils'
import { EmptyState } from './ui'
import { PageTips } from './PageTips'

export function ProcessManager({ agentId }: { agentId: string }) {
  const api = useStore((s) => s.api)
  const agents = useStore((s) => s.agents) || {}
  const closeUiTab = useStore((s) => s.closeUiTab)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  const openConsole = useStore((s) => s.openConsole)
  const interactBoost = useStore((s) => s.interactBoost)
  const acquireInteractBoost = useStore((s) => s.acquireInteractBoost)
  const agent = agents[agentId]
  const boosted = !!interactBoost[agentId]?.ref
  // Linux 主机不做 sleep 0 交互加速（linux_agent 无该命令）
  const linux = isLinuxAgent(agent)

  const [items, setItems] = useState<ProcItem[]>([])
  const [message, setMessage] = useState('')
  const [busy, setBusy] = useState(false)
  const [killing, setKilling] = useState<number | null>(null)
  const [err, setErr] = useState('')
  const [q, setQ] = useState('')
  const [selected, setSelected] = useState<number | null>(null)
  const [ctx, setCtx] = useState<{ x: number; y: number; pid: number; name: string } | null>(null)
  const seq = useRef(0)

  const view = useMemo(() => {
    let arr = items
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((p) =>
        [p.name, String(p.pid), String(p.ppid), p.context, p.arch, String(p.session ?? '')]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
    }
    return arr
  }, [items, q])

  const selectedProc = useMemo(
    () => items.find((p) => p.pid === selected) || null,
    [items, selected],
  )

  async function refresh() {
    const my = ++seq.current
    setBusy(true)
    setErr('')
    setMessage('正在下发 ps list…')
    try {
      const res = await browserListProcs(api, agentId, ({ sec, pending }) => {
        if (my !== seq.current) return
        setMessage(pending ? `已下发，等待回显 ${sec}s…` : `轮询任务 ${sec}s…`)
      })
      if (my !== seq.current) return
      setItems(res.items)
      // 服务端回显文案 "Status: OK"：本地化为「已回显」
      const msg = res.message === 'Status: OK' ? '已回显' : res.message
      setMessage(`${msg} · ${res.items.length} 进程`)
      pushLog('ok', `进程 ${agentId}: ${res.items.length}`, false)
      if (selected != null && !res.items.some((p) => p.pid === selected)) {
        setSelected(null)
      }
    } catch (e: any) {
      if (my !== seq.current) return
      const msg = e?.message || String(e)
      setErr(msg)
      setItems([])
      setMessage('')
      pushToast('err', msg)
    } finally {
      if (my === seq.current) setBusy(false)
    }
  }

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try { await acquireInteractBoost(agentId) } catch { /* store toast */ }
      if (!cancelled) await refresh()
    })()
    return () => { cancelled = true }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [agentId])

  async function kill(pid: number, name: string) {
    if (!confirm(`结束进程？\nPID ${pid}\n${name}`)) return
    setKilling(pid)
    try {
      const res = await browserKillProc(api, agentId, pid)
      pushToast('ok', res.message || `已结束 PID ${pid}`)
      pushLog('ok', `ps kill ${pid} (${name}): ${res.message || 'ok'}`, false)
      setTimeout(() => { refresh().catch(() => {}) }, 800)
    } catch (e: any) {
      pushToast('err', e?.message || String(e))
    } finally {
      setKilling(null)
    }
  }

  async function copyPid(pid: number) {
    const ok = await copyText(String(pid))
    pushToast(ok ? 'ok' : 'err', ok ? `已复制 PID ${pid}` : '复制失败')
  }

  return (
    <div className="page browser-page pm-page">
      <div className="toolbar pm-toolbar">
        <div className="pm-title">
          <strong>进程管理</strong>
          <span className="mono dim">{agent?.a_computer || agentId}</span>
          {linux ? (
            <span className="fm-boost">Linux 进程</span>
          ) : (
            <span className={`fm-boost ${boosted || agent?.a_sleep === 0 ? 'on' : ''}`}>
              {boosted || agent?.a_sleep === 0 ? '交互加速 · sleep 0' : `Sleep ${agent?.a_sleep ?? '-'}s`}
            </span>
          )}
        </div>
        <div className="sep" />
        <button disabled={busy || killing != null} onClick={refresh}>
          {busy ? '加载中…' : '刷新'}
        </button>
        <input
          type="search"
          className="pm-search"
          placeholder="过滤 PID / 名称 / 用户"
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <button onClick={() => openConsole(agentId)}>终端</button>
        <button
          className="danger"
          disabled={!selectedProc || busy || killing != null}
          onClick={() => selectedProc && kill(selectedProc.pid, selectedProc.name)}
        >
          {killing != null && killing === selected ? '结束中…' : '结束选中'}
        </button>
        <div className="stats pm-stats">
          <span className="mono">{view.length}</span>
          {message ? <span className="dim">{message}</span> : null}
          {err ? <span className="err">{err}</span> : null}
        </div>
        <button className="ghost" onClick={() => closeUiTab(`procs:${agentId}` as any)}>关闭</button>
        <PageTips id="procs">
          <p>进程列表 + 搜索过滤；选中行后「结束进程」。</p>
          <p>Linux 主机的结束操作经由 <code>kill &lt;pid&gt;</code> 命令完成（列表随后自动刷新）。</p>
        </PageTips>
      </div>

      <div className="table-wrap pm-table-wrap">
        {busy && !items.length ? (
          <EmptyState title="正在拉取进程列表…" hint={message || '等待主机回显'} />
        ) : view.length === 0 ? (
          <EmptyState title={err || '无进程数据'} hint={err ? '可刷新重试' : '调整过滤条件'} />
        ) : (
          <table className="pm-table">
            <thead>
              <tr>
                <th style={{ width: 72 }}>PID</th>
                <th style={{ width: 72 }}>PPID</th>
                <th>进程</th>
                <th style={{ minWidth: 140 }}>用户</th>
                <th style={{ width: 72 }}>架构</th>
                <th style={{ width: 72 }}>会话</th>
                <th style={{ width: 88 }}></th>
              </tr>
            </thead>
            <tbody>
              {view.map((p) => (
                <tr
                  key={p.pid}
                  className={selected === p.pid ? 'selected' : ''}
                  onClick={() => setSelected(p.pid)}
                  onContextMenu={(e) => {
                    e.preventDefault()
                    setSelected(p.pid)
                    setCtx({ x: e.clientX, y: e.clientY, pid: p.pid, name: p.name })
                  }}
                >
                  <td className="mono">{p.pid}</td>
                  <td className="mono dim">{p.ppid || 0}</td>
                  <td className="cell-main" title={p.name}>{p.name}</td>
                  <td className="pm-user" title={p.context || ''}>{p.context || '—'}</td>
                  <td className="dim">{p.arch || '—'}</td>
                  <td className="dim mono">{p.session ?? '—'}</td>
                  <td style={{ display: 'flex', gap: 4 }}>
                    <button
                      disabled={busy}
                      onClick={(e) => {
                        e.stopPropagation()
                        void copyPid(p.pid)
                      }}
                    >
                      PID
                    </button>
                    <button
                      className="danger"
                      disabled={busy || killing != null}
                      onClick={(e) => {
                        e.stopPropagation()
                        kill(p.pid, p.name)
                      }}
                    >
                      {killing === p.pid ? '…' : '结束'}
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 160), top: Math.min(ctx.y, window.innerHeight - 120) }}
          onClick={(e) => e.stopPropagation()}
          onMouseLeave={() => setCtx(null)}
        >
          <div className="ctx-hd mono">{ctx.name} · {ctx.pid}</div>
          <button type="button" onClick={() => { void copyPid(ctx.pid); setCtx(null) }}>复制 PID</button>
          <button type="button" className="danger" onClick={() => { void kill(ctx.pid, ctx.name); setCtx(null) }}>结束进程</button>
        </div>
      )}
    </div>
  )
}

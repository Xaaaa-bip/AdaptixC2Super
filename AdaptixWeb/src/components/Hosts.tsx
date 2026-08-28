import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import type { Agent } from '../api/types'
import { copyText, formatHeartbeat, fmtRelative, fmtTime, isAgentOnline, loadSessListCollapsed, saveSessListCollapsed } from '../utils'
import { lookupIpGeoSync, preloadIpdb } from '../utils/ipdb'

type Ctx = { x: number; y: number; id: string } | null

export function Hosts({ active = true }: { active?: boolean }) {
  const agents = useStore((s) => s.agents) || {}
  const filter = useStore((s) => s.filter)
  const onlyActive = useStore((s) => s.onlyActive)
  const selected = useStore((s) => s.selectedAgentId)
  const selectAgent = useStore((s) => s.selectAgent)
  const setFilter = useStore((s) => s.setFilter)
  const setOnlyActive = useStore((s) => s.setOnlyActive)
  const removeSelected = useStore((s) => s.removeSelected)
  const refreshRest = useStore((s) => s.refreshRest)
  const openConsole = useStore((s) => s.openConsole)
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)
  const openPty = useStore((s) => s.openPty)
  const openVnc = useStore((s) => s.openVnc)
  const clearConsole = useStore((s) => s.clearConsole)
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)

  const [ctx, setCtx] = useState<Ctx>(null)
  const [openG, setOpenG] = useState({ online: true, offline: true })
  const [listCollapsed, setListCollapsed] = useState(loadSessListCollapsed)
  const [busy, setBusy] = useState(false)
  // 动态心跳：每秒刷新 age 文案（对齐 Qt LastTickWorker 500ms）
  const [nowTick, setNowTick] = useState(() => Date.now())
  const [geoVer, setGeoVer] = useState(0)
  const [cmdDlg, setCmdDlg] = useState<{ id: string; cmd: string } | null>(null)
  const [dataDlg, setDataDlg] = useState<{
    id: string
    sleep: string
    jitter: string
    computer: string
    username: string
    domain: string
    process: string
    internal_ip: string
    external_ip: string
  } | null>(null)

  const filteredFixed = useMemo(() => {
    let arr = Object.values(agents)
    if (onlyActive) arr = arr.filter(isAgentOnline)
    if (filter.trim()) {
      const q = filter.trim().toLowerCase()
      arr = arr.filter((a) =>
        [a.a_id, a.a_computer, a.a_username, a.a_internal_ip, a.a_external_ip, a.a_process, a.a_listener, a.a_tags, a.a_domain]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(q)))
    }
    return arr.sort((a, b) => (b.a_last_tick || 0) - (a.a_last_tick || 0))
  }, [agents, filter, onlyActive])

  const online = filteredFixed.filter(isAgentOnline)
  const offline = filteredFixed.filter((a) => !isAgentOnline(a))
  const selectedAgent = selected ? agents[selected] : null
  const selectedOnline = !!(selectedAgent && isAgentOnline(selectedAgent))
  const ctxAgent = ctx ? agents[ctx.id] : null
  const ctxOnline = !!(ctxAgent && isAgentOnline(ctxAgent))
  const tableList = filteredFixed

  useEffect(() => {
    const close = () => setCtx(null)
    window.addEventListener('click', close)
    window.addEventListener('scroll', close, true)
    return () => {
      window.removeEventListener('click', close)
      window.removeEventListener('scroll', close, true)
    }
  }, [])

  useEffect(() => {
    const t = window.setInterval(() => setNowTick(Date.now()), 1000)
    return () => window.clearInterval(t)
  }, [])

  useEffect(() => {
    preloadIpdb().then((ok) => { if (ok) setGeoVer((v) => v + 1) }).catch(() => {})
  }, [])

  useEffect(() => {
    if (active) return
    setCtx(null)
    setCmdDlg(null)
    setDataDlg(null)
  }, [active])

  function pick(a: Agent) {
    selectAgent(a.a_id)
    openConsole(a.a_id)
  }

  function focusTerm() {
    window.requestAnimationFrame(() => {
      document.querySelector<HTMLInputElement>('.term-in input')?.focus()
    })
  }

  function openData(id: string) {
    const a = agents[id]
    setDataDlg({
      id,
      sleep: a?.a_sleep != null ? String(a.a_sleep) : '5',
      jitter: a?.a_jitter != null ? String(a.a_jitter) : '0',
      computer: a?.a_computer || '',
      username: a?.a_username || '',
      domain: a?.a_domain || '',
      process: a?.a_process || '',
      internal_ip: a?.a_internal_ip || '',
      external_ip: a?.a_external_ip || '',
    })
  }

  function tryFiles(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开文件管理'); return }
    openFilesBrowser(id)
  }
  function tryProcs(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开进程管理'); return }
    openProcsBrowser(id)
  }
  function tryPty(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开 PTY'); return }
    openPty(id)
  }
  function tryVnc(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开 VNC'); return }
    openVnc(id)
  }
  function openTunnels(id: string) {
    selectAgent(id)
    setTab('tunnels')
    openUiTab('tunnels')
  }
  async function clearServerConsole(id: string) {
    try {
      await api.consoleRemove([id])
      clearConsole(id)
      pushToast('ok', '已清除控制台数据')
    } catch (e: any) {
      clearConsole(id)
      pushToast('warn', `服务端清除失败，已清本地：${e?.message || e}`)
    }
  }
  async function doRefresh() {
    setBusy(true)
    try { await refreshRest() } catch (e: any) { pushLog('err', e.message || String(e)) }
    finally { setBusy(false) }
  }

  function SideItem({ a }: { a: Agent }) {
    const live = isAgentOnline(a)
    return (
      <button
        type="button"
        className={`sess-item ${selected === a.a_id ? 'active' : ''}`}
        onClick={() => pick(a)}
        onDoubleClick={() => { pick(a); focusTerm() }}
        onContextMenu={(e) => {
          e.preventDefault()
          selectAgent(a.a_id)
          setCtx({ x: e.clientX, y: e.clientY, id: a.a_id })
        }}
      >
        <span className={`sdot ${live ? 'on' : 'off'}`} title={live ? '在线' : '离线'} />
        <div className="meta">
          <div className="name">{a.a_computer || a.a_id}</div>
          <div className="ip mono">{a.a_internal_ip || a.a_external_ip || '-'}</div>
        </div>
        <span className="tick mono dim">{fmtRelative(a.a_last_tick)}</span>
      </button>
    )
  }

  return (
    <div className={`sess ${active ? '' : 'is-parked'} ${listCollapsed ? 'is-list-collapsed' : ''}`} aria-hidden={!active}>
      {/* 左侧：在线 / 离线分组列表，可整栏折叠让出表格宽度 */}
      <aside className={`sess-list ${listCollapsed ? 'is-collapsed' : ''}`}>
        {listCollapsed ? (
          <button
            type="button"
            className="sess-list-rail"
            title="展开在线主机"
            onClick={() => { setListCollapsed(false); saveSessListCollapsed(false) }}
          >
            <span className="sess-list-rail-ico" aria-hidden>▸</span>
            <span className="sess-list-rail-k">在线</span>
            <span className="signal">{online.length}</span>
          </button>
        ) : (
        <>
        <div className="sess-list-hd">
          <div>
            <div className="sess-k">主机</div>
            <div className="sess-count">
              <span className="signal">{online.length}</span>
              <span className="dim"> / {filteredFixed.length}</span>
            </div>
          </div>
          <div className="sess-list-hd-actions">
            <button type="button" className="ghost" style={{ height: 28 }} disabled={busy} onClick={() => void doRefresh()}>
              {busy ? '…' : '刷新'}
            </button>
            <button
              type="button"
              className="ghost sess-list-fold"
              style={{ height: 28 }}
              title="折叠在线列表"
              onClick={() => { setListCollapsed(true); saveSessListCollapsed(true) }}
            >
              ◂
            </button>
          </div>
        </div>
        <div className="sess-list-bd">
          <button type="button" className="sess-g" onClick={() => setOpenG((g) => ({ ...g, online: !g.online }))}>
            <span>{openG.online ? '▾' : '▸'}</span>
            <span>在线</span>
            <span className="n">{online.length}</span>
          </button>
          {openG.online && (
            online.length ? online.map((a) => <SideItem key={a.a_id} a={a} />)
              : <div className="sess-empty">无在线主机</div>
          )}

          <button type="button" className="sess-g" onClick={() => setOpenG((g) => ({ ...g, offline: !g.offline }))}>
            <span>{openG.offline ? '▾' : '▸'}</span>
            <span>离线</span>
            <span className="n">{offline.length}</span>
          </button>
          {openG.offline && (
            offline.length ? offline.map((a) => <SideItem key={a.a_id} a={a} />)
              : <div className="sess-empty">无离线主机</div>
          )}
        </div>
        </>
        )}
      </aside>

      {/* 右侧：工具条 + 主机表 */}
      <section className="sess-main">
        <div className="sess-bar">
          {listCollapsed && (
            <button
              type="button"
              className="ghost sess-list-unfold"
              title="展开在线主机"
              onClick={() => { setListCollapsed(false); saveSessListCollapsed(false) }}
            >
              ▸ 在线 {online.length}
            </button>
          )}
          <input
            className="sess-search"
            type="search"
            placeholder="搜索主机 / IP / 用户 / 标签"
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
          />
          <label className="sess-check">
            <input type="checkbox" checked={onlyActive} onChange={(e) => setOnlyActive(e.target.checked)} />
            仅在线
          </label>
          <div className="sess-actions">
            {selectedAgent ? (
              <>
                <span className="sess-sel mono">{selectedAgent.a_computer || selected}</span>
                <span className="sep" />
                <button disabled={!selectedOnline} onClick={() => selected && tryFiles(selected)}>文件</button>
                <button disabled={!selectedOnline} onClick={() => selected && tryProcs(selected)}>进程</button>
                <button disabled={!selectedOnline} onClick={() => selected && tryPty(selected)}>PTY</button>
                <button disabled={!selectedOnline} onClick={() => selected && tryVnc(selected)}>VNC</button>
                <button onClick={() => selected && openTunnels(selected)}>隧道</button>
                <button className="danger" onClick={() => removeSelected().catch((e) => pushLog('err', String(e)))}>删除</button>
              </>
            ) : (
              <span className="dim">在左侧选择主机，或点击右侧表格行</span>
            )}
          </div>
        </div>

        <div className="sess-table-wrap">
          {tableList.length === 0 ? (
            <div className="sess-empty big">
              {Object.keys(agents).length === 0 ? (
                <>
                  <div className="title">还没有主机</div>
                  <div className="hint">打开投递，复制一条命令到目标机器执行</div>
                  <button
                    type="button"
                    className="primary"
                    style={{ marginTop: 12 }}
                    onClick={() => { setTab('delivery'); openUiTab('delivery') }}
                  >
                    去投递
                  </button>
                </>
              ) : (
                <>
                  <div className="title">无匹配主机</div>
                  <div className="hint">调整搜索条件，或取消「仅在线」</div>
                </>
              )}
            </div>
          ) : (
            <table className="sess-table">
              <thead>
                <tr>
                  <th style={{ width: 56 }}>状态</th>
                  <th>主机</th>
                  <th>用户</th>
                  <th>内网</th>
                  <th>外网</th>
                  <th>归属地</th>
                  <th>进程</th>
                  <th>系统</th>
                  <th>监听</th>
                  <th title="距上次回连 / 配置 sleep（动态刷新）">心跳</th>
                  <th>标签</th>
                </tr>
              </thead>
              <tbody>
                {tableList.map((a) => {
                  const live = isAgentOnline(a, { now: nowTick })
                  const hb = formatHeartbeat(a, { now: nowTick })
                  const ext = String(a.a_external_ip || '').trim()
                  // geoVer 仅触发重渲染；lookup 同步读缓存
                  void geoVer
                  const geo = ext ? lookupIpGeoSync(ext) : ''
                  return (
                    <tr
                      key={a.a_id}
                      className={selected === a.a_id ? 'selected' : ''}
                      onClick={() => pick(a)}
                      onDoubleClick={() => { pick(a); focusTerm() }}
                      onContextMenu={(e) => {
                        e.preventDefault()
                        selectAgent(a.a_id)
                        setCtx({ x: e.clientX, y: e.clientY, id: a.a_id })
                      }}
                    >
                      <td>
                        <span className={`st ${live ? 'on' : 'off'}`}>{live ? '在线' : '离线'}</span>
                      </td>
                      <td>
                        <div className="cell-main">{a.a_computer || '-'}</div>
                        <div className="cell-sub mono" title={a.a_id}>{a.a_id}</div>
                      </td>
                      <td>
                        <div>{a.a_domain ? `${a.a_domain}\\` : ''}{a.a_username || '-'}</div>
                        {a.a_elevated ? <div className="cell-sub signal">提权</div> : null}
                      </td>
                      <td className="mono">{a.a_internal_ip || '-'}</td>
                      <td className="mono" title={ext || ''}>{ext || '-'}</td>
                      <td className="dim" title={geo || (ext ? '解析中/未知' : '')}>{geo || (ext ? '…' : '-')}</td>
                      <td title={a.a_process || ''}>
                        <div>{a.a_process || '-'}</div>
                        <div className="cell-sub mono">{a.a_pid || ''}</div>
                      </td>
                      <td className="dim">{a.a_os_desc || a.a_arch || '-'}</td>
                      <td className="mono">{a.a_listener || '-'}</td>
                      <td className="mono" title={fmtTime(a.a_last_tick)}>
                        <div className={live ? '' : 'dim'}>{hb.text}</div>
                        {hb.sub ? <div className="cell-sub">{hb.sub}</div> : null}
                      </td>
                      <td>{a.a_tags || ''}</td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          )}
        </div>
      </section>

      {ctx && (
        <div
          className="ctx sess-ctx"
          style={{
            left: Math.min(ctx.x, window.innerWidth - 228),
            top: Math.min(ctx.y, Math.max(8, window.innerHeight - 80)),
          }}
          onClick={(e) => e.stopPropagation()}
        >
          <div className="ctx-hd mono" title={ctx.id}>{agents[ctx.id]?.a_computer || ctx.id}</div>
          <button type="button" onClick={() => { openConsole(ctx.id); setCtx(null); focusTerm() }}>打开终端</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { setCmdDlg({ id: ctx.id, cmd: '' }); setCtx(null) }}>执行命令</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryPty(ctx.id); setCtx(null) }}>PTY</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryFiles(ctx.id); setCtx(null) }}>文件</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryProcs(ctx.id); setCtx(null) }}>进程</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryVnc(ctx.id); setCtx(null) }}>VNC</button>
          <button type="button" onClick={() => { openTunnels(ctx.id); setCtx(null) }}>隧道</button>
          <button type="button" disabled={!ctxOnline} onClick={async () => {
            try { await api.commandRaw(ctx.id, 'screenshot_bof'); openConsole(ctx.id); pushToast('ok', '已下发截图') }
            catch (e: any) { pushLog('err', e.message || String(e)) }
            setCtx(null)
          }}>截图</button>
          <div className="sep" />
          <button type="button" disabled={!ctxOnline} onClick={async () => {
            try {
              await api.commandRaw(ctx.id, 'sleep 0')
              openConsole(ctx.id)
              pushToast('ok', '已下发 sleep 0')
            } catch (e: any) { pushLog('err', e.message || String(e)) }
            setCtx(null)
          }}>交互加速</button>
          <button type="button" onClick={() => { openData(ctx.id); setCtx(null) }}
            title={ctxAgent?.a_sleep != null ? `当前 sleep ${ctxAgent.a_sleep}s` : 'Sleep / 资料'}>
            Sleep / 资料
          </button>
          <div className="sep" />
          <button type="button" onClick={async () => { await clearServerConsole(ctx.id); setCtx(null) }}>清空终端</button>
          <button type="button" onClick={async () => {
            const ok = await copyText(ctx.id)
            pushToast(ok ? 'ok' : 'err', ok ? `已复制 ${ctx.id}` : '复制失败')
            setCtx(null)
          }}>复制 ID</button>
          <button type="button" onClick={async () => {
            const tag = prompt('标签：')
            if (tag != null) {
              try { await api.setTag([ctx.id], tag); await refreshRest(); pushToast('ok', '标签已更新') }
              catch (e: any) { pushLog('err', e.message || String(e)) }
            }
            setCtx(null)
          }}>标签</button>
          <div className="sep" />
          <button type="button" className="danger" onClick={async () => {
            if (!confirm(`删除主机 ${ctx.id}？`)) { setCtx(null); return }
            selectAgent(ctx.id); setCtx(null)
            await removeSelected().catch((e) => pushLog('err', String(e)))
          }}>删除</button>
        </div>
      )}

      {cmdDlg && (
        <div className="mask" onClick={() => setCmdDlg(null)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()}>
            <h2>执行命令</h2>
            <div className="dim mono" style={{ marginBottom: 8 }}>{cmdDlg.id}</div>
            <div className="field">
              <label>命令行</label>
              <input
                className="mono"
                autoFocus
                value={cmdDlg.cmd}
                placeholder="例如: whoami /all"
                onChange={(e) => setCmdDlg({ ...cmdDlg, cmd: e.target.value })}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') {
                    e.preventDefault()
                    const c = cmdDlg.cmd.trim()
                    if (!c) return
                    void (async () => {
                      try {
                        await api.commandRaw(cmdDlg.id, c)
                        openConsole(cmdDlg.id)
                        pushToast('ok', '已下发')
                        setCmdDlg(null)
                      } catch (err: any) {
                        pushToast('err', err?.message || String(err))
                      }
                    })()
                  }
                }}
              />
            </div>
            <div className="btns">
              <button onClick={() => setCmdDlg(null)}>取消</button>
              <button className="primary" onClick={async () => {
                const c = cmdDlg.cmd.trim()
                if (!c) return
                try {
                  await api.commandRaw(cmdDlg.id, c)
                  openConsole(cmdDlg.id)
                  pushToast('ok', '已下发')
                  setCmdDlg(null)
                } catch (err: any) {
                  pushToast('err', err?.message || String(err))
                }
              }}>执行</button>
            </div>
          </div>
        </div>
      )}

      {dataDlg && (
        <div className="mask" onClick={() => setDataDlg(null)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()} style={{ maxWidth: 480 }}>
            <h2>Sleep / 资料</h2>
            <div className="dim mono" style={{ marginBottom: 8 }}>{dataDlg.id}</div>
            <div className="field-grid-2">
              <div className="field"><label>Sleep（秒）</label>
                <input value={dataDlg.sleep} onChange={(e) => setDataDlg({ ...dataDlg, sleep: e.target.value })} />
              </div>
              <div className="field"><label>Jitter %</label>
                <input value={dataDlg.jitter} onChange={(e) => setDataDlg({ ...dataDlg, jitter: e.target.value })} />
              </div>
            </div>
            <div className="hint muted-hint">Sleep / Jitter 会下发任务；下面只改控制台展示。</div>
            <div className="field-grid-2">
              <div className="field"><label>主机名</label>
                <input value={dataDlg.computer} onChange={(e) => setDataDlg({ ...dataDlg, computer: e.target.value })} />
              </div>
              <div className="field"><label>用户</label>
                <input value={dataDlg.username} onChange={(e) => setDataDlg({ ...dataDlg, username: e.target.value })} />
              </div>
              <div className="field"><label>域</label>
                <input value={dataDlg.domain} onChange={(e) => setDataDlg({ ...dataDlg, domain: e.target.value })} />
              </div>
              <div className="field"><label>进程</label>
                <input value={dataDlg.process} onChange={(e) => setDataDlg({ ...dataDlg, process: e.target.value })} />
              </div>
              <div className="field"><label>内网 IP</label>
                <input value={dataDlg.internal_ip} onChange={(e) => setDataDlg({ ...dataDlg, internal_ip: e.target.value })} />
              </div>
              <div className="field"><label>外网 IP</label>
                <input value={dataDlg.external_ip} onChange={(e) => setDataDlg({ ...dataDlg, external_ip: e.target.value })} />
              </div>
            </div>
            <div className="btns">
              <button onClick={() => setDataDlg(null)}>取消</button>
              <button className="primary" onClick={async () => {
                try {
                  const sleepSec = Number(dataDlg.sleep)
                  const jit = Number(dataDlg.jitter)
                  if (!Number.isNaN(sleepSec) && sleepSec >= 0) {
                    const j = Number.isNaN(jit) ? 0 : Math.max(0, Math.min(100, jit))
                    const cmd = j > 0 ? `sleep ${sleepSec}s ${j}` : `sleep ${sleepSec}s`
                    await api.commandRaw(dataDlg.id, cmd)
                  }
                  await api.updateAgentData(dataDlg.id, {
                    computer: dataDlg.computer,
                    username: dataDlg.username,
                    domain: dataDlg.domain,
                    process: dataDlg.process,
                    internal_ip: dataDlg.internal_ip,
                    external_ip: dataDlg.external_ip,
                  })
                  await refreshRest()
                  pushToast('ok', '已更新')
                  setDataDlg(null)
                } catch (e: any) {
                  pushToast('err', e?.message || String(e))
                }
              }}>保存</button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

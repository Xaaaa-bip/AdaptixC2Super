import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { isLinuxAgent } from '../utils'
import { PageTips } from './PageTips'

type TunnelRow = {
  p_tunnel_id?: string
  tunnel_id?: string
  p_agent_id?: string
  a_id?: string
  agent_id?: string
  p_type?: string
  type?: string
  p_info?: string
  desc?: string
  info?: string
  p_interface?: string
  l_host?: string
  p_port?: string | number
  l_port?: string | number
  p_fhost?: string
  p_target_host?: string
  t_host?: string
  p_fport?: string | number
  p_target_port?: string | number
  t_port?: string | number
  p_active?: boolean
  active?: boolean
}

function tunnelId(t: TunnelRow) {
  return String(t.p_tunnel_id || t.tunnel_id || '')
}

function isActive(t: TunnelRow) {
  if (typeof t.p_active === 'boolean') return t.p_active
  if (typeof t.active === 'boolean') return t.active
  // legacy list without flag: assume active (old server deleted on stop)
  return true
}

export function Tunnels() {
  const api = useStore((s) => s.api)
  const agents = useStore((s) => s.agents)
  const selected = useStore((s) => s.selectedAgentId)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const [list, setList] = useState<TunnelRow[]>([])
  const [show, setShow] = useState(false)
  const [editId, setEditId] = useState<string | null>(null)
  const [editInfo, setEditInfo] = useState('')
  const [busy, setBusy] = useState(false)
  const [kind, setKind] = useState<'socks5' | 'socks4' | 'lportfwd' | 'rportfwd'>('socks5')
  const [agentId, setAgentId] = useState(selected || '')
  const [lhost, setLhost] = useState('0.0.0.0')
  const [lport, setLport] = useState(1080)
  const [thost, setThost] = useState('127.0.0.1')
  const [tport, setTport] = useState(3389)
  const [desc, setDesc] = useState('')
  const [useAuth, setUseAuth] = useState(false)
  const [user, setUser] = useState('')
  const [pass, setPass] = useState('')

  const agentOptions = useMemo(() => Object.values(agents), [agents])
  // 当前（新建隧道表单里选中的）主机是否 Linux：RPF 插件未实现
  const linuxSelected = isLinuxAgent(agents[agentId])

  async function refresh() {
    try {
      const data = await api.get('/api/tunnel/list')
      const arr = Array.isArray(data) ? data : (data?.tunnels || data?.data || [])
      setList(Array.isArray(arr) ? arr : [])
    } catch (e: any) {
      pushLog('err', e.message || String(e))
    }
  }

  useEffect(() => { refresh() }, [])
  useEffect(() => { if (selected) setAgentId(selected) }, [selected])

  async function doStop(id: string) {
    if (!id || !confirm(`停止隧道 ${id}？（配置保留，可再启用）`)) return
    try {
      await api.post('/api/tunnel/stop', { p_tunnel_id: id })
      pushLog('warn', `隧道已停止 ${id}`)
      pushToast('ok', '已停止（保留配置）')
      await refresh()
    } catch (e: any) {
      pushLog('err', e.message || String(e))
      pushToast('err', e.message || String(e))
    }
  }

  async function doEnable(id: string) {
    if (!id) return
    try {
      await api.post('/api/tunnel/enable', { p_tunnel_id: id })
      pushLog('ok', `隧道已启用 ${id}`)
      pushToast('ok', '已启用')
      await refresh()
    } catch (e: any) {
      pushLog('err', e.message || String(e))
      pushToast('err', e.message || String(e))
    }
  }

  async function doDelete(id: string) {
    if (!id || !confirm(`删除隧道 ${id}？此操作不可恢复`)) return
    try {
      await api.post('/api/tunnel/delete', { p_tunnel_id: id })
      pushLog('warn', `隧道已删除 ${id}`)
      pushToast('ok', '已删除')
      await refresh()
    } catch (e: any) {
      // fallback: old server only has stop(=delete)
      try {
        await api.post('/api/tunnel/stop', { p_tunnel_id: id })
        pushLog('warn', `隧道已删除(stop) ${id}`)
        await refresh()
      } catch (e2: any) {
        pushLog('err', e2.message || String(e2))
        pushToast('err', e2.message || String(e2))
      }
    }
  }

  async function doSaveInfo() {
    if (!editId) return
    setBusy(true)
    try {
      await api.post('/api/tunnel/set/info', { p_tunnel_id: editId, p_info: editInfo })
      pushLog('ok', `隧道信息已更新 ${editId}`)
      pushToast('ok', '描述已保存')
      setEditId(null)
      await refresh()
    } catch (e: any) {
      pushLog('err', e.message || String(e))
      pushToast('err', e.message || String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="page">
      <div className="toolbar">
        <button className="primary" onClick={() => setShow(true)}>+ 新建隧道</button>
        <button onClick={() => refresh()}>刷新</button>
        <div className="stats"><span>{list.length} 条隧道</span></div>
        <PageTips id="tunnels">
          <p>新建隧道：SOCKS4/SOCKS5 代理、本地/远程端口转发。</p>
          <p>SOCKS5 支持用户名密码认证。</p>
          <p><b>Linux 主机暂不支持 RPF 反向转发</b>（选项自动禁用）。</p>
        </PageTips>
      </div>
      <div className="table-wrap">
        {list.length === 0 ? (
          <div className="empty">
            <div className="big">⇄</div>
            <div>暂无隧道</div>
            <div className="dim" style={{ marginTop: 6 }}>SOCKS / 端口转发监听在 Teamserver 侧</div>
            <div style={{ marginTop: 12 }}>
              <button className="primary" onClick={() => setShow(true)}>+ 新建隧道</button>
            </div>
          </div>
        ) : (
          <table>
            <thead>
              <tr>
                <th>ID</th>
                <th>状态</th>
                <th>主机</th>
                <th>类型</th>
                <th>信息</th>
                <th>监听</th>
                <th>目标</th>
                <th style={{ width: 240 }}></th>
              </tr>
            </thead>
            <tbody>
              {list.map((t, i) => {
                const id = tunnelId(t)
                const active = isActive(t)
                return (
                  <tr key={id || i}>
                    <td className="mono">{id || '-'}</td>
                    <td>
                      <span className={`tag ${active ? 'listen' : 'offline'}`}>
                        {active ? '运行中' : '已停止'}
                      </span>
                    </td>
                    <td className="mono">{t.p_agent_id || t.a_id || t.agent_id || '-'}</td>
                    <td>{t.p_type || t.type || '-'}</td>
                    <td>{t.p_info || t.desc || t.info || '-'}</td>
                    <td className="mono">{t.p_interface || t.l_host || '-'}:{t.p_port || t.l_port || '-'}</td>
                    <td className="mono">{t.p_fhost || t.p_target_host || t.t_host || '-'}:{t.p_fport || t.p_target_port || t.t_port || '-'}</td>
                    <td style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
                      {active ? (
                        <button onClick={() => doStop(id)}>停止</button>
                      ) : (
                        <button className="primary" onClick={() => doEnable(id)}>启用</button>
                      )}
                      <button onClick={() => {
                        setEditId(id)
                        setEditInfo(String(t.p_info || t.desc || t.info || ''))
                      }}>编辑</button>
                      <button className="danger" onClick={() => doDelete(id)}>删除</button>
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        )}
      </div>

      {editId && (
        <div className="mask" onClick={() => !busy && setEditId(null)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()}>
            <h2>编辑隧道信息</h2>
            <div className="dim mono" style={{ marginBottom: 8 }}>{editId}</div>
            <div className="field">
              <label>描述 / Info</label>
              <input value={editInfo} onChange={(e) => setEditInfo(e.target.value)} placeholder="备注" />
            </div>
            <div className="hint">仅修改描述；端口/类型需删除后重建。</div>
            <div className="btns">
              <button disabled={busy} onClick={() => setEditId(null)}>取消</button>
              <button className="primary" disabled={busy} onClick={doSaveInfo}>{busy ? '保存中…' : '保存'}</button>
            </div>
          </div>
        </div>
      )}

      {show && (
        <div className="mask" onClick={() => !busy && setShow(false)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()}>
            <h2>新建隧道</h2>
            <div className="field"><label>主机</label>
              <select value={agentId} onChange={(e) => setAgentId(e.target.value)}>
                <option value="">选择上线主机</option>
                {agentOptions.map((a) => (
                  <option key={a.a_id} value={a.a_id}>{a.a_computer || a.a_id} · {a.a_internal_ip}</option>
                ))}
              </select>
            </div>
            <div className="field"><label>类型</label>
              <select value={kind} onChange={(e) => setKind(e.target.value as any)}>
                <option value="socks5">SOCKS5</option>
                <option value="socks4">SOCKS4</option>
                <option value="lportfwd">本地端口转发 LPF</option>
                <option value="rportfwd" disabled={linuxSelected}>远程端口转发 RPF</option>
              </select>
            </div>
            {linuxSelected && (
              <div className="hint warn-hint">RPF 反向转发暂不支持 Linux 主机（插件未实现）</div>
            )}
            <div className="field"><label>描述</label><input value={desc} onChange={(e) => setDesc(e.target.value)} /></div>
            {(kind === 'socks5' || kind === 'socks4' || kind === 'lportfwd') && (
              <>
                <div className="field"><label>监听地址</label><input value={lhost} onChange={(e) => setLhost(e.target.value)} /></div>
                <div className="field"><label>监听端口</label><input type="number" value={lport} onChange={(e) => setLport(Number(e.target.value))} /></div>
              </>
            )}
            {kind === 'lportfwd' && (
              <>
                <div className="field"><label>目标地址</label><input value={thost} onChange={(e) => setThost(e.target.value)} /></div>
                <div className="field"><label>目标端口</label><input type="number" value={tport} onChange={(e) => setTport(Number(e.target.value))} /></div>
              </>
            )}
            {kind === 'rportfwd' && (
              <>
                <div className="field"><label>远程端口</label><input type="number" value={lport} onChange={(e) => setLport(Number(e.target.value))} /></div>
                <div className="field"><label>目标地址</label><input value={thost} onChange={(e) => setThost(e.target.value)} /></div>
                <div className="field"><label>目标端口</label><input type="number" value={tport} onChange={(e) => setTport(Number(e.target.value))} /></div>
              </>
            )}
            {kind === 'socks5' && (
              <>
                <div className="field"><label>认证</label>
                  <label style={{ color: 'var(--text)' }}>
                    <input type="checkbox" checked={useAuth} onChange={(e) => setUseAuth(e.target.checked)} /> 启用用户名密码
                  </label>
                </div>
                {useAuth && (
                  <>
                    <div className="field"><label>用户名</label><input value={user} onChange={(e) => setUser(e.target.value)} /></div>
                    <div className="field"><label>密码</label><input value={pass} onChange={(e) => setPass(e.target.value)} /></div>
                  </>
                )}
              </>
            )}
            <div className="hint">隧道监听在 Teamserver 侧（listen=true）。停止后配置保留，可再启用；删除才会移除。</div>
            <div className="btns">
              <button disabled={busy} onClick={() => setShow(false)}>取消</button>
              <button className="primary" disabled={busy || !agentId || (linuxSelected && kind === 'rportfwd')} onClick={async () => {
                setBusy(true)
                try {
                  let path = ''
                  let body: any = { agent_id: agentId, listen: true, desc }
                  if (kind === 'socks5') {
                    path = '/api/tunnel/start/socks5'
                    body = { ...body, l_host: lhost, l_port: lport, use_auth: useAuth, username: user, password: pass }
                  } else if (kind === 'socks4') {
                    path = '/api/tunnel/start/socks4'
                    body = { ...body, l_host: lhost, l_port: lport }
                  } else if (kind === 'lportfwd') {
                    path = '/api/tunnel/start/lportfwd'
                    body = { ...body, l_host: lhost, l_port: lport, t_host: thost, t_port: tport }
                  } else {
                    path = '/api/tunnel/start/rportfwd'
                    body = { ...body, port: lport, t_host: thost, t_port: tport }
                  }
                  const res = await api.post(path, body)
                  pushLog('ok', `隧道已创建 ${res.message || ''}`)
                  setShow(false)
                  await refresh()
                } catch (e: any) {
                  pushLog('err', e.message || String(e))
                  alert(e.message || String(e))
                } finally { setBusy(false) }
              }}>创建</button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

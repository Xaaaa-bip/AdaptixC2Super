import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { copyText } from '../utils'
import { EmptyState, PageShell } from './ui'

export function Credentials() {
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const [list, setList] = useState<any[]>([])
  const [show, setShow] = useState(false)
  const [busy, setBusy] = useState(false)
  const [q, setQ] = useState('')
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [realm, setRealm] = useState('')
  const [host, setHost] = useState('')
  const [tag, setTag] = useState('')

  async function refresh() {
    try {
      const data = await api.get('/api/creds/list')
      const arr = Array.isArray(data) ? data : (data?.data || data?.creds || [])
      setList(Array.isArray(arr) ? arr : [])
    } catch (e: any) { pushLog('err', e.message || String(e)) }
  }
  useEffect(() => { refresh() }, [])

  const view = useMemo(() => {
    if (!q.trim()) return list
    const s = q.trim().toLowerCase()
    return list.filter((c) =>
      [c.username, c.user, c.password, c.pass, c.realm, c.host, c.tag, c.type]
        .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
  }, [list, q])

  return (
    <>
      <PageShell
        toolbar={
          <>
            <button className="primary" onClick={() => setShow(true)}>+ 添加凭据</button>
            <input type="search" placeholder="搜索用户 / 主机 / 标签" value={q} onChange={(e) => setQ(e.target.value)} />
            <button onClick={() => refresh()}>刷新</button>
            <div className="stats"><span>{view.length} 条</span></div>
          </>
        }
      >
        {view.length === 0 ? (
          <EmptyState icon="⚿" title="暂无凭据" hint="收集到的账号口令可集中管理" />
        ) : (
          <table>
            <thead>
              <tr><th>用户</th><th>密码</th><th>域</th><th>主机</th><th>类型</th><th>标签</th><th style={{ width: 120 }}></th></tr>
            </thead>
            <tbody>
              {view.map((c: any, i) => {
                const id = c.cred_id || c.c_id || c.id
                const user = c.username || c.user || '-'
                const pass = c.password || c.pass || '-'
                return (
                  <tr key={id || i}>
                    <td>{user}</td>
                    <td className="mono">{pass}</td>
                    <td>{c.realm || ''}</td>
                    <td>{c.host || ''}</td>
                    <td>{c.type || ''}</td>
                    <td>{c.tag || ''}</td>
                    <td style={{ display: 'flex', gap: 6 }}>
                      <button className="ghost" onClick={async () => {
                        const ok = await copyText(`${user}:${pass}`)
                        pushToast(ok ? 'ok' : 'err', ok ? '已复制 user:pass' : '复制失败')
                      }}>复制</button>
                      <button className="danger" onClick={async () => {
                        if (!id || !confirm('删除凭据？')) return
                        try {
                          await api.post('/api/creds/remove', { cred_id_array: [id] })
                          await refresh()
                        } catch (e: any) { pushLog('err', e.message || String(e)) }
                      }}>删</button>
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        )}
      </PageShell>

      {show && (
        <div className="mask" onClick={() => !busy && setShow(false)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()}>
            <h2>添加凭据</h2>
            <div className="field"><label>用户名</label><input value={username} onChange={(e) => setUsername(e.target.value)} /></div>
            <div className="field"><label>密码</label><input value={password} onChange={(e) => setPassword(e.target.value)} /></div>
            <div className="field"><label>域</label><input value={realm} onChange={(e) => setRealm(e.target.value)} /></div>
            <div className="field"><label>主机</label><input value={host} onChange={(e) => setHost(e.target.value)} /></div>
            <div className="field"><label>标签</label><input value={tag} onChange={(e) => setTag(e.target.value)} /></div>
            <div className="btns">
              <button disabled={busy} onClick={() => setShow(false)}>取消</button>
              <button className="primary" disabled={busy || !username} onClick={async () => {
                setBusy(true)
                try {
                  await api.post('/api/creds/add', {
                    username, password, realm, host, tag, type: 'password',
                  })
                  pushToast('ok', '凭据已添加')
                  setShow(false)
                  setUsername(''); setPassword(''); setRealm(''); setHost(''); setTag('')
                  await refresh()
                } catch (e: any) {
                  pushLog('err', e.message || String(e))
                } finally { setBusy(false) }
              }}>保存</button>
            </div>
          </div>
        </div>
      )}
    </>
  )
}

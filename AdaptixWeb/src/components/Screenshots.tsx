import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { EmptyState } from './ui'

/** 对齐服务端 ScreenData JSON 字段 */
function screenId(s: any): string {
  return String(s?.s_screen_id || s?.screen_id || s?.s_id || s?.id || '')
}
function screenUser(s: any): string {
  return String(s?.s_user || s?.user || s?.a_user || '-')
}
function screenComputer(s: any): string {
  return String(s?.s_computer || s?.computer || s?.a_computer || '-')
}
function screenNote(s: any): string {
  return String(s?.s_note || s?.note || '')
}
function screenDate(s: any): string {
  const n = Number(s?.s_date || s?.date || s?.created || 0)
  if (!n) return '-'
  // unix 秒
  const ms = n < 1e12 ? n * 1000 : n
  try { return new Date(ms).toLocaleString() } catch { return String(n) }
}

export function Screenshots() {
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const [list, setList] = useState<any[]>([])
  const [q, setQ] = useState('')
  const [preview, setPreview] = useState<string | null>(null)
  const [previewId, setPreviewId] = useState<string | null>(null)
  const [previewErr, setPreviewErr] = useState('')

  async function refresh() {
    try {
      const data = await api.get('/api/screen/list')
      const arr = Array.isArray(data) ? data : (data?.data || [])
      setList(Array.isArray(arr) ? arr : [])
    } catch (e: any) {
      pushLog('err', e.message || String(e))
    }
  }

  useEffect(() => { refresh() }, [])
  useEffect(() => () => { if (preview) URL.revokeObjectURL(preview) }, [preview])

  const view = useMemo(() => {
    if (!q.trim()) return list
    const s = q.trim().toLowerCase()
    return list.filter((x) =>
      [screenId(x), screenUser(x), screenComputer(x), screenNote(x)]
        .filter(Boolean).some((v) => String(v).toLowerCase().includes(s)))
  }, [list, q])

  async function openPreview(id: string) {
    if (!id) {
      pushToast('err', '截图 ID 无效')
      return
    }
    setPreviewErr('')
    try {
      const base = (api as any).baseUrl || ''
      const url = (base ? base.replace(/\/+$/, '') : '') + `/api/screen/image?screen_id=${encodeURIComponent(id)}`
      const res = await fetch(url, { headers: { Authorization: `Bearer ${(api as any).accessToken}` } })
      const buf = await res.arrayBuffer()
      const head = new Uint8Array(buf.slice(0, 16))
      // 服务端失败时仍可能 200 + JSON
      const asText = new TextDecoder().decode(head)
      if (!res.ok || asText.startsWith('{') || asText.startsWith('{"')) {
        let msg = `预览失败 HTTP ${res.status}`
        try {
          const j = JSON.parse(new TextDecoder().decode(buf))
          if (j?.message) msg = j.message
        } catch { /* ignore */ }
        throw new Error(msg)
      }
      // JPEG / PNG / GIF 嗅探；API 常误标 image/png
      let mime = 'application/octet-stream'
      if (head[0] === 0xff && head[1] === 0xd8) mime = 'image/jpeg'
      else if (head[0] === 0x89 && head[1] === 0x50) mime = 'image/png'
      else if (head[0] === 0x47 && head[1] === 0x49) mime = 'image/gif'
      else if (head[0] === 0x42 && head[1] === 0x4d) mime = 'image/bmp'
      else mime = res.headers.get('Content-Type') || 'image/jpeg'

      const blob = new Blob([buf], { type: mime })
      const obj = URL.createObjectURL(blob)
      if (preview) URL.revokeObjectURL(preview)
      setPreview(obj)
      setPreviewId(id)
    } catch (e: any) {
      setPreviewErr(e.message || String(e))
      pushLog('err', e.message || String(e))
      pushToast('err', e.message || String(e))
    }
  }

  return (
    <div className="page">
      <div className="toolbar">
        <input type="search" placeholder="搜索截图 / 主机 / 用户" value={q} onChange={(e) => setQ(e.target.value)} />
        <button onClick={() => refresh()}>刷新</button>
        <div className="stats"><span>{view.length} 张截图</span></div>
      </div>
      <div className="screen-layout" style={{ display: 'grid', gridTemplateColumns: preview ? '1fr 1fr' : '1fr', minHeight: 0, flex: 1, gap: 0 }}>
        <div className="table-wrap" style={{ borderRadius: preview ? '0 0 0 10px' : undefined }}>
          {view.length === 0 ? (
            <EmptyState icon="⧉" title="暂无截图" hint="在主机终端或右键菜单下发 screenshot_bof" />
          ) : (
            <table>
              <thead>
                <tr><th>ID</th><th>用户</th><th>主机</th><th>备注</th><th>时间</th><th style={{ width: 120 }}></th></tr>
              </thead>
              <tbody>
                {view.map((s: any, i) => {
                  const id = screenId(s)
                  return (
                    <tr key={id || i} className={previewId === id ? 'selected' : ''} onClick={() => openPreview(id)}>
                      <td className="mono">{id || '-'}</td>
                      <td>{screenUser(s)}</td>
                      <td>{screenComputer(s)}</td>
                      <td>{screenNote(s)}</td>
                      <td className="dim">{screenDate(s)}</td>
                      <td style={{ display: 'flex', gap: 6 }}>
                        <button onClick={(e) => { e.stopPropagation(); openPreview(id) }}>预览</button>
                        <button className="danger" onClick={async (e) => {
                          e.stopPropagation()
                          if (!id || !confirm('删除截图？')) return
                          try {
                            await api.post('/api/screen/remove', { screen_id_array: [id] })
                            pushToast('warn', '截图已删除')
                            await refresh()
                            if (previewId === id) { setPreview(null); setPreviewId(null) }
                          } catch (err: any) { pushLog('err', err.message || String(err)) }
                        }}>删</button>
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          )}
        </div>
        {(preview || previewErr) && (
          <div className="screen-preview">
            <div className="screen-preview-hd">
              <span className="mono dim">{previewId || ''}</span>
              <button className="ghost" onClick={() => { setPreview(null); setPreviewId(null); setPreviewErr('') }}>关闭预览</button>
            </div>
            <div className="screen-preview-bd">
              {previewErr ? <div className="err" style={{ padding: 16 }}>{previewErr}</div> : null}
              {preview ? <img src={preview} alt="screenshot" style={{ maxWidth: '100%', height: 'auto', background: '#111' }} /> : null}
            </div>
          </div>
        )}
      </div>
    </div>
  )
}

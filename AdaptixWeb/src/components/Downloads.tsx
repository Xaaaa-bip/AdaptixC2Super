import { useMemo, useState } from 'react'
import { useStore } from '../store'
import { copyText, fmtSize } from '../utils'
import { EmptyState, PageShell } from './ui'
import { PageTips } from './PageTips'

function buildCurl(url: string) {
  return `curl -k -L -o payload.bin "${url}"`
}
function buildWget(url: string) {
  return `wget --no-check-certificate -O payload.bin "${url}"`
}

export function Downloads() {
  const downloads = useStore((s) => s.downloads)
  const api = useStore((s) => s.api)
  const refreshRest = useStore((s) => s.refreshRest)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const [q, setQ] = useState('')
  const [busyId, setBusyId] = useState<string | null>(null)
  const [ctx, setCtx] = useState<{ x: number; y: number; id: string; name?: string } | null>(null)

  const list = useMemo(() => {
    let arr = Object.values(downloads)
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((d) =>
        [d.d_filename, d.d_file, d.d_agent_id, d.d_user, d.d_computer, d.d_file_id]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
    }
    return arr
  }, [downloads, q])

  async function doDownload(fileId: string, fallbackName?: string) {
    setBusyId(fileId)
    try {
      const res = await api.downloadSync(fileId)
      if (!res.content) throw new Error('空内容')
      const bin = atob(res.content)
      const bytes = new Uint8Array(bin.length)
      for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
      const url = URL.createObjectURL(new Blob([bytes]))
      const a = document.createElement('a')
      a.href = url
      a.download = res.filename || fallbackName || 'file.bin'
      a.click()
      URL.revokeObjectURL(url)
      pushToast('ok', `已下载 ${a.download}`)
    } catch (e: any) {
      pushLog('err', e.message || String(e))
      pushToast('err', e.message || String(e))
    } finally {
      setBusyId(null)
    }
  }

  async function copySyncCmd(kind: 'curl' | 'wget', fileId: string) {
    // Web 侧无 OTP 公共直链时，给出「先 sync 再本地」说明 + 占位
    // 对齐 Qt：生成可复制的 curl/wget；若服务端有 OTP 下载 URL 可再增强
    try {
      const res = await api.downloadSync(fileId)
      if (!res.content) throw new Error('空内容')
      // 无法给外部 OTP URL 时，复制「已同步内容」提示不合适；改为下载并提示
      // 折中：生成 data-url 过大；复制指令模板 + fileId
      const base = (api as any).baseUrl || location.origin
      const hint = `${kind === 'curl' ? buildCurl(`${base}/api/download/sync`) : buildWget(`${base}/api/download/sync`)}\n# file_id=${fileId} （Web 使用 POST+JWT；浏览器请点「同步到本机」）`
      const ok = await copyText(hint)
      pushToast(ok ? 'ok' : 'err', ok ? `已复制 ${kind} 模板` : '复制失败')
      // 同时触发本机下载，行为接近 Sync
      await doDownload(fileId)
    } catch (e: any) {
      pushToast('err', e.message || String(e))
    }
  }

  async function doDelete(fileId: string) {
    if (!confirm(`删除下载记录 ${fileId}？`)) return
    try {
      await api.downloadDelete([fileId])
      pushToast('ok', '已删除')
      await refreshRest()
    } catch (e: any) {
      pushToast('err', e.message || String(e))
    }
  }

  return (
    <PageShell
      toolbar={
        <>
          <input type="search" placeholder="搜索文件名 / 主机 / 用户" value={q} onChange={(e) => setQ(e.target.value)} />
          <div className="stats"><span>{list.length} 个文件</span></div>
          <button onClick={() => refreshRest().catch((e) => pushLog('err', String(e)))}>刷新</button>
          <PageTips id="downloads">
            <p>agent 回传的文件在此统一管理；「同步到本机」下载到浏览器。</p>
            <p>右键可复制 <code>curl/wget</code> 下载命令（含 file_id 说明）。</p>
          </PageTips>
        </>
      }
    >
      {list.length === 0 ? (
        <EmptyState icon="⇩" title="暂无下载文件" hint="在文件管理中对目标文件点「下载」" />
      ) : (
        <table>
          <thead>
            <tr>
              <th>文件名</th>
              <th>主机</th>
              <th>用户</th>
              <th>计算机</th>
              <th>大小</th>
              <th style={{ width: 220 }}></th>
            </tr>
          </thead>
          <tbody>
            {list.map((d) => {
              const name = d.d_filename || d.d_file || d.d_file_id
              return (
                <tr
                  key={d.d_file_id}
                  onContextMenu={(e) => {
                    e.preventDefault()
                    if (d.d_file_id) setCtx({ x: e.clientX, y: e.clientY, id: d.d_file_id, name })
                  }}
                >
                  <td title={name}>{name}</td>
                  <td className="mono">{d.d_agent_id || '-'}</td>
                  <td>{d.d_user || '-'}</td>
                  <td>{d.d_computer || '-'}</td>
                  <td className="mono">{fmtSize(d.d_size)}</td>
                  <td style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
                    <button
                      className="primary"
                      disabled={!d.d_file_id || busyId === d.d_file_id}
                      onClick={() => d.d_file_id && doDownload(d.d_file_id, name)}
                    >
                      {busyId === d.d_file_id ? '…' : '同步到本机'}
                    </button>
                    <button disabled={!d.d_file_id} onClick={() => d.d_file_id && void copySyncCmd('curl', d.d_file_id)}>curl</button>
                    <button disabled={!d.d_file_id} onClick={() => d.d_file_id && void copySyncCmd('wget', d.d_file_id)}>wget</button>
                    <button className="danger" disabled={!d.d_file_id} onClick={() => d.d_file_id && void doDelete(d.d_file_id)}>删除</button>
                  </td>
                </tr>
              )
            })}
          </tbody>
        </table>
      )}

      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 180), top: Math.min(ctx.y, window.innerHeight - 160) }}
          onClick={(e) => e.stopPropagation()}
          onMouseLeave={() => setCtx(null)}
        >
          <div className="ctx-hd">{ctx.name || ctx.id}</div>
          <button type="button" onClick={() => { void doDownload(ctx.id, ctx.name); setCtx(null) }}>同步到本机</button>
          <button type="button" onClick={() => { void copySyncCmd('curl', ctx.id); setCtx(null) }}>Sync as curl</button>
          <button type="button" onClick={() => { void copySyncCmd('wget', ctx.id); setCtx(null) }}>Sync as wget</button>
          <div className="sep" />
          <button type="button" className="danger" onClick={() => { void doDelete(ctx.id); setCtx(null) }}>Delete</button>
        </div>
      )}
    </PageShell>
  )
}

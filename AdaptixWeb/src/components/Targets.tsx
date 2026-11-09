import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { EmptyState, PageShell } from './ui'
import { PageTips } from './PageTips'

export function Targets() {
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const [list, setList] = useState<any[]>([])
  const [show, setShow] = useState(false)
  const [busy, setBusy] = useState(false)
  const [q, setQ] = useState('')
  const [computer, setComputer] = useState('')
  const [domain, setDomain] = useState('')
  const [address, setAddress] = useState('')
  const [info, setInfo] = useState('')
  const [tag, setTag] = useState('')

  async function refresh() {
    try {
      const data = await api.get('/api/targets/list')
      const arr = Array.isArray(data) ? data : (data?.data || data?.targets || [])
      setList(Array.isArray(arr) ? arr : [])
    } catch (e: any) { pushLog('err', e.message || String(e)) }
  }
  useEffect(() => { refresh() }, [])

  const view = useMemo(() => {
    if (!q.trim()) return list
    const s = q.trim().toLowerCase()
    return list.filter((t) =>
      [t.t_computer, t.computer, t.t_domain, t.domain, t.t_address, t.address, t.t_tag, t.tag, t.t_info, t.info]
        .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
  }, [list, q])

  return (
    <>
      <PageShell
        toolbar={
          <>
            <button className="primary" onClick={() => setShow(true)}>{t('+ 添加目标')}</button>
            <input type="search" placeholder={t('搜索计算机 / IP / 标签')} value={q} onChange={(e) => setQ(e.target.value)} />
            <button onClick={() => refresh()}>{t('刷新')}</button>
            <div className="stats"><span>{view.length} {t('个目标')}</span></div>
            <PageTips id="targets">
              <p>{t('目标资产登记：计算机名/域/地址/标签/备注。')}</p>
              <p>{t('仅团队服务器侧记录，与 agent 无关。')}</p>
            </PageTips>
          </>
        }
      >
        {view.length === 0 ? (
          <EmptyState icon="◎" title={t('暂无目标')} hint={t('可记录内网资产与备注')} />
        ) : (
          <table className="tgt-table">
            <thead>
              <tr><th>{t('计算机')}</th><th>{t('域')}</th><th>{t('地址')}</th><th>{t('系统')}</th><th>{t('标签')}</th><th>{t('备注')}</th><th></th></tr>
            </thead>
            <tbody>
              {view.map((tg: any, i) => {
                const id = tg.t_target_id || tg.target_id || tg.id
                return (
                  <tr key={id || i}>
                    <td>{tg.t_computer || tg.computer || '-'}</td>
                    <td>{tg.t_domain || tg.domain || ''}</td>
                    <td className="mono">{tg.t_address || tg.address || ''}</td>
                    <td>{tg.t_os_desk || tg.os_desk || tg.os || ''}</td>
                    <td>{tg.t_tag || tg.tag || ''}</td>
                    <td>{tg.t_info || tg.info || ''}</td>
                    <td>
                      {/* 按钮文案原来是「删」一个字 —— 它既是按钮文字又是这一格唯一的标签，
                          而表头那格又是空的，整个操作列在页面上没有名字。补全文案，
                          并用 title 说清删的是什么（兼作无障碍名）。 */}
                      <button
                        className="danger"
                        title="从目标列表移除（只删团队服务器侧的登记，不影响主机）"
                        onClick={async () => {
                        if (!id || !confirm(t('删除目标？'))) return
                        try {
                          await api.post('/api/targets/remove', { target_id_array: [id] })
                          await refresh()
                        } catch (e: any) { pushLog('err', e.message || String(e)) }
                      }}>{t('删除')}</button>
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
            <h2>{t('添加目标')}</h2>
            <div className="field"><label>{t('计算机')}</label><input value={computer} onChange={(e) => setComputer(e.target.value)} /></div>
            <div className="field"><label>{t('域')}</label><input value={domain} onChange={(e) => setDomain(e.target.value)} /></div>
            <div className="field"><label>{t('地址')}</label><input value={address} onChange={(e) => setAddress(e.target.value)} /></div>
            <div className="field"><label>{t('标签')}</label><input value={tag} onChange={(e) => setTag(e.target.value)} /></div>
            <div className="field"><label>{t('备注')}</label><input value={info} onChange={(e) => setInfo(e.target.value)} /></div>
            <div className="btns">
              <button disabled={busy} onClick={() => setShow(false)}>{t('取消')}</button>
              <button className="primary" disabled={busy || !computer} onClick={async () => {
                setBusy(true)
                try {
                  await api.post('/api/targets/add', {
                    t_computer: computer,
                    t_domain: domain,
                    t_address: address,
                    t_tag: tag,
                    t_info: info,
                  })
                  pushToast('ok', '目标已添加')
                  setShow(false)
                  setComputer(''); setDomain(''); setAddress(''); setTag(''); setInfo('')
                  await refresh()
                } catch (e: any) {
                  pushLog('err', e.message || String(e))
                } finally { setBusy(false) }
              }}>{t('保存')}</button>
            </div>
          </div>
        </div>
      )}
    </>
  )
}

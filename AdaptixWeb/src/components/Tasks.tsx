import { useMemo, useState } from 'react'
import { useStore } from '../store'
import { copyText, fmtRelative, fmtTime } from '../utils'
import { EmptyState, PageShell } from './ui'
import { PageTips } from './PageTips'

export function Tasks() {
  const tasks = useStore((s) => s.tasks)
  const selected = useStore((s) => s.selectedAgentId)
  const agents = useStore((s) => s.agents) || {}
  const refreshRest = useStore((s) => s.refreshRest)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const openConsole = useStore((s) => s.openConsole)
  const selectAgent = useStore((s) => s.selectAgent)
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)

  const [q, setQ] = useState('')
  const [onlyRunning, setOnlyRunning] = useState(false)
  const [onlySelected, setOnlySelected] = useState(!!selected)
  const [detail, setDetail] = useState<any | null>(null)

  const list = useMemo(() => {
    let arr = onlySelected && selected ? tasks.filter((t) => t.a_id === selected) : tasks
    if (onlyRunning) arr = arr.filter((t) => !t.a_completed)
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((t) =>
        [t.a_task_id, t.a_id, t.a_client, t.a_cmdline, t.a_message, t.a_text, t.a_computer]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
    }
    return arr
  }, [tasks, selected, onlySelected, onlyRunning, q])

  const running = tasks.filter((t) => !t.a_completed).length

  return (
    <>
      <PageShell
        toolbar={
          <>
            <input type="search" placeholder="搜索任务 / 命令 / 主机" value={q} onChange={(e) => setQ(e.target.value)} />
            <label className="muted check-inline">
              <input type="checkbox" checked={onlySelected} onChange={(e) => setOnlySelected(e.target.checked)} /> 仅当前主机
            </label>
            <label className="muted check-inline">
              <input type="checkbox" checked={onlyRunning} onChange={(e) => setOnlyRunning(e.target.checked)} /> 仅执行中
            </label>
            {selected && onlySelected && (
              <span className="muted mono">{agents[selected]?.a_computer || selected}</span>
            )}
            <div className="stats">
              <span>{list.length}</span>
              <span className="warn">执行中 {running}</span>
            </div>
            <button onClick={() => refreshRest().catch((e) => pushLog('err', String(e)))}>刷新</button>
            <PageTips id="tasks">
              <p><b>双击</b>任务跳转到对应主机控制台；单击查看详情与输出。</p>
              <p>筛选：仅当前主机 / 仅执行中。</p>
              <p>BOF / 下载 / run 等长任务以 <code>job</code> 形式显示（控制台 <code>job list</code> 可查）。</p>
            </PageTips>
          </>
        }
      >
        {list.length === 0 ? (
          <EmptyState icon="☰" title="暂无任务" hint="在主机终端执行命令后会出现在这里" action={
            <button onClick={() => { setTab('sessions'); openUiTab('sessions') }}>去主机列表</button>
          } />
        ) : (
          <table>
            <thead>
              <tr>
                <th style={{ width: 72 }}>状态</th>
                <th>命令</th>
                <th>主机</th>
                <th>操作员</th>
                <th>时间</th>
                <th>摘要</th>
              </tr>
            </thead>
            <tbody>
              {list.map((t, i) => (
                <tr key={t.a_task_id || i} onClick={() => setDetail(t)}
                  onDoubleClick={() => {
                    if (t.a_id) {
                      selectAgent(t.a_id)
                      openConsole(t.a_id)
                      setTab('sessions')
                      openUiTab('sessions')
                    }
                  }}>
                  <td>
                    {/* 原来这里复用的是主机在线用的 .tag.online（绿色）。
                        两个问题：
                          1. 语义串了 —— 绿色在这个界面里的职责是"状态点"，
                             主机在线是机器的状态；"任务完成"是事件的结果，
                             借用同一个绿会让两件事看起来是同一类；
                          2. 47 行全绿等于没有信息 —— 铺满的常态色只会让人
                             对颜色脱敏，真正需要跳出来的"执行中"反而被淹没。
                        现在：完成（常态）用中性，执行中用青（青 = 活的/进行中），
                        琥珀留给真正的警告。 */}
                    {t.a_completed
                      ? <span className="tag done">完成</span>
                      : <span className="tag running">执行中</span>}
                  </td>
                  <td className="mono" title={t.a_cmdline}>{t.a_cmdline || '-'}</td>
                  <td>
                    <div className="cell-main">{t.a_computer || agents[t.a_id || '']?.a_computer || '-'}</div>
                    <div className="cell-sub mono">{t.a_id || ''}</div>
                  </td>
                  <td>{t.a_client || '-'}</td>
                  <td className="dim" title={fmtTime(t.a_start_time)}>{fmtRelative(t.a_start_time)}</td>
                  <td className="mono dim">{(t.a_message || t.a_text || '').slice(0, 80)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </PageShell>

      {detail && (
        <div className="mask" onClick={() => setDetail(null)}>
          <div className="dlg dlg-lg" style={{ height: 'auto', maxHeight: '86vh' }} onClick={(e) => e.stopPropagation()}>
            <div className="dlg-hd">
              <div>
                <h2>任务详情</h2>
                <div className="dim mono" style={{ fontSize: 12 }}>{detail.a_task_id || '-'}</div>
              </div>
              <button onClick={() => setDetail(null)}>关闭</button>
            </div>
            <div className="kv">
              <div><span className="k">主机</span><span className="mono">{detail.a_id || '-'}</span></div>
              <div><span className="k">操作员</span><span>{detail.a_client || '-'}</span></div>
              <div><span className="k">状态</span><span>{detail.a_completed ? '完成' : '执行中'}</span></div>
              <div><span className="k">开始</span><span>{fmtTime(detail.a_start_time)}</span></div>
              <div><span className="k">结束</span><span>{fmtTime(detail.a_finish_time)}</span></div>
              <div className="full"><span className="k">命令</span><span className="mono">{detail.a_cmdline || '-'}</span></div>
            </div>
            <div className="detail-box">
              <div className="detail-hd">
                <span>输出</span>
                <button className="ghost" onClick={async () => {
                  const text = [detail.a_message, detail.a_text].filter(Boolean).join('\n')
                  const ok = await copyText(text)
                  pushToast(ok ? 'ok' : 'err', ok ? '已复制输出' : '复制失败')
                }}>复制</button>
              </div>
              <pre>{[detail.a_message, detail.a_text].filter(Boolean).join('\n') || '(无输出)'}</pre>
            </div>
            {detail.a_id && (
              <div className="btns">
                <button onClick={() => {
                  selectAgent(detail.a_id)
                  openConsole(detail.a_id)
                  setTab('sessions')
                  openUiTab('sessions')
                  setDetail(null)
                }}>打开终端</button>
              </div>
            )}
          </div>
        </div>
      )}
    </>
  )
}

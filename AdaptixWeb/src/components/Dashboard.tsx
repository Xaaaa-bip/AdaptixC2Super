import { useMemo } from 'react'
import { useStore } from '../store'
import { fmtRelative, isAgentOnline } from '../utils'
import { ConnStatus } from './StatusPill'

export function Dashboard({ online, total, listeners }: { online: number; total: number; listeners: number }) {
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)
  const openConsole = useStore((s) => s.openConsole)
  const selectAgent = useStore((s) => s.selectAgent)
  const logs = useStore((s) => s.logs).slice(0, 8)
  const agents = useStore((s) => s.agents) || {}
  const listenerMap = useStore((s) => s.listeners) || {}
  const tasks = useStore((s) => s.tasks) || []
  const downloads = useStore((s) => s.downloads) || {}
  const wsConnected = useStore((s) => s.wsConnected)

  const recentAgents = useMemo(() => {
    return Object.values(agents)
      .sort((a, b) => (b.a_last_tick || 0) - (a.a_last_tick || 0))
      .slice(0, 7)
  }, [agents])

  const recentTasks = useMemo(() => tasks.slice(0, 6), [tasks])
  const dlCount = Object.keys(downloads).length
  const listenerList = useMemo(() => Object.values(listenerMap).slice(0, 5), [listenerMap])
  const pendingTasks = useMemo(() => tasks.filter((t) => t.a_completed === false).length, [tasks])

  function go(id: any) {
    setTab(id)
    openUiTab(id)
  }

  function openAgent(id: string) {
    selectAgent(id)
    openConsole(id)
    go('sessions')
  }

  return (
    <div className="home">
      <header className="home-head">
        <div>
          <h1 className="home-title">概览</h1>
          <p className="home-desc">
            {online > 0
              ? `${online} 台主机在线，可直接进入会话。`
              : total > 0
                ? '有历史主机，当前全部离线。'
                : '从监听与载荷开始，完成第一次上线。'}
          </p>
        </div>
        <div className="home-actions">
          <button type="button" className="primary" onClick={() => go('sessions')}>主机</button>
          <button type="button" onClick={() => go('delivery')}>一键投递</button>
          <button type="button" onClick={() => go('generate')}>生成载荷</button>
          <button type="button" onClick={() => go('listeners')}>监听器</button>
          <button type="button" onClick={() => go('graph')}>拓扑</button>
          <button type="button" onClick={() => go('chat')}>聊天</button>
        </div>
      </header>

      <div className="home-metrics">
        <button type="button" className="hm" onClick={() => go('sessions')}>
          <span className="hm-l">主机</span>
          <span className="hm-n">{total}</span>
          <span className="hm-s"><em>{online}</em> 在线</span>
        </button>
        <button type="button" className="hm" onClick={() => go('listeners')}>
          <span className="hm-l">监听</span>
          <span className="hm-n">{listeners}</span>
          <span className="hm-s">回调入口</span>
        </button>
        <button type="button" className="hm" onClick={() => go('tasks')}>
          <span className="hm-l">任务</span>
          <span className="hm-n">{tasks.length}</span>
          <span className="hm-s">{pendingTasks ? `${pendingTasks} 进行中` : '空闲'} · 下载 {dlCount}</span>
        </button>
        <div className={`hm static hm-link ${wsConnected ? 'is-live' : 'is-rest'}`}>
          <span className="hm-l">通道</span>
          <div className="hm-conn">
            <ConnStatus connected={true} wsConnected={wsConnected} syncing={false} />
          </div>
          <span className="hm-s">
            {wsConnected ? '事件即时推送' : 'REST 约 15 秒轮询'}
          </span>
        </div>
      </div>

      <div className="home-grid">
        <section className="home-panel wide">
          <div className="home-phd">
            <h2>最近主机</h2>
            <button type="button" className="ghost" onClick={() => go('sessions')}>查看全部</button>
          </div>
          {recentAgents.length === 0 ? (
            <div className="home-empty">
              <p>还没有主机</p>
              <button type="button" className="primary" onClick={() => go('generate')}>生成载荷</button>
            </div>
          ) : (
            <div className="home-rows">
              {recentAgents.map((a) => {
                const live = isAgentOnline(a)
                return (
                  <button type="button" key={a.a_id} className="home-row" onClick={() => openAgent(a.a_id)}>
                    <span className={`sdot ${live ? 'on' : 'off'}`} />
                    <div className="meta">
                      <div className="name">{a.a_computer || a.a_id}</div>
                      <div className="sub mono">{a.a_internal_ip || a.a_external_ip || '-'} · {a.a_username || '-'}</div>
                    </div>
                    <span className="time mono">{fmtRelative(a.a_last_tick)}</span>
                  </button>
                )
              })}
            </div>
          )}
        </section>

        <section className="home-panel">
          <div className="home-phd">
            <h2>监听器</h2>
            <button type="button" className="ghost" onClick={() => go('listeners')}>管理</button>
          </div>
          {listenerList.length === 0 ? (
            <div className="home-empty sm">
              <p>暂无监听器</p>
              <button type="button" onClick={() => go('listeners')}>创建</button>
            </div>
          ) : (
            <div className="home-rows">
              {listenerList.map((l) => (
                <button type="button" key={l.l_name} className="home-row" onClick={() => go('listeners')}>
                  <span className="pill">{l.l_status || '—'}</span>
                  <div className="meta">
                    <div className="name mono">{l.l_name}</div>
                    <div className="sub">{l.l_bind_host || '-'}:{l.l_bind_port || '-'}</div>
                  </div>
                </button>
              ))}
            </div>
          )}
        </section>

        <section className="home-panel">
          <div className="home-phd">
            <h2>动态</h2>
            <button type="button" className="ghost" onClick={() => go('logs')}>日志</button>
          </div>
          <div className="home-rows">
            {recentTasks.slice(0, 3).map((t, i) => (
              <button type="button" key={t.a_task_id || i} className="home-row" onClick={() => go('tasks')}>
                <span className={`pill ${t.a_completed ? 'ok' : 'warn'}`}>{t.a_completed ? '完成' : '执行'}</span>
                <div className="meta">
                  <div className="name mono">{(t.a_cmdline || '—').slice(0, 36)}</div>
                  <div className="sub mono">{fmtRelative(t.a_start_time)}</div>
                </div>
              </button>
            ))}
            {logs.slice(0, 4).map((l) => (
              <div key={l.id} className="home-row static">
                <span className={`lv ${l.level}`}>{l.level}</span>
                <div className="meta">
                  <div className="name">{l.text}</div>
                  <div className="sub mono">{new Date(l.ts).toLocaleTimeString()}</div>
                </div>
              </div>
            ))}
            {recentTasks.length === 0 && logs.length === 0 && (
              <div className="home-empty sm"><p>暂无动态</p></div>
            )}
          </div>
        </section>
      </div>
    </div>
  )
}

import { useStore } from '../store'
import { ConnStatus } from './StatusPill'

export function Dashboard({ online, total, listeners }: { online: number; total: number; listeners: number }) {
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)
  const tasks = useStore((s) => s.tasks) || []
  const wsConnected = useStore((s) => s.wsConnected)

  function go(id: 'sessions' | 'delivery') {
    setTab(id)
    openUiTab(id)
  }

  return (
    <div className="home overview">
      <header className="home-head">
        <div>
          <h1 className="home-title">概览</h1>
          <p className="home-desc">控制台状态和连接信息。</p>
        </div>
        <div className="home-actions">
          <button type="button" onClick={() => go('sessions')}>主机</button>
          <button type="button" className="primary" onClick={() => go('delivery')}>一键投递</button>
        </div>
      </header>

      <div className="overview-state" aria-label="控制台状态">
        <ConnStatus connected={true} wsConnected={wsConnected} syncing={false} />
        <span><b>{online}</b> / {total} 在线</span>
        <span>{listeners} 监听</span>
        <span>{tasks.length} 任务</span>
      </div>
    </div>
  )
}

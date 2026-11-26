import { useStore, type TabId } from '../store'
import { t } from '../i18n'
import { ConnStatus } from './StatusPill'

export function Dashboard({ online, total, listeners }: { online: number; total: number; listeners: number }) {
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)
  const tasks = useStore((s) => s.tasks) || []
  const wsConnected = useStore((s) => s.wsConnected)
  const syncing = useStore((s) => s.syncing)
  const username = useStore((s) => s.username)
  const runningTasks = tasks.filter((x) => !x.a_completed).length

  function go(id: TabId) {
    setTab(id)
    openUiTab(id)
  }

  return (
    <div className="qt-overview">
      <div className="qt-overview-head">
        <div>
          <div className="qt-overview-title">{t('状态概览')}</div>
          <div className="qt-overview-sub">
            {username || 'operator'} · {t('工作区实时运行状态与快捷入口')}
          </div>
        </div>
        <div className="qt-overview-actions">
          <ConnStatus connected={true} wsConnected={wsConnected} syncing={syncing} />
          <button type="button" onClick={() => go('sessions')}>{t('主机')}</button>
          <button type="button" onClick={() => go('listeners')}>{t('监听器')}</button>
          <button type="button" className="primary" onClick={() => go('delivery')}>{t('一键投递')}</button>
        </div>
      </div>

      <div className="qt-overview-grid">
        <button type="button" className="qt-stat-card" onClick={() => go('sessions')}>
          <span className="k">{t('在线主机')}</span>
          <span className="v mono"><b>{online}</b> / {total}</span>
          <span className="d">{t('点击定位 Sessions table')}</span>
        </button>
        <button type="button" className="qt-stat-card" onClick={() => go('listeners')}>
          <span className="k">{t('监听器')}</span>
          <span className="v mono"><b>{listeners}</b></span>
          <span className="d">{t('查看或新建 HTTP / WS / TCP 入口')}</span>
        </button>
        <button type="button" className="qt-stat-card" onClick={() => go('tasks')}>
          <span className="k">{t('任务队列')}</span>
          <span className="v mono"><b>{tasks.length}</b></span>
          <span className="d">{runningTasks > 0 ? `${runningTasks} ${t('执行中')}` : t('无运行中任务')}</span>
        </button>
        <button type="button" className="qt-stat-card" onClick={() => go('generate')}>
          <span className="k">{t('快速构建')}</span>
          <span className="v mono"><b>Generate</b></span>
          <span className="d">{t('配置并生成目标载荷或投递入口')}</span>
        </button>
      </div>
    </div>
  )
}

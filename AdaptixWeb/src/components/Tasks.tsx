import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { copyText, fmtTime } from '../utils'

/**
 * Desktop TasksWidget + TaskOutputWidget split view:
 * Left: compact QTableView with collapsible Ctrl+F search bar
 * Right: live Task Output panel (read-only header line + monospace body)
 */
export function Tasks() {
  const tasks = useStore((s) => s.tasks)
  const selected = useStore((s) => s.selectedAgentId)
  const agents = useStore((s) => s.agents) || {}
  const refreshRest = useStore((s) => s.refreshRest)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const openConsole = useStore((s) => s.openConsole)
  const selectAgent = useStore((s) => s.selectAgent)

  const [searchOpen, setSearchOpen] = useState(false)
  const [q, setQ] = useState('')
  const [onlyRunning, setOnlyRunning] = useState(false)
  const [onlySelected, setOnlySelected] = useState(false)
  const [activeId, setActiveId] = useState<string | null>(null)
  const searchRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    refreshRest({ includeTasks: true }).catch(() => {})
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const list = useMemo(() => {
    let arr = onlySelected && selected ? tasks.filter((x) => x.a_id === selected) : tasks
    if (onlyRunning) arr = arr.filter((x) => !x.a_completed)
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((x) =>
        [x.a_task_id, x.a_id, x.a_client, x.a_cmdline, x.a_message, x.a_text, x.a_computer]
          .filter(Boolean).some((v) => String(v).toLowerCase().includes(s)))
    }
    return arr
  }, [tasks, selected, onlySelected, onlyRunning, q])

  const activeTask = useMemo(() => {
    if (!list.length) return null
    if (activeId) {
      const hit = list.find((x) => x.a_task_id === activeId)
      if (hit) return hit
    }
    return list[0]
  }, [list, activeId])

  const running = tasks.filter((x) => !x.a_completed).length

  function onKeyDown(e: React.KeyboardEvent) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'f') {
      e.preventDefault()
      setSearchOpen(true)
      setTimeout(() => { searchRef.current?.focus(); searchRef.current?.select() }, 0)
    } else if (e.key === 'Escape' && searchOpen) {
      setSearchOpen(false)
    }
  }

  return (
    <div className="qt-tasks-split" tabIndex={0} onKeyDown={onKeyDown}>
      <div className="qt-tasks-left">
        {searchOpen && (
          <div className="qt-search-bar">
            <input
              ref={searchRef}
              className="qt-search-input"
              placeholder="filter tasks..."
              value={q}
              onChange={(e) => setQ(e.target.value)}
              onKeyDown={(e) => { if (e.key === 'Escape') setSearchOpen(false) }}
            />
            <label className="qt-check-inline">
              <input type="checkbox" checked={onlySelected} onChange={(e) => setOnlySelected(e.target.checked)} />
              <span>{t('仅当前主机')}</span>
            </label>
            <label className="qt-check-inline">
              <input type="checkbox" checked={onlyRunning} onChange={(e) => setOnlyRunning(e.target.checked)} />
              <span>{t('仅执行中')} ({running})</span>
            </label>
            <button type="button" onClick={() => refreshRest({ includeTasks: true }).catch((e) => pushLog('err', String(e)))}>
              {t('刷新')}
            </button>
            <button type="button" className="qt-search-x" onClick={() => setSearchOpen(false)}>×</button>
          </div>
        )}

        <div className="qt-table-wrap">
          {list.length === 0 ? (
            <div className="qt-table-empty">
              <div>{t('暂无任务')}</div>
              <button type="button" onClick={() => refreshRest({ includeTasks: true }).catch((e) => pushLog('err', String(e)))}>
                {t('刷新')}
              </button>
            </div>
          ) : (
            <table className="qt-table">
              <thead>
                <tr>
                  <th>Task ID</th>
                  <th>Status</th>
                  <th>Agent</th>
                  <th>Client</th>
                  <th>Computer</th>
                  <th>Start Time</th>
                  <th className="align-l">CommandLine</th>
                </tr>
              </thead>
              <tbody>
                {list.map((tk, i) => {
                  const id = tk.a_task_id || String(i)
                  const isSel = activeTask?.a_task_id === tk.a_task_id
                  const comp = tk.a_computer || (tk.a_id ? agents[tk.a_id]?.a_computer : '') || '-'
                  return (
                    <tr
                      key={id}
                      className={isSel ? 'selected' : ''}
                      onClick={() => setActiveId(tk.a_task_id || null)}
                      onDoubleClick={() => {
                        if (tk.a_id) {
                          selectAgent(tk.a_id)
                          openConsole(tk.a_id)
                        }
                      }}
                    >
                      <td className="mono">{tk.a_task_id || '-'}</td>
                      <td style={{ color: tk.a_completed ? 'var(--qt-ok)' : 'var(--qt-warn)', fontWeight: 600 }}>
                        {tk.a_completed ? 'Done' : 'Running'}
                      </td>
                      <td className="mono">{tk.a_id || '-'}</td>
                      <td>{tk.a_client || '-'}</td>
                      <td>{comp}</td>
                      <td className="mono">{fmtTime(tk.a_start_time)}</td>
                      <td className="align-l mono" title={tk.a_cmdline}>{tk.a_cmdline || '-'}</td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          )}
        </div>
      </div>

      {/* Right pane: TaskOutputWidget */}
      <div className="qt-tasks-right">
        <div className="qt-task-out-hd">
          <input
            className="qt-task-out-msg"
            readOnly
            value={activeTask ? `[${activeTask.a_task_id || '-'}] ${activeTask.a_cmdline || activeTask.a_message || ''}` : 'Task Output'}
          />
          <button
            type="button"
            disabled={!activeTask}
            onClick={async () => {
              if (!activeTask) return
              const txt = [activeTask.a_message, activeTask.a_text].filter(Boolean).join('\n')
              const ok = await copyText(txt)
              pushToast(ok ? 'ok' : 'err', ok ? t('已复制输出') : t('复制失败'))
            }}
          >
            {t('复制')}
          </button>
          {activeTask?.a_id && (
            <button
              type="button"
              onClick={() => {
                selectAgent(activeTask.a_id!)
                openConsole(activeTask.a_id!)
              }}
            >
              Console
            </button>
          )}
        </div>
        <pre className="qt-task-out-body">
          {activeTask
            ? ([activeTask.a_message, activeTask.a_text].filter(Boolean).join('\n') || '(empty)')
            : t('选择左侧任务查看输出 (Ctrl+F 搜索)')}
        </pre>
      </div>
    </div>
  )
}

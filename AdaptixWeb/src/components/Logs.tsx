import { useMemo, useState } from 'react'
import { useStore } from '../store'
import { copyText } from '../utils'
import { EmptyState } from './ui'

export function Logs() {
  const logs = useStore((s) => s.logs)
  const pushToast = useStore((s) => s.pushToast)
  const [level, setLevel] = useState<'all' | 'info' | 'ok' | 'warn' | 'err'>('all')
  const [q, setQ] = useState('')

  const list = useMemo(() => {
    let arr = logs
    if (level !== 'all') arr = arr.filter((l) => l.level === level)
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((l) => l.text.toLowerCase().includes(s))
    }
    return arr
  }, [logs, level, q])

  return (
    <div className="page">
      <div className="toolbar">
        <input type="search" placeholder="搜索日志" value={q} onChange={(e) => setQ(e.target.value)} />
        <div className="chip-row">
          {(['all', 'info', 'ok', 'warn', 'err'] as const).map((lv) => (
            <button key={lv} type="button" className={level === lv ? 'primary' : ''} onClick={() => setLevel(lv)}>
              {lv === 'all' ? '全部' : lv}
            </button>
          ))}
        </div>
        <div className="stats"><span>{list.length} / {logs.length}</span></div>
        <button onClick={async () => {
          const text = list.map((l) => `${new Date(l.ts).toISOString()} [${l.level}] ${l.text}`).join('\n')
          const ok = await copyText(text)
          pushToast(ok ? 'ok' : 'err', ok ? '日志已复制' : '复制失败')
        }}>复制</button>
      </div>
      <div className="log-page">
        {list.length === 0 && <EmptyState icon="≡" title="暂无系统日志" hint="操作与同步事件会出现在这里" />}
        {list.map((l) => (
          <div key={l.id} className={`it ${l.level}`}>
            <span className="dim">{new Date(l.ts).toLocaleTimeString()} </span>
            <span className={`lv ${l.level}`}>[{l.level}]</span> {l.text}
          </div>
        ))}
      </div>
    </div>
  )
}

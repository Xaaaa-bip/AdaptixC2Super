import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'

/**
 * 底部常驻事件日志 dock（客户端 LogsDock 的 Web 形态）。
 *
 * 与「日志」页面（Logs.tsx）并存：页面是完整版（复制/导出），dock 是
 * 常驻迷你流 —— 操作员在任何页面都能看到事件滚动。两者共用 store.logs。
 *
 * 自动滚动的规则：贴底时新日志自动滚下来；操作员往上翻了就**停**
 * （否则读不到历史），出现「回到最新」按钮，点一下或手动滚回底部再恢复。
 */
export function EventsDock() {
  const logs = useStore((s) => s.logs)
  const [level, setLevel] = useState<'all' | 'info' | 'ok' | 'warn' | 'err'>('all')
  const [q, setQ] = useState('')
  const [atBottom, setAtBottom] = useState(true)
  const listRef = useRef<HTMLDivElement>(null)
  const stickRef = useRef(true)

  const list = useMemo(() => {
    let arr = logs
    if (level !== 'all') arr = arr.filter((l) => l.level === level)
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((l) => l.text.toLowerCase().includes(s))
    }
    // dock 里只留最近 500 条：这是「盯着看」的地方，不是审计台账
    return arr.slice(-500)
  }, [logs, level, q])

  // 贴底跟随：stickRef 由滚动事件维护，只有本来就在底部的才跟
  useEffect(() => {
    const el = listRef.current
    if (el && stickRef.current) el.scrollTop = el.scrollHeight
  }, [list])

  function onScroll() {
    const el = listRef.current
    if (!el) return
    const bottom = el.scrollHeight - el.scrollTop - el.clientHeight < 8
    stickRef.current = bottom
    setAtBottom(bottom)
  }

  return (
    <>
      <div className="ev-bar">
        <input type="search" placeholder="过滤事件…" value={q} onChange={(e) => setQ(e.target.value)} />
        <div className="chip-row">
          {(['all', 'info', 'ok', 'warn', 'err'] as const).map((lv) => (
            <button key={lv} type="button" className={level === lv ? 'primary' : ''} onClick={() => setLevel(lv)}>
              {lv === 'all' ? '全部' : lv}
            </button>
          ))}
        </div>
        <div className="stats">{list.length} / {logs.length}</div>
        {!atBottom && (
          <button type="button" onClick={() => {
            const el = listRef.current
            if (el) el.scrollTop = el.scrollHeight
            stickRef.current = true
            setAtBottom(true)
          }}>↓ 最新</button>
        )}
      </div>
      <div className="ev-list" ref={listRef} onScroll={onScroll}>
        {list.length === 0 && <div className="ev-empty">暂无事件 — 操作与同步事件会出现在这里</div>}
        {list.map((l) => (
          <div key={l.id} className={`ev-line lv-${l.level}`}>
            <span className="t">{new Date(l.ts).toLocaleTimeString()} </span>
            <span className="lv">[{l.level}]</span> {l.text}
          </div>
        ))}
      </div>
    </>
  )
}

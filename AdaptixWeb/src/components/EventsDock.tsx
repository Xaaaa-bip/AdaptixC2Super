import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { copyText } from '../utils'
import { loadQtSettings, type QtClientSettings } from '../design/qt-themes'

/**
 * Desktop LogsWidget: full-bleed monospace event console with collapsible Ctrl+F
 * search bar and configurable themed wallpaper overlay.
 */
export function EventsDock() {
  const logs = useStore((s) => s.logs)
  const pushToast = useStore((s) => s.pushToast)

  const [settings, setSettings] = useState<QtClientSettings>(() => loadQtSettings())
  const [searchOpen, setSearchOpen] = useState(false)
  const [q, setQ] = useState('')
  const [matchIdx, setMatchIdx] = useState(0)
  const [ctx, setCtx] = useState<{ x: number; y: number } | null>(null)

  const listRef = useRef<HTMLDivElement>(null)
  const searchRef = useRef<HTMLInputElement>(null)
  const stickRef = useRef(true)

  useEffect(() => {
    const onSettings = (e: Event) => {
      const detail = (e as CustomEvent<QtClientSettings>).detail
      setSettings(detail || loadQtSettings())
    }
    const closeCtx = () => setCtx(null)
    window.addEventListener('adaptix-qt-settings', onSettings)
    window.addEventListener('click', closeCtx)
    return () => {
      window.removeEventListener('adaptix-qt-settings', onSettings)
      window.removeEventListener('click', closeCtx)
    }
  }, [])

  const list = useMemo(() => logs.slice(-600), [logs])

  const matches = useMemo(() => {
    const s = q.trim().toLowerCase()
    if (!s) return [] as number[]
    const out: number[] = []
    list.forEach((l, i) => {
      if (l.text.toLowerCase().includes(s)) out.push(i)
    })
    return out
  }, [list, q])

  useEffect(() => {
    setMatchIdx(0)
  }, [q, matches.length])

  useEffect(() => {
    const el = listRef.current
    if (el && stickRef.current && settings.consoleAutoScroll) {
      el.scrollTop = el.scrollHeight
    }
  }, [list.length, settings.consoleAutoScroll])

  function onScroll() {
    const el = listRef.current
    if (!el) return
    stickRef.current = el.scrollHeight - el.scrollTop - el.clientHeight < 12
  }

  function jumpMatch(delta: number) {
    if (!matches.length) return
    const next = (matchIdx + delta + matches.length) % matches.length
    setMatchIdx(next)
    const row = listRef.current?.querySelector(`[data-idx="${matches[next]}"]`) as HTMLElement | null
    row?.scrollIntoView({ block: 'center' })
  }

  function onKeyDown(e: React.KeyboardEvent) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'f') {
      e.preventDefault()
      setSearchOpen(true)
      setTimeout(() => { searchRef.current?.focus(); searchRef.current?.select() }, 0)
    } else if (e.key === 'Escape' && searchOpen) {
      setSearchOpen(false)
      setQ('')
    }
  }

  function lineClass(level: string, text: string): string {
    if (level === 'err') return 'qt-c-err'
    if (level === 'warn') return 'qt-c-log-listener'
    if (/listener|监听/i.test(text)) return 'qt-c-log-listener'
    if (/tunnel|隧道/i.test(text)) return 'qt-c-log-tunnel'
    if (/agent|主机|online|上线|注册/i.test(text)) return 'qt-c-log-agent'
    if (level === 'ok') return 'qt-c-log-agent'
    return 'qt-c-log-conn'
  }

  const activeLineIndex = matches.length ? matches[matchIdx] : -1

  return (
    <div className="qt-console" tabIndex={0} onKeyDown={onKeyDown}>
      {searchOpen && (
        <div className="qt-console-search">
          <button type="button" onClick={() => jumpMatch(-1)} title="Previous">&lt;</button>
          <button type="button" onClick={() => jumpMatch(1)} title="Next">&gt;</button>
          <span className="qt-console-search-count">
            {matches.length ? `${matchIdx + 1} of ${matches.length}` : '0 of 0'}
          </span>
          <input
            ref={searchRef}
            placeholder="Find"
            value={q}
            onChange={(e) => setQ(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') { e.preventDefault(); jumpMatch(e.shiftKey ? -1 : 1) }
              if (e.key === 'Escape') { setSearchOpen(false); setQ('') }
            }}
          />
          <button type="button" onClick={() => { setSearchOpen(false); setQ('') }}>X</button>
        </div>
      )}

      <div
        className="qt-console-out-wrap"
        onContextMenu={(e) => {
          e.preventDefault()
          setCtx({ x: e.clientX, y: e.clientY })
        }}
      >
        <div className="qt-console-bg-img" aria-hidden="true" />
        <div className="qt-console-bg-dim" aria-hidden="true" />
        <div
          ref={listRef}
          className={`qt-console-out ${settings.consoleNoWrap ? 'nowrap' : ''}`}
          onScroll={onScroll}
        >
          {list.length === 0 && (
            <div className="qt-c-debug">{t('暂无事件 — 操作与同步事件会出现在这里')}</div>
          )}
          {list.map((l, i) => {
            const isHit = q.trim() ? matches.includes(i) : false
            const isCur = i === activeLineIndex
            const ts = new Date(l.ts).toLocaleTimeString('en-GB', { hour12: false })
            return (
              <div
                key={l.id}
                data-idx={i}
                className={`qt-c-line ${isCur ? 'hit-cur' : isHit ? 'hit' : ''}`}
              >
                {settings.consoleShowTime ? <span className="qt-c-time">[{ts}] </span> : null}
                <span className={lineClass(l.level, l.text)}>{l.text}</span>
              </div>
            )
          })}
        </div>
      </div>

      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 200), top: Math.min(ctx.y, window.innerHeight - 160) }}
          onClick={(e) => e.stopPropagation()}
        >
          <button type="button" onClick={() => {
            setSearchOpen(true); setCtx(null)
            setTimeout(() => searchRef.current?.focus(), 0)
          }}>
            <span>Find</span><span className="qt-menu-sc">Ctrl+F</span>
          </button>
          <button type="button" onClick={async () => {
            const text = list.map((l) => `[${new Date(l.ts).toLocaleTimeString('en-GB', { hour12: false })}] ${l.text}`).join('\n')
            const ok = await copyText(text)
            pushToast(ok ? 'ok' : 'err', ok ? t('已复制日志') : t('复制失败'))
            setCtx(null)
          }}>
            <span>Copy All</span>
          </button>
          <button type="button" onClick={() => {
            const el = listRef.current
            if (el) el.scrollTop = el.scrollHeight
            stickRef.current = true
            setCtx(null)
          }}>
            <span>Scroll to Bottom</span>
          </button>
        </div>
      )}
    </div>
  )
}

import { memo, useEffect, useMemo, useRef, useState } from 'react'
import { t } from '../i18n'
import type { ConsoleLine } from '../api/types'
import { cmdTabId, useStore } from '../store'
import { copyText, isAgentOnline } from '../utils'
import { FALLBACK_BOFS, FALLBACK_COMMANDS } from '../lib/completions'
import { useCompletion } from '../lib/useCompletion'
import { CmdSuggest } from './CmdSuggest'
import { loadQtSettings, saveQtSettings, type QtClientSettings } from '../design/qt-themes'

const EMPTY_CONSOLE_LINES: ConsoleLine[] = []

function fmtClock(ts?: number): string {
  if (!ts) return ''
  const d = new Date(ts)
  const p = (n: number) => String(n).padStart(2, '0')
  return `[${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}] `
}

function formatLineText(kind: string, text: string): string | null {
  const s = (text || '').trim()
  if (!s) return null
  if (kind === 'local') return s.startsWith('>') ? s : `> ${s}`
  return s
}

/**
 * Desktop ConsoleWidget:
 *  - Collapsible Ctrl+F search row (< > 0 of 0 [Find] X)
 *  - Full-height TextEditConsole with themed background overlay + syntax colors
 *  - Selectable InfoLabel ([id] user @ host.domain)
 *  - Single-line prompt (name >) + InputLineEdit with CmdSuggest autocomplete & history
 */
export const Term = memo(function Term({ agentId: propAgentId }: { agentId?: string } = {}) {
  const activeTab = useStore((s) => (propAgentId ? null : s.activeConsoleTab))
  const agentId = propAgentId ?? activeTab
  const agent = useStore((s) => (agentId ? s.agents[agentId] ?? null : null))
  const agentTick = useStore((s) => (agentId ? s.agentTicks[agentId] : undefined))
  const lines = useStore((s) => (agentId ? s.consoleByAgent[agentId] : undefined)) ?? EMPTY_CONSOLE_LINES
  const openTabs = useStore((s) => s.openConsoleTabs) || []
  const sendCommand = useStore((s) => s.sendCommand)
  const selectAgent = useStore((s) => s.selectAgent)
  const clearConsole = useStore((s) => s.clearConsole)
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)
  const openPty = useStore((s) => s.openPty)
  const openVnc = useStore((s) => s.openVnc)
  const openUiTab = useStore((s) => s.openUiTab)
  const pushToast = useStore((s) => s.pushToast)
  const runningTasks = useStore((s) => {
    if (!agentId) return 0
    let n = 0
    for (const x of s.tasks) {
      if (x.a_id === agentId && !x.a_completed) n++
    }
    return n
  })
  const uiTabs = useStore((s) => s.uiTabs) || []

  const [settings, setSettings] = useState<QtClientSettings>(() => loadQtSettings())
  const [cmd, setCmd] = useState('')
  const [busy, setBusy] = useState(false)
  const [historyByAgent, setHistoryByAgent] = useState<Record<string, string[]>>({})
  const [histIdx, setHistIdx] = useState(-1)
  const [searchOpen, setSearchOpen] = useState(false)
  const [searchQ, setSearchQ] = useState('')
  const [matchIdx, setMatchIdx] = useState(0)
  const [ctx, setCtx] = useState<{ x: number; y: number } | null>(null)

  const outRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)
  const searchRef = useRef<HTMLInputElement>(null)

  const live = agent ? isAgentOnline(agent, { lastTick: agentTick }) : false
  const history = agentId ? (historyByAgent[agentId] || []) : []

  const comp = useCompletion({
    value: cmd,
    agent,
    onAccept: setCmd,
    fallbackCommands: FALLBACK_COMMANDS,
    fallbackBofs: FALLBACK_BOFS,
  })

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

  const view = useMemo(() => {
    return lines
      .map((l) => {
        const text = formatLineText(l.kind, l.text)
        return text ? { ...l, text } : null
      })
      .filter(Boolean) as typeof lines
  }, [lines])

  const matches = useMemo(() => {
    const s = searchQ.trim().toLowerCase()
    if (!s) return [] as number[]
    const out: number[] = []
    view.forEach((l, i) => {
      if (l.text.toLowerCase().includes(s)) out.push(i)
    })
    return out
  }, [view, searchQ])

  useEffect(() => { setMatchIdx(0) }, [searchQ, matches.length])

  useEffect(() => {
    const el = outRef.current
    if (el && settings.consoleAutoScroll) {
      el.scrollTop = el.scrollHeight
    }
  }, [view.length, agentId, settings.consoleAutoScroll])

  useEffect(() => {
    setCmd('')
    setHistIdx(-1)
    setBusy(false)
    if (agentId) inputRef.current?.focus()
  }, [agentId])

  useEffect(() => {
    setHistoryByAgent((h) => {
      const next: Record<string, string[]> = {}
      for (const id of openTabs) {
        if (h[id]) next[id] = h[id]
      }
      return next
    })
  }, [openTabs.join('|')])

  function openCommandRunner() {
    if (!agentId) return
    let n = 0
    while (uiTabs.includes(cmdTabId(agentId, n))) n++
    openUiTab(cmdTabId(agentId, n))
  }

  function jumpMatch(delta: number) {
    if (!matches.length) return
    const next = (matchIdx + delta + matches.length) % matches.length
    setMatchIdx(next)
    const row = outRef.current?.querySelector(`[data-cidx="${matches[next]}"]`) as HTMLElement | null
    row?.scrollIntoView({ block: 'center' })
  }

  async function run(line: string) {
    const s = line.trim()
    if (!s || !agentId || busy) return
    setBusy(true)
    setCmd('')
    setHistIdx(-1)
    setHistoryByAgent((h) => {
      const prev = h[agentId] || []
      const next = prev[0] === s ? prev : [s, ...prev].slice(0, 120)
      return { ...h, [agentId]: next }
    })
    try {
      selectAgent(agentId)
      await sendCommand(s)
    } finally {
      setBusy(false)
      inputRef.current?.focus()
    }
  }

  function onInputKey(e: React.KeyboardEvent<HTMLInputElement>) {
    if (comp.onKeyDown(e)) return
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      void run(cmd)
    } else if (e.key === 'ArrowUp' && history.length) {
      e.preventDefault()
      const idx = Math.min(histIdx + 1, history.length - 1)
      setHistIdx(idx)
      setCmd(history[idx])
    } else if (e.key === 'ArrowDown') {
      e.preventDefault()
      if (histIdx <= 0) {
        setHistIdx(-1)
        setCmd('')
      } else {
        const idx = histIdx - 1
        setHistIdx(idx)
        setCmd(history[idx])
      }
    } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'l') {
      e.preventDefault()
      if (agentId) clearConsole(agentId)
    }
  }

  function onWrapKey(e: React.KeyboardEvent) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'f') {
      e.preventDefault()
      setSearchOpen(true)
      setTimeout(() => { searchRef.current?.focus(); searchRef.current?.select() }, 0)
    } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'l') {
      e.preventDefault()
      if (agentId) clearConsole(agentId)
    } else if (e.key === 'Escape' && searchOpen) {
      setSearchOpen(false)
      setSearchQ('')
    }
  }

  function lineToneClass(kind: string, text: string): string {
    if (kind === 'local') return 'qt-c-cmd'
    if (kind === 'err' || text.startsWith('[-]')) return 'qt-c-err'
    if (kind === 'ok' || text.startsWith('[+]')) return 'qt-c-ok'
    if (kind === 'info' || text.startsWith('[*]')) return 'qt-c-info'
    return ''
  }

  const infoText = agent
    ? (agent.a_domain && agent.a_domain !== agent.a_computer
      ? `[${agent.a_id}] ${agent.a_username || '-'} @ ${agent.a_computer || '-'}.${agent.a_domain}`
      : `[${agent.a_id}] ${agent.a_username || '-'} @ ${agent.a_computer || '-'}`)
    : t('未选择会话')

  const promptLabel = `${agent?.a_name || 'agent'} >`
  const activeMatchLine = matches.length ? matches[matchIdx] : -1

  return (
    <div className="qt-console" tabIndex={0} onKeyDown={onWrapKey}>
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
            value={searchQ}
            onChange={(e) => setSearchQ(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') { e.preventDefault(); jumpMatch(e.shiftKey ? -1 : 1) }
              if (e.key === 'Escape') { setSearchOpen(false); setSearchQ('') }
            }}
          />
          <button type="button" onClick={() => { setSearchOpen(false); setSearchQ('') }}>X</button>
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
        <div ref={outRef} className={`qt-console-out ${settings.consoleNoWrap ? 'nowrap' : ''}`}>
          {view.length === 0 ? (
            <div className="qt-c-info">
              [*] {agent ? `${agent.a_computer || agent.a_id} (${agent.a_internal_ip || '-'}) ready. Type "help" to list commands.` : t('请双击会话列表中的节点以打开控制台')}
            </div>
          ) : (
            view.map((l, i) => {
              const isHit = searchQ.trim() ? matches.includes(i) : false
              const isCur = i === activeMatchLine
              return (
                <div
                  key={l.id || i}
                  data-cidx={i}
                  className={`qt-c-line ${isCur ? 'hit-cur' : isHit ? 'hit' : ''}`}
                >
                  {settings.consoleShowTime && l.ts ? <span className="qt-c-time">{fmtClock(l.ts)}</span> : null}
                  <span className={lineToneClass(l.kind, l.text)}>{l.text}</span>
                </div>
              )
            })
          )}
        </div>
      </div>

      <div className="qt-console-info">
        <span>{infoText}</span>
        <span className="qt-console-info-meta">
          {runningTasks > 0 ? <span style={{ color: 'var(--qt-warn)' }}>{runningTasks} running</span> : null}
          {!live && agent ? <span style={{ color: 'var(--qt-err)' }}>Inactive</span> : null}
          <span>Ctrl+F Find · Right-click Menu</span>
        </span>
      </div>

      <div className="qt-console-prompt-row">
        <span className="qt-console-prompt-lbl">{promptLabel}</span>
        <div className="qt-console-input-wrap">
          {comp.open && (
            <CmdSuggest
              items={comp.items}
              index={comp.index}
              degraded={comp.degraded}
              notice={comp.notice}
              onPick={comp.pick}
              onHover={comp.hover}
              onRefresh={comp.refresh}
            />
          )}
          <input
            ref={inputRef}
            className="qt-console-input"
            value={cmd}
            disabled={!agentId || busy}
            onChange={(e) => { setCmd(e.target.value); setHistIdx(-1) }}
            onKeyDown={onInputKey}
            onBlur={() => setTimeout(() => comp.close(), 120)}
            placeholder={agentId ? (live ? 'help' : t('离线状态，命令将进入队列')) : t('未选择会话')}
            spellCheck={false}
            autoComplete="off"
          />
        </div>
      </div>

      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 210), top: Math.min(ctx.y, window.innerHeight - 280) }}
          onClick={(e) => e.stopPropagation()}
        >
          <button type="button" onClick={() => {
            setSearchOpen(true); setCtx(null)
            setTimeout(() => searchRef.current?.focus(), 0)
          }}>
            <span>Find</span><span className="qt-menu-sc">Ctrl+F</span>
          </button>
          <button type="button" onClick={() => { if (agentId) clearConsole(agentId); setCtx(null) }}>
            <span>Clear</span><span className="qt-menu-sc">Ctrl+L</span>
          </button>
          <button type="button" onClick={async () => {
            const text = view.map((l) => `${fmtClock(l.ts)}${l.text}`).join('\n')
            const ok = await copyText(text)
            pushToast(ok ? 'ok' : 'err', ok ? t('已复制输出') : t('复制失败'))
            setCtx(null)
          }}>
            <span>Copy Output</span>
          </button>
          <button type="button" onClick={() => {
            const cur = loadQtSettings()
            saveQtSettings({ ...cur, consoleShowBg: !cur.consoleShowBg })
            setCtx(null)
          }}>
            <span>{settings.consoleShowBg ? 'Hide Background' : 'Show Background'}</span>
          </button>
          {agentId && (
            <>
              <hr />
              <button type="button" disabled={!live} onClick={() => { openFilesBrowser(agentId); setCtx(null) }}>
                <span>File Browser</span>
              </button>
              <button type="button" disabled={!live} onClick={() => { openProcsBrowser(agentId); setCtx(null) }}>
                <span>Process Browser</span>
              </button>
              <button type="button" disabled={!live} onClick={() => { openPty(agentId); setCtx(null) }}>
                <span>Terminal (PTY)</span>
              </button>
              <button type="button" disabled={!live} onClick={() => { openVnc(agentId); setCtx(null) }}>
                <span>VNC Viewer</span>
              </button>
              <button type="button" disabled={!live} onClick={() => { openCommandRunner(); setCtx(null) }}>
                <span>Command Runner</span>
              </button>
            </>
          )}
        </div>
      )}
    </div>
  )
})

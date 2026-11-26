import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { browserKillProc, browserListProcs } from '../api/browser'
import type { ProcItem } from '../utils/browserParse'
import { copyText, isLinuxAgent } from '../utils'
import { PageTips } from './PageTips'
import { qtAsset } from '../design/qt-themes'

type SortKey = 'pid' | 'ppid' | 'arch' | 'session' | 'tty' | 'context' | 'name'

type TreeNode = {
  proc: ProcItem
  children: TreeNode[]
}

function buildProcForest(items: ProcItem[]): { roots: TreeNode[]; parentOf: Map<number, number> } {
  const byPid = new Map<number, TreeNode>()
  const parentOf = new Map<number, number>()
  for (const p of items) {
    byPid.set(p.pid, { proc: p, children: [] })
  }
  const roots: TreeNode[] = []
  for (const p of items) {
    const node = byPid.get(p.pid)!
    const parent = p.ppid && p.ppid !== p.pid ? byPid.get(p.ppid) : undefined
    if (parent) {
      parent.children.push(node)
      parentOf.set(p.pid, p.ppid)
    } else {
      roots.push(node)
    }
  }
  const sortNodes = (arr: TreeNode[]) => {
    arr.sort((a, b) => a.proc.pid - b.proc.pid)
    for (const n of arr) sortNodes(n.children)
  }
  sortNodes(roots)
  return { roots, parentOf }
}

function isPrivContext(ctx?: string): boolean {
  if (!ctx) return false
  return /\b(system|root|administrator|nt authority\\system)\b/i.test(ctx)
}

/**
 * Native Qt BrowserProcessWidget:
 *  - Draggable horizontal QSplitter: left 3-column Process Tree (Process | PID | Context)
 *    + right sortable Process QTableView
 *  - Bidirectional Tree <-> Table selection & scroll sync
 *  - Own agent PID highlighted in ChiliPepper red (#E96B72)
 *  - Live Ctrl+F filtering, scope pills, full right-click QMenu
 */
export function ProcessManager({ agentId }: { agentId: string }) {
  const api = useStore((s) => s.api)
  const agents = useStore((s) => s.agents) || {}
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  const openConsole = useStore((s) => s.openConsole)
  const interactBoost = useStore((s) => s.interactBoost)
  const acquireInteractBoost = useStore((s) => s.acquireInteractBoost)
  const agent = agents[agentId]
  const boosted = !!interactBoost[agentId]?.ref
  const linux = isLinuxAgent(agent)
  const selfPid = Number(agent?.a_pid || 0)

  const [items, setItems] = useState<ProcItem[]>([])
  const [statusMsg, setStatusMsg] = useState('')
  const [statusTime, setStatusTime] = useState('')
  const [busy, setBusy] = useState(false)
  const [killing, setKilling] = useState<number | null>(null)
  const [err, setErr] = useState('')
  const [q, setQ] = useState('')
  const [scope, setScope] = useState<'all' | 'priv' | 'children'>('all')
  const [childRootPid, setChildRootPid] = useState<number | null>(null)
  const [sortKey, setSortKey] = useState<SortKey>('pid')
  const [sortAsc, setSortAsc] = useState(true)
  const [selected, setSelected] = useState<number | null>(null)
  const [collapsed, setCollapsed] = useState<Set<number>>(new Set())
  const [treePct, setTreePct] = useState(34)
  const [draggingSplit, setDraggingSplit] = useState(false)
  const [ctx, setCtx] = useState<{ x: number; y: number; proc: ProcItem } | null>(null)

  const seq = useRef(0)
  const wrapRef = useRef<HTMLDivElement>(null)
  const treeRef = useRef<HTMLDivElement>(null)
  const tableRef = useRef<HTMLDivElement>(null)
  const filterRef = useRef<HTMLInputElement>(null)

  const { roots, parentOf } = useMemo(() => buildProcForest(items), [items])

  const descendantSet = useMemo(() => {
    if (scope !== 'children' || childRootPid == null) return null
    const set = new Set<number>([childRootPid])
    let changed = true
    while (changed) {
      changed = false
      for (const p of items) {
        if (!set.has(p.pid) && set.has(p.ppid)) {
          set.add(p.pid)
          changed = true
        }
      }
    }
    return set
  }, [items, scope, childRootPid])

  const view = useMemo(() => {
    let arr = items
    if (scope === 'priv') {
      arr = arr.filter((p) => isPrivContext(p.context) || p.pid === selfPid)
    } else if (descendantSet) {
      arr = arr.filter((p) => descendantSet.has(p.pid))
    }
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((p) =>
        [p.name, String(p.pid), String(p.ppid), p.context, p.arch, p.tty, String(p.session ?? '')]
          .filter(Boolean)
          .some((x) => String(x).toLowerCase().includes(s)),
      )
    }
    const dir = sortAsc ? 1 : -1
    return [...arr].sort((a, b) => {
      switch (sortKey) {
        case 'pid': return (a.pid - b.pid) * dir
        case 'ppid': return ((a.ppid || 0) - (b.ppid || 0)) * dir
        case 'session': return ((a.session ?? -1) - (b.session ?? -1)) * dir
        case 'arch': return String(a.arch || '').localeCompare(String(b.arch || '')) * dir
        case 'tty': return String(a.tty || '').localeCompare(String(b.tty || '')) * dir
        case 'context': return String(a.context || '').localeCompare(String(b.context || '')) * dir
        case 'name': return String(a.name || '').localeCompare(String(b.name || '')) * dir
      }
    })
  }, [items, q, scope, descendantSet, selfPid, sortKey, sortAsc])

  const visiblePidSet = useMemo(() => new Set(view.map((p) => p.pid)), [view])
  const selectedProc = useMemo(() => items.find((p) => p.pid === selected) || null, [items, selected])

  // Close context menu on click outside
  useEffect(() => {
    if (!ctx) return
    const close = () => setCtx(null)
    window.addEventListener('click', close)
    return () => window.removeEventListener('click', close)
  }, [ctx])

  // Horizontal splitter drag
  useEffect(() => {
    if (!draggingSplit) return
    const onMove = (e: MouseEvent) => {
      const box = wrapRef.current?.getBoundingClientRect()
      if (!box || box.width <= 0) return
      const pct = Math.max(20, Math.min(56, ((e.clientX - box.left) / box.width) * 100))
      setTreePct(pct)
    }
    const onUp = () => setDraggingSplit(false)
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [draggingSplit])

  async function refresh() {
    const my = ++seq.current
    setBusy(true)
    setErr('')
    setStatusMsg('ps list…')
    try {
      const res = await browserListProcs(api, agentId, ({ sec, pending }) => {
        if (my !== seq.current) return
        setStatusMsg(pending ? `waiting ${sec}s…` : `polling ${sec}s…`)
      })
      if (my !== seq.current) return
      setItems(res.items)
      setStatusMsg(res.message || 'Status: OK')
      setStatusTime(new Date().toLocaleTimeString('en-GB', { hour12: false }))
      pushLog('ok', `进程 ${agentId}: ${res.items.length}`, false)
      if (selected != null && !res.items.some((p) => p.pid === selected)) {
        setSelected(null)
      }
    } catch (e: any) {
      if (my !== seq.current) return
      const msg = e?.message || String(e)
      setErr(msg)
      setStatusMsg('')
      pushToast('err', msg)
    } finally {
      if (my === seq.current) setBusy(false)
    }
  }

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try { await acquireInteractBoost(agentId) } catch { /* store toast */ }
      if (!cancelled) await refresh()
    })()
    return () => { cancelled = true }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [agentId])

  function expandAncestors(pid: number) {
    const ancestors: number[] = []
    let cur = parentOf.get(pid)
    const guard = new Set<number>()
    while (cur != null && !guard.has(cur)) {
      guard.add(cur)
      ancestors.push(cur)
      cur = parentOf.get(cur)
    }
    if (!ancestors.length) return
    setCollapsed((prev) => {
      let changed = false
      const next = new Set(prev)
      for (const a of ancestors) {
        if (next.delete(a)) changed = true
      }
      return changed ? next : prev
    })
  }

  function selectFromTable(pid: number) {
    setSelected(pid)
    expandAncestors(pid)
    setTimeout(() => {
      const el = treeRef.current?.querySelector(`[data-tree-pid="${pid}"]`) as HTMLElement | null
      el?.scrollIntoView({ block: 'nearest' })
    }, 0)
  }

  function selectFromTree(pid: number) {
    setSelected(pid)
    setTimeout(() => {
      const el = tableRef.current?.querySelector(`[data-tbl-pid="${pid}"]`) as HTMLElement | null
      el?.scrollIntoView({ block: 'nearest' })
    }, 0)
  }

  function toggleCollapse(pid: number) {
    setCollapsed((prev) => {
      const next = new Set(prev)
      if (next.has(pid)) next.delete(pid)
      else next.add(pid)
      return next
    })
  }

  function collapseAll() {
    const parents = new Set<number>()
    const walk = (n: TreeNode) => {
      if (n.children.length) parents.add(n.proc.pid)
      for (const c of n.children) walk(c)
    }
    for (const r of roots) walk(r)
    setCollapsed(parents)
  }

  function toggleSort(k: SortKey) {
    if (sortKey === k) setSortAsc((v) => !v)
    else { setSortKey(k); setSortAsc(true) }
  }

  async function kill(proc: ProcItem) {
    const isSelf = selfPid > 0 && proc.pid === selfPid
    const warn = isSelf
      ? `警告：PID ${proc.pid} (${proc.name}) 是当前会话自身进程！结束会导致会话离线。\n确认结束？`
      : `结束进程？\nPID ${proc.pid}\n${proc.name}`
    if (!confirm(warn)) return
    setKilling(proc.pid)
    try {
      const res = await browserKillProc(api, agentId, proc.pid)
      pushToast('ok', res.message || `已结束 PID ${proc.pid}`)
      pushLog('ok', `ps kill ${proc.pid} (${proc.name}): ${res.message || 'ok'}`, false)
      setTimeout(() => { refresh().catch(() => {}) }, 800)
    } catch (e: any) {
      pushToast('err', e?.message || String(e))
    } finally {
      setKilling(null)
    }
  }

  async function copyTextWithToast(val: string, label: string) {
    const ok = await copyText(val)
    pushToast(ok ? 'ok' : 'err', ok ? `已复制 ${label}` : t('复制失败'))
  }

  function renderTreeNode(node: TreeNode, depth: number, visited: Set<number>): React.ReactNode {
    const p = node.proc
    if (visited.has(p.pid)) return null
    visited.add(p.pid)
    const isCollapsed = collapsed.has(p.pid)
    const isSelf = selfPid > 0 && p.pid === selfPid
    const isActive = selected === p.pid
    const dimmed = (q.trim() || scope !== 'all') && !visiblePidSet.has(p.pid)
    return (
      <div key={p.pid}>
        <div
          data-tree-pid={p.pid}
          className={`qt-tree-row ${isActive ? 'active' : ''} ${isSelf ? 'is-self' : ''}`.trim()}
          style={{ paddingLeft: 6 + depth * 14, opacity: dimmed ? 0.36 : 1 }}
          onClick={() => selectFromTree(p.pid)}
          onContextMenu={(e) => {
            e.preventDefault()
            selectFromTree(p.pid)
            setCtx({ x: e.clientX, y: e.clientY, proc: p })
          }}
        >
          <button
            type="button"
            className="qt-tree-caret"
            onClick={(e) => {
              e.stopPropagation()
              if (node.children.length) toggleCollapse(p.pid)
            }}
          >
            {node.children.length ? (isCollapsed ? '▸' : '▾') : '·'}
          </button>
          <span className="qt-tree-lbl mono" title={p.name}>{p.name}</span>
          <span className="qt-tree-pid">{p.pid}</span>
          <span className="qt-tree-ctx" title={p.context || ''}>{p.context || ''}</span>
        </div>
        {!isCollapsed && node.children.map((c) => renderTreeNode(c, depth + 1, visited))}
      </div>
    )
  }

  function onKeyDown(e: React.KeyboardEvent) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'f') {
      e.preventDefault()
      filterRef.current?.focus()
      filterRef.current?.select()
    } else if (e.key === 'Escape' && q) {
      setQ('')
    }
  }

  const arrow = (k: SortKey) => (sortKey === k ? (sortAsc ? ' ▲' : ' ▼') : '')

  return (
    <div className="qt-browser" ref={wrapRef} tabIndex={0} onKeyDown={onKeyDown}>
      {/* Left Pane: 3-column QTreeWidget Process Tree */}
      <aside className="qt-browser-tree" style={{ width: `${treePct}%` }}>
        <div className="qt-tree-hd">
          <div className="qt-tree-hd-cols">
            <span>Process</span>
            <span>PID</span>
            <span style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <span>Context</span>
              <span className="qt-tree-hd-actions">
                <button type="button" className="qt-tree-hd-btn" title="Expand all" onClick={() => setCollapsed(new Set())}>+</button>
                <button type="button" className="qt-tree-hd-btn" title="Collapse all" onClick={collapseAll}>−</button>
              </span>
            </span>
          </div>
        </div>
        <div className="qt-tree-bd" ref={treeRef}>
          {items.length === 0 ? (
            <div className="qt-table-empty" style={{ fontSize: 11.5 }}>
              {busy ? t('正在拉取进程树…') : (err || t('无进程数据'))}
            </div>
          ) : (
            (() => {
              const visited = new Set<number>()
              return roots.map((r) => renderTreeNode(r, 0, visited))
            })()
          )}
        </div>
      </aside>

      {/* Draggable horizontal QSplitter */}
      <div
        className={`qt-vsplit ${draggingSplit ? 'dragging' : ''}`}
        role="separator"
        aria-orientation="vertical"
        title={t('拖拽调整进程树宽度，双击还原')}
        onMouseDown={() => setDraggingSplit(true)}
        onDoubleClick={() => setTreePct(34)}
      />

      {/* Right Pane: Toolbar + Sortable QTableView + Compact Footer */}
      <section className="qt-browser-main">
        <div className="qt-browser-bar">
          <button
            type="button"
            className="qt-b-ico-btn"
            title="Reload (ps)"
            disabled={busy || killing != null}
            onClick={refresh}
          >
            <img className="qt-ico qt-ico-mono" src={qtAsset('icons/autorenew_64dp.png')} alt="" />
          </button>

          <input
            ref={filterRef}
            type="search"
            className="qt-b-filter"
            style={{ flex: '1 1 180px', maxWidth: 320 }}
            placeholder={t('过滤 PID / PPID / 进程 / 用户 (Ctrl+F)')}
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />

          <span className="qt-tb-vline" />

          <button
            type="button"
            className={`qt-b-btn ${scope === 'all' ? 'primary' : ''}`}
            onClick={() => { setScope('all'); setChildRootPid(null) }}
          >
            {t('全部')} ({items.length})
          </button>
          <button
            type="button"
            className={`qt-b-btn ${scope === 'priv' ? 'primary' : ''}`}
            onClick={() => { setScope('priv'); setChildRootPid(null) }}
            title="SYSTEM / root / Administrator"
          >
            SYSTEM/root
          </button>
          {selfPid > 0 && (
            <button
              type="button"
              className="qt-b-btn"
              title={`Locate current agent PID ${selfPid}`}
              onClick={() => selectFromTable(selfPid)}
            >
              Self ({selfPid})
            </button>
          )}
          {scope === 'children' && childRootPid != null && (
            <button
              type="button"
              className="qt-b-btn primary"
              onClick={() => { setScope('all'); setChildRootPid(null) }}
            >
              PPID={childRootPid} ×
            </button>
          )}

          <span className="qt-tb-vline" />

          <button
            type="button"
            className="qt-b-btn danger"
            disabled={!selectedProc || busy || killing != null}
            onClick={() => selectedProc && kill(selectedProc)}
          >
            {killing != null && killing === selected ? t('结束中…') : t('结束进程')}
          </button>
          <button type="button" className="qt-b-btn" onClick={() => openConsole(agentId)}>
            Console
          </button>

          <div style={{ flex: 1 }} />

          <span className={`qt-b-status ${err ? 'err' : busy ? 'busy' : 'ok'}`}>
            {err ? err : busy ? statusMsg : `${statusMsg || 'Status: OK'}${statusTime ? ` >> ${statusTime}` : ''}`}
          </span>

          {!linux && (
            <span className={`qt-b-boost ${boosted || agent?.a_sleep === 0 ? 'on' : ''}`}>
              {boosted || agent?.a_sleep === 0 ? 'sleep 0' : `${agent?.a_sleep ?? '-'}s`}
            </span>
          )}

          <PageTips id="procs">
            <p>{t('左侧为完整 PID/PPID 进程树，右侧为可排序进程表，点击双向联动定位。')}</p>
            <p>{t('红色标出当前会话自身进程（防止误杀）；右键支持复制 PID、筛选子树与结束进程。')}</p>
          </PageTips>
        </div>

        <div className="qt-table-wrap" ref={tableRef}>
          {busy && items.length === 0 ? (
            <div className="qt-table-empty">
              <div>{t('正在拉取进程列表…')}</div>
              <div style={{ fontSize: 11 }}>{statusMsg || t('等待主机回显')}</div>
            </div>
          ) : view.length === 0 ? (
            <div className="qt-table-empty">
              <div>{err || t('无匹配进程')}</div>
              <button type="button" onClick={refresh}>{t('刷新')}</button>
            </div>
          ) : (
            <table className="qt-table">
              <thead>
                <tr>
                  <th style={{ width: 76 }} onClick={() => toggleSort('pid')}>PID{arrow('pid')}</th>
                  <th style={{ width: 76 }} onClick={() => toggleSort('ppid')}>PPID{arrow('ppid')}</th>
                  {linux ? (
                    <th style={{ width: 84 }} onClick={() => toggleSort('tty')}>TTY{arrow('tty')}</th>
                  ) : (
                    <>
                      <th style={{ width: 72 }} onClick={() => toggleSort('arch')}>Arch{arrow('arch')}</th>
                      <th style={{ width: 74 }} onClick={() => toggleSort('session')}>Session{arrow('session')}</th>
                    </>
                  )}
                  <th className="align-l" style={{ minWidth: 160 }} onClick={() => toggleSort('context')}>Context{arrow('context')}</th>
                  <th className="align-l" onClick={() => toggleSort('name')}>Process{arrow('name')}</th>
                </tr>
              </thead>
              <tbody>
                {view.map((p) => {
                  const isSelf = selfPid > 0 && p.pid === selfPid
                  const isSel = selected === p.pid
                  const priv = isPrivContext(p.context)
                  return (
                    <tr
                      key={p.pid}
                      data-tbl-pid={p.pid}
                      className={`${isSel ? 'selected' : ''} ${isSelf ? 'is-self-proc' : ''}`.trim()}
                      onClick={() => selectFromTable(p.pid)}
                      onContextMenu={(e) => {
                        e.preventDefault()
                        selectFromTable(p.pid)
                        setCtx({ x: e.clientX, y: e.clientY, proc: p })
                      }}
                    >
                      <td className="mono">{p.pid}</td>
                      <td className="mono">{p.ppid || 0}</td>
                      {linux ? (
                        <td className="mono">{p.tty || '?'}</td>
                      ) : (
                        <>
                          <td className="mono">{p.arch || ''}</td>
                          <td className="mono">{p.session ?? ''}</td>
                        </>
                      )}
                      <td className={`align-l ${priv && !isSelf ? 'qt-priv-ctx' : ''}`} title={p.context || ''}>
                        {p.context || ''}
                      </td>
                      <td className="align-l mono" title={p.name}>
                        {p.name}
                        {isSelf ? <span className="qt-self-badge">CURRENT</span> : null}
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          )}
        </div>

        <footer className="qt-browser-foot">
          <span className="mono">{view.length} / {items.length} procs · {roots.length} roots</span>
          <span className="qt-south-sep" />
          <span className="qt-browser-foot-path">
            {selectedProc
              ? `PID ${selectedProc.pid} (PPID ${selectedProc.ppid || 0}) · ${selectedProc.context || '-'} · ${selectedProc.name}`
              : `${agent?.a_computer || agentId} · ${t('点击树或表格项双向联动，右键更多操作')}`}
          </span>
          {selectedProc && (
            <span className="qt-browser-foot-actions">
              <button type="button" onClick={() => void copyTextWithToast(String(selectedProc.pid), `PID ${selectedProc.pid}`)}>
                Copy PID
              </button>
              <button type="button" className="danger" disabled={busy || killing != null} onClick={() => void kill(selectedProc)}>
                Kill
              </button>
            </span>
          )}
        </footer>
      </section>

      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.max(8, Math.min(ctx.x, window.innerWidth - 235)), top: Math.max(8, Math.min(ctx.y, window.innerHeight - 340)) }}
          onClick={(e) => e.stopPropagation()}
        >
          <div className="ctx-hd mono">{ctx.proc.name} · {ctx.proc.pid}</div>
          <button type="button" onClick={() => { void copyTextWithToast(String(ctx.proc.pid), `PID ${ctx.proc.pid}`); setCtx(null) }}>
            <span>{t('复制 PID')}</span>
          </button>
          <button type="button" onClick={() => { void copyTextWithToast(ctx.proc.name, ctx.proc.name); setCtx(null) }}>
            <span>{t('复制进程名')}</span>
          </button>
          <div className="sep" />
          <button type="button" onClick={() => { setChildRootPid(ctx.proc.pid); setScope('children'); setCtx(null) }}>
            <span>{t('筛选该进程及子进程')}</span>
          </button>
          {ctx.proc.context && (
            <button type="button" onClick={() => { setQ(ctx.proc.context || ''); setScope('all'); setCtx(null) }}>
              <span>{t('按同用户筛选')} ({ctx.proc.context})</span>
            </button>
          )}
          <div className="sep" />
          <button type="button" onClick={() => { openConsole(agentId); setCtx(null) }}>
            <span>{t('打开终端')}</span>
          </button>
          <button type="button" onClick={() => { void refresh(); setCtx(null) }}>
            <span>{t('刷新列表')}</span>
          </button>
          <div className="sep" />
          <button type="button" className="danger" onClick={() => { void kill(ctx.proc); setCtx(null) }}>
            <span>{t('结束进程')}</span>
          </button>
        </div>
      )}
    </div>
  )
}

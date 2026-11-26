import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore, cmdTabId } from '../store'
import { t } from '../i18n'
import type { Agent } from '../api/types'
import { copyText, formatHeartbeat, fmtRelative, fmtTime, isAgentOnline, isLinuxAgent } from '../utils'
import { lookupIpGeoSync, preloadIpGeo } from '../utils/ipdb'
import { FALLBACK_BOFS, FALLBACK_COMMANDS } from '../lib/completions'
import { useCompletion } from '../lib/useCompletion'
import { CmdSuggest } from './CmdSuggest'
import { loadQtSettings, qtAsset, type QtClientSettings } from '../design/qt-themes'

function osIconPath(a: Agent, live: boolean): string {
  const desc = (a.a_os_desc || '').toLowerCase()
  const fam = isLinuxAgent(a)
    ? 'linux'
    : (a.a_os === 3 || desc.includes('mac') || desc.includes('darwin') ? 'mac' : 'windows')
  const tone = !live ? 'grey' : (a.a_elevated ? 'red' : 'blue')
  return qtAsset(`icons/os/icon-${fam}-${tone}.png`)
}

function formatQtSleep(a: Agent): string {
  if (a.a_mark) return a.a_mark
  const sec = Number(a.a_sleep ?? 0)
  if (sec <= 0) return '⟶⟶⟶'
  return `${sec}s (${Number(a.a_jitter ?? 0)}%)`
}

type Ctx = { x: number; y: number; id: string } | null

/**
 * 「执行命令」对话框。
 *
 * 提到**顶层**（而不是像 SideItem 那样写在 Hosts 内部）是被 Hooks 规则逼的：
 * 定义在渲染函数里的组件每次父组件重渲染都是一个**新类型**，React 会整棵卸载
 * 重挂 —— 输入框每敲一个字就丢一次焦点，补全弹层也永远打不开。
 * SideItem / TaggedGroup 无状态，才敢写成内部组件。
 */
function CmdDlg({
  id, agent, onClose, onSubmit,
}: {
  id: string
  agent: Agent | null | undefined
  onClose: () => void
  onSubmit: (cmd: string) => void | Promise<void>
}) {
  const [cmd, setCmd] = useState('')
  // ★ 这个 ref 以前**只被读、没被声明**（`inputRef.current?.focus()` 引用了它），
  //   运行时是 `ReferenceError: inputRef is not defined` —— 发完命令输入框虽然清空了，
  //   焦点却回不来，操作员得再点一下才能敲下一条。tsc 一直报 TS2304。
  const inputRef = useRef<HTMLInputElement>(null)
  const comp = useCompletion({
    value: cmd,
    agent,
    onAccept: setCmd,
    fallbackCommands: FALLBACK_COMMANDS,
    fallbackBofs: FALLBACK_BOFS,
  })

  async function submit() {
    const c = cmd.trim()
    if (!c) return
    try {
      await onSubmit(c)
    } catch {
      return // 失败时保留命令原文，方便改一下重试
    }
    // ★ 发完**清输入、留对话框**：主机页底部的 Terminal 已按需求去掉，右键
    //   「执行命令」就是命令执行的入口，要能连跑多条。关不关由操作员点「关闭」
    //   或按 Esc 决定 —— 跑一条就自动关，会逼着他每跑一条重新右键一次。
    setCmd('')
    inputRef.current?.focus()
  }

  return (
    <div className="mask" onClick={onClose}>
      <div className="dlg" onClick={(e) => e.stopPropagation()}>
        <h2>{t('执行命令')}</h2>
        <div className="dim mono" style={{ marginBottom: 8 }}>{id}</div>
        <div className="field">
          <label>{t('命令行')}</label>
          <input
            ref={inputRef}
            className="mono"
            autoFocus
            value={cmd}
            placeholder={isLinuxAgent(agent) ? '例如: getuid / shell id' : '例如: whoami /all'}
            onChange={(e) => setCmd(e.target.value)}
            onKeyDown={(e) => {
              // 补全先过一道。它只在 Tab、弹层已开时的 ↑/↓、以及**已高亮**时的
              // 回车上返回 true —— 什么都不高亮时回车照旧提交，与改造前一致。
              if (comp.onKeyDown(e)) return
              if (e.key === 'Enter') { e.preventDefault(); void submit() }
              // 两级 Esc：弹层开着时上面那次已经消费掉了，再按一次才关对话框
              else if (e.key === 'Escape') { e.preventDefault(); onClose() }
            }}
          />
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
        </div>
        <div className="btns">
          <button onClick={onClose}>{t('取消')}</button>
          <button className="primary" onClick={submit}>{t('执行')}</button>
        </div>
      </div>
    </div>
  )
}

export function Hosts() {
  const agents = useStore((s) => s.agents) || {}
  const filter = useStore((s) => s.filter)
  const onlyActive = useStore((s) => s.onlyActive)
  const selected = useStore((s) => s.selectedAgentId)
  const selectAgent = useStore((s) => s.selectAgent)
  const setFilter = useStore((s) => s.setFilter)
  const setOnlyActive = useStore((s) => s.setOnlyActive)
  const removeSelected = useStore((s) => s.removeSelected)
  const refreshRest = useStore((s) => s.refreshRest)
  const openConsole = useStore((s) => s.openConsole)
  const openUiTab = useStore((s) => s.openUiTab)
  const openTabs = useStore((s) => s.uiTabs) || []
  /**
   * 开一个新的「命令执行」会话。**每次都找一个没被占用的序号**（cmd:<id>、#1、#2…）
   * —— 复用同一个 tab id 只会把旧会话切到前台，开不出第二个。
   */
  function openCmdTab(id: string) {
    let n = 0
    while (openTabs.includes(cmdTabId(id, n))) n++
    openUiTab(cmdTabId(id, n))
  }
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)
  const openPty = useStore((s) => s.openPty)
  const openVnc = useStore((s) => s.openVnc)
  const clearConsole = useStore((s) => s.clearConsole)
  const setTab = useStore((s) => s.setTab)
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  // ★ 直接取 Set 本身，**不要** `useStore(s => new Set(s.checkedIds))` ——
  //   selector 里 new 出来的每次都是新引用，zustand v5 用 Object.is 比 ⇒ 无限重渲染。
  const checkedIds = useStore((s) => s.checkedIds)
  const toggleChecked = useStore((s) => s.toggleChecked)
  const setChecked = useStore((s) => s.setChecked)
  const clearChecked = useStore((s) => s.clearChecked)
  const removeChecked = useStore((s) => s.removeChecked)

  const [ctx, setCtx] = useState<Ctx>(null)
  const [busy, setBusy] = useState(false)
  const [agentType, setAgentType] = useState('ALL')
  const [searchOpen, setSearchOpen] = useState(false)
  const [autoSearch, setAutoSearch] = useState(true)
  const [draftFilter, setDraftFilter] = useState(filter)
  const [sortCol, setSortCol] = useState<string>('created')
  const [sortAsc, setSortAsc] = useState(false)
  const [cols, setCols] = useState<Record<string, boolean>>(() => loadQtSettings().sessionsCols)
  const searchRef = useRef<HTMLInputElement>(null)
  // 动态心跳：每秒刷新 age 文案（对齐 Qt LastTickWorker 500ms）
  const [nowTick, setNowTick] = useState(() => Date.now())
  const [geoVer, setGeoVer] = useState(0)
  // 「执行命令」对话框只带主机 id —— 命令行文本是 CmdDlg 自己的 state，
  // 因为补全 hook 必须在**对话框内部**（它要跟着那台主机过滤候选）。
  const [cmdDlg, setCmdDlg] = useState<string | null>(null)
  const [bulkDlg, setBulkDlg] = useState(false)
  const [dataDlg, setDataDlg] = useState<{
    id: string
    sleep: string
    jitter: string
    computer: string
    username: string
    domain: string
    process: string
    internal_ip: string
    external_ip: string
  } | null>(null)

  const agentTypes = useMemo(() => {
    const s = new Set<string>()
    for (const a of Object.values(agents)) {
      if (a.a_name) s.add(a.a_name)
    }
    return Array.from(s).sort()
  }, [agents])

  // 仅在线筛选时每 5 秒跟随 nowTick 刷新一次，避免已超时离线的主机滞留在「仅在线」结果中
  const onlyActiveBucket = onlyActive ? Math.floor(nowTick / 5000) : 0
  const filteredFixed = useMemo(() => {
    void onlyActiveBucket
    let arr = Object.values(agents)
    if (onlyActive) arr = arr.filter((a) => isAgentOnline(a, { now: nowTick }))
    if (agentType !== 'ALL') arr = arr.filter((a) => a.a_name === agentType)
    if (filter.trim()) {
      const q = filter.trim().toLowerCase()
      arr = arr.filter((a) =>
        [a.a_id, a.a_name, a.a_computer, a.a_username, a.a_internal_ip, a.a_external_ip, a.a_process, a.a_listener, a.a_tags, a.a_domain]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(q)))
    }
    // ★ 排序键是 a_create_time（首次注册时间，恒定），不是 a_last_tick：
    //   last_tick 每次 WS 心跳推送都变，按它排在线主机每次回连都跳到表格
    //   顶部，正在点的行被顶走。AGENT_TICK 只改 a_last_tick（store.ts），
    //   AGENT_UPDATE 包里没有 CreateTime 字段（utils.go SyncPackerAgentUpdate），
    //   所以按注册时间排完后心跳不再触发重排。缺 create_time 的旧包回退
    //   a_id 字典序 —— 不能回退 last_tick，否则同样的跳动又回来。
    return arr.sort((a, b) => {
      const ta = a.a_create_time || 0
      const tb = b.a_create_time || 0
      if (ta !== tb) return tb - ta // 新注册在前，贴近原「最新在顶部」的习惯
      return String(a.a_id).localeCompare(String(b.a_id))
    })
  }, [agents, filter, onlyActive, onlyActiveBucket, agentType])

  const online = filteredFixed.filter((a) => isAgentOnline(a, { now: nowTick }))
  const offline = filteredFixed.filter((a) => !isAgentOnline(a, { now: nowTick }))
  const selectedAgent = selected ? agents[selected] : null
  const selectedOnline = !!(selectedAgent && isAgentOnline(selectedAgent, { now: nowTick }))
  const ctxAgent = ctx ? agents[ctx.id] : null
  const ctxOnline = !!(ctxAgent && isAgentOnline(ctxAgent, { now: nowTick }))
  // Linux 主机：无截图 BOF（PostEx 是 Windows 专有）。sleep 已支持（2026-11-17）。
  const ctxLinux = !!isLinuxAgent(ctxAgent)
  const tableList = filteredFixed

  // ── 批量选择 ──
  // 全选的作用域是**当前筛选结果**（tableList），不是全部主机：操作员看到什么
  // 就勾什么。而勾选集合本身**不随筛选收窄**——勾 30 台后搜索收窄到 3 台，
  // 那 30 台仍然是勾着的，批量条会说出真实数量并标出其中几台不在当前结果里。
  // 这是刻意的：静默丢掉勾选，操作员就会以为自己只删 3 台。
  const tableIds = tableList.map((a) => a.a_id)
  const checkedVisible = tableIds.filter((id) => checkedIds.has(id)).length
  const allChecked = tableIds.length > 0 && checkedVisible === tableIds.length
  const someChecked = checkedVisible > 0 && !allChecked
  const hiddenChecked = checkedIds.size - checkedVisible
  const bulkAgents = [...checkedIds].map((id) => agents[id]).filter(Boolean) as Agent[]
  const bulkOnline = bulkAgents.filter((a) => isAgentOnline(a, { now: nowTick })).length

  useEffect(() => {
    const close = () => setCtx(null)
    window.addEventListener('click', close)
    window.addEventListener('scroll', close, true)
    return () => {
      window.removeEventListener('click', close)
      window.removeEventListener('scroll', close, true)
    }
  }, [])

  useEffect(() => {
    const t = window.setInterval(() => setNowTick(Date.now()), 1000)
    return () => window.clearInterval(t)
  }, [])

  // Ctrl+F toggles collapsible search bar + live column config sync
  useEffect(() => {
    const onSettings = (e: Event) => {
      const detail = (e as CustomEvent<QtClientSettings>).detail
      setCols((detail || loadQtSettings()).sessionsCols)
    }
    const onKey = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && (e.key === 'f' || e.key === 'F')) {
        const tag = (e.target as HTMLElement | null)?.tagName
        if (tag === 'INPUT' || tag === 'TEXTAREA') return
        e.preventDefault()
        setSearchOpen(true)
        setTimeout(() => { searchRef.current?.focus(); searchRef.current?.select() }, 0)
      } else if (e.key === 'Escape' && searchOpen) {
        setSearchOpen(false)
      }
    }
    window.addEventListener('adaptix-qt-settings', onSettings)
    window.addEventListener('keydown', onKey)
    return () => {
      window.removeEventListener('adaptix-qt-settings', onSettings)
      window.removeEventListener('keydown', onKey)
    }
  }, [searchOpen])

  // 归属地：把这一屏主机的**外网 IP** 批量丢给服务端查一次（库在服务端，见 utils/ipdb.ts）。
  // 表是同步渲染的、查库是异步的，所以拿回新结果后 bump 一次 geoVer 触发重渲染。
  // 依赖 tableList：新主机进来或筛选变化时自动补查。
  const extIps = useMemo(
    () => tableList.map((a) => String(a.a_external_ip || '').trim()).filter(Boolean),
    [tableList],
  )
  useEffect(() => {
    if (!api || !extIps.length) return
    preloadIpGeo(api, extIps).then((changed) => { if (changed) setGeoVer((v) => v + 1) }).catch(() => {})
  }, [api, extIps])

  function pick(a: Agent) {
    selectAgent(a.a_id)
    openConsole(a.a_id)
  }

  function tryFiles(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', t('主机离线，无法打开文件管理')); return }
    openFilesBrowser(id)
  }
  function tryProcs(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', t('主机离线，无法打开进程管理')); return }
    openProcsBrowser(id)
  }
  function tryPty(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', t('主机离线，无法打开 PTY')); return }
    openPty(id)
  }
  function tryVnc(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', t('主机离线，无法打开 VNC')); return }
    openVnc(id)
  }
  function openTunnels(id: string) {
    selectAgent(id)
    setTab('tunnels')
    openUiTab('tunnels')
  }
  async function clearServerConsole(id: string) {
    try {
      await api.consoleRemove([id])
      clearConsole(id)
      pushToast('ok', t('已清除控制台数据'))
    } catch (e: any) {
      clearConsole(id)
      pushToast('warn', `服务端清除失败，已清本地：${e?.message || e}`)
    }
  }
  async function doRefresh() {
    setBusy(true)
    try { await refreshRest() } catch (e: any) { pushLog('err', e.message || String(e)) }
    finally { setBusy(false) }
  }
  /** 「执行命令」对话框的提交。成功才关框 —— 失败时留住文本让操作员能改。 */
  async function submitCmd(id: string, c: string) {
    try {
      await api.commandRaw(id, c)
      pushToast('ok', t('已下发'))
      // ★ 不关对话框：主机页底部的 Terminal 已按需求去掉，右键「执行命令」就是
      //   命令执行的入口。它要能连跑多条 —— 关不关由操作员点「关闭」决定，
      //   跑一条就自动关会逼着他每跑一条就重新右键一次。
    } catch (err: any) {
      pushToast('err', err?.message || String(err))
    }
  }

  function SideItem({ a }: { a: Agent }) {
    const live = isAgentOnline(a, { now: nowTick })
    return (
      <button
        type="button"
        className={`sess-item ${selected === a.a_id ? 'active' : ''}`}
        onClick={() => pick(a)}
        onContextMenu={(e) => {
          e.preventDefault()
          selectAgent(a.a_id)
          setCtx({ x: e.clientX, y: e.clientY, id: a.a_id })
        }}
      >
        <span className={`sdot ${live ? 'on' : 'off'}`} title={live ? t('在线') : t('离线')} />
        <div className="meta">
          <div className="name">{a.a_computer || a.a_id}</div>
          <div className="ip mono">{a.a_internal_ip || a.a_external_ip || '-'}</div>
        </div>
        <span className="tick mono dim">{fmtRelative(a.a_last_tick)}</span>
      </button>
    )
  }

  // 在线/离线组内按标签分子组（设置标签后主机归入对应标签组）
  function TaggedGroup({ list }: { list: Agent[] }) {
    const tagMap: Record<string, Agent[]> = {}
    const untagged: Agent[] = []
    for (const a of list) {
      const t = (a.a_tags || '').trim()
      if (t) { (tagMap[t] = tagMap[t] || []).push(a) } else { untagged.push(a) }
    }
    const tags = Object.keys(tagMap).sort()
    return (
      <>
        {tags.map((t) => (
          <div key={t}>
            <div className="sess-tag-g mono"># {t} <span className="n">{tagMap[t].length}</span></div>
            {tagMap[t].map((a) => <SideItem key={a.a_id} a={a} />)}
          </div>
        ))}
        {untagged.map((a) => <SideItem key={a.a_id} a={a} />)}
      </>
    )
  }

  function toggleSort(col: string) {
    if (sortCol === col) setSortAsc((v) => !v)
    else { setSortCol(col); setSortAsc(true) }
  }

  const sortedList = useMemo(() => {
    const arr = [...tableList]
    const dir = sortAsc ? 1 : -1
    const val = (a: Agent): string | number => {
      switch (sortCol) {
        case 'id': return a.a_id || ''
        case 'type': return a.a_name || ''
        case 'external': return a.a_external_ip || ''
        case 'listener': return a.a_listener || ''
        case 'internal': return a.a_internal_ip || ''
        case 'domain': return a.a_domain || ''
        case 'computer': return a.a_computer || ''
        case 'user': return a.a_username || ''
        case 'os': return a.a_os_desc || ''
        case 'process': return a.a_process || ''
        case 'pid': return Number(a.a_pid) || 0
        case 'tid': return Number(a.a_tid) || 0
        case 'tags': return a.a_tags || ''
        case 'created': return a.a_create_time || 0
        case 'last': return a.a_last_tick || 0
        case 'sleep': return a.a_sleep || 0
        case 'geo': return lookupIpGeoSync(String(a.a_external_ip || '').trim()) || ''
        default: return a.a_create_time || 0
      }
    }
    return arr.sort((a, b) => {
      const va = val(a), vb = val(b)
      if (typeof va === 'number' && typeof vb === 'number') return (va - vb) * dir
      return String(va).localeCompare(String(vb)) * dir
    })
  }, [tableList, sortCol, sortAsc, geoVer])

  const showSearchBar = searchOpen || !!filter || onlyActive || agentType !== 'ALL' || checkedIds.size > 0
  const colOn = (k: string) => cols[k] !== false

  const headers: { key: string; label: string; alignLeft?: boolean }[] = [
    { key: 'id', label: 'ID' },
    { key: 'type', label: 'Type' },
    { key: 'external', label: 'External' },
    { key: 'listener', label: 'Listener' },
    { key: 'internal', label: 'Internal' },
    { key: 'domain', label: 'Domain' },
    { key: 'computer', label: 'Computer' },
    { key: 'user', label: 'User' },
    { key: 'os', label: 'OS' },
    { key: 'process', label: 'Process', alignLeft: true },
    { key: 'pid', label: 'PID' },
    { key: 'tid', label: 'TID' },
    { key: 'tags', label: 'Tags', alignLeft: true },
    { key: 'created', label: 'Created' },
    { key: 'last', label: 'Last' },
    { key: 'sleep', label: 'Sleep' },
    { key: 'geo', label: 'Geo', alignLeft: true },
  ].filter((h) => colOn(h.key))

  function onRowClick(e: React.MouseEvent, a: Agent) {
    if (e.ctrlKey || e.metaKey) {
      toggleChecked(a.a_id, !checkedIds.has(a.a_id))
      selectAgent(a.a_id)
      return
    }
    if (checkedIds.size > 0) clearChecked()
    selectAgent(a.a_id)
  }

  return (
    <div className="qt-sessions">
      {showSearchBar && (
        <div className="qt-search-bar">
          <input
            ref={searchRef}
            className="qt-search-input"
            type="search"
            placeholder="filter: (admin | root) & ^(test)"
            value={autoSearch ? filter : draftFilter}
            onChange={(e) => {
              const v = e.target.value
              setDraftFilter(v)
              if (autoSearch) setFilter(v)
            }}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && !autoSearch) setFilter(draftFilter)
              if (e.key === 'Escape') setSearchOpen(false)
            }}
          />
          <label className="qt-check-inline" title="Auto search on text change">
            <input
              type="checkbox"
              checked={autoSearch}
              onChange={(e) => {
                setAutoSearch(e.target.checked)
                if (e.target.checked) setFilter(draftFilter)
              }}
            />
            <span>auto</span>
          </label>
          <select
            className="qt-search-combo"
            value={agentType}
            onChange={(e) => setAgentType(e.target.value)}
            title="Agent type"
          >
            <option value="ALL">All types</option>
            {agentTypes.map((tp) => (
              <option key={tp} value={tp}>{tp}</option>
            ))}
          </select>
          <label className="qt-check-inline">
            <input type="checkbox" checked={onlyActive} onChange={(e) => setOnlyActive(e.target.checked)} />
            <span>only active</span>
          </label>
          {checkedIds.size > 0 && (
            <>
              <span className="qt-south-sep" />
              <span className="mono" style={{ fontSize: 11.5 }}>{checkedIds.size} selected</span>
              <button type="button" className="danger" style={{ height: 22, padding: '0 8px' }} onClick={() => setBulkDlg(true)}>
                Remove ({checkedIds.size})
              </button>
              <button type="button" className="ghost" style={{ height: 22, padding: '0 6px' }} onClick={clearChecked}>
                Clear
              </button>
            </>
          )}
          <div style={{ flex: 1 }} />
          <button
            type="button"
            className="qt-search-x"
            title="Hide search (Esc)"
            onClick={() => {
              setSearchOpen(false)
              setFilter('')
              setDraftFilter('')
              setAgentType('ALL')
              setOnlyActive(false)
            }}
          >
            ×
          </button>
        </div>
      )}

      <div className="qt-table-wrap">
        {sortedList.length === 0 ? (
          <div className="qt-table-empty">
            <div>{Object.keys(agents).length === 0 ? t('还没有主机 — 等待接入或打开投递') : t('无匹配主机')}</div>
            {Object.keys(agents).length === 0 ? (
              <button type="button" className="primary" onClick={() => { setTab('delivery'); openUiTab('delivery') }}>
                {t('去投递')}
              </button>
            ) : null}
          </div>
        ) : (
          <table className="qt-table">
            <thead>
              <tr>
                {headers.map((h) => (
                  <th
                    key={h.key}
                    className={h.alignLeft ? 'align-l' : ''}
                    onClick={() => toggleSort(h.key)}
                  >
                    {h.label}
                    {sortCol === h.key ? (sortAsc ? ' ▲' : ' ▼') : ''}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {sortedList.map((a) => {
                const live = isAgentOnline(a, { now: nowTick })
                const hb = formatHeartbeat(a, { now: nowTick }, isLinuxAgent(a))
                const ext = String(a.a_external_ip || '').trim()
                void geoVer
                const geo = ext ? lookupIpGeoSync(ext) : ''
                const isSel = selected === a.a_id || checkedIds.has(a.a_id)
                const procText = a.a_arch ? `${a.a_process || '-'} (${a.a_arch})` : (a.a_process || '-')
                return (
                  <tr
                    key={a.a_id}
                    className={`${isSel ? 'selected' : ''} ${live ? '' : 'is-offline'}`.trim()}
                    onClick={(e) => onRowClick(e, a)}
                    onDoubleClick={() => pick(a)}
                    onContextMenu={(e) => {
                      e.preventDefault()
                      selectAgent(a.a_id)
                      setCtx({ x: e.clientX, y: e.clientY, id: a.a_id })
                    }}
                  >
                    {colOn('id') && <td className="mono" title={a.a_id}>{a.a_id}</td>}
                    {colOn('type') && <td className="mono">{a.a_name || '-'}</td>}
                    {colOn('external') && <td className="mono">{ext || '-'}</td>}
                    {colOn('listener') && <td className="mono">{a.a_listener || '-'}</td>}
                    {colOn('internal') && <td className="mono">{a.a_internal_ip || '-'}</td>}
                    {colOn('domain') && <td>{a.a_domain || ''}</td>}
                    {colOn('computer') && <td>{a.a_computer || '-'}</td>}
                    {colOn('user') && (
                      <td>
                        {a.a_elevated ? <span className="qt-user-elev">* </span> : null}
                        {a.a_username || '-'}
                      </td>
                    )}
                    {colOn('os') && (
                      <td title={a.a_os_desc || a.a_arch || ''}>
                        <span className="qt-os-cell">
                          <img className="qt-os-icon" src={osIconPath(a, live)} alt="" />
                          <span>{a.a_os_desc || a.a_arch || '-'}</span>
                        </span>
                      </td>
                    )}
                    {colOn('process') && <td className="align-l mono" title={procText}>{procText}</td>}
                    {colOn('pid') && <td className="mono">{a.a_pid || ''}</td>}
                    {colOn('tid') && <td className="mono">{a.a_tid || ''}</td>}
                    {colOn('tags') && (
                      <td className="align-l">
                        {a.a_tags ? <span className="qt-tag-pill">{a.a_tags}</span> : ''}
                      </td>
                    )}
                    {colOn('created') && <td className="mono">{fmtTime(a.a_create_time)}</td>}
                    {colOn('last') && <td className="mono" title={fmtTime(a.a_last_tick)}>{hb.text}</td>}
                    {colOn('sleep') && <td className="mono">{formatQtSleep(a)}</td>}
                    {colOn('geo') && <td className="align-l" title={geo}>{geo || ''}</td>}
                  </tr>
                )
              })}
            </tbody>
          </table>
        )}
      </div>

      {ctx && (
        <div
          className="ctx sess-ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 220), top: Math.min(ctx.y, window.innerHeight - 420) }}
          onClick={(e) => e.stopPropagation()}
        >
          <div className="ctx-hd mono">{agents[ctx.id]?.a_computer || ctx.id}</div>
          <button type="button" onClick={() => { openConsole(ctx.id); setCtx(null) }}>{t('命令终端')}</button>
          {/* 全屏**终端式**的命令执行台：走命令通道（不是交互 PTY，要真 shell 用下面的
              「交互 PTY」）。**每次都新开一个会话** —— 一台主机同时开几个是刚需
              （跟 shell / 跟日志 / 另一个窗口各一个），关掉就是关页签，随时可再开。 */}
          <button type="button" disabled={!ctxOnline} onClick={() => { openCmdTab(ctx.id); setCtx(null) }}>{t('执行命令…')}</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryPty(ctx.id); setCtx(null) }}>{t('交互 PTY')}</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryFiles(ctx.id); setCtx(null) }}>{t('文件管理')}</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryProcs(ctx.id); setCtx(null) }}>{t('进程管理')}</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryVnc(ctx.id); setCtx(null) }}>VNC</button>
          <button type="button" onClick={() => { openTunnels(ctx.id); setCtx(null) }}>{t('打开隧道')}</button>
          {/* 「查看当前目录」「获取当前用户」与 Term 快捷按钮重复，已移除 */}
          {/* Linux 主机无截图 BOF（Windows PostEx-BOF 专有） */}
          {!ctxLinux && (
            <button type="button" disabled={!ctxOnline} onClick={async () => {
              // PostEx-BOF 注册名为 screenshot_bof（不是内置 screenshot）
              try { await api.commandRaw(ctx.id, 'screenshot_bof'); openConsole(ctx.id); pushToast('ok', '已下发截图 BOF (screenshot_bof)') }
              catch (e: any) { pushLog('err', e.message || String(e)) }
              setCtx(null)
            }}>{t('截图 (BOF)')}</button>
          )}
          {/* ★ 2026-11-17 起 linux agent 支持运行时 sleep 命令（默认 30s），
              Sleep… / 交互加速对 Linux 一并开放；jitter 仅 beacon 生效。 */}
          {(
            <>
              <div className="sep" />
              <button type="button" disabled={!ctxOnline} onClick={() => {
                const a = agents[ctx.id]
                setDataDlg({
                  id: ctx.id,
                  sleep: a?.a_sleep != null ? String(a.a_sleep) : '5',
                  jitter: a?.a_jitter != null ? String(a.a_jitter) : '0',
                  computer: a?.a_computer || '',
                  username: a?.a_username || '',
                  domain: a?.a_domain || '',
                  process: a?.a_process || '',
                  internal_ip: a?.a_internal_ip || '',
                  external_ip: a?.a_external_ip || '',
                })
                setCtx(null)
              }} title="修改 Sleep / Jitter（下发 sleep 命令）">
                Sleep… {ctxAgent?.a_sleep != null ? `(${ctxAgent.a_sleep}s)` : ''}
              </button>
              <button type="button" disabled={!ctxOnline} onClick={async () => {
                try {
                  await api.commandRaw(ctx.id, 'sleep 0')
                  openConsole(ctx.id)
                  pushToast('ok', '已下发 sleep 0（交互加速）')
                } catch (e: any) { pushLog('err', e.message || String(e)) }
                setCtx(null)
              }}>{t('交互加速 (sleep 0)')}</button>
            </>
          )}
          <div className="sep" />
          <button type="button" onClick={async () => { await clearServerConsole(ctx.id); setCtx(null) }}>{t('清除控制台数据')}</button>
          <button type="button" onClick={() => {
            const a = agents[ctx.id]
            setDataDlg({
              id: ctx.id,
              sleep: a?.a_sleep != null ? String(a.a_sleep) : '5',
              jitter: a?.a_jitter != null ? String(a.a_jitter) : '0',
              computer: a?.a_computer || '',
              username: a?.a_username || '',
              domain: a?.a_domain || '',
              process: a?.a_process || '',
              internal_ip: a?.a_internal_ip || '',
              external_ip: a?.a_external_ip || '',
            })
            setCtx(null)
          }}>{t('设置资料…')}</button>
          <button type="button" onClick={async () => {
            const ok = await copyText(ctx.id)
            pushToast(ok ? 'ok' : 'err', ok ? `已复制 ${ctx.id}` : t('复制失败'))
            setCtx(null)
          }}>{t('复制会话 ID')}</button>
          <button type="button" onClick={async () => {
            const tag = prompt('标签：')
            if (tag != null) {
              try { await api.setTag([ctx.id], tag); await refreshRest(); pushToast('ok', t('标签已更新')) }
              catch (e: any) { pushLog('err', e.message || String(e)) }
            }
            setCtx(null)
          }}>{t('设置标签')}</button>
          <div className="sep" />
          <button type="button" className="danger" onClick={async () => {
            if (!confirm(`删除主机 ${ctx.id}？`)) { setCtx(null); return }
            selectAgent(ctx.id); setCtx(null)
            await removeSelected().catch((e) => pushLog('err', String(e)))
          }}>{t('删除主机')}</button>
        </div>
      )}

      {cmdDlg && (
        <CmdDlg
          id={cmdDlg}
          agent={agents[cmdDlg]}
          onClose={() => setCmdDlg(null)}
          onSubmit={(c) => submitCmd(cmdDlg, c)}
        />
      )}

      {/* 用本文件已有的 .mask + .dlg，不用原生 confirm()：50 台的清单在原生
          confirm 里既没法读也没法排版，而「删离线主机」和「删活会话」是两个
          不同的决定，必须让操作员看清拆分比例再按下去。 */}
      {bulkDlg && (
        <div className="mask" onClick={() => setBulkDlg(false)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()}>
            <h2>{t('删除')} {checkedIds.size} {t('台主机')}</h2>
            <div className="dim mono" style={{ marginBottom: 8 }}>
              <span className="signal">{bulkOnline} 台在线</span>
              {' · '}{bulkAgents.length - bulkOnline} 台离线
            </div>
            <div className="mono" style={{ fontSize: 12, lineHeight: 1.7 }}>
              {bulkAgents.slice(0, 5).map((a) => (
                <div key={a.a_id}>{a.a_computer || a.a_id}</div>
              ))}
              {bulkAgents.length > 5 && <div className="dim">… {t('等')} {bulkAgents.length}</div>}
            </div>
            <p className="hint muted-hint">
              这些主机的控制台记录、文件/进程页签与本地缓存会一并清除。
              在线主机会在下次回连时重新注册。
            </p>
            <div className="btns">
              <button onClick={() => setBulkDlg(false)}>{t('取消')}</button>
              <button className="danger" onClick={() => {
                setBulkDlg(false)
                removeChecked().catch((e) => pushLog('err', String(e)))
              }}>{t('删除')} {checkedIds.size}</button>
            </div>
          </div>
        </div>
      )}

      {dataDlg && (
        <div className="mask" onClick={() => setDataDlg(null)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()} style={{ maxWidth: 480 }}>
            <h2>{t('设置资料')}</h2>
            <div className="dim mono" style={{ marginBottom: 8 }}>{dataDlg.id}</div>
            <div className="field-grid-2">
              <div className="field"><label>{t('Sleep（秒，下发任务）')}</label>
                <input value={dataDlg.sleep} onChange={(e) => setDataDlg({ ...dataDlg, sleep: e.target.value })} />
              </div>
              <div className="field"><label>Jitter % {!isLinuxAgent(agents[dataDlg.id]) ? '' : '(beacon only)'}</label>
                <input value={dataDlg.jitter} onChange={(e) => setDataDlg({ ...dataDlg, jitter: e.target.value })} />
              </div>
            </div>
            <div className="hint muted-hint">{t('Sleep/Jitter 通过')} <code>sleep</code> {t('命令下发；下方字段写入服务端展示数据。')}</div>
            <div className="field-grid-2">
              <div className="field"><label>Computer</label>
                <input value={dataDlg.computer} onChange={(e) => setDataDlg({ ...dataDlg, computer: e.target.value })} />
              </div>
              <div className="field"><label>Username</label>
                <input value={dataDlg.username} onChange={(e) => setDataDlg({ ...dataDlg, username: e.target.value })} />
              </div>
              <div className="field"><label>Domain</label>
                <input value={dataDlg.domain} onChange={(e) => setDataDlg({ ...dataDlg, domain: e.target.value })} />
              </div>
              <div className="field"><label>Process</label>
                <input value={dataDlg.process} onChange={(e) => setDataDlg({ ...dataDlg, process: e.target.value })} />
              </div>
              <div className="field"><label>Internal IP</label>
                <input value={dataDlg.internal_ip} onChange={(e) => setDataDlg({ ...dataDlg, internal_ip: e.target.value })} />
              </div>
              <div className="field"><label>External IP</label>
                <input value={dataDlg.external_ip} onChange={(e) => setDataDlg({ ...dataDlg, external_ip: e.target.value })} />
              </div>
            </div>
            <div className="btns">
              <button onClick={() => setDataDlg(null)}>{t('取消')}</button>
              <button className="primary" onClick={async () => {
                try {
                  const sleepSec = Number(dataDlg.sleep)
                  if (!Number.isNaN(sleepSec) && sleepSec >= 0) {
                    if (isLinuxAgent(agents[dataDlg.id])) {
                      // linux sleep 命令只吃秒数（30 / 30s 均可），jitter 不适用
                      await api.commandRaw(dataDlg.id, `sleep ${sleepSec}`)
                    } else {
                      const jit = Number(dataDlg.jitter)
                      const j = Number.isNaN(jit) ? 0 : Math.max(0, Math.min(100, jit))
                      const cmd = j > 0 ? `sleep ${sleepSec}s ${j}` : `sleep ${sleepSec}s`
                      await api.commandRaw(dataDlg.id, cmd)
                    }
                  }
                  await api.updateAgentData(dataDlg.id, {
                    computer: dataDlg.computer,
                    username: dataDlg.username,
                    domain: dataDlg.domain,
                    process: dataDlg.process,
                    internal_ip: dataDlg.internal_ip,
                    external_ip: dataDlg.external_ip,
                  })
                  await refreshRest()
                  pushToast('ok', t('已更新'))
                  setDataDlg(null)
                } catch (e: any) {
                  pushToast('err', e?.message || String(e))
                }
              }}>{t('保存')}</button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

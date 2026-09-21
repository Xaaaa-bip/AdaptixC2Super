import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import type { Agent } from '../api/types'
import { copyText, formatHeartbeat, fmtRelative, fmtTime, isAgentOnline, isLinuxAgent } from '../utils'
import { lookupIpGeoSync, preloadIpGeo } from '../utils/ipdb'
import { FALLBACK_BOFS, FALLBACK_COMMANDS } from '../lib/completions'
import { useCompletion } from '../lib/useCompletion'
import { CmdSuggest } from './CmdSuggest'
import { PageTips } from './PageTips'

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
        <h2>执行命令</h2>
        <div className="dim mono" style={{ marginBottom: 8 }}>{id}</div>
        <div className="field">
          <label>命令行</label>
          <input
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
          <button onClick={onClose}>取消</button>
          <button className="primary" onClick={submit}>执行</button>
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

  const filteredFixed = useMemo(() => {
    let arr = Object.values(agents)
    if (onlyActive) arr = arr.filter((a) => isAgentOnline(a))
    if (filter.trim()) {
      const q = filter.trim().toLowerCase()
      arr = arr.filter((a) =>
        [a.a_id, a.a_computer, a.a_username, a.a_internal_ip, a.a_external_ip, a.a_process, a.a_listener, a.a_tags, a.a_domain]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(q)))
    }
    return arr.sort((a, b) => (b.a_last_tick || 0) - (a.a_last_tick || 0))
  }, [agents, filter, onlyActive])

  const online = filteredFixed.filter((a) => isAgentOnline(a))
  const offline = filteredFixed.filter((a) => !isAgentOnline(a))
  const selectedAgent = selected ? agents[selected] : null
  const selectedOnline = !!(selectedAgent && isAgentOnline(selectedAgent))
  const ctxAgent = ctx ? agents[ctx.id] : null
  const ctxOnline = !!(ctxAgent && isAgentOnline(ctxAgent))
  // Linux 主机：无截图 BOF、sleep 0 无意义（linux_agent 不写注册表式 sleep 0 加速）
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
  const bulkOnline = bulkAgents.filter((a) => isAgentOnline(a)).length

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
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开文件管理'); return }
    openFilesBrowser(id)
  }
  function tryProcs(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开进程管理'); return }
    openProcsBrowser(id)
  }
  function tryPty(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开 PTY'); return }
    openPty(id)
  }
  function tryVnc(id: string) {
    const a = agents[id]
    if (!a || !isAgentOnline(a)) { pushToast('warn', '主机离线，无法打开 VNC'); return }
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
      pushToast('ok', '已清除控制台数据')
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
      pushToast('ok', '已下发')
      // ★ 不关对话框：主机页底部的 Terminal 已按需求去掉，右键「执行命令」就是
      //   命令执行的入口。它要能连跑多条 —— 关不关由操作员点「关闭」决定，
      //   跑一条就自动关会逼着他每跑一条就重新右键一次。
    } catch (err: any) {
      pushToast('err', err?.message || String(err))
    }
  }

  function SideItem({ a }: { a: Agent }) {
    const live = isAgentOnline(a)
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
        <span className={`sdot ${live ? 'on' : 'off'}`} title={live ? '在线' : '离线'} />
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

  return (
    <div className="sess">
      <section className="sess-main">
        <div className="sess-bar">
          <input
            className="sess-search"
            type="search"
            placeholder="搜索主机 / IP / 用户 / 标签"
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
          />
          <label className="sess-check">
            <input type="checkbox" checked={onlyActive} onChange={(e) => setOnlyActive(e.target.checked)} />
            仅在线
          </label>

          {/* 批量条独立于 .sess-actions：后者是**单选**工具条（作用于 selectedAgentId），
              两者混在一起，操作员就分不清「删除」删的到底是哪一个。 */}
          {checkedIds.size > 0 && (
            <div className="sess-bulk">
              <span className="sb-n">已选 <b>{checkedIds.size}</b> 台</span>
              {hiddenChecked > 0 && (
                <span className="sb-note" title="勾选不会被搜索收窄悄悄丢掉">
                  （{hiddenChecked} 台不在当前筛选里）
                </span>
              )}
              <button className="ghost" onClick={() => setChecked(tableIds, !allChecked)}>
                {allChecked ? '取消全选' : '全选当前结果'}
              </button>
              <button className="ghost" onClick={clearChecked}>清除</button>
              <button className="danger" onClick={() => setBulkDlg(true)}>
                删除 {checkedIds.size} 台
              </button>
            </div>
          )}

          <div className="sess-actions">
            {selectedAgent ? (
              <>
                <span className="sess-sel mono">{selectedAgent.a_computer || selected}</span>
                <span className="sep" />
                <button disabled={!selectedOnline} onClick={() => selected && tryFiles(selected)}>文件</button>
                <button disabled={!selectedOnline} onClick={() => selected && tryProcs(selected)}>进程</button>
                <button disabled={!selectedOnline} onClick={() => selected && tryPty(selected)}>PTY</button>
                <button disabled={!selectedOnline} onClick={() => selected && tryVnc(selected)}>VNC</button>
                <button onClick={() => selected && openTunnels(selected)}>隧道</button>
                <button className="danger" onClick={() => removeSelected().catch((e) => pushLog('err', String(e)))}>删除</button>
              </>
            ) : (
              <span className="dim">点表格行选中并打开控制台 · 右键出操作菜单 · 勾选可批量</span>
            )}
          </div>
          <PageTips id="hosts">
            <p><b>右键</b>主机行打开操作菜单：终端 / PTY / 文件 / 进程 / 隧道 / 执行命令。</p>
            <p><code>Ctrl+Shift+F</code> 文件管理 · <code>Ctrl+Shift+P</code> 进程管理 · <code>/</code> 聚焦搜索 · 双击行打开控制台。</p>
            <p>Linux 主机无截图与 sleep 交互加速（菜单已自动隐藏）。</p>
          </PageTips>
        </div>

        <div className="sess-table-wrap">
          {tableList.length === 0 ? (
            <div className="sess-empty big">
              {Object.keys(agents).length === 0 ? (
                <>
                  <div className="title">还没有主机</div>
                  <div className="hint">打开投递，复制一条命令到目标机器执行</div>
                  <button
                    type="button"
                    className="primary"
                    style={{ marginTop: 12 }}
                    onClick={() => { setTab('delivery'); openUiTab('delivery') }}
                  >
                    去投递
                  </button>
                </>
              ) : (
                <>
                  <div className="title">无匹配主机</div>
                  <div className="hint">调整搜索条件，或取消「仅在线」</div>
                </>
              )}
            </div>
          ) : (
            <table className="sess-table">
              <thead>
                <tr>
                  <th className="sess-cell-check">
                    <label className="sess-check" title="全选 / 取消全选当前筛选结果">
                      <input
                        type="checkbox"
                        checked={allChecked}
                        // indeterminate 不是 React prop（只是 DOM 属性），得手动设。
                        // 回调 ref 每次渲染都会重跑，所以这个值不会滞留。
                        ref={(el) => { if (el) el.indeterminate = someChecked }}
                        onChange={(e) => setChecked(tableIds, e.target.checked)}
                      />
                    </label>
                  </th>
                  <th style={{ width: 56 }}>状态</th>
                  <th>主机</th>
                  <th>用户</th>
                  <th>内网</th>
                  <th>外网</th>
                  <th>归属地</th>
                  <th>进程</th>
                  <th>系统</th>
                  <th>监听</th>
                  <th title="距上次回连 / 配置 sleep（动态刷新）">心跳</th>
                  <th>标签</th>
                </tr>
              </thead>
              <tbody>
                {tableList.map((a) => {
                  const live = isAgentOnline(a, { now: nowTick })
                  const hb = formatHeartbeat(a, { now: nowTick }, isLinuxAgent(a))
                  const ext = String(a.a_external_ip || '').trim()
                  // geoVer 仅触发重渲染；lookup 同步读缓存
                  void geoVer
                  const geo = ext ? lookupIpGeoSync(ext) : ''
                  return (
                    <tr
                      key={a.a_id}
                      className={`${selected === a.a_id ? 'selected' : ''} ${checkedIds.has(a.a_id) ? 'checked' : ''}`.trim()}
                      onClick={() => pick(a)}
                      onContextMenu={(e) => {
                        e.preventDefault()
                        selectAgent(a.a_id)
                        setCtx({ x: e.clientX, y: e.clientY, id: a.a_id })
                      }}
                    >
                      <td className="sess-cell-check">
                        {/* ★ stopPropagation 是本功能最大的回归风险：这一行上有
                            onClick={() => pick(a)}（选中 + 开控制台）和 onContextMenu。
                            不拦传播的话，每点一次复选框就多开一个控制台页签。
                            用 <label> 包住 <input> 可同时保住「点哪儿都能切换」和拦截。 */}
                        <label className="sess-check" onClick={(e) => e.stopPropagation()}>
                          <input
                            type="checkbox"
                            checked={checkedIds.has(a.a_id)}
                            onChange={(e) => toggleChecked(a.a_id, e.target.checked)}
                          />
                        </label>
                      </td>
                      <td>
                        <span className={`st ${live ? 'on' : 'off'}`}>{live ? '在线' : '离线'}</span>
                      </td>
                      <td>
                        <div className="cell-main">{a.a_computer || '-'}</div>
                        <div className="cell-sub mono" title={a.a_id}>{a.a_id}</div>
                      </td>
                      <td>
                        <div>{a.a_domain ? `${a.a_domain}\\` : ''}{a.a_username || '-'}</div>
                        {a.a_elevated ? <div className="cell-sub signal">elevated</div> : null}
                      </td>
                      <td className="mono">{a.a_internal_ip || '-'}</td>
                      <td className="mono" title={ext || ''}>{ext || '-'}</td>
                      <td className="dim" title={geo || (ext ? '解析中/未知' : '')}>{geo || (ext ? '…' : '-')}</td>
                      <td>
                        <div>{a.a_process || '-'}</div>
                        <div className="cell-sub mono">{a.a_pid || ''}</div>
                      </td>
                      <td className="dim">{a.a_os_desc || a.a_arch || '-'}</td>
                      <td className="mono">{a.a_listener || '-'}</td>
                      <td className="mono" title={fmtTime(a.a_last_tick)}>
                        <div className={live ? '' : 'dim'}>{hb.text}</div>
                        {hb.sub ? <div className="cell-sub">{hb.sub}</div> : null}
                      </td>
                      <td>{a.a_tags || ''}</td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          )}
        </div>
      </section>

      {ctx && (
        <div
          className="ctx sess-ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 220), top: Math.min(ctx.y, window.innerHeight - 420) }}
          onClick={(e) => e.stopPropagation()}
        >
          <div className="ctx-hd mono">{agents[ctx.id]?.a_computer || ctx.id}</div>
          <button type="button" onClick={() => { openConsole(ctx.id); setCtx(null) }}>命令终端</button>
          {/* 全屏 PTY（xterm），**每次都新开一个会话** —— 一台主机同时开几个终端是刚需
    （盯 shell / 跟日志 / 另一个 shell 各一个）。关掉就是关页签，随时可再开。 */}
          <button type="button" disabled={!ctxOnline} onClick={() => { openPty(ctx.id, { newSession: true }); setCtx(null) }}>执行命令…</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryPty(ctx.id); setCtx(null) }}>交互 PTY</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryFiles(ctx.id); setCtx(null) }}>文件管理</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryProcs(ctx.id); setCtx(null) }}>进程管理</button>
          <button type="button" disabled={!ctxOnline} onClick={() => { tryVnc(ctx.id); setCtx(null) }}>VNC</button>
          <button type="button" onClick={() => { openTunnels(ctx.id); setCtx(null) }}>打开隧道</button>
          {/* 「查看当前目录」「获取当前用户」与 Term 快捷按钮重复，已移除 */}
          {/* Linux 主机无截图 BOF（Windows PostEx-BOF 专有） */}
          {!ctxLinux && (
            <button type="button" disabled={!ctxOnline} onClick={async () => {
              // PostEx-BOF 注册名为 screenshot_bof（不是内置 screenshot）
              try { await api.commandRaw(ctx.id, 'screenshot_bof'); openConsole(ctx.id); pushToast('ok', '已下发截图 BOF (screenshot_bof)') }
              catch (e: any) { pushLog('err', e.message || String(e)) }
              setCtx(null)
            }}>截图 (BOF)</button>
          )}
          {/* Linux 主机 sleep 0 交互加速无意义：隐藏 Sleep… 与交互加速两项 */}
          {!ctxLinux && (
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
              }}>交互加速 (sleep 0)</button>
            </>
          )}
          <div className="sep" />
          <button type="button" onClick={async () => { await clearServerConsole(ctx.id); setCtx(null) }}>清除控制台数据</button>
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
          }}>设置资料…</button>
          <button type="button" onClick={async () => {
            const ok = await copyText(ctx.id)
            pushToast(ok ? 'ok' : 'err', ok ? `已复制 ${ctx.id}` : '复制失败')
            setCtx(null)
          }}>复制会话 ID</button>
          <button type="button" onClick={async () => {
            const tag = prompt('标签：')
            if (tag != null) {
              try { await api.setTag([ctx.id], tag); await refreshRest(); pushToast('ok', '标签已更新') }
              catch (e: any) { pushLog('err', e.message || String(e)) }
            }
            setCtx(null)
          }}>设置标签</button>
          <div className="sep" />
          <button type="button" className="danger" onClick={async () => {
            if (!confirm(`删除主机 ${ctx.id}？`)) { setCtx(null); return }
            selectAgent(ctx.id); setCtx(null)
            await removeSelected().catch((e) => pushLog('err', String(e)))
          }}>删除主机</button>
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
            <h2>删除 {checkedIds.size} 台主机</h2>
            <div className="dim mono" style={{ marginBottom: 8 }}>
              <span className="signal">{bulkOnline} 台在线</span>
              {' · '}{bulkAgents.length - bulkOnline} 台离线
            </div>
            <div className="mono" style={{ fontSize: 12, lineHeight: 1.7 }}>
              {bulkAgents.slice(0, 5).map((a) => (
                <div key={a.a_id}>{a.a_computer || a.a_id}</div>
              ))}
              {bulkAgents.length > 5 && <div className="dim">…等 {bulkAgents.length} 台</div>}
            </div>
            <p className="hint muted-hint">
              这些主机的控制台记录、文件/进程页签与本地缓存会一并清除。
              在线主机会在下次回连时重新注册。
            </p>
            <div className="btns">
              <button onClick={() => setBulkDlg(false)}>取消</button>
              <button className="danger" onClick={() => {
                setBulkDlg(false)
                removeChecked().catch((e) => pushLog('err', String(e)))
              }}>删除 {checkedIds.size} 台</button>
            </div>
          </div>
        </div>
      )}

      {dataDlg && (
        <div className="mask" onClick={() => setDataDlg(null)}>
          <div className="dlg" onClick={(e) => e.stopPropagation()} style={{ maxWidth: 480 }}>
            <h2>设置资料</h2>
            <div className="dim mono" style={{ marginBottom: 8 }}>{dataDlg.id}</div>
            {/* Linux 主机 sleep 为编译期固定：不下发 sleep 命令，隐藏输入 */}
            {isLinuxAgent(agents[dataDlg.id]) ? (
              <span className="hint muted-hint">Linux 主机 sleep 为编译期固定，不可修改</span>
            ) : (
              <>
                <div className="field-grid-2">
                  <div className="field"><label>Sleep（秒，下发任务）</label>
                    <input value={dataDlg.sleep} onChange={(e) => setDataDlg({ ...dataDlg, sleep: e.target.value })} />
                  </div>
                  <div className="field"><label>Jitter %</label>
                    <input value={dataDlg.jitter} onChange={(e) => setDataDlg({ ...dataDlg, jitter: e.target.value })} />
                  </div>
                </div>
                <div className="hint muted-hint">Sleep/Jitter 通过 <code>sleep</code> 命令下发；下方字段写入服务端展示数据。</div>
              </>
            )}
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
              <button onClick={() => setDataDlg(null)}>取消</button>
              <button className="primary" onClick={async () => {
                try {
                  // Linux 主机 sleep 为编译期固定：跳过 sleep 下发，仅更新展示数据
                  if (!isLinuxAgent(agents[dataDlg.id])) {
                    const sleepSec = Number(dataDlg.sleep)
                    const jit = Number(dataDlg.jitter)
                    if (!Number.isNaN(sleepSec) && sleepSec >= 0) {
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
                  pushToast('ok', '已更新')
                  setDataDlg(null)
                } catch (e: any) {
                  pushToast('err', e?.message || String(e))
                }
              }}>保存</button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

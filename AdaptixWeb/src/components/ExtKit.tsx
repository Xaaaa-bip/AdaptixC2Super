import { useCallback, useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { loadCmdTree } from '../api/cmdTree'
import type { CmdArgDef, CmdBatch, CmdDef, CmdGroup } from '../api/types'
import { copyText, isAgentOnline } from '../utils'
import { EmptyState } from './ui'
import { PageTips } from './PageTips'

/* Extension-Kit 命令总览。
 *
 * 数据源就是控制台自己用的那棵命令树（POST /api/axscript/commands），
 * 只取 source=1 的批次。为什么是 1：`SourceType` 在 registry.go 里是
 * `SourceAgent=iota, SourceProfile, SourceUser`，序列化成**数字**（api/types.ts
 * 的 CmdBatch.source 也标注了同一件事）。Extension-Kit 是 profile 的 `axscripts:`
 * 项加载的，落进 ProfileRegistry ⇒ source=1；source=0 是各 agent 自带的那套
 * （beacon 按 listener 分 5 组、kharon 13 条、linux 36 条…），不在本页范围。
 *
 * ★ 这页**一个命令名都不硬编码**，全部现读服务端。理由和 Bofs.tsx 同源：
 *   命令集在 .axs 里，抄一份到前端就等于多一份会漂移的真值，而漂移的表现
 *   不是报错、是「页面上没有这条命令」——操作员只会以为自己记错了。
 *
 * ★ 为什么展示 example 而不是自己拼一份「标准用法」：这批命令带**预钩子**
 *   （has_pre_hook）。服务端拿到 cmdline 后先跑 .axs 的钩子，把整条命令展开成
 *   `execute bof <script_dir>_bin/<file>.<arch>.o <bof_params>` —— 也就是说
 *   **参数的写法由钩子决定，不由这里的参数表决定**。参数表是 schema（给补全和
 *   本页展示用），mark 才是钩子认的开关。所以：example 是权威用法，参数表是
 *   参考，两者冲突时以 example 为准。
 *
 * ★ mark 的三种形态（实测 beacon 批次的 ldapsearch / dir / nslookup / askcreds）：
 *     name 非空 + flag   → `-a <值>`、`--dc <值>`、`-c 40`（带值开关）
 *     name 为空 + flag   → `--ldaps`、`/s`、`-o`（纯开关，只要 mark 本身）
 *     name 非空 + !flag  → 位置参数，直接放值（ldapsearch 的 query、inject-* 的三个）
 *   判据是 name 是否为空，**不是** required：纯开关的 required 可能是 false，
 *   而 askcreds 的 `-p/-n/-t` 是 required=true 的带值开关。
 *
 * ★ 「下发」只在**命令名本身就是一条完整命令**时才出现，判据三条全中：
 *     ① 没有必填的值参数（纯开关不算，它没值可填）；
 *     ② 没有子命令 —— 有子命令就说明必须挑一个（`dcsync` 的示例是
 *        `dcsync {subcommand} [options]`，示例里那个 `{subcommand}` 不是装饰）；
 *     ③ 示例里没有占位符（`<dn>`、`{subcommand}`、`[options]`）。
 *   62 条里只有 18 条全中（whoami / hashdump / ipconfig / lsadump_* …），其余
 *   一律只给复制，让操作员把 example 改好粘进控制台。理由是这个项目已经吃过一次
 *   「参数错位照跑、不报错」的亏（见 Bofs.tsx 顶部的 notes 约定），与其在页面上
 *   替他猜钩子的语法，不如把权威文本交给他。
 *   ★ 这条规则在浏览器里被验证逼出来的：最初只判 ①，于是 36 条可下发 —— 其中
 *   18 条是带子命令的派发器（dcsync / mssql / ldap / kerbeus / token…），
 *   点下去发出去的是光秃秃一个名字，**不报错但没有意义**。
 */

/** axscript.SourceProfile —— 见 api/types.ts 的 CmdBatch.source 注释 */
const SOURCE_PROFILE = 1

type AgentBlock = { agent: string; groups: CmdGroup[]; names: Set<string> }

/** 「必填且要填值」的参数——纯开关不算（它没值可填） */
function requiredArgs(c: CmdDef): CmdArgDef[] {
  return (c.args || []).filter((a) => a.required && !(a.flag && !a.name))
}

/** 参数表里一行怎么念 */
function argUsage(a: CmdArgDef): string {
  if (a.flag) return a.name ? `${a.mark || '--' + a.name} <${a.name}>` : (a.mark || `--${a.name}`)
  return `<${a.name || '值'}>`
}

/**
 * 一条命令的命令行 —— 就是命令名。
 * ★ 这里**不能**把子命令拼上去：早先写成 `[name, ...subcommands].join(' ')`，
 *   于是 dcsync 的「复制命令」给出 `dcsync single all` —— 语法像个命令，
 *   实际是三个互斥选项连在一起，粘进控制台只会静默失败。
 */
function cmdlineOf(c: CmdDef): string {
  return c.name
}

/** 示例里的占位符。出现任何一个就说明「只有命令名」不成命令 */
const PLACEHOLDER = /[<{[]/
function exampleHasPlaceholder(ex: string): boolean {
  return PLACEHOLDER.test(ex || '')
}

/**
 * 命令名本身是否就是一条完整命令（= 可以一键下发）。
 * 三条判据见文件头；三条里最容易被漏掉的是子命令那条 —— 子命令是**必选**的，
 * 而它在参数表里根本不出现（不是 arg），所以只看 args 会把它判成无参命令。
 */
function sendsAsIs(c: CmdDef): boolean {
  if (requiredArgs(c).length) return false
  if ((c.subcommands || []).length) return false
  return !exampleHasPlaceholder(c.example)
}

function matches(c: CmdDef, needle: string): boolean {
  if (!needle) return true
  const n = needle.toLowerCase()
  const hay = [
    c.name, c.description, c.message, c.example,
    ...(c.args || []).flatMap((a) => [a.name, a.mark, a.description, a.type]),
    ...(c.subcommands || []).flatMap((s) => [s.name, s.description]),
  ]
  return hay.some((v) => (v || '').toLowerCase().includes(n))
}

export function ExtKit() {
  const api = useStore((s) => s.api)
  const agents = useStore((s) => s.agents) || {}
  const selectedAgentId = useStore((s) => s.selectedAgentId)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  const openConsole = useStore((s) => s.openConsole)

  const [tree, setTree] = useState<CmdBatch[]>([])
  const [loading, setLoading] = useState(false)
  const [err, setErr] = useState('')
  const [agent, setAgent] = useState('')
  const [group, setGroup] = useState('')
  const [q, setQ] = useState('')
  const [busy, setBusy] = useState('')

  const load = useCallback(async (force = false) => {
    if (!api) return
    setLoading(true)
    setErr('')
    try {
      setTree(await loadCmdTree(api, force))
    } catch (e: any) {
      setTree([])
      setErr(String(e?.message || e || '拉取命令树失败'))
    } finally {
      setLoading(false)
    }
  }, [api])

  useEffect(() => { void load() }, [load])

  /** 只留 Extension-Kit（profile 批次），丢掉没有命令的批次 */
  const blocks = useMemo<AgentBlock[]>(() => {
    const out: AgentBlock[] = []
    for (const b of tree) {
      if (b.source !== SOURCE_PROFILE) continue
      const groups = (b.groups || []).filter((g) => (g.commands || []).length > 0)
      if (!groups.length) continue
      const names = new Set<string>()
      for (const g of groups) for (const c of g.commands) names.add(c.name)
      out.push({ agent: b.agent, groups, names })
    }
    return out
  }, [tree])

  // 默认显示「当前选中主机所属的那一份」，而不是固定第一份：三份的 agent 名
  // 不同（beacon/gopher/kharon），显示错的那份会让操作员以为自己主机支持这条命令。
  /**
   * ★ 焦点主机 = 显式选中的那台（且在线），没选就退到**第一台在线主机**。
   *
   *   为什么必须退：本页没有任何「选主机」的入口，只认主机页的选中项。于是操作员
   *   手里有在线 beacon、本页正显示 beacon 的 62 条命令，18 个「下发」却全是灰的
   *   —— 除了一句要悬停才看到的 title 之外没有任何提示，读起来就是「这功能坏了」。
   *   BOF 模块页（Bofs.tsx 的 effectiveAgent）早就有同样的兜底，本页漏了。
   *
   *   为什么焦点要**同时**喂给下面那条 effect：它按焦点主机决定显示哪一份命令，而
   *   `canSend` 又要求「显示的批次 == 这台 agent 的名字」。两处各自兜底就会打架
   *   —— 焦点退到 beacon、显示的却还是 blocks[0]，按钮照样是灰的。
   *
   *   兜底只挑**名字能对上某个批次**的在线主机，免得退到一台 gopher 上、把显示
   *   切成 gopher 又反过来判自己不合格。
   */
  const focusId = (selectedAgentId && isAgentOnline(agents[selectedAgentId] as any))
    ? selectedAgentId
    : (((Object.values(agents) as any[]).find(
        (a) => a && isAgentOnline(a) && blocks.some((b) => b.agent === a.a_name),
      )?.a_id as string) || '')
  const focusName = ((focusId && (agents[focusId] as any)?.a_name) || '') as string
  useEffect(() => {
    if (agent && blocks.some((b) => b.agent === agent)) return
    setAgent(blocks.some((b) => b.agent === focusName) ? focusName : (blocks[0]?.agent || ''))
  }, [blocks, agent, focusName])

  const block = blocks.find((b) => b.agent === agent) || blocks[0]

  /** 三份集合是否一致；不一致时每条命令标「只有谁有」 */
  const uniform = useMemo(() => {
    if (!block) return true
    return blocks.filter((b) => b.agent !== block.agent).every(
      (o) => o.names.size === block.names.size && [...block.names].every((n) => o.names.has(n)),
    )
  }, [blocks, block])

  const missingOf = useCallback((name: string): string[] => {
    if (!block) return []
    return blocks.filter((b) => b.agent !== block.agent && !b.names.has(name)).map((b) => b.agent)
  }, [blocks, block])

  const searched = useMemo(() => {
    if (!block) return [] as CmdDef[]
    const out: CmdDef[] = []
    for (const g of block.groups) for (const c of g.commands) if (matches(c, q)) out.push(c)
    return out
  }, [block, q])

  const activeGroup = block?.groups.find((g) => g.group_name === group) || null
  const shownGroups = activeGroup ? [activeGroup] : (block?.groups || [])
  const visible = activeGroup
    ? searched.filter((c) => activeGroup.commands.some((x) => x.name === c.name))
    : searched

  const totalCmds = block ? block.names.size : 0

  const target = focusId
  const targetName = focusName
  /** 兜底选中的（不是操作员显式点的那台）要在界面上写明，别让人以为发错了地方 */
  const targetImplicit = !!target && target !== selectedAgentId
  const targetHost = ((target && (agents[target] as any)?.a_computer) || '') as string
  /**
   * ★ 能不能往这台主机发：显示的批次必须**就是**这台 agent 的那一份。
   *   显示 gopher 的命令却发给 beacon，钩子按 gopher 的名字解析、插件按 beacon 的
   *   名字派发 —— 这正是「静默做错事」的配方，所以在按钮上就拦住。
   */
  const canSend = !!target && !!block && targetName === block.agent

  async function send(c: CmdDef) {
    if (!api || !target || !block) return
    if (targetName !== block.agent) {
      pushToast('warn', `当前主机是 ${targetName || '未知'} agent，本页显示的是 ${block.agent} 的命令`)
      return
    }
    // 按钮已经拦过一道，这里再拦一道：按钮是 UI 约定，这条是行为保证
    if (!sendsAsIs(c)) {
      pushToast('warn', `${c.name} 需要子命令或参数，请照示例改好再发`)
      return
    }
    const cmdline = cmdlineOf(c)
    setBusy(c.name)
    try {
      await api.commandExecute(target, cmdline, { command: c.name }, false)
      pushLog('info', `扩展命令 ${cmdline} 已下发`, false)
      openConsole(target)
    } catch {
      pushToast('err', `${c.name} 下发失败`)
    } finally {
      setBusy('')
    }
  }

  async function copy(text: string, what: string) {
    const ok = await copyText(text)
    pushToast(ok ? 'ok' : 'err', ok ? `${what}已复制` : '复制失败')
  }

  return (
    <div className="page">
      <div className="toolbar">
        <span className="xkit-title">Extension-Kit 命令</span>
        <span className="dim" style={{ fontSize: 12 }}>
          {blocks.length
            ? <>{blocks.length} 个 agent 批次 · 当前 <b>{totalCmds}</b> 条 / <b>{block?.groups.length || 0}</b> 组 · 命令树 source=profile</>
            : '等待命令树'}
        </span>
        <span style={{ flex: 1 }} />
        {/* 「下发」到底发给谁，必须写在脸上：焦点主机没选时会兜底到第一台在线主机，
            不写出来就是一次静默改道 —— 本页的存在意义正是拦住这种静默 */}
        <span
          className="dim"
          style={{ fontSize: 12 }}
          title={target
            ? (targetImplicit
                ? '主机页没有选中主机，按第一台在线主机下发'
                : '与主机页选中的主机一致')
            : '当前没有在线主机，所有「下发」都不可用'}
        >
          {target
            ? <>下发目标 <b className="mono">{targetName}</b>{targetHost ? ` · ${targetHost}` : ''}{targetImplicit ? '（未选中，取第一台在线）' : ''}</>
            : '下发目标 无在线主机'}
        </span>
        {blocks.length > 1 && (
          <span className="xkit-chips">
            {blocks.map((b) => (
              <button
                key={b.agent}
                type="button"
                className={`xkit-chip ${b.agent === block?.agent ? 'on' : ''}`}
                onClick={() => { setAgent(b.agent); setGroup('') }}
                title={`${b.agent} 注册的命令集（${b.names.size} 条）`}
              >
                {b.agent} <span className="dim">{b.names.size}</span>
              </button>
            ))}
          </span>
        )}
        <input
          className="xkit-search"
          placeholder="搜索命令 / 参数 / 示例…"
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <button type="button" className="ghost" onClick={() => { void load(true) }} disabled={loading || !api} title="重新拉取命令树">
          {loading ? '…' : '刷新'}
        </button>
        <PageTips id="extkit">
          <p><b>这一页是什么。</b>Extension-Kit 那 11 个 <code>.axs</code> 注册进服务端的命令，按分组列全。数据来自控制台自己用的那棵命令树（<code>POST /api/axscript/commands</code> 里 <code>source=profile</code> 的批次），页面上没有任何硬编码的命令名 —— 改 <code>.axs</code> 后重载 extender 再刷新即可。</p>
          <p><b>为什么给「示例」而不是「参数表单」。</b>这些命令带预钩子：服务端拿到命令行后先跑 <code>.axs</code> 的钩子，把它展开成 <code>execute bof …_bin/&lt;file&gt;.&lt;arch&gt;.o</code>。参数怎么写由钩子决定，参数表只是 schema。所以示例是权威用法，参数表是参考，两者冲突时以示例为准。</p>
          <p><b>下发按钮为什么只出现在一部分命令上。</b>只有「命令名本身就是一条完整命令」的才给下发，三条全中才算：没有必填值参数、<b>没有子命令</b>、示例里没有 <code>&lt;dn&gt;</code>/<code>&#123;subcommand&#125;</code>/<code>[options]</code> 这类占位符。全表 62 条里只有 18 条合格（<code>whoami</code>、<code>hashdump</code>、<code>ipconfig</code>…）；其余一律只给复制。最容易看错的是子命令那条：子命令是必选的，但它不在参数表里，所以「无参数」不等于「无输入」—— <code>dcsync</code> 的示例就是 <code>dcsync &#123;subcommand&#125; [options]</code>，发一个光秃秃的 <code>dcsync</code> 出去不会报错，只是没有意义。</p>
          <p><b>下发到哪台。</b>只发给主机页当前选中的那台，且必须与页面上显示的 agent 名一致（<span className="mono">主机 a_name == 批次 agent</span>）；不一致时按钮是灰的，不会拿 gopher 的命令去发给 beacon。</p>
          <p><b>产物缺失不会报错。</b>这些命令最终落到 <code>&lt;script_dir&gt;_bin/</code> 下的 <code>.o</code>；产物没编译时下发不报错、也没有回显。到 <b>BOF 模块</b>页确认真有那个产物，别在控制台里等一个永远不来的输出。</p>
          <p><b>跨 agent 差异。</b>{uniform ? '当前各 agent 的命令集合完全相同。' : '各 agent 的命令集合不一致，差异标在对应命令上（「仅 xx」）。'}</p>
        </PageTips>
      </div>

      {!!(block && block.groups.length) && (
        <div className="xkit-groups">
          <button type="button" className={`xkit-chip ${group === '' ? 'on' : ''}`} onClick={() => setGroup('')}>
            全部 <span className="dim">{totalCmds}</span>
          </button>
          {block!.groups.map((g) => (
            <button
              key={g.group_name}
              type="button"
              className={`xkit-chip ${group === g.group_name ? 'on' : ''}`}
              onClick={() => setGroup(group === g.group_name ? '' : g.group_name)}
              title={g.group_description || g.group_name}
            >
              {g.group_name} <span className="dim">{g.commands.length}</span>
            </button>
          ))}
        </div>
      )}

      {err ? (
        <div className="banner warn" style={{ marginBottom: 10 }}>
          拉取命令树失败：{err}
          <button type="button" className="ghost" style={{ marginLeft: 8 }} onClick={() => { void load(true) }}>重试</button>
        </div>
      ) : null}

      {!err && !loading && !blocks.length && (
        <EmptyState
          icon="❯"
          title="服务端没有 Extension-Kit 命令"
          hint="命令树里没有 source=profile 的批次。确认 profile 的 axscripts 指向 extension-kit.axs，且 extender 已重载。"
        />
      )}

      {!err && !!blocks.length && !visible.length && (
        <EmptyState icon="❯" title="没有匹配的命令" hint={q ? `没有命令匹配「${q}」` : '这个分组下没有命令'} />
      )}

      <div className="xkit-wrap">
        {shownGroups.map((g) => {
          const cmds = visible.filter((v) => g.commands.some((c) => c.name === v.name))
          if (!cmds.length) return null
          return (
            <section key={g.group_name} className="xkit-group">
              <div className="xkit-group-head">
                <span className="xkit-group-name">{g.group_name}</span>
                <span className="xkit-group-n">{cmds.length}</span>
                {g.group_description ? <span className="xkit-group-desc">{g.group_description}</span> : null}
                <span style={{ flex: 1 }} />
                {g.script_name ? <span className="xkit-src mono" title="注册这些命令的 .axs（分组按 script_name 归类）">{g.script_name}</span> : null}
              </div>
              <div className="xkit-grid">
                {cmds.map((c) => {
                  const req = requiredArgs(c)
                  const miss = uniform ? [] : missingOf(c.name)
                  return (
                    <article key={c.name} className="xkit-card">
                      <div className="xkit-card-head">
                        <code className="xkit-name">{c.name}</code>
                        {c.has_pre_hook ? <span className="xkit-badge hook" title="带预钩子：服务端会先把命令行展开成 execute bof …，参数写法以示例为准">钩子</span> : null}
                        {(c.subcommands || []).length > 0 ? <span className="xkit-badge" title={`有 ${c.subcommands!.length} 个子命令，必须挑一个 —— 所以这条不能一键下发，照示例写`}>子命令 {c.subcommands!.length}</span> : null}
                        {(c.args || []).length > 0 ? <span className="xkit-badge" title={`${c.args!.length} 个参数，其中 ${req.length} 个必填`}>参数 {c.args!.length}</span> : null}
                        {miss.length > 0 ? (
                          <span className="xkit-badge warn" title={`${miss.join(' / ')} 的批次里没有这条命令`}>仅 {block?.agent}</span>
                        ) : null}
                      </div>
                      {c.description ? <div className="xkit-desc">{c.description}</div> : null}
                      {(c.subcommands || []).length > 0 && (
                        <div className="xkit-subs">
                          {(c.subcommands || []).map((s) => (
                            <span key={s.name} className="xkit-sub mono" title={s.description || s.example || s.name}>{s.name}</span>
                          ))}
                        </div>
                      )}
                      {(c.args || []).length > 0 && (
                        <div className="xkit-args">
                          {c.args!.map((a, i) => (
                            <div className="xkit-arg" key={`${a.name}-${a.mark}-${i}`}>
                              <code className={`xkit-arg-k ${a.required ? 'req' : ''}`}>{argUsage(a)}</code>
                              <span className="xkit-arg-t mono">{a.type}</span>
                              <span className="xkit-arg-d">{a.description || (a.required ? '必填' : '选填')}</span>
                            </div>
                          ))}
                        </div>
                      )}
                      {/* 这里不放弹性占位 span：`.xkit-ex` 自己 flex:1 会吃掉余量，
                          再塞一个 flex:1 的空 span 会把余量对半分，按钮被挤到换行 */}
                      <div className="xkit-foot">
                        {c.example
                          ? <code className="xkit-ex" title={c.example}>{c.example}</code>
                          : <code className="xkit-ex dim">（.axs 没给示例）</code>}
                        {c.example ? (
                          <button type="button" className="ghost" onClick={() => void copy(c.example!, '示例')} title="复制示例（权威用法）">复制示例</button>
                        ) : null}
                        <button type="button" className="ghost" onClick={() => void copy(cmdlineOf(c), '命令名')} title={`复制命令名：${c.name}`}>复制命令</button>
                        {sendsAsIs(c) ? (
                          <button
                            type="button"
                            className="primary"
                            disabled={!canSend || busy === c.name}
                            onClick={() => void send(c)}
                            title={!target
                              ? '没有在线主机'
                              : !canSend
                                ? `目标是 ${targetName} agent，本页显示的是 ${block?.agent} 的命令`
                                : `在 ${targetName} 上执行 ${c.name}${targetImplicit ? '（没显式选主机，发往第一台在线主机）' : ''}`}
                          >
                            {busy === c.name ? '…' : '下发'}
                          </button>
                        ) : null}
                      </div>
                    </article>
                  )
                })}
              </div>
            </section>
          )
        })}
      </div>
    </div>
  )
}

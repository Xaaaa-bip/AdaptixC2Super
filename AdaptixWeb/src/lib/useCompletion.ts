import { useCallback, useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import {
  clearCompletionCache,
  commandsFor,
  completionContext,
  loadBofSuggestions,
  loadCommands,
  subcommandsFor,
  type BofResult,
  type CommandBatch,
  type Suggestion,
} from './completions'

/** 弹层里最多显示多少条。再多也没人翻，反而会盖住输出。 */
const MAX_ITEMS = 60

/**
 * 服务端 BOF 清单拿不到时的说明。必须写清「要自己补路径」——
 * 兜底清单里是**裸文件名**，而执行侧按服务端 cwd 解析，裸名字必然失败。
 */
const BOF_FALLBACK_NOTICE =
  '服务端 BOF 清单不可用 · 下面是内置**文件名**，需自行补成服务端绝对路径（服务端按自己的 cwd 解析）'

/** `execute bof ` 但没有候选时，把「为什么没有」说出来，而不是静默不弹。 */
const BOF_EMPTY_NOTICE = '服务端没有可用的 BOF 对象文件（.o）· 见「BOF」页的上传 / 编译状态'

export type UseCompletionOpts = {
  /** 当前输入框内容 */
  value: string
  /**
   * 目标主机：决定按哪个 agent 类型 / os / arch 过滤候选。
   * 三个字段都要 —— BOF 能力是 agent 的属性，而 arch 决定 `.x64.o` / `.x86.o`
   * 是哪一批文件（见 completions.ts 的 loadBofSuggestions）。
   */
  agent: { a_name?: string; a_os?: number; a_arch?: string } | null | undefined
  /** 接受候选时把拼好的新串交回调用方写进输入框 */
  onAccept: (next: string) => void
  /**
   * 服务端拿不到时用的内置兜底清单。
   * 它们是**兜底而不是默认**：一旦走了这条路，弹层上会明说，
   * 见下面 `degraded` 的注释。
   */
  fallbackCommands?: string[]
  fallbackBofs?: string[]
}

export type CompletionApi = {
  open: boolean
  items: Suggestion[]
  index: number
  /** 本次候选来自内置兜底（服务端不可用）。调用方**必须**把它显示出来。 */
  degraded: boolean
  /**
   * 「这批候选为什么是这样」的一句话。
   *
   * ★ 它和 degraded 是两件事：degraded 说的是「数据源降级了」，
   *   notice 说的是「当前这个上下文没有可用候选，原因是 X」。
   *   后者覆盖了本项目最隐蔽的一类故障：补全**功能正常但结果是空的**，
   *   界面上与「补全坏了」完全同形（原来 `execute bof ` 就是这样 —— 弹层
   *   静默关闭，不报错、不提示，操作员只会以为补全不工作）。
   *   有 notice 时弹层必须保持打开把它显示出来。
   */
  notice: string | null
  /** 处理按键；返回 true 表示已消费，调用方不要再处理这个键。 */
  onKeyDown: (e: React.KeyboardEvent) => boolean
  pick: (i: number) => void
  hover: (i: number) => void
  close: () => void
  /**
   * 丢掉缓存并**立刻重取**当前主机的 BOF 候选。
   *
   * 给「BOF」页编译 / 上传完成之后用（也是给操作员的一个手动刷新入口）。
   * 光靠 `clearCompletionCache()` 不够：那只让**下一次**取数是新的，
   * 而控制台此刻可能已经拿着旧结果、且不会再取一次 —— 表现为「编译完了，
   * 按 Tab 还是旧的」，与缺陷 B 的现象一模一样。
   */
  refresh: () => void
  /**
   * 这台主机的 agent 有没有 BOF 加载器。**由服务端回答**（BofResult.capable）。
   *
   * ★ 只在服务端明确说 `true` 时才是 true。拿不到（在途 / 请求失败 / 老服务端）
   *   一律 false —— 不给一个**可能打不开的门**。反过来「候选为空」绝不能推出
   *   「不支持」：全新部署就是 0 条候选但能力为真（见 completions.ts 的注释）。
   */
  bofCapable: boolean
  /**
   * 这台主机**服务端实际认得的**顶层命令名集合。
   *
   * 给快捷按钮用：Term 里那几组 `QUICK_*` 是手抄的，而实测每个 agent 的命令集
   * 并不相同 —— kharon 没有 `pwd`/`ls`/`ps`/`shell`（它用 `fs`/`process`/`token`），
   * Linux 上的 gopher 没有 `getuid`/`netstat`/`mounts`。手抄清单必然有点不动的按钮，
   * 而服务端这张表是权威。用它过滤一遍，死按钮就不会被渲染出来。
   * （`help` 是核心内建、不在任何 batch 里，由调用方单独放行。）
   */
  availableCommands: Set<string>
}

function names(list: string[] | undefined): Suggestion[] {
  return (list || []).map((n) => ({ name: n }))
}

/**
 * 命令补全。Term 与「执行命令」对话框共用这一份，
 * 免得 Tab / 方向键 / Esc / Enter 的语义写两遍然后各自漂移。
 *
 * **Enter 是这里最容易出事的地方。** 「执行命令」对话框原来是无条件回车提交；
 * 如果补全抢走回车，用户输入一条完整命令后按回车就会被换成候选、而不是执行。
 * 所以规则是：`index === -1` 时**完全不碰**回车，行为与改造前逐字一致；
 * 只有当用户明确高亮过某条（Tab 或方向键）才拦截。
 *
 * 同理，方向键在弹层关着时**不消费** —— Term 的 ↑/↓ 是命令历史，
 * 抢掉它等于把历史功能删了。
 */
export function useCompletion(opts: UseCompletionOpts): CompletionApi {
  const api = useStore((s) => s.api)
  const [batches, setBatches] = useState<CommandBatch[] | null>(null)
  const [bofResult, setBofResult] = useState<BofResult | null>(null)
  const [open, setOpen] = useState(false)
  const [index, setIndex] = useState(-1)
  // 失效计数器：refresh() 自增它 ⇒ 下面那个 effect 重跑 ⇒ 强制重取。
  const [nonce, setNonce] = useState(0)

  const { value, agent, onAccept } = opts

  // ★ BOF 候选按 **agent + os + arch** 取，三者缺一不可：
  //   - agent 决定**用哪一批文件**（beacon/gopher/kharon 走 Extension-Kit 的
  //     Windows COFF，linux 走 linux_agent 运行期编译的 .o，macos 没有）；
  //   - os 决定 **gopher 有没有加载器**（它的 execute 分支只在 Windows 下做事，
  //     实测本部署同时在线着两台 a_name='gopher'、os 分别为 1 和 2 的主机）；
  //   - arch 决定后缀（.x64.o / .x86.o 不是同一批文件）。
  //   所以依赖里必须有这三项 —— 原来只有 os，切到同 os 不同 agent 的主机时
  //   清单压根不会刷新，补出来的候选属于上一台主机，且**看着完全正常**。
  //   都是标量，拿它们当依赖不会因为 agent 对象每次重建而抖。
  const agentName = agent?.a_name || ''
  const os = Number(agent?.a_os) || 0
  const arch = agent?.a_arch || ''

  useEffect(() => {
    if (!api) return
    let alive = true
    void (async () => {
      const [b, r] = await Promise.all([
        loadCommands(api, nonce > 0),
        loadBofSuggestions(api, agentName, os, arch, nonce > 0),
      ])
      if (!alive) return
      setBatches(b)
      setBofResult(r)
    })()
    return () => { alive = false }
  }, [api, agentName, os, arch, nonce])

  const refresh = useCallback(() => {
    // 先清缓存（含作废在途请求），再自增 nonce 触发重取。
    // 两件事都要做：只清不触发 = 界面还是旧的；只触发不清 = 取回缓存里的旧的。
    clearCompletionCache()
    setNonce((n) => n + 1)
  }, [])

  const ctx = useMemo(
    () => completionContext(value, batches, agent),
    [value, batches, agent],
  )

  // 这一步只负责「这个上下文里有哪些候选」，**不管前缀**。
  const candidate = useMemo((): { items: Suggestion[]; degraded: boolean; notice: string | null } => {
    if (ctx.kind === 'command') {
      if (batches) return { items: commandsFor(batches, agent), degraded: false, notice: null }
      return { items: names(opts.fallbackCommands), degraded: true, notice: null }
    }
    if (ctx.kind === 'subcommand') {
      const subs = subcommandsFor(batches, agent, ctx.parent)
      if (subs.length) return { items: subs, degraded: false, notice: null }
      return { items: [], degraded: false, notice: null }
    }
    if (ctx.kind === 'bof') {
      // 拿到了服务端结果：有候选就正常给；没候选就把服务端那句「为什么」
      // 原样透出来（它比前端能猜的任何话都准，比如具体扫了哪个目录）。
      if (bofResult) {
        return {
          items: bofResult.items,
          degraded: false,
          notice: bofResult.items.length ? null : (bofResult.notice ?? BOF_EMPTY_NOTICE),
        }
      }
      // 请求本身失败（网络 / 非预期形状）⇒ 退兜底，并说清它需要自己补路径。
      return { items: names(opts.fallbackBofs), degraded: true, notice: BOF_FALLBACK_NOTICE }
    }
    return { items: [], degraded: false, notice: null }
  }, [ctx, batches, bofResult, agent, opts.fallbackCommands, opts.fallbackBofs])

  /**
   * 前缀过滤**只在这一处**做，四种上下文一视同仁。
   *
   * 原来它被拆成了两半：兜底清单在 candidate 里滤、子命令在下面滤，而
   * **服务端路径一次都没滤** —— 于是敲 `zz` 弹出来的是该 agent 的全部命令，
   * Tab 选中的是字母序第一条，跟用户敲的前缀毫无关系。这种「每条路径各自
   * 决定要不要过滤」的写法就是漂移的温床，所以合并成一处。
   */
  const items = useMemo(() => {
    const p = ctx.prefix.toLowerCase()
    const base = p ? candidate.items.filter((s) => s.name.toLowerCase().startsWith(p)) : candidate.items
    return base.slice(0, MAX_ITEMS)
  }, [candidate.items, ctx.prefix])

  const degraded = candidate.degraded
  const notice = candidate.notice

  // 关掉弹层：候选空了 / 换了主机 / 上下文变成不可补全。
  //
  // ★ 有 notice 时**不关** —— 那正是要给人看的东西。原来无条件关掉，
  //   于是「服务端一个 .o 都没有」在界面上表现为「按了 Tab 没反应」。
  useEffect(() => {
    if (!open) return
    if (ctx.kind === 'none' || (items.length === 0 && !notice)) setOpen(false)
  }, [open, items.length, ctx.kind, notice])

  // 用户重新打字 → 撤掉高亮。这样回车永远回到「执行」，
  // 只有显式 Tab / 方向键之后那一次回车才是「接受候选」。
  useEffect(() => {
    if (open) setIndex(-1)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [value])

  const accept = useCallback((i: number) => {
    const it = items[i]
    if (!it) return
    onAccept(value.slice(0, ctx.replaceFrom) + it.name + ' ')
    setOpen(false)
    setIndex(-1)
  }, [items, value, ctx.replaceFrom, onAccept])

  const onKeyDown = useCallback((e: React.KeyboardEvent): boolean => {
    if (e.key === 'Tab') {
      // 不能补的时候不消费 Tab —— 让焦点照常走，别把键吞了还不干活。
      // 例外：有 notice 时**要**消费并弹出来，否则「为什么没有候选」
      // 永远没机会被看见（这正是 `execute bof ` 原来的样子）。
      if (ctx.kind === 'none' || (items.length === 0 && !notice)) return false
      e.preventDefault()
      if (!open) {
        if (items.length === 1) { accept(0); return true }
        setOpen(true)
        setIndex(0)
        return true
      }
      if (items.length === 0) { setOpen(true); return true }  // 只有说明，没有可轮转的候选
      setIndex((i) => (i + 1) % items.length)
      return true
    }
    if (!open) return false          // ← 弹层没开时，方向键/Esc/回车一律不碰
    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setIndex((i) => (i < 0 ? 0 : Math.min(i + 1, items.length - 1)))
      return true
    }
    if (e.key === 'ArrowUp') {
      e.preventDefault()
      setIndex((i) => (i <= 0 ? 0 : i - 1))
      return true
    }
    if (e.key === 'Escape') {
      e.preventDefault()
      setOpen(false)
      setIndex(-1)
      return true
    }
    if (e.key === 'Enter') {
      // 没有高亮 = 用户没在选择，回车该干它本来的事（执行 / 提交）
      if (index < 0) return false
      e.preventDefault()
      accept(index)
      return true
    }
    return false
  }, [ctx.kind, items.length, open, index, accept, notice])

  const close = useCallback(() => { setOpen(false); setIndex(-1) }, [])
  const hover = useCallback((i: number) => setIndex(i), [])

  // 服务端明确回答 true 才算有；其余一律 false（不给打不开的门）。
  const bofCapable = bofResult?.capable === true

  // 交给调用方过滤快捷按钮：只认服务端认得的命令。
  const availableCommands = useMemo(
    () => new Set(commandsFor(batches, agent).map((s) => s.name)),
    [batches, agent],
  )

  return {
    open, items, index, degraded, notice, onKeyDown, pick: accept, hover, close, refresh,
    bofCapable, availableCommands,
  }
}

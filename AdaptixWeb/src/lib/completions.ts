/**
 * 命令补全的数据源：服务端 `POST /api/axscript/commands`。
 *
 * 为什么不再用硬编码清单：Term.tsx 原来那份 COMPLETIONS / BOF_FILES 是手抄的，
 * 而且已经漂了 —— BOF_FILES 抄了 18 个、服务端却有 26 个，argprobe / hide_add /
 * matrix_pick 等 8 个在控制台里永远补不出来，**且没有任何地方会报错**。
 * 服务端本来就有一份权威表（各 extender 的 ax_config.axs 注册进去的），
 * 直接取它，两边就不可能再漂。
 *
 * 三条形态约束（都是对着源码核过的，不是推测）：
 *   1. 成功时 `TcAxScriptCommands` 走 `ctx.Data(...)` 发**裸 JSON 数组**，
 *      没有 `{ok:...}` 信封（core/connector/tc_axscript.go:30-37）。
 *   2. `ScriptManager == nil` 时返回**字面量 `"{}"`**（core/server/ts_axscript.go:200-202）
 *      —— 所以 `Array.isArray` 守卫是**必须**的，不是防御性编程。
 *   3. 出错时返回 `{message, ok:false}`，已被 http.ts:92 抛掉，到不了这里。
 *
 * 本模块**从不抛异常**：它挂在输入框的按键路径上，一个未捕获的异常会直接
 * 砸掉整个控制台页签。拿不到就返回 null，由调用方决定怎么降级。
 */

/** 与 core/axscript/types.go 的 ArgumentDef 对齐（default_value 是 interface{}）。 */
export type ArgDef = {
  type?: string
  name?: string
  required?: boolean
  flag?: boolean
  mark?: string
  description?: string
  default_used?: boolean
  default_value?: unknown
}

export type CommandDef = {
  name: string
  message?: string
  description?: string
  example?: string
  args?: ArgDef[]
  subcommands?: CommandDef[]
  has_pre_hook?: boolean
  has_post_hook?: boolean
  has_handler?: boolean
}

export type CommandGroup = {
  group_name?: string
  group_description?: string
  script_name?: string
  commands?: CommandDef[]
  source?: string
}

/** 与 core/axscript/registry.go:252-258 的 CommandBatch 对齐。 */
export type CommandBatch = {
  source?: string
  /** 服务端注册表里的 agent 名，如 `linux` / `beacon`（取自 extender config.yaml 的 agent_name） */
  agent?: string
  listener?: string
  /** 1=Windows 2=Linux 3=MacOS（core/axscript/types.go:14-18） */
  os?: number
  groups?: CommandGroup[]
}

/** 一条候选。name 必填，其余用于弹层里的第二行说明。 */
export type Suggestion = {
  /** **插入输入框的那一串**。BOF 候选这里是服务端绝对路径（见 loadBofSuggestions）。 */
  name: string
  /**
   * 弹层里**显示**的名字。不给就显示 name。
   *
   * 为什么要能和 name 分开：BOF 的实参必须是服务端绝对路径
   * （`/home/ad/dist/Extension-Kit/SAL-BOF/_bin/sc_enum.x64.o`），
   * 直接铺在弹层里没人读得下去 —— 显示 basename、插入全路径。
   */
  label?: string
  description?: string
  example?: string
  group?: string
  /**
   * 产物比源码旧（Extension-Kit 的 `.c` 比 `.x64.o` 新，或 linux_agent 的
   * `.c` 比 `.o` 新）。**只标注、仍可选** —— 服务端不下架它，前端也不拦：
   * 「旧」不等于「错」，操作员可能就是要跑那个版本。
   */
  stale?: boolean
  /** stale 的原因（含是哪份源码更新）。服务端保证与 stale 同生同灭。 */
  staleReason?: string
}

type ApiLike = {
  get<T = any>(path: string): Promise<T>
  post<T = any>(path: string, body?: any): Promise<T>
}

const CACHE_MS = 5 * 60 * 1000

let cmdCache: { at: number; batches: CommandBatch[] } | null = null
let cmdInflight: Promise<CommandBatch[] | null> | null = null

/**
 * 缓存「朝代」。`clearCompletionCache()` 自增它，在途请求据此判断自己是否已作废。
 *
 * ★ 为什么光清 map 不够：在途请求的 promise **体内部**就会写缓存
 *   （`bofCache.set(key, r)`）。清完 map 之后它照跑不误，跑完把**清除前**
 *   发起的那次结果写回来 —— 现象与「缓存没清」完全一致，而且不报错。
 *   清 map 只能阻止**新的**读取命中，阻止不了**旧的**写回。
 */
let cmdGen = 0
let bofGen = 0

/**
 * BOF 候选的缓存 key 是 `agent|os|arch`。
 *
 * ★ **为什么不能只按 os**（原来就是）：BOF 能力是 **agent 的属性**，不是 OS 的。
 *   实测本部署同时在线两台 `a_name='gopher'` 的主机，一台 os=1 一台 os=2 ——
 *   只按 os 缓存，若哪天两者的键真的撞上（此前 os=0 的兜底就撞），
 *   后一次请求会拿到前一次的清单，而**清单本身看着完全正常**，只是内容属于
 *   另一台主机。arch 同理：x64 与 x86 的 `.o` 不是同一批文件。
 */
const bofKey = (agent: string, os: number, arch: string) =>
  `${normType(agent)}|${Number(os) || 0}|${String(arch || '').trim().toLowerCase()}`

/**
 * BOF 候选缓存。**没有 TTL** —— 与上面的命令表缓存刻意不同。
 *
 * ★ 这是一次「便宜的东西不该背昂贵的正确性」的取舍：
 *   该列表只在**切换主机时**取一次（不是每次击键），服务端重算是本地 FS 遍历
 *   （实测 Extension-Kit 全树 714 文件 / 28ms），缓存的收益很小；
 *   而缓存错了的代价正是用户撞上的那个 bug —— 在「BOF」页传了新 `.c` 并编译，
 *   控制台里却怎么都补不出来，且**不报错**（旧的 5 分钟 TTL 还没过期）。
 *   所以这里只保留单飞去重，失效交给**明确的事件**（编译/上传完成、刷新按钮）。
 */
const bofCache = new Map<string, BofResult>()
const bofInflight = new Map<string, Promise<BofResult | null>>()

/**
 * agent 类型归一名。
 *
 * 服务端注册表里的 `agent` 字段来自各 extender `config.yaml` 的 `agent_name:`
 * （linux / beacon / kharon / gopher / macos），而主机记录里的 `a_name` 在野外
 * 见到过带 `_agent` 后缀的写法（webdelivery/pl_main.go:116-119 就是为此做的松匹配）。
 * 两边都归一化再比，避免因为一个后缀把补全整片判空。
 */
export function normType(name: unknown): string {
  return String(name ?? '').trim().toLowerCase().replace(/_agent$/, '')
}

/** 一次拉全部 agent 类型的命令表。
 *
 *  注意**不要**按 agent 类型分别请求：`GetCommandsJSON()` marshal 的是
 *  `GetAllCommandsOrdered()`，它不接受参数、一次返回所有 agent / listener / os
 *  组合的 batch（core/axscript/registry.go:135-185）。所以整个会话只需要 1 个请求。
 */
export async function loadCommands(api: ApiLike, force = false): Promise<CommandBatch[] | null> {
  if (!force && cmdCache && Date.now() - cmdCache.at < CACHE_MS) return cmdCache.batches
  // 单飞：连打 20 个字符也只会发 1 个请求（缓存未命中时的并发去重）
  if (cmdInflight) return cmdInflight
  const gen = cmdGen
  // 先声明、后赋值：finally 里要比对**自己这一个** promise 的引用，
  // 而用 `const p = (async () => …)()` 时 TS 无法证明 finally 跑的时刻 p
  // 已初始化（TS2454）。初值给 null 既满足类型，也让比对语义明确。
  let p: Promise<CommandBatch[] | null> | null = null
  p = (async () => {
    try {
      const data = await api.post<unknown>('/api/axscript/commands', {})
      // 形态 2：字面量 "{}"，以及任何其它非数组形状，一律当作「不可用」
      if (!Array.isArray(data)) return null
      // 被 clear 过就别写回：那是清除之前发起的请求，结果可能已经过时。
      // 但仍然**返回**它 —— 调用方拿到的是自己那次请求的答案，比 null 有用。
      if (gen === cmdGen) cmdCache = { at: Date.now(), batches: data as CommandBatch[] }
      return data as CommandBatch[]
    } catch {
      return null
    } finally {
      // ★ 只清**自己**那一条：clearCompletionCache() 会把 cmdInflight 置空，
      //   此后若已有新请求占位，这里无条件 `= null` 会把它也抹掉 ——
      //   单飞随即失效，且是「多打几个字符就多发几个请求」这种看不出来的退化。
      if (cmdInflight === p) cmdInflight = null
    }
  })()
  cmdInflight = p
  return p
}

/**
 * 某台主机可用的顶层命令。
 *
 * 过滤维度有两个，都不做「猜」：
 *   - agent 类型必须相等（归一化后）；
 *   - os：两侧都已知且非 0 时才比。服务端偶尔不填（0）视为通配，
 *     免得一条缺失的 os 字段把整台主机的补全清空。
 * 按 name 去重：同一命令名会**合法地**同时出现在多个 batch 里
 * （registry 会把 agent / profile / user 三个来源拼接，见 registry.go:135-185）。
 */
export function commandsFor(
  batches: CommandBatch[] | null,
  agent: { a_name?: string; a_os?: number } | null | undefined,
): Suggestion[] {
  if (!batches || !agent) return []
  const want = normType(agent.a_name)
  if (!want) return []
  const os = Number(agent.a_os) || 0
  const out: Suggestion[] = []
  const seen = new Set<string>()
  for (const b of batches) {
    if (normType(b?.agent) !== want) continue
    const bos = Number(b?.os) || 0
    if (bos !== 0 && os !== 0 && bos !== os) continue
    for (const g of b.groups || []) {
      for (const c of g?.commands || []) {
        const n = String(c?.name || '').trim()
        if (!n || seen.has(n)) continue
        seen.add(n)
        out.push({ name: n, description: c.description, example: c.example, group: g.group_name })
      }
    }
  }
  out.sort((a, b) => a.name.localeCompare(b.name))
  return out
}

/** 某个顶层命令的子命令（`execute` → `bof` / `shell` …）。没有就返回空数组。 */
export function subcommandsFor(
  batches: CommandBatch[] | null,
  agent: { a_name?: string; a_os?: number } | null | undefined,
  cmdName: string,
): Suggestion[] {
  if (!batches || !agent || !cmdName) return []
  const want = normType(agent.a_name)
  const os = Number(agent.a_os) || 0
  const key = String(cmdName).trim().toLowerCase()
  for (const b of batches) {
    if (normType(b?.agent) !== want) continue
    const bos = Number(b?.os) || 0
    if (bos !== 0 && os !== 0 && bos !== os) continue
    for (const g of b.groups || []) {
      for (const c of g?.commands || []) {
        if (String(c?.name || '').trim().toLowerCase() !== key) continue
        const subs = (c.subcommands || [])
          .map((s) => ({
            name: String(s?.name || '').trim(),
            description: s?.description,
            example: s?.example,
            group: g.group_name,
          }))
          .filter((s) => s.name)
        if (subs.length) return subs.sort((a, b) => a.name.localeCompare(b.name))
      }
    }
  }
  return []
}

/** 一次 BOF 候选查询的结果。 */
export type BofResult = {
  /** 候选。**每条的 name 都是可直接插入的服务端绝对路径。** */
  items: Suggestion[]
  /** 服务端报的来源（`linux_agent` / `Extension-Kit`），仅供排障显示。 */
  source?: string
  /** 「为什么是空的」的人话。有值时调用方**必须**显示，不许静默关弹层。 */
  notice?: string
  /**
   * **这台主机的 agent 有没有 BOF 加载器** —— 与「当前有几条候选」是两个问题。
   *
   * 服务端才是权威（`/api/bofs/exec` 的 `capable`），前端**不维护第二份能力矩阵**。
   * 全新部署时 linux_agent 只有 `.c` 没有 `.o`，候选是 0 条但能力是**有**的；
   * 若用「候选为空 ⇒ 不支持」去判定，就会把操作员挡在**用来编译**的那个页面外面
   * —— 本项目反复归档的「静默空」陷阱。
   *
   * `undefined` = 还没拿到服务端回答（在途 / 请求失败）。
   * 调用方按「不知道」处理，**不要**当成 false 之外再猜一个值。
   */
  capable?: boolean
}

/** 该主机未上报类型时的说明 —— 不猜，直接说不知道。 */
const BOF_OS_UNKNOWN =
  '该主机未上报 agent 类型与操作系统 · 无法确定可用的 BOF 清单'

/**
 * `execute bof` 的候选，取自 `POST /api/bofs/exec {agent, os, arch}`。
 *
 * ── 为什么不再用 /api/bofs/list ────────────────────────────────────────
 * 那个接口回答的是「linux_agent 自己的 .c→.o 编译状态」，与「这台主机上
 * execute bof 能填什么」是两个问题：Windows 主机要 Extension-Kit 的 COFF，
 * 而它一个都不在里面。
 *
 * ── 更要紧的是 name 的形态 ────────────────────────────────────────────
 * 旧实现返回**裸文件名**（`host_recon.o`），而服务端执行侧是按**自己的 cwd**
 * 解析这个实参的（cwd = /home/ad/dist）。于是补齐了一个「看起来对」的名字，
 * 提交后 os.Stat 失败 ⇒ 静默落进 base64 分支 ⇒ **agent 收到一串路径文本当
 * 机器码**，报错却是「BOF 格式错误」。服务端现在直接回绝对路径，两端对齐。
 *
 * ── agent 是第一判据，os 是第二 ───────────────────────────────────────
 * BOF 能力是 **agent 的属性**：`gopher` 的 BOF 加载器只在 Windows 上存在
 * （gopher_agent/pl_main.go 的 execute 分支有 `Os != WINDOWS` 守卫），
 * 而 `linux` 用的是 linux_agent 在服务端运行期编译的 `.o`，两批文件互不通用。
 * 实测本部署有两台 `a_name='gopher'` 的主机同时在线（os=1 / os=2），
 * 所以 agent 名本身也不够 —— 两者都要传，服务端据此选清单。
 *
 * ── 根目录是动态的（用户 2026-09-19 的要求）────────────────────────────
 * 服务端不再猜目录，而是问「本部署到底加载了哪些 `.axs`」再扫它们所在的目录，
 * 所以换服务器部署 / 新写一个 BOF 丢进 `Extension-Kit/<类>/_bin/` /
 * 往 profile 的 `axscripts` 加一行，候选都会跟着变，**不需要改代码**。
 * 响应里的 `roots` 就是「实际用了哪些根、凭什么」，排障时先看它。
 */
export async function loadBofSuggestions(
  api: ApiLike,
  agentName: string | undefined | null,
  os: number,
  arch?: string | null,
  force = false,
): Promise<BofResult | null> {
  const want = normType(agentName)
  const key = bofKey(agentName ?? '', os, arch ?? '')

  // 什么都没上报：不猜。宁可给一句说明，也不发一个注定只能靠 os 推断的请求
  // ——「按 os 推断」正是缺陷 D 的成因（见服务端 deprecated 分支）。
  if (!want && !(Number(os) || 0)) return { items: [], notice: BOF_OS_UNKNOWN }

  if (!force) {
    const hit = bofCache.get(key)
    if (hit) return hit          // 无 TTL：见 bofCache 的注释
  }
  const inflight = bofInflight.get(key)
  if (inflight) return inflight

  const gen = bofGen
  // 同 loadCommands：先声明后赋值，finally 要比对自己这一个 promise 的引用。
  let p: Promise<BofResult | null> | null = null
  p = (async (): Promise<BofResult | null> => {
    try {
      const d = await api.post<any>('/api/bofs/exec', {
        agent: want,
        os: Number(os) || 0,
        arch: String(arch || '').trim() || 'x64',
      })
      // 与 loadCommands 同理：非数组/非预期形状一律当「不可用」，由调用方降级。
      const entries = Array.isArray(d?.entries) ? d.entries : null
      if (!entries) return null

      const items: Suggestion[] = entries
        .filter((e: any) => e?.name)
        .map((e: any) => ({
          name: String(e.name),
          label: e?.label ? String(e.label) : undefined,
          group: e?.group ? String(e.group) : undefined,
          stale: e?.stale === true,
          staleReason: e?.stale_reason ? String(e.stale_reason) : undefined,
        }))

      // 「为什么是空的 / 为什么不可信」优先取服务端的原话 —— 它比前端能猜的
      // 任何一句都准（知道具体扫了哪个目录、哪个 agent 没有加载器）。
      // deprecated 是**带着候选也要说出来**的：那说明这次的判据是推断的。
      const note = typeof d?.note === 'string' && d.note ? String(d.note) : undefined
      const depr = typeof d?.deprecated === 'string' && d.deprecated ? String(d.deprecated) : undefined
      const r: BofResult = {
        items,
        source: typeof d?.source === 'string' && d.source ? d.source : undefined,
        notice: items.length ? depr : (note ?? depr),
        capable: typeof d?.capable === 'boolean' ? d.capable : undefined,
      }
      // 被 clear 过就别写回（同 loadCommands）：在「BOF」页编译完的清缓存
      // 有可能正好撞上一个在途的、编译前发起的请求，写回它等于编译白做。
      if (gen === bofGen) bofCache.set(key, r)
      return r
    } catch {
      return null
    } finally {
      // 同 loadCommands：只清自己那一条，别把 clearCompletionCache() 之后
      // 新占位的请求一并抹掉（否则单飞静默失效）。
      if (bofInflight.get(key) === p) bofInflight.delete(key)
    }
  })()

  bofInflight.set(key, p)
  return p
}

/** 补全上下文：正在补的是哪一段、以及替换区间。 */
export type CompletionContext =
  | { kind: 'command'; prefix: string; replaceFrom: number }
  | { kind: 'bof'; prefix: string; replaceFrom: number }
  | { kind: 'subcommand'; prefix: string; replaceFrom: number; parent: string }
  | { kind: 'none'; prefix: string; replaceFrom: number }

/**
 * 解析输入框内容，判断「现在该补什么」。
 *
 * 尾随空格表示正在起一个新 token（即空前缀）—— 这不是细节而是必须的：
 * 少了它，`execute bof ` 会被当成「前缀是 bof」，于是候选被过滤成空，
 * 用户敲完 `execute bof` 再按 Tab 什么都不会发生。
 */
export function completionContext(
  raw: string,
  batches: CommandBatch[] | null,
  agent: { a_name?: string; a_os?: number } | null | undefined,
): CompletionContext {
  const trailing = /\s$/.test(raw)
  const trimmed = raw.trim()
  const parts = trimmed ? trimmed.split(/\s+/) : []
  const prefix = trailing ? '' : (parts[parts.length - 1] ?? '')
  const head = trailing ? parts : parts.slice(0, -1)
  const replaceFrom = raw.length - prefix.length

  if (head.length === 0) return { kind: 'command', prefix, replaceFrom }
  // `execute bof <Tab>`：head 正好是这两个词。
  //
  // ★ 这里**不再**看清单有没有拿到。原来的写法是「拿不到就退成 none」，
  //   而 none 在 useCompletion 里等于「无候选、静默关弹层」——
  //   于是「服务端一个 .o 都没有」和「补全根本不知道有 BOF 这回事」
  //   在界面上完全同形：都是敲完 `execute bof ` 按 Tab 什么都不发生。
  //   现在一律进 bof 上下文，由上下文自己给候选、给不出来就给**说明**
  //   （BofResult.notice），「没有」必须是一种看得见的状态。
  if (head.length >= 2 && head[0].toLowerCase() === 'execute' && head[1].toLowerCase() === 'bof') {
    return { kind: 'bof', prefix, replaceFrom }
  }
  if (head.length === 1) {
    const subs = subcommandsFor(batches, agent, head[0])
    if (subs.length) return { kind: 'subcommand', prefix, replaceFrom, parent: head[0] }
  }
  return { kind: 'none', prefix, replaceFrom }
}

/**
 * 清掉缓存。切登录 / 重新加载 axs / **在「BOF」页上传或编译之后**用。
 *
 * ★ 必须连**在途请求**一起作废，否则刚清完就被上一个响应写回来 ——
 *   那不是「缓存没清」，是「清了也没用」，而且现象一模一样：
 *   编译完按 Tab，看到的还是编译前的清单，不报错。
 *   单飞 map 只保证同一时刻一个请求，不保证它的结果还有效。
 *
 * ★ 本函数**有真实调用方**（Bofs.tsx 的三个入口）。这不是细节：
 *   它此前是全仓死代码 —— 唯一出现就是它自己的定义 —— 于是「编译完补全不知道」
 *   这个缺陷（B）在代码里完全看不出来，因为**看起来有个清理函数**。
 */
export function clearCompletionCache() {
  cmdCache = null
  bofCache.clear()
  // 在途请求作废：自增朝代号 ⇒ 它们跑完也不会写回（见 cmdGen/bofGen 的注释）。
  // 单飞 map 只保证同一时刻一个请求，不保证它的结果在清除之后还有效。
  cmdGen++
  bofGen++
  cmdInflight = null
  bofInflight.clear()
}

// ── 兜底清单 ──
// 补全的主数据源是服务端 `POST /api/axscript/commands`，它带着 description /
// example / 子命令，比这两份手抄表强得多。这两份只在服务端拿不到时用，
// 而且弹层会**明确标出来**（不静默降级，见 CmdSuggest.tsx 的 degraded）。
//
// 之所以还留着：axs 没加载 / ScriptManager 为 nil 时服务端只回一个 "{}"，
// 那时控制台仍然应该能用，只是候选不全 —— 但必须让操作员知道它不全。
//
// 放在这里而不是某个组件里：Term 与「执行命令」对话框都要用，而**两份手抄的表
// 就是这次要修的 bug 本身** —— BOF 这份原来只抄了 18 个而服务端有 26 个，
// argprobe / ebpf_unload / evacuate / hide_add / hide_del / matrix_pick /
// spawn_detached / systemd_persist 在控制台里永远补不出来，且没有任何地方会报错。

export const FALLBACK_COMMANDS = [
  'getuid', 'whoami', 'pwd', 'netstat', 'ipconfig', 'mounts', 'ps', 'help',
  'cat', 'cd', 'cp', 'download', 'edr', 'env', 'exit', 'inject', 'job',
  'kill', 'link', 'ls', 'masquerade', 'migrate', 'mkdir', 'mv', 'persist',
  'rm', 'run', 'shell', 'socks', 'timestomp', 'unlink', 'upload', 'container',
  'creds', 'cleanlog', 'execute', 'disks',
]

/**
 * 兜底 BOF **文件名**（不是路径）。
 *
 * ★ 这份只在服务端 BOF 清单拿不到时用，而且**它补出来的东西直接用不了** ——
 *   服务端执行侧按自己的 cwd 解析实参，裸名字必然 os.Stat 失败。
 *   保留它是因为「知道有个叫 host_recon 的模块」总比一片空白强，
 *   但弹层会明说「需自行补成服务端绝对路径」（见 useCompletion 的 BOF_FALLBACK_NOTICE）。
 *   **不要**把它当成可用清单 —— 正常路径下它一次都不该出现。
 */
export const FALLBACK_BOFS = [
  // 侦察
  'host_recon.o', 'proc_enum.o', 'net_enum.o', 'service_enum.o', 'container_detect.o',
  'kernel_exploit_check.o', 'argprobe.o',
  // 提权
  'sudo_check.o', 'suid_scan.o', 'ld_preload_check.o',
  // 凭据
  'shadow_dump.o', 'cred_harvest.o', 'ssh_keys.o',
  // 维持
  'cron_persist.o', 'ssh_persist.o', 'ld_preload_install.o', 'ko_load.o',
  'systemd_persist.o',
  // 隐匿 · 撤离
  'ebpf_load.o', 'ebpf_unload.o', 'hide_add.o', 'hide_del.o', 'evacuate.o',
  'matrix_pick.o',
  // 执行
  'memfd_exec.o', 'spawn_detached.o',
]

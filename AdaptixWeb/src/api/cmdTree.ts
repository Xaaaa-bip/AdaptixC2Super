import type { ApiClient } from './http'
import type { CmdBatch, CmdDef } from './types'

/**
 * 浏览器操作的命令从**服务端命令树**里解析，而不是前端手写一张表。
 *
 * 为什么必须这样：命令集不只看 agent，还看 listener —— ax_config.axs 里的
 * create_commands_group 是按 listenerType 分支调用的（beacon 的 BeaconDNS /
 * HTTP+WS / SMB+TCP 各一组；kharon 只有 KharonHTTP 有命令，其余 listenerType
 * 直接 `return ax.create_commands_group("none",[])`）。参数名各家也不同。
 *
 * 手抄一张表必然对不齐。实测的代价（都不是「报个错」，而是静默做错事）：
 *   · macos / gopher 的 `ps kill` 会被插件自己的 `case "ps"` 吞掉 —— 它们的
 *     ps 压根不读子命令，于是「杀进程」静默变成「列进程」。这两个 agent 的 kill
 *     是独立顶层命令，必须发 `kill <pid>`。
 *   · beacon 的 download 参数名叫 file（pl_main.go:1749），发 path 直接报
 *     parameter 错；kharon 叫 remote_path，其余 agent 才叫 path。
 *   · kharon 的 ls/cat/rm 全在 `fs` 组下、进程在 `process` 组下，顶层名不存在。
 *
 * 命令树是 ax_config.axs 里 create_command / addArgString / addSubCommands 的
 * 产物，团队端派发用的就是同一棵注册树（core/axscript/registry.go 的
 * groups[agent][listener][os]），所以它不会漂移：新增 agent、改 listener 分支，
 * 前端自动跟上。
 *
 * 注意插件是拿 args["command"] / args["subcommand"] 派发的，cmdline 只用于
 * 钩子解析和回显 —— 两者都由这里按同一棵树生成，保持一致。
 */

/**
 * 语义 → 候选命令路径，按序取第一个成立的。
 *
 * 第二项（子命令）三种写法：
 *   省略   裸命令，且要求它**没有**子命令。beacon 的 ps 有 list/kill/run。
 *   '*'    裸命令，不管有没有子命令。给「列表动作就是裸命令、但另有子命令」
 *          的用：linux 的 ps 只挂了 kill 子命令，列进程发的是裸 ps。
 *   具名   必须是该命令真实存在的子命令；不存在则整个候选作废。
 *
 * 第三项是**参数名约束**：只有生效命令（子命令优先）声明了同名参数，这个候选
 * 才成立。杀进程靠它认 pid 来跟「杀任务」区分开 —— beacon/macos/gopher 都有
 * 一对同名兄弟：`jobs kill`（参数 task_id）与 `ps kill`/顶层 `kill`（参数 pid，
 * macos/gopher 的 kill 是独立顶层命令）。今天 job kill 恰好嵌在 jobs 组下、
 * 顶层取不到，所以纯按顺序也能对；但哪天有人把 job kill 提成顶层，顺序就会
 * 静默把「杀进程」变成「杀任务」。参数名是 schema 里现成的判据，比顺序可靠。
 *
 * 顺带说明为什么裸 ps 在 linux 上安全：ResolveFromCmdline 确实会对有子命令的
 * 命令报 "subcommand required"，但 ResolveAndExecutePreHook 把这个错误整个吞掉
 * （返回 nil），命令照常派发到插件 —— 而插件是按 args["command"] 派发的，
 * linux 的 case "ps" 对非 kill 子命令一律落到列进程。这几家的 ax_config 里
 * 一个 handler/posthook 都没有，浏览器回显是插件原生发的，所以解析失败无副作用。
 */
const OP_CANDIDATES = {
  listDir: [['ls'], ['fs', 'ls']],
  listProcs: [['ps', 'list'], ['ps', '*'], ['process', 'list']],
  killProc: [['kill', undefined, 'pid'], ['ps', 'kill', 'pid'], ['process', 'kill', 'pid']],
  cat: [['cat'], ['fs', 'cat']],
  rm: [['rm'], ['fs', 'rm']],
  download: [['download']],
  disks: [['disks']],
} satisfies Record<string, CmdPath[]>

type CmdPath = [name: string, sub?: string, reqArg?: string]

export type BrowserOp = keyof typeof OP_CANDIDATES

export type AgentCtx = {
  /** agent_name（= AgentData.a_name） */
  agent: string
  /** 监听器**注册名**（= l_reg_name，如 KharonHTTP）。拿不到就留空 */
  listenerReg?: string
  /** AgentData.a_os：1=Windows 2=Linux 3=macOS */
  os?: number
}

export type Invocation = { cmdline: string; args: Record<string, unknown> }

let cache: CmdBatch[] | null = null
let inflight: Promise<CmdBatch[]> | null = null

/**
 * 会话期内命令树不变（改 .axs 要重载 extender），拉一次就缓存。
 * 多个浏览器窗口同时打开只会产生一次请求。
 */
export function loadCmdTree(api: ApiClient, force = false): Promise<CmdBatch[]> {
  if (cache && !force) return Promise.resolve(cache)
  if (!inflight || force) {
    inflight = api
      .axscriptCommands()
      .then((r) => {
        cache = Array.isArray(r) ? r : []
        inflight = null
        return cache
      })
      .catch((e) => {
        inflight = null
        throw e
      })
  }
  return inflight
}

export function invalidateCmdTree() {
  cache = null
  inflight = null
}

function isEmptyGroups(b: CmdBatch) {
  return !(b.groups || []).some((g) => (g.commands || []).length > 0)
}

/** axscript.SourceAgent（registry.go:11）。Profile/User 批次的 agent 名与 agent
 *  批次**同名**、listener 为空（profile 脚本不分 listener），但里面只有
 *  Extension-Kit 那些 BOF 命令。实测 154 的命令树里 kharon/beacon/gopher 各有
 *  一条 `listener=""` 的批次，带 62 条 AD-BOF/Kerbeus-BOF 之类 —— 一旦退化到它
 *  上面，七个浏览器操作会**全部**解析失败。所以只认 agent 自己的批次。 */
const SOURCE_AGENT = 0

/**
 * 定位这个会话的候选命令批次，按优先级排列。a_listener 是监听器**显示名**
 * （操作员随便起），而命令树按**注册名**分组，所以外面要先把显示名映射成注册名。
 *
 * 返回的是一个**列表**而不是单个批次：调用方要逐个试，因为某个批次里没有
 * `ls` 不代表别的批次也没有（kharon 的 fs 组、beacon 的三个 listener 变体等）。
 * 事先挑一个再失败，会变成「明明有命令却说没有」。
 */
function findBatches(tree: CmdBatch[], ctx: AgentCtx): CmdBatch[] {
  const mine = tree.filter(
    (b) => b.agent === ctx.agent && b.source === SOURCE_AGENT && !isEmptyGroups(b),
  )
  if (!mine.length) return []
  const reg = ctx.listenerReg || ''
  const out: CmdBatch[] = []
  const push = (b: CmdBatch | undefined) => {
    if (b && !out.includes(b)) out.push(b)
  }
  // 精确匹配优先；拿不到注册名就跳过这两档，直接按 os 退化。
  // 对浏览器这几个操作，beacon 的几个 listener 变体命令集是一样的，退化安全。
  if (reg) {
    push(mine.find((b) => b.listener === reg && b.os === ctx.os))
    push(mine.find((b) => b.listener === reg))
  }
  push(mine.find((b) => b.os === ctx.os))
  mine.forEach(push)
  return out
}

function pick(batch: CmdBatch, path: CmdPath): { cmd: CmdDef; sub: CmdDef | null } | null {
  const [name, sub, reqArg] = path
  for (const g of batch.groups || []) {
    for (const c of g.commands || []) {
      if (c.name !== name) continue
      const subs = c.subcommands || []
      let chosen: CmdDef | null = null
      if (sub === undefined) {
        if (subs.length !== 0) continue
      } else if (sub !== '*') {
        const hit = subs.find((s) => s.name === sub)
        // 命令在、子命令不在 ⇒ 这个候选不成立，试下一个
        if (!hit) continue
        chosen = hit
      }
      // 参数名约束认的是生效命令：有子命令看子命令，否则看命令本身
      if (reqArg && !((chosen || c).args || []).some((a) => a.name === reqArg)) continue
      return { cmd: c, sub: chosen }
    }
  }
  return null
}

/**
 * 解析出该会话该发的命令与参数；这个 agent/listener 组合下没有对应命令时返回 null。
 *
 * values 按**位置**绑到命令声明的参数上 —— 调用方因此不需要知道参数叫什么，
 * path / directory / file / remote_path 一律由树决定。
 */
export async function resolveOp(
  api: ApiClient,
  ctx: AgentCtx,
  op: BrowserOp,
  values: unknown[] = [],
): Promise<Invocation | null> {
  const tree = await loadCmdTree(api)
  // 批次在外层、候选在内层：先把**最贴合这个会话的批次**用尽，再退化到下一个。
  // 反过来（候选在外层）会让一个宽泛的退化批次抢在精确批次前面命中。
  for (const batch of findBatches(tree, ctx)) {
    for (const path of OP_CANDIDATES[op]) {
      const hit = pick(batch, path)
      if (!hit) continue

      const def = hit.sub || hit.cmd
      const parts = [hit.cmd.name]
      if (hit.sub) parts.push(hit.sub.name)

      const args: Record<string, unknown> = { command: hit.cmd.name }
      if (hit.sub) args.subcommand = hit.sub.name

      const declared = def.args || []
      for (let i = 0; i < declared.length; i++) {
        const a = declared[i]
        const v = values[i]
        if (v === undefined || v === null || v === '') {
          // 没给值就走 schema 里的默认值（例如 ls 的目录默认 "."）
          if (a.default_used && a.default_value !== undefined && a.default_value !== null) {
            args[a.name] = a.default_value
            parts.push(String(a.default_value))
          }
          continue
        }
        args[a.name] = v
        if (a.flag) parts.push(String(a.mark || `--${a.name}`), String(v))
        else parts.push(String(v))
      }
      return { cmdline: parts.join(' '), args }
    }
  }
  return null
}

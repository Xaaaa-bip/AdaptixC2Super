import type { ApiClient } from './http'
import type { BuildSchema, BuildSchemaField } from './types'

/**
 * 生成参数表按 **(agent, listener_type)** 缓存 —— 服务端就是按这两个键预计算并缓存的
 * （axscript/manager.go 的 buildSchemaKey = agent + "|" + listenerType），前端跟着
 * 同一个粒度走，不做更粗的缓存：同一张表在 BeaconHTTP 和 BeaconDNS 下的区别正是
 * show_if（代理/轮换只在 HTTP/WS 出现，DNS 模式只在 BeaconDNS 出现），缓存粗了就错。
 *
 * 会话期内表不变（改 .axs 要重载 extender 才有新表），所以拉过就留着。多个窗口
 * 同时打开也只会产生一次请求 —— 与 api/cmdTree.ts 同一套写法。
 */
const cache = new Map<string, BuildSchema | null>()
const inflight = new Map<string, Promise<BuildSchema | null>>()

const keyOf = (agent: string, listenerType: string) => `${agent}|${listenerType}`

export function loadBuildSchema(
  api: ApiClient,
  agent: string,
  listenerType: string,
): Promise<BuildSchema | null> {
  const k = keyOf(agent, listenerType)
  if (cache.has(k)) return Promise.resolve(cache.get(k) as BuildSchema | null)
  const flying = inflight.get(k)
  if (flying) return flying
  const p = api
    .agentBuildSchema(agent, listenerType)
    .then(normalize)
    .then((s) => {
      cache.set(k, s)
      inflight.delete(k)
      return s
    })
    .catch((e) => {
      inflight.delete(k)
      throw e
    })
  inflight.set(k, p)
  return p
}

/**
 * ★ `{}` 必须归一化成 `null`。
 *
 * 服务端对三种情况都返回 `{}`：这个 agent 没声明 BuildSchema（契约里的回退）、
 * 不认识的 listener_type（缓存没命中）、以及字段表被解析空了。而调用方要区分的是
 * 另外两件事：**「没有表 ⇒ 走内置界面」** 和 **「拉取失败 ⇒ 报错」**。
 * null 表示前者；失败走 reject。混在一起的话，网络断了会静默退回内置界面 ——
 * 看起来像「这个 agent 没迁移」，实际是没人知道的一处故障。
 *
 * 表本身只认契约内的形状：key 非空字符串、options 是字符串数组、show_if 的两个
 * 键是字符串数组。**形状不对就整张丢掉**：拿半张表渲染出半个表单，比退回内置界面
 * 危险得多（少一个控件 = 操作员配不了，而界面看上去是完整的）。
 */
function normalize(raw: unknown): BuildSchema | null {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null
  const fields: BuildSchemaField[] = []
  const seen = new Set<string>()
  for (const f of asArray((raw as any).fields)) {
    if (!f || typeof f !== 'object') continue
    const key = (f as any).key
    if (typeof key !== 'string' || !key) continue
    // 重复的 key 会让两个控件绑同一个值（改这个那个也跟着变）。生成器那边的
    // 契约测试本来就拒绝重复 key，走到这里说明 .axs 被手改过 —— 留第一个并出声。
    if (seen.has(key)) {
      console.warn('[buildSchema] 字段表里 key 重复，已忽略后一个：', key)
      continue
    }
    seen.add(key)
    const out: BuildSchemaField = {
      key,
      label: str((f as any).label) || key,
      type: str((f as any).type) || 'text',
    }
    const opts = asArray((f as any).options).filter((o) => typeof o === 'string')
    if (opts.length) out.options = opts as string[]
    if ((f as any).default !== undefined) out.default = (f as any).default
    for (const n of ['min', 'max'] as const) {
      if (typeof (f as any)[n] === 'number') out[n] = (f as any)[n]
    }
    const s = (n: string) => str((f as any)[n])
    if (s('placeholder')) out.placeholder = s('placeholder')
    if (s('hint')) out.hint = s('hint')
    if (s('accept')) out.accept = s('accept')
    if (s('group')) out.group = s('group')
    const si = (f as any).show_if
    if (si && typeof si === 'object') {
      const cond: BuildSchemaField['show_if'] = {}
      for (const n of ['format_in', 'listener_in'] as const) {
        const arr = asArray(si[n]).filter((x) => typeof x === 'string')
        if (arr.length) cond[n] = arr as string[]
      }
      if (Object.keys(cond).length) out.show_if = cond
    }
    fields.push(out)
  }
  if (!fields.length) return null
  const v = (raw as any).version
  return { version: typeof v === 'number' ? v : 1, fields }
}

function asArray(v: unknown): any[] {
  return Array.isArray(v) ? v : []
}
function str(v: unknown): string {
  return typeof v === 'string' ? v : ''
}

/** 字段的显隐：两个条件都给就要都满足；没给的条件不参与判断。 */
export function fieldVisible(f: BuildSchemaField, format: string, listenerType: string): boolean {
  const si = f.show_if
  if (!si) return true
  // listener_type 为空（还没选监听器）时 listener_in 一律不满足 —— 与其猜一个，
  // 不如把依赖监听器的字段收起来；反正没选监听器也生成不了。
  if (si.listener_in && !si.listener_in.includes(listenerType)) return false
  if (si.format_in && !si.format_in.includes(format)) return false
  return true
}

/**
 * 当前组合下该下发的字段，**按 schema 顺序**（生成器保证同组连续）。
 *
 * ★ 显隐不只是渲染层的事：生成时按同一张可见表收集配置，所以「收起」等于
 *   「不下发」。否则在 HTTP 监听器上填的代理、切到 TCP 之后会照样发出去 ——
 *   插件不报错，只是把一个它用不上的代理配置编进载荷。
 *
 * 注意 format 和 listener_type 都取自**控制台当前状态**，不是 schema 里的字段值 ——
 * 这两个是显隐判据本身，不能又当判据又当被判断的东西。
 */
export function visibleFields(
  schema: BuildSchema,
  format: string,
  listenerType: string,
): BuildSchemaField[] {
  return schema.fields.filter((f) => fieldVisible(f, format, listenerType))
}

/** 按 group 切块，保序；没写 group 的归到 `fallback`（调用方传「基础」）。 */
export function groupFields(
  fields: BuildSchemaField[],
  fallback = '基础',
): { name: string; fields: BuildSchemaField[] }[] {
  const out: { name: string; fields: BuildSchemaField[] }[] = []
  const byName = new Map<string, { name: string; fields: BuildSchemaField[] }>()
  for (const f of fields) {
    const name = f.group || fallback
    // 按名字归并，而不是只看上一个：分组名和折叠状态是一对一的，同名出现两块
    // 会让它们共用一份开合状态（点一个另一个跟着动），看起来像界面串了。
    // 生成器保证同组连续，这里只是不让手改过的 .axs 把它变成一件怪事。
    let g = byName.get(name)
    if (!g) {
      g = { name, fields: [] }
      byName.set(name, g)
      out.push(g)
    }
    g.fields.push(f)
  }
  return out
}

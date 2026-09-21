/**
 * 拓扑图上「操作员自己画的那部分」。
 *
 * 服务端（extenders/topology 插件）把这份数据当**不透明 blob** 存进
 * ExtenderData 表，只理解外面的 rev。所以这里可以自由加字段 ——
 * 加完不用改插件、不用重编 .so、不用重启。
 *
 * 但反过来说：**服务端不会替我们校验**。存进去的东西可能是旧版本、
 * 被手改过、或者干脆是坏的。所以读取一律走 normBoard() 兜底，
 * 任何一格不合法就退化成默认值，绝不让整个拓扑页白屏。
 */

/** 14 种设备类型。交换机/防火墙/路由器上没有 agent，只能在图上手画。 */
export type DeviceKind =
  | 'switch' | 'router' | 'firewall' | 'database' | 'bastion' | 'web' | 'dc' | 'file'
  | 'attacker' | 'vpn' | 'cloud' | 'ics' | 'honeypot' | 'unknown'

export const DEVICE_KIND_KEYS: DeviceKind[] = [
  'switch', 'router', 'firewall', 'database', 'bastion', 'web', 'dc', 'file',
  'attacker', 'vpn', 'cloud', 'ics', 'honeypot', 'unknown',
]

export type ManualDevice = {
  id: string
  kind: DeviceKind
  name: string
  /**
   * 操作员手填的地址，惯例是 `IP:PORT`（`10.0.0.5:3306`）。
   * **非必须** —— 没填就是空串。
   *
   * 服务端把整块 board 当不透明 blob 存，对这里一个字节都不校验；前端也**故意不校验** ——
   * 写 `dc01.corp.local:389`、`10.0.0.0/24`、`机柜 A-3` 都该收得下。
   * 真正需要机器可读的地址时不缺字段，缺的是别把操作员的自由输入拦在门外。
   */
  addr: string
  /** true = 已控（青），false = 未控（灰）。只对手工设备有意义。 */
  controlled: boolean
  x: number
  y: number
}

export type ManualEdge = {
  id: string
  /** 可以是手工设备的 id，也可以是 agent 的 a_id。 */
  from: string
  to: string
  label?: string
}

/** agent 节点被手工拖动后的位置覆盖。没拖过的走自动布局。 */
export type Pin = { x: number; y: number }

export type GraphBoard = {
  v: 1
  devices: ManualDevice[]
  edges: ManualEdge[]
  pins: Record<string, Pin>
}

/** 服务端信封：{ rev, board }。GET 与 WebSocket 推送是同一种形状。 */
export type GraphSnapshot = { rev: number; board: GraphBoard }

export const EMPTY_BOARD: GraphBoard = { v: 1, devices: [], edges: [], pins: {} }

export const emptySnapshot = (): GraphSnapshot => ({ rev: 0, board: EMPTY_BOARD })

let seq = 0
/** 本地生成 id。不需要全局唯一 —— 冲突概率可忽略，且真撞了也只是两条边重合。 */
export function newBoardId(prefix: 'dev' | 'edge'): string {
  seq += 1
  return `${prefix}_${Date.now().toString(36)}_${seq.toString(36)}`
}

const isObj = (v: any): boolean => !!v && typeof v === 'object' && !Array.isArray(v)

const num = (v: any, dflt = 0): number => (typeof v === 'number' && Number.isFinite(v) ? v : dflt)

const str = (v: any): string => (typeof v === 'string' ? v : '')

function normDevice(raw: any): ManualDevice | null {
  if (!isObj(raw)) return null
  const id = str(raw.id)
  if (!id) return null
  const kind = DEVICE_KIND_KEYS.includes(raw.kind) ? (raw.kind as DeviceKind) : 'unknown'
  return {
    id,
    kind,
    name: str(raw.name),
    // 旧版本写下的设备没有 addr 字段 ⇒ str(undefined) = ''，天然向后兼容。
    addr: str(raw.addr),
    controlled: raw.controlled === true,
    x: num(raw.x),
    y: num(raw.y),
  }
}

function normEdge(raw: any): ManualEdge | null {
  if (!isObj(raw)) return null
  const id = str(raw.id)
  const from = str(raw.from)
  const to = str(raw.to)
  // 自环没有意义，且会让贝塞尔控制点算出 NaN 路径
  if (!id || !from || !to || from === to) return null
  const label = str(raw.label)
  return label ? { id, from, to, label } : { id, from, to }
}

function normPins(raw: any): Record<string, Pin> {
  if (!isObj(raw)) return {}
  const out: Record<string, Pin> = {}
  for (const [k, v] of Object.entries(raw)) {
    if (!k || !isObj(v)) continue
    const x = num((v as any).x, NaN)
    const y = num((v as any).y, NaN)
    if (Number.isFinite(x) && Number.isFinite(y)) out[k] = { x, y }
  }
  return out
}

/**
 * 把任何东西归一成一块能画的图。
 *
 * 不抛异常是刻意的：读到的可能是上个版本写的、手改过的、或者截断的 JSON。
 * 一块坏数据不该让操作员连拓扑页都打不开 —— 丢掉那一格就是了。
 */
export function normBoard(raw: any): GraphBoard {
  if (!isObj(raw)) return EMPTY_BOARD
  const devices = (Array.isArray(raw.devices) ? raw.devices : [])
    .map(normDevice)
    .filter((d: ManualDevice | null): d is ManualDevice => d !== null)
  const edges = (Array.isArray(raw.edges) ? raw.edges : [])
    .map(normEdge)
    .filter((e: ManualEdge | null): e is ManualEdge => e !== null)
  // ★ 这里**不**过滤「指向不存在节点的边」——归一化阶段分不清一个未知 id 是
  // 「已被删掉的设备」还是「agent id」，而 agent id 在服务端重启后是会变的。
  // 按前者处理就会在一次重启后把操作员画的边全删光。
  // 规矩：删设备时由删除逻辑显式带走它的边（数据始终自洽），
  // 渲染时对解析不出来的端点直接跳过（见 Graph.tsx）——数据保留，只是暂时不画。
  return { v: 1, devices, edges, pins: normPins(raw.pins) }
}

/** 从服务端信封里取出快照。GET 与 WS 推送共用这一条路径。 */
export function normSnapshot(raw: any): GraphSnapshot {
  if (!isObj(raw)) return emptySnapshot()
  return { rev: num(raw.rev), board: normBoard(raw.board) }
}

/**
 * 服务端的回答到底是不是**一份真快照**。
 *
 * ★ 不能只看 isObj —— `normSnapshot(null)` 会安静地给出一个空图，而
 *   `api/http.ts` 的 request() 对「HTTP 200 但响应体是空串」会返回 **null**
 *   （`res.text()` 得到空串 ⇒ 跳过 JSON.parse ⇒ data 保持 null ⇒ 一路 return）。
 *   实测：插件没加载时打 `/api/topology/graph` 就是 200 + 空体。
 *
 *   少了这道判据会同时坏两头：
 *     · 读：进拓扑页拉一次全量 → 拿 null 归一成空图 → 把本地编辑悄悄抹掉；
 *     · 写：POST 同样返回 null，而 saveGraph 读 `res?.rev` 落进 `|| rev+1`
 *       的兜底 ⇒ **报告保存成功**，实际一个字节都没落库，刷新即全丢。
 *   后者更毒：操作员看到的是"没报错"，会以为图已经存好了。
 *
 * rev 是插件唯一保证一定在的字段（服务端只理解 rev）。它必须是有限数字。
 */
export function isSnapshot(raw: any): boolean {
  return isObj(raw) && typeof raw.rev === 'number' && Number.isFinite(raw.rev)
}

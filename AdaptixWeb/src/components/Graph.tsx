import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { clamp, isAgentOnline } from '../utils'
import { PageTips } from './PageTips'
import { DeviceIcon, DEVICE_KINDS, ROLE_CHOICES, deviceMeta, roleToKind } from './DeviceIcons'
import { DeviceKind, GraphBoard, ManualDevice, newBoardId } from '../graphBoard'

type GNode = {
  id: string
  sid: string // a_id 短标识 —— 卡片上唯一能区分两台主机的东西
  label: string
  sub: string
  listener: string
  live: boolean
  depth: number
  x: number
  y: number
  isPivotChild: boolean
  /** a_tags 原串。用来在卡片上画「角色」图标（见 DeviceIcons.roleToKind）。 */
  tags: string
}

type GLayer = { depth: number; x: number; w: number; count: number }

/* ── 画布几何：这些常量决定"一台主机占多大地方" ── */
const NODE_W = 168
const NODE_H = 64
const GAP_X = 28 // 同一层里相邻子列的水平间隙
const GAP_Y = 18 // 同一子列里相邻节点的垂直间隙
const LAYER_GAP = 112 // 层与层之间的水平间隙
const PAD = 44 // 画布内边距
const MIN_W = 960
const MIN_H = 560

const STEP_X = NODE_W + GAP_X
const STEP_Y = NODE_H + GAP_Y

/** 缩放范围。下限低于 1 是为了"主机多时拉远看全貌"，上限 2.5 够看清单张卡。 */
const K_MIN = 0.35
const K_MAX = 2.5
/** 滚轮每一格（deltaY）对应的缩放指数系数。 */
const WHEEL_K = 0.0015
/** 位移超过这么多像素才算"拖动"而不是"点击"。 */
const DRAG_SLOP = 3

/* 一层里不超过这么多台就排一列。pivot 子机通常是个位数，这个阈值让常见场景
   （几台根节点 + 几个 pivot）保持"一层一列"的经典读法，只有真堆不下才折成块。 */
const SINGLE_COL_MAX = 12

/**
 * 拓扑布局。
 *
 * 原实现是 `gap = Math.min(96, (H - 120) / list.length)` —— 只封了上限、
 * 没有下限，等于假设"节点总塞得进 620 高的画布"。
 *
 * 真机上这个假设不成立：454 台主机、0 个 pivot，全部落在 depth 0，于是
 *   gap = min(96, 500/454) ≈ 1.1px，而节点高度是 64px。
 * 实测 453 对相邻节点互相重叠，最大重叠深度 75px —— 每张卡片身上压着五十多张，
 * 454 张卡糊成一条竖线。屏幕上只剩"一块红块分不开"，同时 x=470 / x=770
 * 两列完全空着，画布约 2/3 是空的。
 *
 * 所以布局得先回答"放不下怎么办"，再谈排布：
 *   1. 垂直间距有硬下限（GAP_Y），节点永不相叠；
 *   2. 一层放不下就折成多块，块数按"像素上接近正方"选 ——
 *      454 台排成 14 列 × 33 行的一块，而不是 1 × 454 的一条；
 *   3. 画布尺寸由内容算出来（不再写死 1100×620），容器真的可滚
 *      （见 .graph-scroll），而不是把内容等比缩小塞进视口。
 *
 * 顺带去掉原来 `Math.min(depth, COL_X.length - 1)` 的深度截断：
 * 它把 depth ≥ 2 的机器并进第 2 列，使"左=根、右=pivot"这条说明
 * 对深链变成假的。现在有几层画几层，画布向右生长。
 *
 * ★ 这里只算**自动布局**。操作员手工拖过的位置存在 graphBoard.pins 里，
 *   在调用方叠加（见下面的 nodes useMemo）—— 布局函数保持纯函数，
 *   换一张 board 不需要重算布局。
 */
function useLayout(agentList: any[], depthOf: Map<string, number>, parentOf: Map<string, string>) {
  return useMemo(() => {
    const buckets = new Map<number, any[]>()
    for (const a of agentList) {
      const depth = depthOf.get(a.a_id) ?? 0
      const arr = buckets.get(depth) || []
      arr.push(a)
      buckets.set(depth, arr)
    }

    const nodes: GNode[] = []
    const layers: GLayer[] = []
    let cursor = PAD
    let maxH = 0

    for (const [depth, list] of [...buckets.entries()].sort((a, b) => a[0] - b[0])) {
      const n = list.length
      // 像素上接近正方所需的列数：cols*STEP_X ≈ rows*STEP_Y 且 cols*rows = n
      const cols = n > SINGLE_COL_MAX
        ? Math.max(1, Math.round(Math.sqrt((n * STEP_Y) / STEP_X)))
        : 1
      const rows = Math.ceil(n / cols)
      const w = cols * NODE_W + (cols - 1) * GAP_X
      const h = rows * NODE_H + (rows - 1) * GAP_Y
      maxH = Math.max(maxH, h)

      list.forEach((a, i) => {
        // 列优先：先填满一列再换下一列，读起来是自上而下、自左而右
        const c = Math.floor(i / rows)
        const r = i % rows
        nodes.push({
          id: a.a_id,
          sid: String(a.a_id || '').slice(0, 8),
          label: a.a_computer || String(a.a_id || '').slice(0, 8),
          sub: `${a.a_username || '-'}@${a.a_internal_ip || a.a_external_ip || '-'}`,
          listener: a.a_listener || '',
          live: isAgentOnline(a),
          depth,
          x: cursor + c * STEP_X,
          y: PAD + r * STEP_Y,
          isPivotChild: parentOf.has(a.a_id),
          tags: String(a.a_tags || ''),
        })
      })
      layers.push({ depth, x: cursor, w, count: n })
      cursor += w + LAYER_GAP
    }

    return {
      nodes,
      layers,
      W: Math.max(MIN_W, cursor - LAYER_GAP + PAD),
      H: Math.max(MIN_H, maxH + PAD * 2),
    }
  }, [agentList, depthOf, parentOf])
}

/**
 * 连线上该写什么。
 *
 * 线上绝大多数 pivot 都叫 p0 / p1 / p2 —— 那是**服务端自动编号**的产物：
 * `ts_pivot.go` 在 pivotName 为空时会从 p0 开始取第一个没被占用的名字，
 * 而 beacon 插件的 `link` 处理器传的正是空串。所以这个名字对操作员零信息，
 * 摆在连线上只是噪音。
 *
 * 拓扑图的连线真正该回答的是「这条链路怎么走的」：SMB 管道还是 TCP 隧道。
 * 取值顺序：
 *   ① 操作员显式起过的自定义名（非 pN 形态）优先 —— 那是他的命名，不该被顶掉；
 *   ② 子机监听器的协议（bind_smb / bind_tcp），来自 listeners 表，是权威来源；
 *   ③ 监听器名兜底（操作员可能自己把监听器叫成 xxx-smb）；
 *   ④ 都认不出来就**不画胶囊**，而不是把 pN 又摆回去。
 */
function pivotEdgeLabel(pivotName: string | undefined, childListener: string | undefined, listeners: Record<string, any>): string {
  const name = String(pivotName || '').trim()
  if (name && !/^p\d+$/.test(name)) return name
  const lname = String(childListener || '')
  const proto = lname ? String(listeners[lname]?.l_protocol || '') : ''
  if (proto === 'bind_smb') return 'SMB'
  if (proto === 'bind_tcp') return 'TCP'
  if (/smb/i.test(lname)) return 'SMB'
  if (/tcp/i.test(lname)) return 'TCP'
  return ''
}

type XY = { x: number; y: number }

/**
 * 从卡片中心朝目标射出，求与卡片矩形边界的交点。
 *
 * 手工连线的两端是**操作员自己指定的方向**，所以箭头方向有意义（攻击路径要读得出来）。
 * 直接连中心的话箭头会被卡片盖住；连固定边（比如"总是从右边出"）在反向连接时
 * 会绕一大圈。求交点是最短的写法，而且任意方向都对。
 */
function rectExit(c: XY, to: XY): XY {
  const dx = to.x - c.x
  const dy = to.y - c.y
  if (!dx && !dy) return c
  const hw = NODE_W / 2
  const hh = NODE_H / 2
  const tx = dx ? hw / Math.abs(dx) : Infinity
  const ty = dy ? hh / Math.abs(dy) : Infinity
  const t = Math.min(tx, ty)
  return { x: c.x + dx * t, y: c.y + dy * t }
}

function centerOf(p: XY): XY {
  return { x: p.x + NODE_W / 2, y: p.y + NODE_H / 2 }
}

function linkPath(a: XY, b: XY): string {
  const ca = centerOf(a)
  const cb = centerOf(b)
  const p1 = rectExit(ca, cb)
  const p2 = rectExit(cb, ca)
  return `M ${p1.x} ${p1.y} L ${p2.x} ${p2.y}`
}

/** pivot 连线：父节点右边 → 子节点左边，水平 S 形贝塞尔。 */
function edgePath(a: XY, b: XY): string {
  const x1 = a.x + NODE_W
  const y1 = a.y + NODE_H / 2
  const x2 = b.x
  const y2 = b.y + NODE_H / 2
  const mx = (x1 + x2) / 2
  return `M ${x1} ${y1} C ${mx} ${y1}, ${mx} ${y2}, ${x2} ${y2}`
}

type MenuItem = { label?: string; run?: () => void; danger?: boolean; sep?: boolean }
type Menu = { x: number; y: number; items: MenuItem[] }

export function Graph() {
  const agents = useStore((s) => s.agents) || {}
  const pivots = useStore((s) => s.pivots) || {}
  const listeners = useStore((s) => s.listeners) || {}
  const selectAgent = useStore((s) => s.selectAgent)
  const openConsole = useStore((s) => s.openConsole)
  const api = useStore((s) => s.api)
  const refreshRest = useStore((s) => s.refreshRest)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  // 手工图：存在服务端，所有人共享。rev 用于乐观锁（见 store.saveGraph）。
  const board = useStore((s) => s.graph.board)
  const applyGraph = useStore((s) => s.applyGraph)
  const saveGraph = useStore((s) => s.saveGraph)
  const loadGraph = useStore((s) => s.loadGraph)

  const [q, setQ] = useState('')
  const [hover, setHover] = useState<string | null>(null)
  /** 缩放倍数。1 = 100%，渲染尺寸与 viewBox 一一对应。 */
  const [k, setK] = useState(1)
  const [panning, setPanning] = useState(false)
  /** 正在拖的节点（含未提交的坐标）。null = 没在拖。 */
  const [drag, setDrag] = useState<{ id: string; x: number; y: number; dev: boolean } | null>(null)
  const [menu, setMenu] = useState<Menu | null>(null)
  const [addOpen, setAddOpen] = useState(false)
  const [selEdge, setSelEdge] = useState<string | null>(null)
  /** 正在为哪个节点选"连线到…"的目标。 */
  const [linkFrom, setLinkFrom] = useState<string | null>(null)

  const scrollRef = useRef<HTMLDivElement | null>(null)
  /** 缩放后要恢复的滚动位置。见 onWheel 的注释。 */
  const pendingScroll = useRef<{ left: number; top: number } | null>(null)
  /** 刚刚发生过拖动 ⇒ 随后那一次 click 必须丢掉。 */
  const movedRef = useRef(false)

  useEffect(() => { void loadGraph() }, [loadGraph])

  /* ── 右键菜单 / 添加下拉的关闭：沿用全站范式（4 处先例） ── */
  useEffect(() => {
    const close = () => { setMenu(null); setAddOpen(false) }
    window.addEventListener('click', close)
    window.addEventListener('scroll', close, true)
    return () => {
      window.removeEventListener('click', close)
      window.removeEventListener('scroll', close, true)
    }
  }, [])

  const agentList = useMemo(() => {
    let arr = Object.values(agents)
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((a) =>
        [a.a_id, a.a_computer, a.a_username, a.a_internal_ip, a.a_listener, a.a_tags]
          .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
    }
    return arr
  }, [agents, q])

  const pivotList = useMemo(() => Object.values(pivots), [pivots])

  const parentOf = useMemo(() => {
    const m = new Map<string, string>()
    for (const p of pivotList) {
      if (p.p_child_agent_id && p.p_parent_agent_id) m.set(p.p_child_agent_id, p.p_parent_agent_id)
    }
    return m
  }, [pivotList])

  // 层级：根（无父）depth 0；pivot 子节点 depth = 父 depth + 1
  const depthOf = useMemo(() => {
    const d = new Map<string, number>()
    const resolve = (id: string, seen: Set<string>): number => {
      if (seen.has(id)) return 0
      seen.add(id)
      const p = parentOf.get(id)
      if (!p) return 0
      const v = resolve(p, seen) + 1
      seen.delete(id)
      return v
    }
    for (const a of agentList) d.set(a.a_id, resolve(a.a_id, new Set()))
    return d
  }, [agentList, parentOf])

  const auto = useLayout(agentList, depthOf, parentOf)

  /* 自动布局 + 手工位置覆盖（pins）。拖动中的节点用未提交的坐标画， */
  /* 这样 mousemove 不必每帧写一次 store / 发一次 HTTP。 */
  const nodes = useMemo(() => auto.nodes.map((n) => {
    if (drag && !drag.dev && drag.id === n.id) return { ...n, x: drag.x, y: drag.y }
    const p = board.pins[n.id]
    return p ? { ...n, x: p.x, y: p.y } : n
  }), [auto.nodes, board.pins, drag])

  const devices = useMemo(() => board.devices.map((d) =>
    (drag && drag.dev && drag.id === d.id) ? { ...d, x: drag.x, y: drag.y } : d,
  ), [board.devices, drag])

  /**
   * 画布要把手工摆过的东西都装进去。只按自动布局算的话，拖到右下就会被裁掉。
   *
   * ★ 必须按上面「渲染后的 nodes / devices」算，不能按 board.devices + drag 算。
   *   差别就在 agent 节点：它松手后位置落在 board.pins，而 drag 同时被清空 ——
   *   只认 drag 的话，拖动中画布跟着长、一松手就缩回 auto.H，节点被
   *   .graph-svg 的 overflow:hidden 裁掉；内容又比视口矮时连滚动条都没有，
   *   于是该节点永远够不着（2026-09-19 实测：pin y=845.56 vs viewBox 高 726）。
   *   从 nodes/devices 反推，pins 与拖动坐标就天然都算进去了。
   *   （只需下界：拖动要求指针落在节点上，摆不出负坐标。）
   */
  const W = useMemo(() => {
    let w = auto.W
    for (const n of nodes) w = Math.max(w, n.x + NODE_W + PAD)
    for (const d of devices) w = Math.max(w, d.x + NODE_W + PAD)
    return w
  }, [auto.W, nodes, devices])

  const H = useMemo(() => {
    let h = auto.H
    for (const n of nodes) h = Math.max(h, n.y + NODE_H + PAD)
    for (const d of devices) h = Math.max(h, d.y + NODE_H + PAD)
    return h
  }, [auto.H, nodes, devices])

  const edges = useMemo(() => {
    const byId = new Map(nodes.map((n) => [n.id, n]))
    const list: Array<{ from: GNode; to: GNode; label: string }> = []
    for (const p of pivotList) {
      const a = byId.get(p.p_parent_agent_id || '')
      const b = byId.get(p.p_child_agent_id || '')
      if (a && b) list.push({ from: a, to: b, label: pivotEdgeLabel(p.p_pivot_name, b.listener, listeners) })
    }
    return list
  }, [nodes, pivotList, listeners])

  /**
   * 手工连线的端点解析表（agent 节点 + 手工设备）。
   *
   * ★ 解析不出来的端点**直接跳过不画**，但数据保留。原因见 graphBoard.ts：
   *   归一化阶段分不清一个未知 id 是"已被删掉的设备"还是"agent id"，
   *   而 agent id 在服务端重启后是会变的。级联删边 = 一次重启毁掉整张图。
   *   删设备时由删除逻辑显式带走它的边，数据始终自洽。
   */
  const posOf = useMemo(() => {
    const m = new Map<string, XY>()
    for (const n of nodes) m.set(n.id, n)
    for (const d of devices) if (!m.has(d.id)) m.set(d.id, d)
    return m
  }, [nodes, devices])

  const manualEdges = useMemo(() => board.edges
    .map((e) => {
      const a = posOf.get(e.from)
      const b = posOf.get(e.to)
      return a && b ? { e, a, b } : null
    })
    .filter((x): x is { e: typeof board.edges[number]; a: XY; b: XY } => x !== null),
  [board.edges, posOf])

  /** 图上全部的节点（agent + 手工设备）—— "连线到…"的候选表。 */
  const allTargets = useMemo(() => [
    ...nodes.map((n) => ({ id: n.id, label: n.label, sub: '主机', kind: roleToKind(n.tags) })),
    // 设备有地址就优先显示地址：类型已经由图标表达了，而"选哪一台"要靠地址。
    ...devices.map((d) => ({ id: d.id, label: d.name || deviceMeta(d.kind).label, sub: d.addr || deviceMeta(d.kind).label, kind: d.kind as DeviceKind })),
  ], [nodes, devices])

  /* ─────────────── 写入 ─────────────── */

  /**
   * 落一次盘。本地立即生效（applyGraph），服务端异步跟上（saveGraph）。
   *
   * ★ 顺序不能反：saveGraph 用 store 里当前的 rev 当 base_rev，
   *   先 applyGraph 把 rev 保持不变地写回本地，base_rev 才是对的。
   */
  function commit(next: GraphBoard) {
    applyGraph({ rev: useStore.getState().graph.rev, board: next })
    void saveGraph(next)
  }

  function deviceById(id: string) { return board.devices.find((d) => d.id === id) }

  function addDevice(kind: DeviceKind) {
    const el = scrollRef.current
    const meta = deviceMeta(kind)
    // 落在当前**视口中心**，不是画布原点 —— 放大/平移过之后，
    // 落在原点的设备会在视口外，看起来像"点了没反应"。
    let x = PAD + 40
    let y = PAD + 40
    if (el) {
      x = (el.scrollLeft + el.clientWidth / 2) / k - NODE_W / 2
      y = (el.scrollTop + el.clientHeight / 2) / k - NODE_H / 2
    }
    x = Math.max(0, x)
    y = Math.max(0, y)
    // 连点两次不该叠成一张（叠上了就看不出加了两个）
    let guard = 0
    while (board.devices.some((d) => Math.abs(d.x - x) < 8 && Math.abs(d.y - y) < 8) && guard++ < 40) {
      x += 26
      y += 22
    }
    // 地址留空：加设备是一步动作，不该被一个"可选"字段打断成两步。
    // 要填就走右键 →「设置地址…」。
    const dev: ManualDevice = { id: newBoardId('dev'), kind, name: meta.label, addr: '', controlled: false, x, y }
    commit({ ...board, devices: [...board.devices, dev] })
    setAddOpen(false)
    pushToast('ok', `已添加${meta.label}`)
  }

  /**
   * 设备地址（惯例 `IP:PORT`，**非必须**）。
   *
   * 刻意不做格式校验：这是个给人看的标注，不是给机器解析的字段。
   * 写域名、网段、机柜号都该收得下 —— 拦下来只会逼操作员把信息记在别处。
   * 清空 = 删掉这一行（卡片上那一行随之消失）。
   */
  function setDeviceAddr(id: string) {
    const d = deviceById(id)
    if (!d) return
    const addr = prompt('设备地址（如 10.0.0.5:3306，可留空）：', d.addr)
    if (addr == null) return
    const v = addr.trim()
    commit({ ...board, devices: board.devices.map((x) => (x.id === id ? { ...x, addr: v } : x)) })
  }

  function removeDevice(id: string) {
    const d = deviceById(id)
    if (!d) return
    // ★ 连带删掉它的连线。这和"悬空引用要保留"那条规则不冲突：
    //   那条规则是给「agent 重启后 id 变了」用的（数据不能丢），
    //   而这里的意图明确 —— 设备没了，挂在它上面的线自然也没了。
    //   少删这一处，图上就会留下永远悬空的虚线。
    commit({
      ...board,
      devices: board.devices.filter((x) => x.id !== id),
      edges: board.edges.filter((e) => e.from !== id && e.to !== id),
    })
    if (linkFrom === id) setLinkFrom(null)
    setMenu(null)
  }

  function removeEdge(id: string) {
    commit({ ...board, edges: board.edges.filter((e) => e.id !== id) })
    setSelEdge(null)
    setMenu(null)
  }

  function addEdge(from: string, to: string) {
    if (!from || !to || from === to) return
    if (board.edges.some((e) => (e.from === from && e.to === to) || (e.from === to && e.to === from))) {
      pushToast('warn', '这两个节点之间已经有连线了')
      return
    }
    commit({ ...board, edges: [...board.edges, { id: newBoardId('edge'), from, to }] })
    setLinkFrom(null)
  }

  function toggleControlled(id: string) {
    commit({
      ...board,
      devices: board.devices.map((d) => (d.id === id ? { ...d, controlled: !d.controlled } : d)),
    })
    setMenu(null)
  }

  function renameDevice(id: string) {
    const d = deviceById(id)
    if (!d) return
    const name = prompt('设备名称：', d.name)
    if (name == null) return
    commit({ ...board, devices: board.devices.map((x) => (x.id === id ? { ...x, name } : x)) })
  }

  function setDeviceKind(id: string, kind: DeviceKind) {
    const meta = deviceMeta(kind)
    commit({
      ...board,
      devices: board.devices.map((d) => {
        if (d.id !== id) return d
        // 名字还是出厂默认（= 旧类型名）就跟着换；操作员改过的名字不覆盖。
        const wasDefault = !d.name || d.name === deviceMeta(d.kind).label
        return { ...d, kind, name: wasDefault ? meta.label : d.name }
      }),
    })
  }

  function clearManual() {
    const n = board.devices.length + board.edges.length + Object.keys(board.pins).length
    if (!n) { pushToast('warn', '还没有手工画的内容'); return }
    if (!confirm(`清空全部手工内容？\n\n· ${board.devices.length} 个手工设备\n· ${board.edges.length} 条手工连线\n· ${Object.keys(board.pins).length} 个手工摆放的主机位置\n\n（agent 主机本身不受影响）`)) return
    commit({ ...board, devices: [], edges: [], pins: {} })
  }

  function resetPositions() {
    if (!Object.keys(board.pins).length) { pushToast('warn', '没有手工摆放的主机'); return }
    commit({ ...board, pins: {} })
  }

  /** 角色 = a_tags 里那个人类可读的词。走**已有的** set/tag 接口，不新增字段。 */
  async function setRole(id: string, word: string) {
    try {
      await api.setTag([id], word)
      // 照抄 Hosts.tsx:572 的既有写法：标签要靠重拉才回到 store。
      await refreshRest()
      pushToast('ok', word ? `已设为 ${word}` : '已清除角色')
    } catch (e: any) {
      pushLog('err', e?.message || String(e))
    }
  }

  /* ─────────────── 缩放 / 平移 / 拖动 ─────────────── */

  /**
   * 滚轮缩放，以光标为锚点。
   *
   * ★ 必须用原生 addEventListener 而不是 React 的 onWheel：React 在 root 上
   *   把 wheel 注册成 **passive**，onWheel 里调 preventDefault() 是无效的
   *   （控制台会报 "Unable to preventDefault inside passive event listener"），
   *   结果是页面一边缩放一边还照常滚动。
   */
  useEffect(() => {
    const el = scrollRef.current
    if (!el) return
    const onWheel = (e: WheelEvent) => {
      e.preventDefault()
      const rect = el.getBoundingClientRect()
      const vx = e.clientX - rect.left
      const vy = e.clientY - rect.top
      // 光标下的**图坐标**（缩放前）。缩放的语义就是"让这个点不动"。
      const gx = (el.scrollLeft + vx) / k
      const gy = (el.scrollTop + vy) / k
      const next = clamp(k * Math.exp(-e.deltaY * WHEEL_K), K_MIN, K_MAX)
      if (Math.abs(next - k) < 1e-6) return
      setK(next)
      pendingScroll.current = { left: gx * next - vx, top: gy * next - vy }
    }
    el.addEventListener('wheel', onWheel, { passive: false })
    return () => el.removeEventListener('wheel', onWheel)
  }, [k])

  /* ★ 滚动位置必须等 React 把新的 width/height 提交到 DOM 之后再设：
     在那之前 scrollWidth 还是旧值，直接写 scrollLeft 会被旧范围 clamp 掉，
     表现为"放大时光标下的点会漂"。 */
  useLayoutEffect(() => {
    const el = scrollRef.current
    const p = pendingScroll.current
    if (!el || !p) return
    pendingScroll.current = null
    void el.scrollWidth // 强制一次排版，确保下面的赋值按新范围 clamp
    el.scrollLeft = p.left
    el.scrollTop = p.top
  }, [k])

  function fitToView() {
    const el = scrollRef.current
    if (!el) return
    // 只缩小不放大：内容装得下时保持 100%，放大到 100% 以上没有意义。
    const kk = clamp(Math.min(el.clientWidth / W, el.clientHeight / H), K_MIN, 1)
    pendingScroll.current = { left: 0, top: 0 }
    setK(kk)
  }

  function resetZoom() {
    pendingScroll.current = { left: 0, top: 0 }
    setK(1)
  }

  /** 背景拖动 = 平移。直接改 scrollLeft/Top，不进 state —— 每帧 setState 会卡。 */
  function onBgMouseDown(e: React.MouseEvent) {
    if (e.button !== 0) return
    const el = scrollRef.current
    if (!el) return
    movedRef.current = false
    const sx = e.clientX
    const sy = e.clientY
    const sl = el.scrollLeft
    const st = el.scrollTop
    let moved = false
    setPanning(true)
    const move = (ev: MouseEvent) => {
      const dx = ev.clientX - sx
      const dy = ev.clientY - sy
      if (!moved && Math.abs(dx) + Math.abs(dy) > DRAG_SLOP) moved = true
      if (!moved) return
      el.scrollLeft = sl - dx
      el.scrollTop = st - dy
    }
    const up = () => {
      setPanning(false)
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  /**
   * 拖节点。写法和 App.tsx:281-300 的终端分隔条一致（window 监听 + 起止差值），
   * 但补了两件那边没有的事：
   *   1. 增量要 **除以 k** —— 图坐标 ≠ 屏幕像素，放大后差 2.5 倍；
   *   2. movedRef 防 click —— 拖完鼠标若回到同一元素，浏览器照样派发 click，
   *      不拦的话每拖一次节点就弹一个控制台。
   */
  function startDrag(e: React.MouseEvent, id: string, x: number, y: number, dev: boolean) {
    if (e.button !== 0) return
    e.preventDefault() // 防拖拽时选中卡片文字
    e.stopPropagation() // 防同时触发背景平移
    movedRef.current = false
    const sx = e.clientX
    const sy = e.clientY
    let moved = false
    let nx = x
    let ny = y
    const move = (ev: MouseEvent) => {
      const dx = ev.clientX - sx
      const dy = ev.clientY - sy
      if (!moved && Math.abs(dx) + Math.abs(dy) > DRAG_SLOP) moved = true
      if (!moved) return
      nx = Math.max(0, x + dx / k)
      ny = Math.max(0, y + dy / k)
      setDrag({ id, x: nx, y: ny, dev })
    }
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
      setDrag(null)
      movedRef.current = moved
      if (!moved) return
      if (dev) {
        commit({ ...board, devices: board.devices.map((d) => (d.id === id ? { ...d, x: nx, y: ny } : d)) })
      } else {
        commit({ ...board, pins: { ...board.pins, [id]: { x: nx, y: ny } } })
      }
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  function openAgent(id: string) {
    // ★ 拖动结束时那一次 click 必须丢掉，否则拖完就弹控制台
    if (movedRef.current) { movedRef.current = false; return }
    selectAgent(id)
    openConsole(id)
  }

  /* ─────────────── 菜单构造 ─────────────── */

  function roleMenu(x: number, y: number, id: string): Menu {
    return {
      x, y,
      items: [
        ...ROLE_CHOICES.map((r) => ({ label: r.word, run: () => { setMenu(null); void setRole(id, r.word) } })),
        { sep: true },
        { label: '清除角色', run: () => { setMenu(null); void setRole(id, '') } },
      ],
    }
  }

  function kindMenu(x: number, y: number, id: string): Menu {
    return {
      x, y,
      items: DEVICE_KINDS.map((dk) => ({
        label: dk.label,
        run: () => { setMenu(null); setDeviceKind(id, dk.key) },
      })),
    }
  }

  /** 菜单的越界回退，沿用全站范式。 */
  function menuStyle(m: Menu): React.CSSProperties {
    return { left: Math.min(m.x, window.innerWidth - 200), top: Math.min(m.y, window.innerHeight - 380) }
  }

  const liveCount = agentList.filter((a) => isAgentOnline(a)).length
  const pinnedCount = Object.keys(board.pins).length

  return (
    <div className="page graph-page">
      <div className="toolbar">
        <strong>拓扑图</strong>
        <input type="search" placeholder="过滤主机" value={q} onChange={(e) => setQ(e.target.value)} style={{ maxWidth: 180 }} />
        {/* ★ 这个 stopPropagation 不是可选的。上面那个 window 级 click 监听会把
            addOpen 设回 false；本按钮用的是普通 onClick（右键菜单们由 onContextMenu
            打开、不产生 click 事件，所以它们躲过了这一劫）。少了它，同一次点击先
            置 true 再置 false，批处理完的净效果就是"按钮点不动"。
            顺带 setMenu(null)：面板与右键菜单不该同时开着。 */}
        <button
          type="button"
          className="ghost"
          title="在图上添加一个没有 agent 的设备（交换机 / 防火墙 / 数据库…）"
          onClick={(e) => { e.stopPropagation(); setMenu(null); setAddOpen((v) => !v) }}
        >
          添加设备 ▾
        </button>
        <button type="button" className="ghost" onClick={fitToView} title="缩放到刚好装下整张图">适配</button>
        <span className="dim mono" title="滚轮缩放，拖动空白处平移">{Math.round(k * 100)}%</span>
        <div className="stats">
          <span className="signal">{liveCount} 在线</span>
          <span className="dim">/ {agentList.length} 主机</span>
          <span className="dim">· {pivotList.length} pivot</span>
          {board.devices.length > 0 && <span className="dim">· {board.devices.length} 设备</span>}
        </div>
        <PageTips id="graph">
          <p>拓扑图展示主机与 pivot 链路；点击节点打开对应主机控制台。</p>
          <p>pivot 由控制台 <code>link</code> / <code>unlink</code> 命令建立与拆除。</p>
          <p><b>滚轮</b>缩放（以鼠标位置为中心），<b>拖动空白处</b>平移，<b>双击空白处</b>回到 100%。</p>
          <p><b>拖动节点</b>可手工摆放；位置、手工设备与连线都存在<b>服务端</b>，所有操作员共享。</p>
          <p><b>右键节点</b>：连线、标记已控/未控、改名、改类型、删除、设为角色。</p>
          <p>交换机 / 防火墙 / 数据库这类<b>没有 agent 的设备</b>用「添加设备」画；已控=青、未控=灰。</p>
          <p>手工设备可以右键「<b>设置地址</b>」填 <code>IP:PORT</code>（<b>非必须</b>，留空即不显示）。</p>
          <p>给已有 beacon 的主机打「数据库 / 堡垒机…」<b>标签</b>，卡片上会自动出现对应图标。</p>
        </PageTips>
      </div>

      {/* .graph-wrap 只负责边框和定位；真正滚动的是内层 .graph-scroll。
          图例是 .graph-wrap 的绝对定位子元素 —— 如果让 .graph-wrap 自己滚，
          图例会被内容带着滚出视口。 */}
      <div className="graph-wrap">
        <div
          className={`graph-scroll${panning ? ' panning' : ''}`}
          ref={scrollRef}
          onMouseDown={onBgMouseDown}
        >
          {/* .graph-zoom 只为撑出滚动范围（宽高 = 内容 × 缩放），
              它本身不画东西；真正被缩放的是里面那个 <svg>。
              反过来做（给 svg 设 width=W*k）会让 SVG 重新按 viewBox 缩放内容，
              与"渲染尺寸和 viewBox 一一对应"的既有约定打架。 */}
          <div className="graph-zoom" style={{ width: W * k, height: H * k }}>
            <svg
              viewBox={`0 0 ${W} ${H}`}
              width={W}
              height={H}
              className="graph-svg"
              style={{ transform: `scale(${k})`, transformOrigin: '0 0' }}
              role="img"
              aria-label={`拓扑图，共 ${agentList.length} 台主机、${board.devices.length} 个手工设备`}
            >
              <defs>
                <pattern id="ggrid" width="28" height="28" patternUnits="userSpaceOnUse">
                  <path d="M 28 0 L 0 0 0 28" fill="none" stroke="rgba(43,213,118,0.05)" strokeWidth="1" />
                </pattern>
                <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
                  <path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(34,211,238,0.6)" />
                </marker>
                <marker id="arrowManual" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
                  <path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(148,163,184,0.85)" />
                </marker>
                <filter id="glowG" x="-60%" y="-60%" width="220%" height="220%">
                  <feGaussianBlur stdDeviation="6" result="b" />
                  <feMerge>
                    <feMergeNode in="b" />
                    <feMergeNode in="SourceGraphic" />
                  </feMerge>
                </filter>
              </defs>

              <rect width={W} height={H} fill="url(#ggrid)" onDoubleClick={resetZoom} />

              {/* 层分隔线 + 层标：让操作员一眼知道这一块是什么、有多少台 */}
              {auto.layers.map((L) => (
                <g key={L.depth}>
                  <line
                    x1={L.x - LAYER_GAP / 2}
                    y1={PAD - 26}
                    x2={L.x - LAYER_GAP / 2}
                    y2={Math.max(H - PAD + 26, PAD + 40)}
                    className="graph-layer-rule"
                  />
                  <text x={L.x} y={PAD - 30} className="graph-layer-label">
                    {L.depth === 0 ? '根节点' : `第 ${L.depth} 层`} · {L.count}
                  </text>
                </g>
              ))}

              {/* edges：贝塞尔曲线 + 流向动画 + 中点点名。画在节点之前，
                  所以大量节点时连线会被卡片盖住而不是压在卡片上。 */}
              {edges.map((e, i) => {
                const midX = (e.from.x + NODE_W + e.to.x) / 2
                const midY = (e.from.y + e.to.y) / 2 + NODE_H / 2
                const hl = hover === e.from.id || hover === e.to.id
                return (
                  <g key={i} className={`graph-edge ${hl ? 'hl' : ''}`}>
                    <path d={edgePath(e.from, e.to)} className="graph-edge-flow" />
                    <path d={edgePath(e.from, e.to)} className="graph-edge-path" markerEnd="url(#arrow)" />
                    {/* 标签直接压在线上，靠 .graph-edge-label 的底色描边把线挖断，不画外框。
                        带边框的圆角胶囊看着是"贴在图上的一颗按钮"，不是这张图自己的标注；
                        而且两条线汇聚到同一个父节点时，两个胶囊会上下叠在同一高度，
                        更像一列按钮。挖空画法下，标签读起来是"线给字让了路"。 */}
                    {e.label ? (
                      <text x={midX} y={midY} textAnchor="middle" dy="3.5" className="graph-edge-label">{e.label}</text>
                    ) : null}
                  </g>
                )
              })}

              {/* 手工连线：虚线 + 中性灰箭头，和 pivot 的青色实线动画流线一眼分得开。
                  两条 path 叠着画：粗的那条透明，只为把点击热区放大到 12px ——
                  1.6px 的线本身几乎点不中。 */}
              {manualEdges.map(({ e, a, b }) => {
                const d = linkPath(a, b)
                return (
                  <g key={e.id} className={`graph-edge-manual-g${selEdge === e.id ? ' sel' : ''}`}>
                    <path
                      d={d}
                      className="graph-edge-hit"
                      onMouseDown={(ev) => ev.stopPropagation()}
                      onClick={(ev) => { ev.stopPropagation(); setSelEdge(selEdge === e.id ? null : e.id) }}
                      onContextMenu={(ev) => {
                        ev.preventDefault()
                        ev.stopPropagation()
                        setSelEdge(e.id)
                        setMenu({ x: ev.clientX, y: ev.clientY, items: [{ label: '删除连线', danger: true, run: () => removeEdge(e.id) }] })
                      }}
                    />
                    <path d={d} className="graph-edge-manual" markerEnd="url(#arrowManual)" />
                  </g>
                )
              })}

              {/* nodes */}
              {nodes.map((n) => {
                const hl = hover === n.id
                const role = roleToKind(n.tags)
                return (
                  <g
                    key={n.id}
                    className={`graph-node ${n.live ? 'live' : 'dead'} ${hl ? 'hl' : ''}`}
                    transform={`translate(${n.x},${n.y})`}
                    onMouseDown={(e) => startDrag(e, n.id, n.x, n.y, false)}
                    onClick={() => openAgent(n.id)}
                    onContextMenu={(e) => {
                      e.preventDefault()
                      e.stopPropagation()
                      const mx = e.clientX, my = e.clientY
                      setMenu({
                        x: mx, y: my,
                        items: [
                          { label: '打开控制台', run: () => openAgent(n.id) },
                          { sep: true },
                          { label: '连线到…', run: () => { setMenu(null); setLinkFrom(n.id) } },
                          { label: '设为角色…', run: () => setMenu(roleMenu(mx, my, n.id)) },
                          { label: '清除角色', run: () => { setMenu(null); void setRole(n.id, '') } },
                        ],
                      })
                    }}
                    onMouseEnter={() => setHover(n.id)}
                    onMouseLeave={() => setHover(null)}
                    style={{ cursor: drag?.id === n.id ? 'grabbing' : 'pointer' }}
                  >
                    <title>{`${n.label}\n${n.sub}\n${n.sid}${n.listener ? ` · ${n.listener}` : ''}${n.tags ? `\n标签: ${n.tags}` : ''}`}</title>
                    <rect
                      width={NODE_W}
                      height={NODE_H}
                      rx={9}
                      className={`graph-node-bg${n.isPivotChild ? ' pivot' : ''}`}
                      filter={n.live ? 'url(#glowG)' : undefined}
                    />
                    <circle cx={14} cy={14} r={5} className="graph-dot" filter={n.live ? 'url(#glowG)' : undefined} />
                    <text x={28} y={20} className="graph-label">{n.label}</text>
                    <text x={28} y={38} className="graph-sub">{n.sub}</text>
                    {/* 底部一行左右分列：左边监听器，右边 agent 短 ID。
                        短 ID 是这张卡片上唯一真正唯一的东西 —— 实测 454 台主机里
                        453 台主机名都叫 10-7-34-40、448 台的 user@ip 都是 root@0.0.0.0，
                        只有 label+sub+listener 全算上也才 7 种可区分的长相。
                        原来卡片只画了 label/sub/listener，
                        于是"点开哪台"完全靠猜。ID 本来就存在（a_id 是 agents 的键），
                        只是从来没显示出来。 */}
                    <text x={28} y={54} className="graph-listener">{n.listener}</text>
                    <text x={NODE_W - 12} y={54} textAnchor="end" className="graph-sid">{n.sid}</text>
                    {/* 角色图标：把 a_tags 里那个人类可读的词画成设备图标。
                        有图标时 PIVOT 让位 —— 两个都挤在右上角会叠字。 */}
                    {role && (
                      <g transform={`translate(${NODE_W - 30},7)`} className="graph-role-ico">
                        <DeviceIcon kind={role} size={18} />
                      </g>
                    )}
                    {n.isPivotChild && (
                      <text x={role ? NODE_W - 36 : NODE_W - 12} y={16} textAnchor="end" className="graph-pivot-tag">PIVOT</text>
                    )}
                  </g>
                )
              })}

              {/* 手工设备：形状沿用 agent 卡片，颜色和图标说明它不是一台有 beacon 的机器 */}
              {devices.map((d) => {
                const meta = deviceMeta(d.kind)
                const hl = hover === d.id
                return (
                  <g
                    key={d.id}
                    className={`graph-node graph-dev ${d.controlled ? 'ctrl' : 'unctrl'} ${hl ? 'hl' : ''}`}
                    transform={`translate(${d.x},${d.y})`}
                    onMouseDown={(e) => startDrag(e, d.id, d.x, d.y, true)}
                    onClick={() => { if (movedRef.current) movedRef.current = false }}
                    onContextMenu={(e) => {
                      e.preventDefault()
                      e.stopPropagation()
                      const mx = e.clientX, my = e.clientY
                      setMenu({
                        x: mx, y: my,
                        items: [
                          { label: d.controlled ? '标记为未控' : '标记为已控', run: () => toggleControlled(d.id) },
                          { label: '重命名…', run: () => { setMenu(null); renameDevice(d.id) } },
                          { label: d.addr ? '设置地址…' : '设置地址（IP:PORT）…', run: () => { setMenu(null); setDeviceAddr(d.id) } },
                          { label: '更改类型…', run: () => setMenu(kindMenu(mx, my, d.id)) },
                          { sep: true },
                          { label: '连线到…', run: () => { setMenu(null); setLinkFrom(d.id) } },
                          { sep: true },
                          { label: '删除设备', danger: true, run: () => removeDevice(d.id) },
                        ],
                      })
                    }}
                    onMouseEnter={() => setHover(d.id)}
                    onMouseLeave={() => setHover(null)}
                    style={{ cursor: drag?.id === d.id ? 'grabbing' : 'move' }}
                  >
                    <title>{`${d.name || meta.label}${d.addr ? `\n${d.addr}` : ''}\n${meta.label} · ${d.controlled ? '已控' : '未控'}`}</title>
                    <rect width={NODE_W} height={NODE_H} rx={9} className="graph-node-bg graph-dev-bg" />
                    <g transform="translate(13,13)" className="graph-dev-ico">
                      <DeviceIcon kind={d.kind} size={24} />
                    </g>
                    <text x={46} y={26} className="graph-label">{d.name || meta.label}</text>
                    <text x={46} y={43} className="graph-sub">{meta.label}</text>
                    {/* 地址非必须：没填就整行不画。画一个空行会让卡片看起来缺了东西，
                        也会让"没地址"和"有地址但是空的"两种状态长得一样。 */}
                    {d.addr && <text x={46} y={58} className="graph-dev-addr">{d.addr}</text>}
                    <circle cx={NODE_W - 14} cy={14} r={5} className="graph-dot" />
                    <text x={NODE_W - 26} y={18} textAnchor="end" className="graph-dev-state">
                      {d.controlled ? '已控' : '未控'}
                    </text>
                  </g>
                )
              })}

              {nodes.length === 0 && devices.length === 0 && (
                <g transform={`translate(${W / 2},${H / 2})`}>
                  <text textAnchor="middle" className="graph-empty-title">暂无主机</text>
                  <text y={26} textAnchor="middle" className="graph-empty-hint">agent 上线后自动出现在拓扑中</text>
                  <text y={48} textAnchor="middle" className="graph-empty-hint">也可以用工具栏的「添加设备」先画交换机 / 防火墙这类没有 agent 的设备</text>
                </g>
              )}
            </svg>
          </div>
        </div>

        <div className="graph-legend">
          <span><i className="lgd lgd-live" />在线</span>
          <span><i className="lgd lgd-dead" />离线</span>
          {/* 没有 pivot 时不摆这条图例 —— 解释一个不存在的东西只会让人去找它 */}
          {edges.length > 0 && <span><i className="lgd lgd-pivot" />pivot 链路</span>}
          {board.devices.some((d) => d.controlled) && <span><i className="lgd lgd-ctrl" />设备·已控</span>}
          {board.devices.some((d) => !d.controlled) && <span><i className="lgd lgd-unctrl" />设备·未控</span>}
          {manualEdges.length > 0 && <span><i className="lgd lgd-manual" />手工连线</span>}
        </div>

        {/* ── 添加设备（14 种类型）。做成固定面板而不是下拉：14 项的下拉太长，
            而且这个面板要能一边看图一边挑。 ── */}
        {addOpen && (
          <div className="graph-pop graph-pop-add" onClick={(e) => e.stopPropagation()}>
            <div className="ctx-hd mono">添加设备</div>
            <div className="graph-kind-grid">
              {DEVICE_KINDS.map((dk) => (
                <button key={dk.key} type="button" className="graph-kind" onClick={() => addDevice(dk.key)} title={dk.label}>
                  <span className="graph-kind-ico"><DeviceIcon kind={dk.key} size={20} /></span>
                  <span>{dk.short}</span>
                </button>
              ))}
            </div>
            <div className="sep" />
            <button type="button" className="danger" onClick={clearManual}>清空手工图</button>
            {pinnedCount > 0 && <button type="button" onClick={resetPositions}>恢复自动排列（{pinnedCount} 台）</button>}
          </div>
        )}

        {/* ── 「连线到…」的目标选择器。用菜单而不是拖手柄：
            拖手柄要做拖拽中的命中测试与视觉反馈，是本轮风险最高的部分；
            菜单方案零几何计算，而且能连到远处看不见的节点。 ── */}
        {linkFrom && (
          <div className="graph-pop graph-pop-link" onClick={(e) => e.stopPropagation()}>
            <div className="ctx-hd mono">
              连线到… <span className="dim">{allTargets.find((t) => t.id === linkFrom)?.label || ''}</span>
            </div>
            <div className="graph-link-list">
              {allTargets.filter((t) => t.id !== linkFrom).map((t) => (
                <button key={t.id} type="button" onClick={() => addEdge(linkFrom, t.id)}>
                  {t.kind && <span className="graph-link-ico"><DeviceIcon kind={t.kind} size={15} /></span>}
                  <span className="graph-link-name">{t.label}</span>
                  <span className="dim">{t.sub}</span>
                </button>
              ))}
              {allTargets.length <= 1 && <div className="dim" style={{ padding: '6px 8px' }}>图上还没有别的节点</div>}
            </div>
            <div className="sep" />
            <button type="button" onClick={() => setLinkFrom(null)}>取消</button>
          </div>
        )}

        {menu && (
          <div className="ctx graph-ctx" style={menuStyle(menu)} onClick={(e) => e.stopPropagation()}>
            {menu.items.map((it, i) => (
              it.sep
                ? <div key={i} className="sep" />
                : <button key={i} type="button" className={it.danger ? 'danger' : ''} onClick={() => it.run?.()}>{it.label}</button>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

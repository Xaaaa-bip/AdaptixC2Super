/**
 * 拓扑图上「设备类型」的图标与词表。
 *
 * 14 种类型里有 11 种（交换机/路由器/防火墙/数据库/堡垒机/Web/域控/文件服务器/
 * 攻击机/VPN 入口/云主机…）在 agents 表里根本不存在 —— 那些设备上没有 agent，
 * 只能靠操作员手画。所以图标必须是**自带语义**的，不能依赖任何后端字段。
 *
 * 画法沿用 App.tsx:36-56 的 Icon：viewBox 0 0 24 24、stroke=currentColor、
 * strokeWidth 1.75、圆头圆角、fill=none。不引第三方图标库 ——
 * 本项目依赖只有 react / react-dom / zustand，为 14 个图标引一整个库不划算，
 * 而且库的线宽风格和现有 Icon 对不齐。
 *
 * ★ 颜色一律交给 CSS（stroke 是 currentColor）：已控=青、未控=灰这类语义
 *   在 .graph-dev-* 规则里定，不在这个文件里写死。
 */
import React from 'react'
import type { DeviceKind } from '../graphBoard'
import { DEVICE_KIND_KEYS } from '../graphBoard'

/** 24×24 网格上的线条。只画 <path>/<circle>/<rect>，不写外层 <svg>。 */
const ART: Record<DeviceKind, React.ReactNode> = {
  // 交换机：一个扁盒子，里面双向箭头（"把流量在口之间搬来搬去"）
  switch: (
    <>
      <rect x="3" y="7.5" width="18" height="9" rx="1.5" />
      <path d="M6.5 10.5h11m-2.5-2l2.5 2-2.5 2" />
      <path d="M17.5 13.5h-11m2.5 2l-2.5-2 2.5-2" />
    </>
  ),
  // 路由器：中心点 + 四向箭头
  router: (
    <>
      <circle cx="12" cy="12" r="3" />
      <path d="M12 9V4m-2 2l2-2 2 2" />
      <path d="M12 15v5m-2-2l2 2 2-2" />
      <path d="M9 12H4m2-2l-2 2 2 2" />
      <path d="M15 12h5m-2-2l2 2-2 2" />
    </>
  ),
  // 防火墙：错缝砖墙（顺缝就变成"网格"了，认不出）
  firewall: (
    <>
      <rect x="2.5" y="6" width="19" height="12" rx="1" />
      <path d="M2.5 10h19M2.5 14h19" />
      <path d="M8 6v4M16 6v4M5.5 10v4M12 10v4M18.5 10v4M8 14v4M16 14v4" />
    </>
  ),
  // 数据库：圆柱
  database: (
    <>
      <ellipse cx="12" cy="6.5" rx="7.5" ry="3" />
      <path d="M4.5 6.5v11c0 1.66 3.36 3 7.5 3s7.5-1.34 7.5-3v-11" />
      <path d="M4.5 12c0 1.66 3.36 3 7.5 3s7.5-1.34 7.5-3" />
    </>
  ),
  // 堡垒机：城垛塔楼（跳板机的"堡垒"本义，比盾牌更好认且不与域控撞）
  bastion: (
    <>
      <path d="M5.5 21V4.5H8.5V6.5H11V4.5H13V6.5H15.5V4.5H18.5V21" />
      <path d="M4 21h16" />
      <path d="M10 21v-4a2 2 0 0 1 4 0v4" />
    </>
  ),
  // Web 服务器：地球
  web: (
    <>
      <circle cx="12" cy="12" r="8.5" />
      <path d="M3.5 12h17" />
      <path d="M12 3.5c2.5 2.6 3.8 5.4 3.8 8.5s-1.3 5.9-3.8 8.5c-2.5-2.6-3.8-5.4-3.8-8.5S9.5 6.1 12 3.5z" />
    </>
  ),
  // 域控：盾牌 + 钥匙孔（"掌管钥匙的那台"）
  dc: (
    <>
      <path d="M12 3.2 5 6v6.2c0 4.3 3 7.6 7 8.6 4-1 7-4.3 7-8.6V6l-7-2.8z" />
      <circle cx="12" cy="10.5" r="2" />
      <path d="M12 12.5v3.5" />
    </>
  ),
  // 文件服务器：文件夹
  file: (
    <path d="M3.5 7.5a2 2 0 0 1 2-2h3.4l2 2.5h7.6a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2v-10.5z" />
  ),
  // 攻击机：准星
  attacker: (
    <>
      <circle cx="12" cy="12" r="7" />
      <circle cx="12" cy="12" r="2" />
      <path d="M12 2v4M12 18v4M2 12h4M18 12h4" />
    </>
  ),
  // VPN 入口：挂锁
  vpn: (
    <>
      <rect x="4.5" y="10.5" width="15" height="10" rx="2" />
      <path d="M8 10.5V7.8a4 4 0 0 1 8 0v2.7" />
      <circle cx="12" cy="15.5" r="1.3" />
      <path d="M12 16.8v2" />
    </>
  ),
  // 云主机
  cloud: (
    <path d="M7.2 19a4.2 4.2 0 0 1-.35-8.38 5.6 5.6 0 0 1 10.72-1.15A4.05 4.05 0 0 1 17.2 19H7.2z" />
  ),
  // 哑终端 / 工控：带命令提示符的终端屏
  ics: (
    <>
      <rect x="2.5" y="4.5" width="19" height="12.5" rx="2" />
      <path d="M12 17v3.5M8 20.5h8" />
      <path d="M6.5 9l2.5 2.2-2.5 2.2" />
      <path d="M11.5 13.4h5" />
    </>
  ),
  // 蜜罐：带盖的罐子
  honeypot: (
    <>
      <path d="M5.2 10h13.6l-1.3 8.9a2 2 0 0 1-2 1.1H8.5a2 2 0 0 1-2-1.1L5.2 10z" />
      <path d="M3.5 10h17" />
      <path d="M9 10V7.2a3 3 0 0 1 6 0V10" />
    </>
  ),
  // 未知设备
  unknown: (
    <>
      <circle cx="12" cy="12" r="8.5" />
      <path d="M9.6 9.4a2.5 2.5 0 0 1 4.85.85c0 1.7-2.45 2.05-2.45 3.6" />
      <path d="M12 17.2h.01" />
    </>
  ),
}

export function DeviceIcon({ kind, size = 18 }: { kind: DeviceKind; size?: number }) {
  return (
    <svg
      viewBox="0 0 24 24"
      width={size}
      height={size}
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      stroke="currentColor"
      strokeWidth={1.75}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
      focusable="false"
      style={{ display: 'block', color: 'inherit', flexShrink: 0 }}
    >
      {ART[kind]}
    </svg>
  )
}

export type DeviceKindMeta = {
  key: DeviceKind
  /** 菜单/图例里的全名 */
  label: string
  /** 卡片与列表里省地方用的短名 */
  short: string
}

const KIND_META: Record<DeviceKind, { label: string; short: string }> = {
  switch: { label: '交换机', short: '交换' },
  router: { label: '路由器', short: '路由' },
  firewall: { label: '防火墙', short: '防火墙' },
  database: { label: '数据库', short: '数据库' },
  bastion: { label: '堡垒机', short: '堡垒机' },
  web: { label: 'Web 服务器', short: 'Web' },
  dc: { label: '域控', short: '域控' },
  file: { label: '文件服务器', short: '文件' },
  attacker: { label: '攻击机', short: '攻击机' },
  vpn: { label: 'VPN 入口', short: 'VPN' },
  cloud: { label: '云主机', short: '云' },
  ics: { label: '哑终端 / 工控', short: '工控' },
  honeypot: { label: '蜜罐', short: '蜜罐' },
  unknown: { label: '未知设备', short: '未知' },
}

/**
 * 菜单与图例用的有序列表。顺序取自 DEVICE_KIND_KEYS。
 *
 * ★ 用 Record<DeviceKind, …> 而不是直接写数组：少写一种类型时，tsc 会在
 *   **编译期**报缺键。写成数组的话少一项是静默的 —— 菜单里就是少一个选项，
 *   没有人会去数菜单是不是 14 项，要等到有人想画防火墙时才发现。
 */
export const DEVICE_KINDS: DeviceKindMeta[] = DEVICE_KIND_KEYS.map((key) => ({
  key, label: KIND_META[key].label, short: KIND_META[key].short,
}))

/** kind → 元数据。查不到时退回「未知设备」，绝不返回 undefined 让调用方崩。 */
export function deviceMeta(kind: DeviceKind): DeviceKindMeta {
  return DEVICE_KINDS.find((d) => d.key === kind) || DEVICE_KINDS[DEVICE_KINDS.length - 1]
}

/**
 * agent 的「角色」词表 —— 给已有 beacon 的主机在拓扑图上打一个设备图标。
 *
 * ★ 存进 a_tags 的就是**人类可读的词本身**（"数据库"、"堡垒机"），
 *   不是 `role:database` 这类新语法。原因：Hosts.tsx 的 TaggedGroup 把 a_tags
 *   整串当左侧分组名显示 —— 写 `数据库` 那里就显示 `# 数据库`，写 `role:database`
 *   就显示 `# role:database`。同一个字段，两边都好看的那个写法才是对的那个。
 *
 * 认不出来的标签一律不画图标（只是没图标，标签本身照常在 Hosts 里工作）。
 */
const ROLE_WORDS: Array<[string, DeviceKind]> = [
  ['数据库', 'database'], ['数据库服务器', 'database'], ['db', 'database'],
  ['mysql', 'database'], ['oracle', 'database'], ['sqlserver', 'database'], ['mssql', 'database'],
  ['交换机', 'switch'], ['核心交换', 'switch'],
  ['路由器', 'router'], ['网关', 'router'],
  ['防火墙', 'firewall'], ['fw', 'firewall'],
  ['堡垒机', 'bastion'], ['跳板机', 'bastion'], ['跳板', 'bastion'],
  ['web', 'web'], ['web服务器', 'web'], ['网站', 'web'], ['web服务', 'web'],
  ['域控', 'dc'], ['域控制器', 'dc'], ['dc', 'dc'], ['ad', 'dc'],
  ['文件服务器', 'file'], ['文件', 'file'], ['共享', 'file'], ['nas', 'file'],
  ['攻击机', 'attacker'], ['攻击', 'attacker'], ['kali', 'attacker'], ['c2', 'attacker'],
  ['vpn', 'vpn'], ['vpn入口', 'vpn'], ['入口', 'vpn'],
  ['云主机', 'cloud'], ['云', 'cloud'], ['ecs', 'cloud'], ['aws', 'cloud'],
  ['工控', 'ics'], ['哑终端', 'ics'], ['plc', 'ics'], ['ot', 'ics'], ['scada', 'ics'],
  ['蜜罐', 'honeypot'], ['honeypot', 'honeypot'],
]

/**
 * 「设为角色」菜单里的候选项：给**已经有 beacon** 的主机选一个身份。
 *
 * 只列"一台机器可能是什么角色"。交换机/路由器/防火墙不在此列 ——
 * 它们本来就不该有 agent，那种设备走手工节点（见 DEVICE_KINDS）。
 * 「未知设备」同理：把一台真主机标成"未知"没有任何意义，只会占掉图标位。
 */
export const ROLE_CHOICES: Array<{ word: string; kind: DeviceKind }> = [
  { word: '数据库', kind: 'database' },
  { word: '堡垒机', kind: 'bastion' },
  { word: '域控', kind: 'dc' },
  { word: 'Web', kind: 'web' },
  { word: '文件服务器', kind: 'file' },
  { word: '攻击机', kind: 'attacker' },
  { word: 'VPN 入口', kind: 'vpn' },
  { word: '云主机', kind: 'cloud' },
  { word: '工控', kind: 'ics' },
  { word: '蜜罐', kind: 'honeypot' },
]

const norm = (s: string) => s.trim().toLowerCase().replace(/\s+/g, '')

/**
 * a_tags → 角色图标。取不到就返回 null（不是 'unknown' —— unknown 是一个
 * 真实的设备类型，画上去会让人以为这台机器被标成了"未知设备"）。
 */
// 渲染期每张卡片都要查一次。454 台主机 × 每次渲染重扫 50 条词表是白烧的 CPU，
// 而标签串的取值空间很小，缓存一下就没有成本了。
const roleCache = new Map<string, DeviceKind | null>()

export function roleToKind(tags: string | undefined | null): DeviceKind | null {
  const whole = norm(String(tags || ''))
  if (!whole) return null
  const hit = roleCache.get(whole)
  if (hit !== undefined) return hit
  const found = lookupRole(whole)
  roleCache.set(whole, found)
  return found
}

function lookupRole(whole: string): DeviceKind | null {
  for (const [word, kind] of ROLE_WORDS) {
    if (whole === word) return kind
  }
  // a_tags 里可能挂了好几个标签（逗号/分号/竖线分隔）。
  // 只做**整段相等**匹配，不做包含匹配："不是数据库" 不该被认成数据库。
  for (const token of whole.split(/[,;|/、]+/)) {
    if (!token) continue
    for (const [word, kind] of ROLE_WORDS) {
      if (token === word) return kind
    }
  }
  return null
}

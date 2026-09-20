/* GopherTCP（gopher_listener_tcp）配置的**纯逻辑**：默认值 + 校验归一 + circuit 地址派生。
 *
 * 为什么单独一个文件、且**零依赖**：
 *   这三件事既被 UI 调用，也被无浏览器的契约测试调用
 *   （scripts/gopher_ui_check.mjs 用 esbuild 把本文件转译成 ESM 后直接 import）。
 *   判据必须打在**同一份代码**上 —— 在测试里抄一份实现，抄出来的第二份迟早与第一份漂移，
 *   而漂移的表现恰好就是本项目反复归档的「UI 填了但没生效」静默走错族。
 *   所以本文件不得 import React / api client / store，否则测试离不开浏览器。
 *
 * 权威性声明：这里只是把**服务端**的规则提前到前端提示。
 *   服务端仍是唯一权威：validConfig → axValidateLibp2pAddrs（callback_addresses 必须是
 *   能解出 /p2p/<peerID> 的 multiaddr）、axLibp2pRelayInfos（relays 必须带 /p2p/<relayPeerID>
 *   且不得含 /p2p-circuit）、axFillLibp2pRelayAddrs（relays + 私钥 ⇒ 派生 l2p_addrs）。
 *   前端拦不住的（如 /p2p/ 里的 peer id 与私钥不符）由服务端明确报错，不会静默放过。
 */

/** libp2p 组的键 —— 与 ax_config.axs·ListenerUI 的 container.put 逐字对齐。 */
export const GOPHER_L2P_KEYS = [
  'l2p_protocol', 'l2p_privkey', 'l2p_relays', 'l2p_addrs', 'l2p_dial_timeout',
]

/** libp2p 组默认值。l2p_protocol 的默认值必须与 ax_config.axs 的 textline 默认一致。 */
export const GOPHER_L2P_DEFAULTS: Record<string, any> = {
  l2p_protocol: '/gopher/1.0.0',
  l2p_privkey: '',
  l2p_relays: '',
  l2p_addrs: '',
  l2p_dial_timeout: 0,
}

/**
 * ★★ 只被 **tcp** 传输消费的键 —— l2p 模式下渲染它们属于「UI 填了但没生效」族。
 * 逐条给得出出处（gopher_listener_tcp 交付树源码，行号为该版本）：
 *   · tcp_banner — pl_transport.go:384 `if Protocol != "libp2p" && len(TcpBanner) > 0`。
 *     ★ 不只是"不生效"：同处注释（审计 C-F9）写明 l2p 分支的 agent **既不设也不读**
 *       BannerSize，写出去的 banner 会被当成 4 字节长度头 ⇒ **帧错位**，
 *       错位解析出的"长度"直接落进 recvMsg 的分配路径。⇒ l2p 下必须隐藏，不是整齐问题。
 *   · ssl (mTLS)  — pl_transport.go:310 `if Protocol == "libp2p" { … } else if Ssl { tls.Listen(
 *     … RequireAndVerifyClientCert) }`；且 pl_main.go:193-199 里 `if Ssl { Protocol="mtls" }`
 *     紧接着被 `if Protocol == "libp2p" { Protocol="libp2p" }` 覆盖
 *     ⇒ 连"显示成 mtls"这条副作用都不成立。两条独立证据。
 * ★ 与之相对，timeout / encrypt_key / error_answer 在 l2p 下**照常消费**，不该隐藏：
 *   pl_transport.go:238,242 两条校验位于 250 行 `if Protocol == "libp2p" { return … }`
 *   短路**之前**（同处注释说明这是刻意的：让 l2p 分支整体短路而不必复制检查）；
 *   error_answer 的写出点在 724/759，属两协议共用的错误路径。
 */
export const GOPHER_TCP_ONLY_KEYS = ['tcp_banner', 'ssl']

/** 分隔符集合与后端 axSplitAddrs 一致（换行 / 回车 / 逗号 / 空格 / Tab）。 */
export function splitLines(raw: unknown): string[] {
  if (Array.isArray(raw)) return raw.map((x) => String(x).trim()).filter(Boolean)
  if (raw == null) return []
  return String(raw)
    .split(/[\n\r,\s\t]+/)
    .map((x) => x.trim())
    .filter(Boolean)
}

/* peer id 的形状：base58btc，12D3Koo… 这类。用宽松下限（>=20）避免把合法 id 判死。 */
const PEER_ID_RE = /\/p2p\/[1-9A-HJ-NP-Za-km-z]{20,}/

/**
 * 把 relay 地址组合成 **agent 该拨的** circuit 地址。
 * 与后端 axLibp2pCircuitAddrs 同规则：<relayAddr>/p2p-circuit/p2p/<自身 PeerID>
 * （末尾自身 peer id 必须有：relay 靠它知道转发给谁，agent 靠它做 Noise 身份校验）。
 * selfPeerId 为空时按后端语义返回空数组（后端返回 nil）。
 */
export function deriveCircuitAddrs(relays: unknown, selfPeerId: string): string[] {
  if (!selfPeerId) return []
  const out: string[] = []
  for (const a of splitLines(relays)) {
    const t = a.replace(/\/+$/, '')
    if (!t) continue
    out.push(`${t}/p2p-circuit/p2p/${selfPeerId}`)
  }
  return out
}

/* ---------------- 协议切换时 callback_addresses 的形状转换 ----------------
 *
 * 为什么需要：`callback_addresses` 是**唯一**一个形状随 protocol 变的字段
 * （tcp = host:port，libp2p = multiaddr），但表单里它只是一个 textarea。
 * 用户在 tcp 下填好、再把「传输协议」改成 libp2p，那串 host:port 会**原样留在**
 * 标签写着「回连地址（multiaddr）」的框里 —— 标签与值互相矛盾，而错误要等到
 * 点「创建」才由 normalizeGopherConfig 抛出（用户实测截图即此形态）。
 *
 * 修法**不是**清空（契约 B3f 已断言：切协议不丢已填的值，与 Qt 一致），而是
 * **能无损转的就转，转不了的原样保留并显式告知**。后半句是本项目的核心纪律：
 * 静默丢值正是反复归档的「UI 填了但没生效」族；而"猜"一个 /dns4 还是 /ip4
 * 之外的语义（比如把 /p2p/<id> 段悄悄砍掉）会造出一个**用户没写过**的地址。
 */

/** host:port ⇒ multiaddr。返回 null = 转不了（不猜）。 */
function hostPortToMultiaddr(a: string): string | null {
  const okPort = (p: string) => { const n = Number(p); return n >= 1 && n <= 65535 }
  const okV4 = (h: string) => h.split('.').every((o) => /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$/.test(o))
  let m = /^\[([0-9A-Fa-f:.]+)\]:(\d{1,5})$/.exec(a)          // [::1]:4444
  if (m && okPort(m[2])) return `/ip6/${m[1]}/tcp/${m[2]}`
  m = /^(\d{1,3}(?:\.\d{1,3}){3}):(\d{1,5})$/.exec(a)          // 1.2.3.4:4444
  if (m && okV4(m[1]) && okPort(m[2])) return `/ip4/${m[1]}/tcp/${m[2]}`
  m = /^([A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?):(\d{1,5})$/.exec(a)   // h.example.com:4444
  if (m && okPort(m[2])) {
    // ★ 形状是「四段全数字」却没过上面的 IPv4 分支 ⇒ 它不是合法 IPv4。**不能降级当域名**：
    //   那会凭空造出一个永远解析不了的 /dns4/999.1.1.1 —— 属于"猜"，而"猜"出来的地址
    //   用户没写过、服务端还会照收。判定转不了，原样保留交回 operator。
    if (/^\d+(?:\.\d+){3}$/.test(m[1])) return null
    return `/dns4/${m[1]}/tcp/${m[2]}`
  }
  return null
}

/**
 * multiaddr ⇒ host:port。**只接受能无损还原的形态**：
 * 恰为 `/ip4|ip6|dns4|dns6/<host>/tcp/<port>`，且**不含** /p2p 段。
 * 带 /p2p/<id>、/p2p-circuit、/unix/、/ws 等一律返回 null ⇒ 原样保留。
 * （服务端 axValidateLibp2pAddrs 会在校验前按私钥把 /p2p/<自身PeerID> 补上，
 *   所以存下来的值通常不带 /p2p；真带了说明是 operator 手写的，砍掉即丢信息。）
 */
function multiaddrToHostPort(a: string): string | null {
  const m = /^\/(ip4|ip6|dns4|dns6)\/([^/]+)\/tcp\/(\d{1,5})\/?$/.exec(a)
  if (!m) return null
  const [, fam, host, port] = m
  if (Number(port) < 1 || Number(port) > 65535) return null
  return fam === 'ip6' ? `[${host}]:${port}` : `${host}:${port}`
}

/**
 * 把 callback_addresses 转成 `toProto` 要求的形状。
 * 返回：value = 转换后的完整列表（转不了的**原样在内**，绝不丢）；
 *       converted = "旧 ⇒ 新" 的人类可读记录（用于 inline 提示）；
 *       kept = 转不了、被原样保留的项。
 */
export function convertCallbackAddrs(raw: unknown, toProto: string):
{ value: string[]; converted: string[]; kept: string[] } {
  const value: string[] = [], converted: string[] = [], kept: string[] = []
  for (const a of splitLines(raw)) {
    const n = toProto === 'libp2p' ? hostPortToMultiaddr(a) : multiaddrToHostPort(a)
    if (n === null) { kept.push(a); value.push(a) }
    else { converted.push(`${a} ⇒ ${n}`); value.push(n) }
  }
  return { value, converted, kept }
}

/** 从回调地址列表里取第一个能识别出的 tcp 端口（用于切协议时联动 port_bind）。 */
export function firstTcpPort(raw: unknown): number | null {
  for (const a of splitLines(raw)) {
    const m = /^\/(?:ip4|ip6|dns4|dns6)\/[^/]+\/tcp\/(\d{1,5})\/?$/.exec(a) ||
              /^(?:\[[0-9A-Fa-f:.]+\]|[^:]+):(\d{1,5})$/.exec(a)
    if (m) { const n = Number(m[1]); if (n >= 1 && n <= 65535) return n }
  }
  return null
}

/**
 * 校验并归一 GopherTCP 的 config（按传输协议分流）。成功返回**可直接提交**的 config。
 * 抛出 Error 时 message 面向 operator（含具体是哪一条有问题 + 怎么改）。
 */
export function normalizeGopherConfig(cfg: Record<string, any>): Record<string, any> {
  const out: Record<string, any> = { ...cfg }
  const proto = String(out.protocol || 'tcp')
  const cbs = splitLines(out.callback_addresses)
  if (!cbs.length) throw new Error('GopherTCP 请至少添加一个 Callback address（host:port）')

  if (proto === 'libp2p') {
    // callback_addresses：multiaddr。缺 /p2p/<PeerID> 时服务端按私钥自动补，所以这里只校验
    // 「是不是 multiaddr 形状」——要求 operator 手抄 52 字符 peer id 既易错又没必要。
    for (const a of cbs) {
      if (!a.startsWith('/')) {
        throw new Error(`libp2p 的回连地址必须是 multiaddr（以 / 开头）：${a}\n（host:port 是 tcp 的写法）`)
      }
    }
    const relays = splitLines(out.l2p_relays)
    for (const r of relays) {
      if (!r.startsWith('/')) {
        throw new Error(`l2p_relays 必须是 multiaddr（以 / 开头）：${r}`)
      }
      if (r.includes('/p2p-circuit')) {
        throw new Error(`l2p_relays 里不应含 /p2p-circuit（那段由 listener 自己补）：${r}`)
      }
      if (!PEER_ID_RE.test(r)) {
        throw new Error(`l2p_relays 缺 /p2p/<relayPeerID>：${r}\n（relay 的 peer id 就是预约的路由键，没有它预约不上）`)
      }
    }
    const addrs = splitLines(out.l2p_addrs)
    for (const a of addrs) {
      if (!a.startsWith('/')) {
        throw new Error(`l2p_addrs 必须是 multiaddr（以 / 开头）：${a}`)
      }
    }
    // Qt 侧 TextMulti 经 jsonMarshal 就是 string ⇒ 统一给 string（后端 StringOrArray 两种都收）。
    out.l2p_relays = relays.join('\n')
    out.l2p_addrs = addrs.join('\n')
  } else {
    // tcp 分支服务端走 net.SplitHostPort，multiaddr 在那一刻必然报 "cannot split host:port"。
    // 这是「选错协议」的典型症状（不是地址写错），所以在源头给出指向协议选择器的提示。
    for (const a of cbs) {
      if (a.startsWith('/')) {
        throw new Error(`当前传输协议是 tcp，而回连地址是 multiaddr：${a}\n（multiaddr 属于 libp2p：请把「传输协议」改成 libp2p）`)
      }
    }
  }
  out.protocol = proto
  out.callback_addresses = cbs.join('\n')
  return out
}

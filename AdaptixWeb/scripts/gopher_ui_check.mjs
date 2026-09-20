#!/usr/bin/env node
/* Phase5 批次16：AdaptixWeb 的 GopherTCP 表单**契约验收**（无浏览器）。
 *
 * 为什么需要它：
 *   1. Web 端与 Qt(ax_config.axs) 端是**两份**表单，字段却必须落在同一份
 *      TransportConfig 上。任意一边把键名写错，症状都是"填了但没生效"——
 *      既不报错也不上线，正是本项目反复归档的静默走错族。
 *   2. `npm run build` 绿只证明"能编译"，不证明"字段对得上、校验拦得住"。
 *
 * 判据（B1–B5）：
 *   B1 键位对齐：Web 的 GopherTCP 默认 config 键集合 == ax_config.axs 的 container.put 键集合
 *      （逐个列出差集；★ 这是两端漂移的唯一防线）
 *   B2 常量对齐：l2p_protocol 的默认值 == axs 里 textline 的默认值
 *   B3 校验行为：test 里**直接调用 UI 用的同一个函数**（src/lib/gopherListener.ts）
 *      - 合法 libp2p config 通过，且 relays/addrs 归一为字符串（后端 StringOrArray）
 *      - relays 缺 /p2p/<relayPeerID> ⇒ 拒
 *      - relays 含 /p2p-circuit ⇒ 拒
 *      - libp2p 模式下 callback_addresses 是 host:port ⇒ 拒
 *      - 空 callback_addresses ⇒ 拒
 *      - tcp 模式下 callback_addresses 是 multiaddr ⇒ 拒（选错协议的典型症状）
 *      - tcp 模式合法 host:port ⇒ 通过，且 l2p_* 键**原样保留**（与 Qt 一致：切协议不丢值）
 *   B4 派生地址：deriveCircuitAddrs 的输出 == 后端 axLibp2pCircuitAddrs 的拼接规则
 *   B5 开关语义（源码级）：libp2p 组在 UI 里必须由 protocol 联动禁用/启用
 *   ── 批次20 追加（修用户实测的 UI BUG）──
 *   B6 形状转换：convertCallbackAddrs 把 callback_addresses 在 host:port ↔ multiaddr 间转
 *      - 可无损转的四类（IPv4 / 域名 / IPv6 / 边界端口）必须转对
 *      - ★ 转不了的（带 /p2p/<id>、/p2p-circuit、/unix、非法 v4、端口越界、无端口）
 *        **必须原样保留** —— 不丢值、不猜。静默丢值正是本项目反复归档的静默走错族
 *      - 闭环：转换产物必须能过提交前校验（否则等于生成了服务端会拒的值）
 *      - 往返一致 + firstTcpPort 联动（切协议时同步 port_bind）
 *   B7 l2p 下**确凿失效**的字段不得渲染：tcp_banner（pl_transport.go:384 硬守卫，
 *      且 C-F9 注释写明 l2p 下写它会帧错位）与 ssl/mTLS（pl_transport.go:310 + pl_main.go:193-199
 *      两条独立证据）；同时**反向**判 timeout/encrypt_key/error_answer 未被误藏
 *      （它们的校验位于 l2p 短路之前 ⇒ 两协议共用）。
 *
 * 覆盖面（诚实声明）：**不**验证渲染像素/交互，也不启动浏览器；jsdom/e2e 属人工清单。
 *   验证的是"同一份校验代码的行为 + 两端字段契约"。
 *
 * 用法（在 AdaptixWeb/ 下）: node scripts/gopher_ui_check.mjs [ax_config.axs 路径]
 */
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import { fileURLToPath } from 'node:url'
import { createRequire } from 'node:module'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const WEB = path.join(__dirname, '..')
const AXS = process.argv[2] ||
  path.join(WEB, '..', 'AdaptixServer', 'extenders', 'gopher_listener_tcp', 'ax_config.axs')

const OK = '[ok  ]', BAD = '[BAD ]', INFO = '[info]'
const fails = []
function ck(cond, msg) {
  console.log(`${cond ? OK : BAD} ${msg}`)
  if (!cond) fails.push(msg)
  return !!cond
}

/* ---- 把被测 TS 源码转译成 ESM（用 vite 自带的 esbuild，不引入新依赖）---- */
const require_ = createRequire(import.meta.url)
let esbuild
try {
  esbuild = require_('esbuild')
} catch (e) {
  console.log(`${BAD} 找不到 esbuild（应为 vite 的依赖）: ${e.message}`)
  process.exit(1)
}
const LIB_TS = path.join(WEB, 'src', 'lib', 'gopherListener.ts')
const js = esbuild.transformSync(fs.readFileSync(LIB_TS, 'utf8'), {
  loader: 'ts', format: 'esm', target: 'node18',
}).code
const tmp = path.join(os.tmpdir(), `gopherListener.${process.pid}.mjs`)
fs.writeFileSync(tmp, js)
const L = await import(`file://${tmp.replace(/\\/g, '/')}`)
fs.unlinkSync(tmp)
const { GOPHER_L2P_DEFAULTS, GOPHER_L2P_KEYS, GOPHER_TCP_ONLY_KEYS, normalizeGopherConfig,
        deriveCircuitAddrs, splitLines, convertCallbackAddrs, firstTcpPort } = L

/* ---- 从两端源码里把「字段清单」抽出来（B1 的判据）---- */
const axsSrc = fs.readFileSync(AXS, 'utf8')
const axsKeys = [...axsSrc.matchAll(/container\.put\("([a-z0-9_]+)"/g)].map((m) => m[1])

const apiSrc = fs.readFileSync(path.join(WEB, 'src', 'api', 'listeners.ts'), 'utf8')
const gopherBlock = apiSrc.slice(apiSrc.indexOf("case 'GopherTCP':"))
  .split(/\n\s*case '/)[0]
const explicitKeys = [...gopherBlock.matchAll(/^\s{8}([a-z0-9_]+):/gm)].map((m) => m[1])
// l2p_* 通过 ...GOPHER_L2P_DEFAULTS 展开 ⇒ 对象字面量里看不到，按常量补回来
// （只补**尚未出现**的键：否则会把展开出来的重复再算一遍 ⇒ 假红）
const webKeys = [...explicitKeys]
if (/\.\.\.GOPHER_L2P_DEFAULTS/.test(gopherBlock)) {
  for (const k of GOPHER_L2P_KEYS) if (!webKeys.includes(k)) webKeys.push(k)
}

console.log(`${INFO} axs: ${AXS}`)
console.log(`${INFO} web: ${path.relative(WEB, path.join('src', 'api', 'listeners.ts'))} + src/lib/gopherListener.ts`)

/* ---- B1：键位对齐 ---- */
const a = new Set(axsKeys), b = new Set(webKeys)
const onlyAxs = [...a].filter((k) => !b.has(k))
const onlyWeb = [...b].filter((k) => !a.has(k))
ck(axsKeys.length === a.size, `B1a axs 无重复 container 键（${axsKeys.length} 个 put）`)
ck(explicitKeys.length === new Set(explicitKeys).size,
  `B1b Web 默认 config 无重复键（字面量 ${explicitKeys.length} 个；重复: ${
    JSON.stringify(explicitKeys.filter((k, i) => explicitKeys.indexOf(k) !== i))}）`)
ck(onlyAxs.length === 0 && onlyWeb.length === 0,
  `B1c 两端键集合一致（axs ${a.size} 键 / web ${b.size} 键；仅 axs 有: ${JSON.stringify(onlyAxs)}；仅 Web 有: ${JSON.stringify(onlyWeb)}）`)
ck(GOPHER_L2P_KEYS.every((k) => a.has(k) && b.has(k)),
  `B1d 5 个 libp2p 载荷键两端齐备（${GOPHER_L2P_KEYS.join(',')}）`)

/* ---- B2：常量对齐 ---- */
const m = axsSrc.match(/create_textline\("([^"]*)"\)\s*;?\s*\n?\s*[^\n]*L2pProto/) ||
  axsSrc.match(/textlineL2pProto\s*=\s*form\.create_textline\("([^"]*)"\)/)
const axsProtoDefault = m ? m[1] : null
ck(axsProtoDefault !== null && axsProtoDefault === GOPHER_L2P_DEFAULTS.l2p_protocol,
  `B2 l2p_protocol 默认值两端一致（axs="${axsProtoDefault}" web="${GOPHER_L2P_DEFAULTS.l2p_protocol}"）`)

/* ---- B3：校验行为（直接调用 UI 用的同一函数）---- */
const RELAY = '/ip4/203.0.113.7/tcp/4446/p2p/12D3KooWM5cNF3ozC3FwdmP7LYZeR823Lc4nEADPRkR2vHmqiYSF'
const SELF = '12D3KooWPdLJtJXabcdefghijklmnopqrstuvwxyz12345678'
const rejects = (cfg, why) => {
  try { normalizeGopherConfig(cfg); return { ok: false, err: '未拒（静默放过）' } }
  catch (e) { return { ok: true, msg: e.message.split('\n')[0] } }
}
const base = () => ({ protocol: 'libp2p', callback_addresses: ['/ip4/198.51.100.9/tcp/39017'], timeout: 10 })

// 合法 libp2p
{
  const out = normalizeGopherConfig({ ...base(), l2p_relays: RELAY, l2p_addrs: '' })
  ck(out.callback_addresses === '/ip4/198.51.100.9/tcp/39017',
    `B3a 合法 libp2p 通过，callback_addresses 归一为字符串`)
  ck(typeof out.l2p_relays === 'string' && out.l2p_relays === RELAY,
    `B3b l2p_relays 归一为字符串（后端 StringOrArray 两种都收，Qt 侧也是 string）`)
}
// 一行一条 / 逗号混排
{
  const out = normalizeGopherConfig({ ...base(), l2p_relays: `${RELAY}\n${RELAY}`, l2p_addrs: '' })
  ck(splitLines(out.l2p_relays).length === 2, `B3c 多行 relays 拆得出 2 条`)
}
// 各类拒绝
for (const [why, cfg] of [
  ['relays 缺 /p2p/<relayPeerID>', { ...base(), l2p_relays: '/ip4/203.0.113.7/tcp/4446' }],
  ['relays 含 /p2p-circuit', { ...base(), l2p_relays: `${RELAY}/p2p-circuit/p2p/${SELF}` }],
  ['libp2p 的 callback 是 host:port', { ...base(), callback_addresses: ['1.2.3.4:4444'] }],
  ['callback 为空', { ...base(), callback_addresses: [] }],
  ['l2p_addrs 不是 multiaddr', { ...base(), l2p_addrs: '1.2.3.4:4444' }],
  ['tcp 却填 multiaddr（选错协议）', { protocol: 'tcp', callback_addresses: ['/ip4/1.2.3.4/tcp/4444'], timeout: 10 }],
]) {
  const r = rejects(cfg, why)
  ck(r.ok, `B3d 拒: ${why}（${r.ok ? r.msg : '★ ' + r.err}）`)
}
// tcp 合法 + l2p 键保留
{
  const out = normalizeGopherConfig({
    protocol: 'tcp', callback_addresses: ['a.example.com:4444', '1.2.3.4:5555'], timeout: 10,
    ...GOPHER_L2P_DEFAULTS, l2p_relays: RELAY,
  })
  ck(out.callback_addresses === 'a.example.com:4444\n1.2.3.4:5555', `B3e tcp 合法 host:port 通过`)
  ck(out.l2p_relays === RELAY && out.l2p_protocol === GOPHER_L2P_DEFAULTS.l2p_protocol,
    `B3f tcp 模式下 l2p_* 原样保留（与 Qt 一致：切协议不丢已填的值）`)
}

/* ---- B4：派生地址 == 后端 axLibp2pCircuitAddrs 规则 ---- */
{
  const out = deriveCircuitAddrs(`${RELAY}\n${RELAY}/`, SELF)
  const want = `${RELAY}/p2p-circuit/p2p/${SELF}`
  ck(out.length === 2 && out[0] === want && out[1] === want,
    `B4a 派生地址 = <relay>/p2p-circuit/p2p/<自身PeerID>，且去尾斜杠（实测 ${out[0]}）`)
  ck(deriveCircuitAddrs(RELAY, '').length === 0, `B4b 自身 PeerID 未知 ⇒ 返回空（与后端返回 nil 同语义）`)
}

/* ---- B5：开关语义（源码级）---- */
const tsx = fs.readFileSync(path.join(WEB, 'src', 'components', 'Listeners.tsx'), 'utf8')
ck(/isL2p\s*=\s*proto === 'libp2p'/.test(tsx) && /isL2p && \(/.test(tsx),
  `B5a libp2p 组随 protocol 联动渲染/禁用（源码判据）`)
ck(/normalizeGopherConfig\(/.test(tsx),
  `B5b 提交路径调用的是与契约测试同一个 normalizeGopherConfig（不是另一份实现）`)

/* ---- B6：切协议时 callback_addresses 的形状转换（批次20）----
 * 用户实测：tcp 下填的 154.198.162.34:4444 在切到 libp2p 后**原样留在**标签写着
 * 「回连地址（multiaddr）」的框里，错误只在点「创建」时才暴露。
 * 判据分两半：① 能无损转的必须转；② **转不了的必须原样保留**（不丢值、不猜）。
 */
{
  const conv = (addrs, to) => convertCallbackAddrs(addrs, to)
  const one = (addrs, to) => conv(addrs, to).value

  // ① 可无损转换的四类 host:port
  for (const [inp, want, why] of [
    ['1.2.3.4:4444', '/ip4/1.2.3.4/tcp/4444', 'IPv4'],
    ['a.example.com:4444', '/dns4/a.example.com/tcp/4444', '域名'],
    ['[::1]:4444', '/ip6/::1/tcp/4444', 'IPv6 方括号'],
    ['sub.host-1.example.co:65535', '/dns4/sub.host-1.example.co/tcp/65535', '带连字符的域名+边界端口'],
  ]) {
    const got = one([inp], 'libp2p')
    ck(got.length === 1 && got[0] === want, `B6a tcp→l2p 转换 ${why}：${inp} ⇒ ${got[0]}`)
  }
  for (const [inp, want, why] of [
    ['/ip4/198.51.100.9/tcp/39017', '198.51.100.9:39017', 'ip4'],
    ['/dns4/x.example.com/tcp/4444', 'x.example.com:4444', 'dns4'],
    ['/ip6/::1/tcp/4444', '[::1]:4444', 'ip6'],
  ]) {
    const got = one([inp], 'tcp')
    ck(got.length === 1 && got[0] === want, `B6b l2p→tcp 转换 ${why}：${inp} ⇒ ${got[0]}`)
  }

  // ★② 转不了的必须**原样保留**（既不丢、也不猜）
  for (const [inp, why] of [
    [`/ip4/198.51.100.9/tcp/39017/p2p/${SELF}`, '带 /p2p/<id>（砍掉即丢 operator 手写的信息）'],
    [`/ip4/203.0.113.7/tcp/4446/p2p-circuit/p2p/${SELF}`, '含 /p2p-circuit'],
    ['/unix/var/run/x.sock', '/unix 形态'],
    ['999.1.1.1:4444', '非法 IPv4（不能生成服务端会拒的地址）'],
    ['1.2.3.4:99999', '端口越界'],
    ['1.2.3.4', '没有端口'],
  ]) {
    const r = conv([inp], inp.startsWith('/') ? 'tcp' : 'libp2p')
    ck(r.kept.length === 1 && r.value.length === 1 && r.value[0] === inp,
      `B6c 转不了则原样保留（不丢值）：${why}`)
  }

  // ★③ 闭环：转换产物必须能过**服务端同一套规则**（前端 pre-check 是 normalizeGopherConfig）
  {
    const converted = one(['1.2.3.4:4444', 'a.example.com:5555'], 'libp2p')
    let ok = true, msg = ''
    try { normalizeGopherConfig({ protocol: 'libp2p', callback_addresses: converted, timeout: 10 }) }
    catch (e) { ok = false; msg = e.message.split('\n')[0] }
    ck(ok, `B6d 转换产物能过提交前校验（否则等于生成了服务端会拒的值）${ok ? '' : '★ ' + msg}`)
  }

  // ④ 往返一致 + 空输入 + 端口联动
  {
    const back = one(one(['1.2.3.4:4444'], 'libp2p'), 'tcp')
    ck(back.length === 1 && back[0] === '1.2.3.4:4444', `B6e 往返一致：tcp→l2p→tcp 回到原值（${back[0]}）`)
    ck(conv([], 'libp2p').value.length === 0, `B6f 空输入 ⇒ 空（不抛）`)
    ck(firstTcpPort(['1.2.3.4:4444']) === 4444 && firstTcpPort(['/ip4/1.2.3.4/tcp/4444']) === 4444,
      `B6g firstTcpPort 两种形状都识别（用于切协议时联动 port_bind）`)
    ck(firstTcpPort(['a.example.com']) === null, `B6h 无端口 ⇒ null（不瞎填）`)
  }
}

/* ---- B7：l2p 下**确凿失效**的字段不得渲染（批次20）----
 * 证据（gopher_listener_tcp 源码）：tcp_banner — pl_transport.go:384 `Protocol != "libp2p"` 硬守卫，
 *   且同处注释（审计 C-F9）写明 l2p 下写 banner 会让 agent 帧错位；
 *   ssl(mTLS) — pl_transport.go:310 `if l2p {} else if Ssl {…}` 且 pl_main.go:193-199 里
 *   `if Ssl { Protocol="mtls" }` 被 l2p 覆盖 ⇒ 两条独立证据。
 * 而 timeout / encrypt_key / error_answer 在 l2p 下照常消费（校验位于 l2p 短路之前）
 *   ⇒ 不该隐藏，只该把标签写清楚。判据覆盖这两侧。
 */
{
  ck(Array.isArray(GOPHER_TCP_ONLY_KEYS) && GOPHER_TCP_ONLY_KEYS.length === 2 &&
     GOPHER_TCP_ONLY_KEYS.includes('tcp_banner') && GOPHER_TCP_ONLY_KEYS.includes('ssl'),
    `B7a 仅 tcp 消费的键被具名（${GOPHER_TCP_ONLY_KEYS.join(',')}）`)

  // ★ 必须**按分支切片**再找 setCfg：'ssl' / 'timeout' 等键在 BeaconHTTP、KharonHTTP
  //   分支里也有同名的 setCfg 调用，对整文件 indexOf 会命中别的分支 ⇒ 假红。
  const gpBlock = tsx.slice(tsx.indexOf("if (t === 'GopherTCP')"), tsx.indexOf("if (t === 'BeaconDiscord')"))
  ck(gpBlock.length > 500, `B7b' GopherTCP 分支切片非空（${gpBlock.length} 字符；防"切片失败后判据空转"）`)
  const guardIdx = gpBlock.indexOf('{!isL2p && (')
  ck(guardIdx > 0, `B7b 存在 l2p 反向守卫 {!isL2p && (（GopherTCP 分支内，源码判据）`)
  for (const k of GOPHER_TCP_ONLY_KEYS) {
    const setIdx = gpBlock.indexOf(`setCfg('${k}'`)
    ck(setIdx > guardIdx, `B7c ${k} 的输入控件位于 l2p 守卫之后（l2p 下不渲染；实测偏移 ${setIdx} > ${guardIdx}）`)
  }
  ck(gpBlock.includes('TCP 横幅（仅 tcp）') && gpBlock.includes('mTLS（仅 tcp）'),
    `B7d 两个仅-tcp 字段带明确归属标签`)
  // 反向：共享字段必须**没有**被误藏
  for (const [k, label] of [['timeout', '握手超时'], ['encrypt_key', '加密密钥'], ['error_answer', '错误应答']]) {
    ck(gpBlock.includes(`setCfg('${k}'`) && gpBlock.includes(label),
      `B7e 共享字段 ${k} 仍渲染且标签写明共用（未误藏）`)
  }
  // 防回退：旧的无归属标签不得再出现
  ck(!/label>TCP 横幅</.test(tsx) && !/<label>mTLS<\/label>/.test(tsx) && !/<label>超时（秒）<\/label>/.test(tsx),
    `B7f 旧的无归属标签（TCP 横幅 / mTLS / 超时（秒））已不存在`)

  // A1 的直接判据：协议选择器必须走转换，而不是裸 setCfg('protocol')
  ck(/setGopherProtocol\(/.test(tsx) && !/onChange=\{\(e\) => setCfg\('protocol'/.test(tsx),
    `B7g 「传输协议」的 onChange 走 setGopherProtocol（不再裸赋值 ⇒ 形状随协议转）`)
  ck(/convertCallbackAddrs\(/.test(tsx), `B7h 转换调用的是契约测试覆盖的同一份 convertCallbackAddrs`)
}

console.log(`\nRESULT: ${fails.length ? `${fails.length} 项失败: ${JSON.stringify(fails)}` : '全部判据绿（B1–B7）'}`)
process.exit(fails.length ? 1 : 0)

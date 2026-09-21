import { useMemo, useState } from 'react'
import { useStore } from '../store'
import { LISTENER_TYPES, defaultListenerConfig, randomEncryptKey } from '../api/listeners'
import { applyTrafficTemplate, templatesFor } from '../api/trafficTemplates'
import { convertCallbackAddrs, deriveCircuitAddrs, firstTcpPort, normalizeGopherConfig, splitLines } from '../lib/gopherListener'
import { pickCallbackHost, dialableInterfaces } from '../lib/callbackHost'
import { fileToB64 } from '../utils'
import { EmptyState } from './ui'
import { PageTips } from './PageTips'

type FormState = {
  name: string
  type: string
  config: Record<string, any>
}

type TabId = 'main' | 'headers' | 'error' | 'payload'

/**
 * 监听器是否在运行。
 *
 * 这个判定不是新发明的 —— 它和状态列徽章用的是同一条
 * （`String(l.l_status).toLowerCase().includes('listen')`，
 * 本文件 744 行 / Generate.tsx:280 / WebDelivery.tsx:364 三处都在用），
 * 已在真实数据上验证过：实测 l_status 为 "Listen"。
 *
 * 之所以要把它提出来，是因为操作列的「暂停」和「恢复」原本是无条件渲染的：
 * 状态是 Listen 时两个按钮都亮着、都能点，「恢复一个正在运行的监听器」
 * 会做什么从界面上读不出来。既然代码库对"在不在跑"的判断本来就是二值的
 * （只有 listen / 非 listen 两种徽章），按钮也照这个二值来启停。
 */
const isRunning = (l: { l_status?: string }) => String(l.l_status).toLowerCase().includes('listen')

function linesToArr(s: string) {
  return s.split(/\r?\n|,/).map((x) => x.trim()).filter(Boolean)
}

/**
 * 「回连地址」下面那行灰色提示：把服务端自报的网卡地址摆出来。
 *
 * 缺省值是按运行期事实推的（见 lib/callbackHost.ts），但它只可能是**一个猜测** ——
 * 目标机该从哪个地址回连，只有操作员知道。Qt 侧把这栏做成绑定地址的下拉就是这个道理，
 * 而在 Web 这边把候选摆出来，缺省值才不是「看着对其实是错的」那种陷阱。
 * 服务端还没报（同步未到）时整条不渲染，不占位。
 */
function ServerAddrHint() {
  const ifaces = useStore((s) => s.serverInterfaces)
  // 用 dialableInterfaces 而不是原始列表：原始列表**恒定**含着通配地址 "0.0.0.0"，
  // 摆给操作员看等于推荐一个拨不通的地址（见 lib/callbackHost.ts 顶部）。
  const list = dialableInterfaces(ifaces)
  if (!list.length) return null
  return (
    <div className="hint muted-hint">
      回连地址要填<b>目标机能拨到</b>的服务端地址（缺省是按你访问控制台时用的地址推的）。
      服务端自报的其他地址：
      <div className="mono">{list.join('  ')}</div>
    </div>
  )
}

function arrToLines(v: any): string {
  if (Array.isArray(v)) return v.join('\n')
  if (typeof v === 'string') return v
  return ''
}

function suggestName(type: string) {
  const map: Record<string, string> = {
    BeaconHTTP: 'http',
    BeaconHTTPS: 'https',
    BeaconWS: 'ws',
    BeaconTCP: 'tcp',
    BeaconSMB: 'smb',
    BeaconDNS: 'dns',
    BeaconDiscord: 'discord',
    LinuxTCP: 'linux_tcp',
    GopherTCP: 'gopher',
    KharonHTTP: 'kharon',
  }
  return map[type] || type.toLowerCase()
}

/** 可编辑列表（对齐 ax form.create_list + setButtonsEnabled） */
function EditableList({
  value,
  onChange,
  placeholder,
  disabled,
}: {
  value: string[]
  onChange: (v: string[]) => void
  placeholder?: string
  disabled?: boolean
}) {
  const [draft, setDraft] = useState('')
  return (
    <div className="elist">
      <div className="elist-items">
        {value.length === 0 && <div className="dim empty-sm" style={{ padding: 8 }}>空列表</div>}
        {value.map((item, i) => (
          <div key={`${item}-${i}`} className="elist-row">
            <span className="mono">{item}</span>
            {!disabled && (
              <button type="button" className="ghost ico" title="删除" onClick={() => onChange(value.filter((_, j) => j !== i))}>×</button>
            )}
          </div>
        ))}
      </div>
      {!disabled && (
        <div className="elist-add">
          <input
            value={draft}
            placeholder={placeholder || '添加一项后回车'}
            onChange={(e) => setDraft(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') {
                e.preventDefault()
                const t = draft.trim()
                if (!t) return
                onChange([...value, t])
                setDraft('')
              }
            }}
          />
          <button type="button" onClick={() => {
            const t = draft.trim()
            if (!t) return
            onChange([...value, t])
            setDraft('')
          }}>添加</button>
        </div>
      )}
    </div>
  )
}

export function Listeners() {
  const listeners = useStore((s) => s.listeners)
  const refreshRest = useStore((s) => s.refreshRest)
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  // 回连地址的缺省主机名：按运行期事实推（服务端自报的网卡地址 + 操作员当前访问控制台的主机名），
  // 不写死。规则与理由见 lib/callbackHost.ts。
  const serverInterfaces = useStore((s) => s.serverInterfaces)
  const cbHost = useMemo(() => pickCallbackHost(serverInterfaces, window.location.hostname), [serverInterfaces])
  const list = useMemo(() => Object.values(listeners), [listeners])

  const [show, setShow] = useState(false)
  const [editMode, setEditMode] = useState(false)
  const [busy, setBusy] = useState(false)
  const [tab, setTab] = useState<TabId>('main')
  const [q, setQ] = useState('')
  // 编辑既有 listener 时记住服务端报的 AgentAddr —— libp2p 模式下它含本 listener 的 /p2p/<PeerID>，
  // 用来在前端把「relays ⇒ agent 实际拨的 circuit 地址」直接显示出来（不必等生成 agent 才知道）。
  const [editAgentAddr, setEditAgentAddr] = useState('')
  // 切传输协议时 callback_addresses 的形状会变（tcp=host:port / libp2p=multiaddr）。
  // 转换结果与"转不了的项"都在这里，用于在字段下方**显式**告知，而不是静默丢值。
  const [protoNote, setProtoNote] = useState<{ converted: string[]; kept: string[] } | null>(null)
  // KharonHTTP 的 profile：`uploaded_file` 存的是**内容**（上传的文件 → base64）或路径。
  // 这两个只服务于界面显示 —— 选了哪个文件、读失败的原因 —— 不进配置。
  const [profileName, setProfileName] = useState('')
  const [profileErr, setProfileErr] = useState('')
  const [form, setForm] = useState<FormState>(() => ({
    name: 'http',
    type: 'BeaconHTTP',
    config: defaultListenerConfig('BeaconHTTP', cbHost),
  }))
  // 当前选中的流量模板 id（只影响这一栏的高亮，不进配置本身）。
  // 编辑既有监听器时无从反推它当初套的是哪套 —— 一律回到 'official'，
  // 硬猜一个会让操作员以为「这套是模板 X」而实际不是。
  const [tplId, setTplId] = useState('official')


  const filtered = useMemo(() => {
    if (!q.trim()) return list
    const s = q.trim().toLowerCase()
    return list.filter((l) =>
      [l.l_name, l.l_reg_name, l.l_protocol, l.l_bind_host, l.l_bind_port, l.l_status, l.l_agent_addr]
        .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
  }, [list, q])

  const typeDef = LISTENER_TYPES.find((t) => t.type === form.type)
  // 这三者的表单是同一套（主要设置 / HTTP 头 / 错误页 / 页面模板 四个标签页）。
  // ★ 漏掉 BeaconHTTPS 的后果不是"少几个字段"，而是整个 HTTP 表单**不渲染** —— 只剩
  // 名称和类型两个框。
  const isHttpLike = form.type === 'BeaconHTTP' || form.type === 'BeaconWS' || form.type === 'BeaconHTTPS'

  function setType(type: string) {
    setForm({
      name: suggestName(type),
      type,
      config: defaultListenerConfig(type, cbHost),
    })
    setTplId('official')
    setEditAgentAddr('')
    setProtoNote(null)
    setProfileName('')
    setProfileErr('')
    setTab('main')
  }

  function setCfg(key: string, value: any) {
    setForm((f) => ({ ...f, config: { ...f.config, [key]: value } }))
  }

  /**
   * 套用一套流量模板。
   *
   * 模板只覆盖**传输外观**那八个键（方法 / URI / UA / 心跳头 / 请求头 / 响应头 /
   * 错误页 / 页面模板），绑定地址、回连地址、加密密钥、SSL 一律不动 —— 覆盖它们
   * 会把操作员刚填的回连地址冲掉，那是不可逆的静默丢失。
   *
   * protocol 由 applyTrafficTemplate 按监听器类型写死（http / ws / https）：
   * ★ https 不能落成 http —— linux_agent/pl_main.go 从 l_data 读 protocol，
   *   只有 `protocol == "https"` 才置 isHttpsEmu，写错会让 agent 退回 WebSocket
   *   升级模式，而 https_emu 只注册 POST/GET、永不回 101（见 trafficTemplates.ts 注释）。
   */
  function applyTpl(id: string) {
    const tpl = templatesFor(form.type).find((x) => x.id === id)
    if (!tpl) return
    setTplId(id)
    setForm((f) => ({ ...f, config: applyTrafficTemplate(f.config, f.type, tpl) }))
  }

  /**
   * 切「传输协议」。★ 不能像普通字段那样只赋值：callback_addresses 是唯一形状随协议
   * 变的字段，tcp 下填的 host:port 直接留在标签写着「multiaddr」的框里，用户实测
   * 截图正是这个形态（154.198.162.34:4444 出现在 multiaddr 栏）。错误只在点「创建」
   * 时才由 normalizeGopherConfig 抛出。
   * 这里做**无损转换**；转不了的（/p2p-circuit、带 /p2p/<id>、/unix 等）原样保留并提示 ——
   * 契约 B3f 已断言"切协议不丢已填的值"，静默丢值正是本项目反复归档的静默走错族。
   */
  function setGopherProtocol(next: string) {
    if (String(form.config.protocol || 'tcp') === next) return
    const { value, converted, kept } = convertCallbackAddrs(form.config.callback_addresses, next)
    setProtoNote(converted.length || kept.length ? { converted, kept } : null)
    const cfg: Record<string, any> = { ...form.config, protocol: next, callback_addresses: value }
    // 切到 libp2p 时联动 port_bind：服务端在 port_bind=0 时才从 multiaddr 反推
    // （pl_transport.go:250 → axValidateLibp2pAddrs），这里既然已经知道端口就直接填上。
    if (next === 'libp2p') {
      const p = firstTcpPort(value)
      if (p !== null) cfg.port_bind = p
    }
    setForm({ ...form, config: cfg })
  }

  function openCreate(type = 'BeaconHTTP') {
    setEditMode(false)
    setForm({ name: suggestName(type), type, config: defaultListenerConfig(type, cbHost) })
    setTplId('official')
    setEditAgentAddr('')
    setProtoNote(null)
    setProfileName('')
    setProfileErr('')
    setTab('main')
    setShow(true)
  }

  function openEdit(l: any) {
    const type = l.l_reg_name || 'BeaconHTTP'
    let cfg: Record<string, any> = {}
    try {
      if (l.l_data) cfg = typeof l.l_data === 'string' ? JSON.parse(l.l_data) : (l.l_data || {})
    } catch { cfg = {} }
    // merge with defaults so missing tabs still have fields
    const base = defaultListenerConfig(type, cbHost)
    setEditMode(true)
    setForm({
      name: l.l_name,
      type,
      config: { ...base, ...cfg },
    })
    setTplId('official')
    setEditAgentAddr(String(l.l_agent_addr || ''))
    setProtoNote(null)
    setProfileName('')
    setProfileErr('')
    setTab('main')
    setShow(true)
  }

  function renderMain() {
    const c = form.config
    const t = form.type

    // ★ 三个类型共用这一套表单（主要设置 / HTTP 头 / 错误页 / 页面模板），BeaconHTTPS
    //   必须在这里。本函数是一串**独立的 `if … return`**：谁都不命中就落到底返回
    //   undefined，而 `{tab === 'main' && renderMain()}` 的 undefined 渲染成空白 ——
    //   现象是「标签栏在、主面板整个空」，不是「少几个字段」，很容易误判成后端坏了。
    //   同族的第四个写死类型清单（前三个见 doc §6.20）。列类型别再手抄枚举。
    if (t === 'BeaconHTTP' || t === 'BeaconWS' || t === 'BeaconHTTPS') {
      return (
        <>
          <div className="field"><label>绑定地址与端口</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} placeholder="0.0.0.0" />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 443}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
          </div>
          <div className="field field-top"><label>回连地址</label>
            <EditableList
              value={Array.isArray(c.callback_addresses) ? c.callback_addresses : linesToArr(String(c.callback_addresses || ''))}
              onChange={(v) => setCfg('callback_addresses', v)}
              placeholder="host:port"
            />
            <ServerAddrHint />
          </div>
          {/* ── 流量模板 ────────────────────────────────────────────────
              一键把**传输外观**的八个键换成某类真实业务的形态（方法 / URI / UA /
              心跳头 / 请求头 / 响应头 / 错误页 / 页面模板），绑定地址、回连地址、
              加密密钥、SSL 一律不碰 —— 覆盖它们会把刚填好的回连地址冲掉。
              选中态只表示「最近套用的是哪套」，之后手动改字段不会取消选中，
              所以别把它当"当前配置等于该模板"的证据。 */}
          {templatesFor(t).length > 0 && (
            <div className="field field-top"><label>流量模板</label>
              <div className="chip-row traffic-tpls">
                {templatesFor(t).map((tpl) => (
                  <button
                    key={tpl.id}
                    type="button"
                    className={tplId === tpl.id ? 'primary' : ''}
                    title={tpl.hint}
                    onClick={() => applyTpl(tpl.id)}
                  >
                    {tpl.label}
                  </button>
                ))}
              </div>
              <div className="field-hint">
                {templatesFor(t).find((x) => x.id === tplId)?.hint || ''}
                {' · '}覆盖 方法/URI/UA/心跳头/请求头/响应头/诱饵页 八个键，其余字段不动；
                套用后手动改字段不会取消选中
              </div>
            </div>
          )}
          <div className="field"><label>请求方法</label>
            {/* BeaconHTTPS 只给 POST，对齐它自己的 ax_config.axs:24 `addItems(["POST"])`。
                注意这个键在 https_emu 里是**惰性**的：pl_transport.go:185-186 无条件把
                POST 和 GET 都注册（GET 是给探针回同一张诱饵页），而 `Config.HttpMethod`
                全文件只在 pl_main.go:157 被赋值、**没有任何读者**。放两个选项会让操作员
                以为改它有用 —— 而 linux agent 在 https 仿真模式下永远发 POST。 */}
            <select value={c.http_method || (t === 'BeaconWS' ? 'GET' : 'POST')} onChange={(e) => setCfg('http_method', e.target.value)}
              disabled={t === 'BeaconWS' || t === 'BeaconHTTPS'}>
              {t === 'BeaconWS' ? <option>GET</option>
                : t === 'BeaconHTTPS' ? <option>POST</option>
                : (<><option>POST</option><option>GET</option></>)}
            </select>
          </div>
          <div className="field field-top"><label>URI 路径</label>
            <EditableList
              value={Array.isArray(c.uri) ? c.uri : linesToArr(String(c.uri || ''))}
              onChange={(v) => setCfg('uri', v)}
              placeholder="/path"
            />
          </div>
          <div className="field field-top"><label>User-Agent</label>
            <EditableList
              value={Array.isArray(c.user_agent) ? c.user_agent : linesToArr(String(c.user_agent || ''))}
              onChange={(v) => setCfg('user_agent', v)}
              placeholder="Mozilla/5.0 ..."
            />
          </div>
          <div className="field"><label>心跳头</label>
            <input value={c.hb_header || ''} onChange={(e) => setCfg('hb_header', e.target.value)} />
          </div>
          <div className="field"><label>加密密钥</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>生成</button>
            </div>
          </div>
          <div className="field field-top"><label>使用 SSL (HTTPS)</label>
            <div className="ssl-box">
              <label className="check-inline" style={{ color: 'var(--text)' }}>
                <input type="checkbox" checked={!!c.ssl} onChange={(e) => setCfg('ssl', e.target.checked)} /> 启用 SSL/TLS
              </label>
              {!!c.ssl && (
                <div className="dim" style={{ fontSize: 11.5, marginTop: 6 }}>
                  Web 端暂不上传证书文件；留空将由服务端按配置自动生成/使用默认证书（对齐 AXS optional cert/key）。
                </div>
              )}
            </div>
          </div>
        </>
      )
    }

    if (t === 'BeaconTCP') {
      return (
        <>
          <div className="field"><label>绑定端口</label>
            <input type="number" value={c.port_bind || 9000} onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
          </div>
          <div className="field"><label>前置数据</label>
            <input className="mono" value={c.prepend_data || ''} onChange={(e) => setCfg('prepend_data', e.target.value)} />
          </div>
          <div className="field"><label>加密密钥</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>生成</button>
            </div>
          </div>
        </>
      )
    }

    if (t === 'BeaconSMB') {
      return (
        <>
          <div className="field"><label>管道名 (C2)</label>
            <input value={c.pipename || ''} onChange={(e) => setCfg('pipename', e.target.value)} placeholder="mypipe" />
          </div>
          <div className="field"><label>加密密钥</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>生成</button>
            </div>
          </div>
        </>
      )
    }

    if (t === 'BeaconDNS') {
      return (
        <>
          <div className="field"><label>绑定地址与端口</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 53}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
          </div>
          <div className="field"><label>权威域名</label>
            <input value={c.domain || ''} onChange={(e) => setCfg('domain', e.target.value)} placeholder="ns1.c2.com,ns2.backup.com" />
          </div>
          <div className="field"><label>最大载荷（字节）</label>
            <input type="number" value={c.pkt_size || 4096} onChange={(e) => setCfg('pkt_size', Number(e.target.value))} />
          </div>
          <div className="field"><label>DNS TTL（秒）</label>
            <input type="number" value={c.ttl || 5} onChange={(e) => setCfg('ttl', Number(e.target.value))} />
          </div>
          <div className="field"><label>加密密钥</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>生成</button>
            </div>
          </div>
          <div className="field"><label>突发模式</label>
            <label className="check-inline" style={{ color: 'var(--text)' }}>
              <input type="checkbox" checked={!!c.burst_enabled} onChange={(e) => setCfg('burst_enabled', e.target.checked)} /> 启用突发模式
            </label>
          </div>
          {!!c.burst_enabled && (
            <>
              <div className="field"><label>突发间隔（毫秒）</label>
                <input type="number" value={c.burst_sleep ?? 50} onChange={(e) => setCfg('burst_sleep', Number(e.target.value))} />
              </div>
              <div className="field"><label>突发抖动（%）</label>
                <input type="number" value={c.burst_jitter ?? 0} onChange={(e) => setCfg('burst_jitter', Number(e.target.value))} />
              </div>
            </>
          )}
        </>
      )
    }

    if (t === 'LinuxTCP') {
      // linux_listener_tcp: Bind port + Encryption key + optional reverse-pivot callback list.
      // callback_addresses 留空 => 原行为（生成的 agent 监听端口，需要入站可达）。
      // 填了 => 反向 pivot：生成的 agent 主动 dial 这些地址、自身零监听，只要出站可达。
      // 消费方：linux_agent/pl_main.go 的 "bind_tcp" 分支（Addresses = bindAddrs）。
      const revAddrs = arrToLines(c.callback_addresses)
      return (
        <>
          <div className="field"><label>绑定端口</label>
            <input type="number" value={c.port_bind || 4444} onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
          </div>
          <div className="field"><label>加密密钥</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>生成</button>
            </div>
          </div>
          <div className="field field-top"><label>回连地址（反向 pivot）</label>
            <textarea rows={3} value={revAddrs}
              onChange={(e) => setCfg('callback_addresses', linesToArr(e.target.value))}
              placeholder={'留空 = 正向（agent 监听端口）\n填入 = 反向（agent 主动拨号）\n例：10.0.0.5:4444'} />
          </div>
          {revAddrs.trim() !== ''
            ? <div className="hint muted-hint">反向 pivot：生成的子 agent <b>主动 dial</b> 上述地址，自身<b>不监听任何端口</b>，只要求出站可达。父 agent 需先执行 <span className="mono">listen &lt;绑定端口&gt;</span>。</div>
            : <div className="hint muted-hint">正向：生成的 agent 监听 <span className="mono">{c.port_bind || 4444}</span>，要求目标可入站。填「回连地址」即切换为反向 pivot。</div>}
        </>
      )
    }

    if (t === 'GopherTCP') {
      const proto = String(c.protocol || 'tcp')
      const isL2p = proto === 'libp2p'
      // 派生显示：l2p_relays + 本 listener 的 peer id ⇒ agent 实际拨的 circuit 地址。
      // 与后端 axFillLibp2pRelayAddrs 同规则（显式 l2p_addrs 优先，否则由 relays 派生）。
      // 派生函数与后端 axLibp2pCircuitAddrs 同实现（src/lib/gopherListener.ts，被契约测试覆盖）。
      const selfId = String(editAgentAddr || '').split('/p2p/')[1] || ''
      const derived = isL2p && !String(c.l2p_addrs || '').trim()
        ? deriveCircuitAddrs(c.l2p_relays, selfId || '<本 listener PeerID（创建后由服务端按私钥生成）>')
        : []
      return (
        <>
          <div className="field field-top"><label>传输协议</label>
            <select value={proto} onChange={(e) => setGopherProtocol(e.target.value)}>
              <option value="tcp">tcp — 直连，agent 拨回连地址</option>
              <option value="libp2p">libp2p — circuit 中继，C2 真实 IP 不进载荷</option>
            </select>
            {protoNote && (
              <div className="hint muted-hint">
                {protoNote.converted.length > 0 && (
                  <>已按新协议改写 {protoNote.converted.length} 条回连地址：{protoNote.converted.map((d, i) => <div key={i} className="mono">{d}</div>)}</>
                )}
                {protoNote.kept.length > 0 && (
                  <>★ 以下 {protoNote.kept.length} 条<b>无法无损转换</b>，已原样保留（未丢值）——形状与当前协议不符，提交会被服务端拒绝，请手工改写：{protoNote.kept.map((d, i) => <div key={i} className="mono">{d}</div>)}</>
                )}
              </div>
            )}
          </div>
          <div className="field"><label>{isL2p ? '绑定地址（libp2p 监听）与端口' : '绑定地址与端口'}</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 4444}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
            {isL2p && <div className="hint muted-hint">libp2p 的 <span className="mono">port_bind</span> 留 0 ⇒ 服务端从下面的 multiaddr 里推；推不出来会建 listener 失败（不会静默绑随机端口）。</div>}
          </div>
          <div className="field field-top"><label>{isL2p ? '回连地址（multiaddr）' : '回连地址'}</label>
            <textarea rows={3} value={arrToLines(c.callback_addresses)}
              onChange={(e) => setCfg('callback_addresses', linesToArr(e.target.value))}
              placeholder={isL2p
                ? '/ip4/1.2.3.4/tcp/39017\n可缺 /p2p/<本 listener PeerID>：服务端按私钥自动补'
                : '192.168.1.1:4444\nserver2.com:5555'} />
            {isL2p
              ? <div className="hint muted-hint">libp2p 模式下这里是 <b>multiaddr</b>（非 host:port）。若手写 /p2p/&lt;peerID&gt;，必须与下面私钥一致，否则建 listener 直接报错。</div>
              : <ServerAddrHint />}
          </div>
          <div className="field"><label>握手超时（秒）· tcp / libp2p 共用</label>
            <input type="number" min={1} value={c.timeout ?? 10} onChange={(e) => setCfg('timeout', Number(e.target.value))} />
            <div className="hint muted-hint">两种传输都消费，且服务端要求 ≥1（该校验位于 libp2p 分支短路<b>之前</b>）。</div>
          </div>
          {isL2p && (
            <>
              <div className="field"><label>libp2p 协议 ID</label>
                <input className="mono" value={c.l2p_protocol || ''} onChange={(e) => setCfg('l2p_protocol', e.target.value)}
                  placeholder="/gopher/1.0.0" />
              </div>
              <div className="field field-top"><label>libp2p relays（隐藏 C2）</label>
                <textarea rows={3} value={String(c.l2p_relays || '')}
                  onChange={(e) => setCfg('l2p_relays', e.target.value)}
                  placeholder={'/ip4/<relay 公网IP>/tcp/4446/p2p/<relayPeerID>\n一行一条；填了它 ⇒ agent 只拨 circuit 地址，C2 真实 IP:端口不进载荷'} />
                <div className="hint muted-hint">每条必须带 <span className="mono">/p2p/&lt;relayPeerID&gt;</span>（预约的路由键），且<b>不能</b>含 <span className="mono">/p2p-circuit</span>（由 listener 自己补）。relay 起在 <span className="mono">extenders/gopher_listener_tcp/relaynode</span>。</div>
              </div>
              <div className="field field-top"><label>libp2p addrs（agent 拨号地址）</label>
                <textarea rows={2} value={String(c.l2p_addrs || '')}
                  onChange={(e) => setCfg('l2p_addrs', e.target.value)}
                  placeholder={'留空 ⇒ 由 relays 自动派生（推荐）\n填了 ⇒ 覆盖回连地址（高级：自建 circuit / 多跳）'} />
                {derived.length > 0 && (
                  <div className="hint muted-hint">本次将下发给 agent 的拨号地址：{derived.map((d, i) => <div key={i} className="mono">{d}</div>)}</div>
                )}
              </div>
              <div className="field"><label>libp2p 拨号超时（秒）· 0 = 用库默认</label>
                <input type="number" value={c.l2p_dial_timeout ?? 0} onChange={(e) => setCfg('l2p_dial_timeout', Number(e.target.value))} />
                <div className="hint muted-hint">与上面的「握手超时」是两回事：这个是 agent <b>拨 relay</b> 的连接超时。</div>
              </div>
              <div className="field field-top"><label>libp2p 私钥（b64）</label>
                <input className="mono" value={c.l2p_privkey || ''} onChange={(e) => setCfg('l2p_privkey', e.target.value)}
                  placeholder="留空 ⇒ 服务端新建时生成" />
                <div className="hint muted-hint">留空 ⇒ 每次<b>新建</b>都换身份（PeerID 变），<b>已生成的 agent 全部失效</b>。要agent 可复用就别清空它 —— 编辑既有 listener 时该值原样回填。</div>
              </div>
            </>
          )}
          {/* ★ 仅 tcp 消费的字段（GOPHER_TCP_ONLY_KEYS）。在 l2p 下渲染它们属于
              「填了但没生效」族，其中 tcp_banner 还不止于此 —— 服务端注释（审计 C-F9）
              写明 l2p 分支的 agent 既不设也不读 BannerSize，写出去的 banner 会被当成
              4 字节长度头 ⇒ 帧错位。故 l2p 下整块不显示。 */}
          {!isL2p && (
            <div className="field field-top"><label>TCP 横幅（仅 tcp）</label>
              <textarea rows={2} value={c.tcp_banner || c.banner || ''} onChange={(e) => setCfg('tcp_banner', e.target.value)} />
              {/* 措辞按**读者所在的分支**写：本条 hint 只在 tcp 下渲染，所以不能说
                  「此处不显示」—— 那正是它此刻在做的事，读者看到的是一句自相矛盾的说明
                  （用户实测：新建 Gopher TCP 时该字段在屏上，而它的帮助文字说它不在）。
                  改成从 tcp 读者的位置描述「切过去会消失」，信息量不变且为真。 */}
              <div className="hint muted-hint">给扫描器看的伪装页。libp2p 下服务端不消费（硬守卫），故切到 libp2p 后本项不再显示。</div>
            </div>
          )}
          <div className="field field-top"><label>错误应答 · tcp / libp2p 共用</label>
            <textarea rows={2} value={c.error_answer || c.answer || 'Connection error...\n'}
              onChange={(e) => setCfg('error_answer', e.target.value)} />
          </div>
          <div className="field"><label>加密密钥 · tcp / libp2p 共用</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>生成</button>
            </div>
          </div>
          {!isL2p && (
            <>
              <div className="field"><label>mTLS（仅 tcp）</label>
                <label className="check-inline" style={{ color: 'var(--text)' }}>
                  <input type="checkbox" checked={!!c.ssl} onChange={(e) => setCfg('ssl', e.target.checked)} /> 启用 mTLS
                </label>
              </div>
              {!!c.ssl && (
                <div className="hint muted-hint">
                  mTLS 证书：Web 暂不传文件内容。字段名 ca_cert / server_cert / server_key / client_cert / client_key；可用 Qt 创建或服务端预置 PEM。
                </div>
              )}
            </>
          )}
        </>
      )
    }

    if (t === 'BeaconDiscord') {
      return (
        <>
          <div className="field"><label>机器人 Token</label>
            <input className="mono" value={c.bot_token || ''} onChange={(e) => setCfg('bot_token', e.target.value)}
              placeholder="Discord bot token（仅服务端）" />
          </div>
          <div className="field"><label>Beacon 频道 ID</label>
            <input className="mono" value={c.channel_beacon || ''} onChange={(e) => setCfg('channel_beacon', e.target.value)}
              placeholder="beacon → server 消息频道" />
          </div>
          <div className="field"><label>Tasks 频道 ID</label>
            <input className="mono" value={c.channel_tasks || ''} onChange={(e) => setCfg('channel_tasks', e.target.value)}
              placeholder="server → beacon 任务频道" />
          </div>
          <div className="field"><label>Webhook 地址</label>
            <input className="mono" value={c.webhook_url || ''} onChange={(e) => setCfg('webhook_url', e.target.value)}
              placeholder="https://discord.com/api/webhooks/..." />
          </div>
          <div className="field"><label>轮询间隔（秒）</label>
            <input type="number" min={1} value={c.poll_interval ?? 5}
              onChange={(e) => setCfg('poll_interval', Number(e.target.value))} />
          </div>
          <div className="field"><label>清理</label>
            <label className="check-inline" style={{ color: 'var(--text)' }}>
              <input type="checkbox" checked={!!c.cleanup} onChange={(e) => setCfg('cleanup', e.target.checked)} />
              读取后删除消息
            </label>
          </div>
          <div className="field"><label>加密密钥</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey() + randomEncryptKey())}>生成</button>
            </div>
          </div>
          <div className="hint muted-hint">对齐 AXS：Discord C2 通道。Token/频道 ID 勿提交到公开仓库。</div>
        </>
      )
    }

    if (t === 'KharonHTTP') {
      return (
        <>
          <div className="field"><label>绑定地址与端口</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 443}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
          </div>
          <div className="field"><label>可信 Host 头</label>
            <label className="check-inline" style={{ color: 'var(--text)' }}>
              <input type="checkbox" checked={!!c.trust_x_forwarded_host}
                onChange={(e) => setCfg('trust_x_forwarded_host', e.target.checked)} />
              信任 X-Forwarded-Host
            </label>
          </div>
          <div className="field field-top"><label>附加可信 Host</label>
            <textarea rows={3} value={c.additional_trusted_hosts || ''}
              onChange={(e) => setCfg('additional_trusted_hosts', e.target.value)}
              placeholder={'domain1.com\ndomain2.com'} />
          </div>
          <div className="field field-top"><label>拦截 User-Agent</label>
            <textarea rows={3} value={c.block_user_agents || ''}
              onChange={(e) => setCfg('block_user_agents', e.target.value)}
              placeholder={'curl*\nwget*'} />
          </div>
          <div className="field"><label>域名轮换</label>
            <select value={c.domain_rotation_strategy || 'Random'}
              onChange={(e) => setCfg('domain_rotation_strategy', e.target.value)}>
              <option>Random</option>
              <option>Round Robin</option>
              <option>Failover</option>
            </select>
          </div>
          <div className="field field-top"><label>Profile 文件</label>
            <div className="field-inline">
              <input type="file" accept=".json,.js,.txt,application/json"
                onChange={async (e) => {
                  const f = e.target.files?.[0]
                  if (!f) return
                  try {
                    setCfg('uploaded_file', await fileToB64(f))
                    setProfileName(f.name)
                  } catch (err: any) {
                    setProfileName('')
                    setProfileErr(`读取 ${f.name} 失败：${err?.message || err}`)
                  }
                }} />
              {profileName && <span className="dim mono" style={{ fontSize: 12 }}>{profileName}</span>}
              {!!c.uploaded_file && (
                <button type="button" className="btn btn-sm"
                  onClick={() => { setCfg('uploaded_file', ''); setProfileName('') }}>清除</button>
              )}
            </div>
            <textarea rows={3} className="mono" value={c.uploaded_file || ''}
              onChange={(e) => { setCfg('uploaded_file', e.target.value); setProfileName('') }}
              placeholder="或直接粘贴 profile JSON / base64" />
            <div className="field-hint">
              {c.uploaded_file
                ? <>当前值 {String(c.uploaded_file).length} 字符（
                    {String(c.uploaded_file).trim().startsWith('{') ? '原始 JSON'
                      : String(c.uploaded_file).trim().startsWith('/') ? '服务端路径 —— 该文件必须真实存在且可读'
                      : 'base64'}）。</>
                : <>必填：KharonHTTP 没有内置默认 profile，留空监听器起不来。</>}
            </div>
            {profileErr && <div className="field-hint" style={{ color: 'var(--err)' }}>{profileErr}</div>}
          </div>

          <div className="field field-top"><label>代理设置</label>
            <div className="ssl-box">
              <label className="check-inline" style={{ color: 'var(--text)' }}>
                <input type="checkbox" checked={!!c.proxy_enabled}
                  onChange={(e) => setCfg('proxy_enabled', e.target.checked)} /> 启用代理
              </label>
              {!!c.proxy_enabled && (
                <div style={{ display: 'grid', gap: 8, marginTop: 8 }}>
                  <input value={c.proxy_url || ''} onChange={(e) => setCfg('proxy_url', e.target.value)}
                    placeholder="http://127.0.0.1:8080" />
                  <div className="field-inline">
                    <input style={{ flex: 1 }} value={c.proxy_user || ''} onChange={(e) => setCfg('proxy_user', e.target.value)}
                      placeholder="用户名" />
                    <input style={{ flex: 1 }} type="password" value={c.proxy_pass || ''} onChange={(e) => setCfg('proxy_pass', e.target.value)}
                      placeholder="密码" />
                  </div>
                </div>
              )}
            </div>
          </div>

          <div className="field field-top"><label>使用 SSL (HTTPS)</label>
            <div className="ssl-box">
              <label className="check-inline" style={{ color: 'var(--text)' }}>
                <input type="checkbox" checked={!!c.ssl} onChange={(e) => setCfg('ssl', e.target.checked)} /> 启用 SSL/TLS
              </label>
              {!!c.ssl && (
                <div className="dim" style={{ fontSize: 11.5, marginTop: 6 }}>
                  证书/密钥文件选择在 Web 端未实现；留空由服务端处理，或在服务器放置 PEM。
                </div>
              )}
            </div>
          </div>
          <div className="hint muted-hint">对齐 <code>listener_kharon_http</code> ListenerUI（字段不同于 BeaconHTTP）。</div>
        </>
      )
    }

    return (
      <div className="hint warn-hint">未实现的监听类型：{t}。请用 Qt 客户端创建，或补充 Web 表单。</div>
    )
  }

  function renderHeaders() {
    const c = form.config
    return (
      <>
        <div className="field"><label>X-Forwarded-For</label>
          <label className="check-inline" style={{ color: 'var(--text)' }}>
            <input type="checkbox" checked={!!c['x-forwarded-for']} onChange={(e) => setCfg('x-forwarded-for', e.target.checked)} />
            信任 X-Forwarded-For
          </label>
        </div>
        <div className="field field-top"><label>Host 头</label>
          <EditableList
            value={Array.isArray(c.host_header) ? c.host_header : linesToArr(String(c.host_header || ''))}
            onChange={(v) => setCfg('host_header', v)}
            placeholder="host.example.com"
          />
        </div>
        <div className="field field-top"><label>请求头</label>
          <textarea rows={5} value={c.request_headers || ''}
            onChange={(e) => setCfg('request_headers', e.target.value)}
            placeholder={'每行一个，需精确匹配：\nX-Custom: value'} />
        </div>
        <div className="field field-top"><label>响应头</label>
          <textarea rows={4} value={c.server_headers || ''}
            onChange={(e) => setCfg('server_headers', e.target.value)}
            placeholder="Server: nginx" />
        </div>
      </>
    )
  }

  async function onSubmit() {
    setBusy(true)
    try {
      const cfg = { ...form.config }
      if (form.type === 'BeaconHTTP') cfg.protocol = 'http'
      if (form.type === 'BeaconWS') cfg.protocol = 'ws'
      // BeaconHTTPS 的这条是**功能性**的，不是凑对称：linux_agent/pl_main.go:429 那条
      // 推断的触发条件是 `protocol == "" || protocol == "http"`，而本监听器的 l_data
      // 必带 uri（validConfig 要 uri 正则）⇒ protocol 一旦缺失就会被改判成 "ws"，
      // agent 随即走 WebSocket 升级，而 beacon_listener_https_emu 永不回 101。
      // openEdit 的 `{...base, ...cfg}` 已经从缺省值表回填过；这里再钉一次，是为了让
      // 这个不变量不依赖那张表（缺省值被改动时提交路径仍然自洽）。
      if (form.type === 'BeaconHTTPS') cfg.protocol = 'https'
      // normalize list fields
      for (const k of ['callback_addresses', 'uri', 'user_agent', 'host_header']) {
        if (typeof cfg[k] === 'string') cfg[k] = linesToArr(cfg[k])
      }
      if (form.type === 'BeaconSMB' && !String(cfg.pipename || '').trim()) {
        throw new Error('SMB 监听需要填写 Pipename')
      }
      if ((form.type === 'BeaconHTTP' || form.type === 'BeaconWS' || form.type === 'BeaconHTTPS') && !(cfg.callback_addresses || []).length) {
        throw new Error('请至少添加一个 Callback address（host:port）')
      }
      if (form.type === 'GopherTCP') {
        // ★ 校验与归一只有一份实现：src/lib/gopherListener.ts（同一个函数被
        //   scripts/gopher_ui_check.mjs 直接调用 ⇒ 判据打在同一份代码上，不是抄一份测）。
        //   gopher_listener_tcp 的 callback_addresses 收 string（换行/逗号列表），
        //   不像 BeaconHTTP 那样收 []string；Qt 侧 TextMulti 也是 string。
        Object.assign(cfg, normalizeGopherConfig(cfg))
      }
      if (form.type === 'BeaconDiscord') {
        if (!String(cfg.bot_token || '').trim()) throw new Error('Discord 需要 Bot Token')
        if (!String(cfg.channel_beacon || '').trim() || !String(cfg.channel_tasks || '').trim()) {
          throw new Error('Discord 需要填写 Beacon / Tasks Channel ID')
        }
      }
      if (form.type === 'KharonHTTP') {
        cfg.protocol = 'http'
        // ★ 空 profile 必失败：服务端 resolve_profile_content("") 直接报
        //   "profile is empty"（pl_http.go:99），kharon 没有内置默认 profile。
        //   在提交前拦住 —— 否则现象是「创建成功但监听器不出现」，操作员无从判断
        //   是端口被占、profile 没填、还是服务端坏了。
        if (!String(cfg.uploaded_file || '').trim()) {
          throw new Error('KharonHTTP 需要 profile：选一个 profile JSON 文件，或粘贴 JSON / base64')
        }
      }
      if (editMode) {
        await api.editListener(form.name, form.type, cfg)
        pushLog('ok', `监听器 ${form.name} 已更新`)
        pushToast('ok', `监听器 ${form.name} 已更新`)
      } else {
        await api.createListener(form.name, form.type, cfg)
        pushLog('ok', `监听器 ${form.name} (${form.type}) 已创建`)
        pushToast('ok', `监听器 ${form.name} 已创建`)
      }
      setShow(false)
      await refreshRest()
    } catch (e: any) {
      pushLog('err', e.message || String(e))
      pushToast('err', e.message || String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="page">
      <div className="toolbar">
        <button className="primary" onClick={() => openCreate('BeaconHTTP')}>+ Windows HTTP</button>
        <button onClick={() => openCreate('BeaconWS')}>+ Linux WS</button>
        <input type="search" placeholder="搜索名称 / 类型 / 端口" value={q} onChange={(e) => setQ(e.target.value)} />
        <button onClick={() => refreshRest().catch((e) => pushLog('err', String(e)))}>刷新</button>
        <div className="stats">
          <span>{filtered.length} / {list.length}</span>
          <span className="dim">Windows 用 HTTP · Linux 用 WS</span>
        </div>
        <PageTips id="listeners">
          <p>按「新建监听器」选择类型并填写表单；状态列可启停/暂停/恢复。</p>
          <p><b>GopherTCP</b> 为改造监听器（ChaCha20 会话加密 + 随机化信标）。</p>
          <p>传输协议选 <b>libp2p</b> 即走 circuit 中继：agent 只拨 relay 的 <span className="mono">/p2p-circuit</span> 地址，C2 真实 IP:端口不进载荷。</p>
          <p>回连地址决定 agent 从外网回连的入口，需与目标机可达性一致。</p>
        </PageTips>
      </div>

      <div className="table-wrap">
        {filtered.length === 0 ? (
          <EmptyState
            icon="◉"
            title="暂无监听器"
            hint="Windows 上线建 HTTP，Linux 上线建 WS"
            action={<button className="primary" onClick={() => openCreate()}>+ Windows HTTP</button>}
          />
        ) : (
          <table>
            <thead>
              <tr>
                <th>名称</th>
                <th>类型</th>
                <th>协议</th>
                <th>绑定</th>
                <th>回连/地址</th>
                <th>状态</th>
                <th style={{ width: 180 }}></th>
              </tr>
            </thead>
            <tbody>
              {filtered.map((l) => (
                <tr key={l.l_name}>
                  <td className="mono">{l.l_name}</td>
                  <td>{l.l_reg_name || '-'}</td>
                  <td>{l.l_protocol || '-'}</td>
                  <td className="mono">{l.l_bind_host || '-'}:{l.l_bind_port || '-'}</td>
                  <td className="mono">{l.l_agent_addr || '-'}</td>
                  <td>
                    <span className={`tag ${String(l.l_status).toLowerCase().includes('listen') ? 'listen' : 'offline'}`}>
                      {l.l_status || '-'}
                    </span>
                  </td>
                  <td className="lst-actions">
                    <button onClick={() => openEdit(l)} title="修改这个监听器的配置">编辑</button>
                    {/* 暂停 / 恢复是一对互斥动作：原来两个都无条件渲染，
                        运行中的监听器上「恢复」照样能点，点下去会发生什么
                        界面上看不出来。现在按状态二选一启用，另一个置灰
                        并说明原因 —— 按钮还在，不会让人找不到功能。 */}
                    <button
                      disabled={!isRunning(l)}
                      title={isRunning(l) ? '暂停监听：暂不接受新的回连，可以恢复' : '监听器当前不在运行状态，无需暂停'}
                      onClick={async () => {
                      try {
                        await api.pauseListener(l.l_name, l.l_reg_name || 'BeaconHTTP')
                        pushLog('warn', `已暂停 ${l.l_name}`)
                        await refreshRest()
                      } catch (e: any) { pushLog('err', e.message || String(e)) }
                    }}>暂停</button>
                    <button
                      disabled={isRunning(l)}
                      title={isRunning(l) ? '监听器正在运行中，无需恢复' : '恢复监听：重新接受回连'}
                      onClick={async () => {
                      try {
                        await api.resumeListener(l.l_name, l.l_reg_name || 'BeaconHTTP')
                        pushLog('ok', `已恢复 ${l.l_name}`)
                        await refreshRest()
                      } catch (e: any) { pushLog('err', e.message || String(e)) }
                    }}>恢复</button>
                    <button className="danger" onClick={async () => {
                      if (!confirm(`停止并删除 ${l.l_name}？`)) return
                      try {
                        await api.stopListener(l.l_name, l.l_reg_name || 'BeaconHTTP')
                        pushLog('warn', `已停止 ${l.l_name}`)
                        await refreshRest()
                      } catch (e: any) { pushLog('err', e.message || String(e)) }
                    }}>停止</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      {show && (
        <div className="mask" onClick={() => !busy && setShow(false)}>
          <div className="dlg listener-dlg" onClick={(e) => e.stopPropagation()}>
            <div className="dlg-hd">
              <div>
                <h2>{editMode ? '编辑监听器' : '新建监听器'}</h2>
                <div className="dim" style={{ fontSize: 12 }}>
                  {typeDef ? `${typeDef.label} · ${typeDef.agentHint}` : '选择类型'}
                  {editMode ? ' · 名称/类型创建后不可改' : ''}
                </div>
              </div>
              <button className="ghost" disabled={busy} onClick={() => setShow(false)}>关闭</button>
            </div>

            <div className="field"><label>显示名称</label>
              <input
                value={form.name}
                disabled={editMode}
                onChange={(e) => setForm((f) => ({ ...f, name: e.target.value }))}
              />
            </div>
            <div className="field field-top"><label>类型</label>
              <div className="listener-type-grid">
                {LISTENER_TYPES.map((t) => (
                  <button
                    key={t.type}
                    type="button"
                    className={`listener-type ${form.type === t.type ? 'active' : ''}`}
                    disabled={editMode}
                    onClick={() => !editMode && setType(t.type)}
                  >
                    <b>{t.label}</b>
                    <span className="sub">{t.agentHint}</span>
                  </button>
                ))}
              </div>
            </div>

            {isHttpLike ? (
              <>
                <div className="ax-tabs">
                  <button type="button" className={tab === 'main' ? 'active' : ''} onClick={() => setTab('main')}>主要设置</button>
                  <button type="button" className={tab === 'headers' ? 'active' : ''} onClick={() => setTab('headers')}>HTTP 头</button>
                  <button type="button" className={tab === 'error' ? 'active' : ''} onClick={() => setTab('error')}>错误页</button>
                  <button type="button" className={tab === 'payload' ? 'active' : ''} onClick={() => setTab('payload')}>载荷页</button>
                </div>
                <div className="ax-tab-body">
                  {tab === 'main' && renderMain()}
                  {tab === 'headers' && renderHeaders()}
                  {tab === 'error' && (
                    <div className="field field-top"><label>错误页内容</label>
                      <textarea rows={12} className="mono" value={form.config['page-error'] || ''}
                        onChange={(e) => setCfg('page-error', e.target.value)} />
                    </div>
                  )}
                  {tab === 'payload' && (
                    <div className="field field-top"><label>载荷页内容</label>
                      <textarea rows={10} className="mono" value={form.config['page-payload'] || ''}
                        onChange={(e) => setCfg('page-payload', e.target.value)} />
                      <div className="field-hint">使用占位符 <code>&lt;&lt;&lt;PAYLOAD_DATA&gt;&gt;&gt;</code></div>
                    </div>
                  )}
                </div>
              </>
            ) : (
              <div className="ax-tab-body">{renderMain()}</div>
            )}

            <div className="btns" style={{ marginTop: 12 }}>
              <button disabled={busy} onClick={() => setShow(false)}>取消</button>
              <button className="primary" disabled={busy || !form.name.trim()} onClick={onSubmit}>
                {busy ? (editMode ? '保存中…' : '创建中…') : (editMode ? '保存修改' : '创建监听器')}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

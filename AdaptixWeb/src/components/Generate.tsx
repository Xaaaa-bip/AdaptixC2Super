import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { AGENT_PROFILES, defaultAgentConfig } from '../api/listeners'
import { ApiClient } from '../api/http'
import { downloadBytes, fmtSize } from '../utils'

type Preset = {
  id: string
  title: string
  desc: string
  agent: string
  format: string
  arch?: string
  os?: string
  badge?: string
}

const PRESETS: Preset[] = [
  { id: 'win-stager', title: 'Windows Stager', desc: '小加载器 + 服务端托管 shellcode，推荐投递', agent: 'beacon', format: 'Stager', arch: 'x64', badge: '推荐' },
  { id: 'win-exe', title: 'Windows EXE', desc: '完整 beacon 可执行文件', agent: 'beacon', format: 'Exe', arch: 'x64' },
  { id: 'win-svc', title: 'Windows Service', desc: '服务 EXE，适合持久化场景', agent: 'beacon', format: 'Service Exe', arch: 'x64' },
  { id: 'win-dll', title: 'Windows DLL', desc: '导出 DLL，可配合 sideload', agent: 'beacon', format: 'DLL', arch: 'x64' },
  { id: 'win-shell', title: 'Windows Shellcode', desc: '原始 shellcode（raw）', agent: 'beacon', format: 'Shellcode', arch: 'x64' },
  { id: 'linux-stager', title: 'Linux Stager', desc: 'Linux 外置 payload 加载器', agent: 'linux', format: 'Stager', arch: 'x86_64', os: 'linux', badge: 'Linux' },
]

export function Generate() {
  const listeners = useStore((s) => s.listeners) || {}
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const openUiTab = useStore((s) => s.openUiTab)
  const setTab = useStore((s) => s.setTab)
  const listenerList = useMemo(() => Object.values(listeners), [listeners])

  const [presetId, setPresetId] = useState('win-stager')
  const preset = PRESETS.find((p) => p.id === presetId) || PRESETS[0]

  const [agent, setAgent] = useState(preset.agent)
  const profile = AGENT_PROFILES.find((p) => p.agent === agent) || AGENT_PROFILES[0]
  const compatible = useMemo(
    () => listenerList.filter((l) => profile.listeners.includes(l.l_reg_name || '')),
    [listenerList, profile],
  )

  const [listener, setListener] = useState('')
  const [format, setFormat] = useState(preset.format)
  const [arch, setArch] = useState(preset.arch || profile.arches[0])
  const [os, setOs] = useState(preset.os || profile.osOptions?.[0] || 'windows')
  const [sleep, setSleep] = useState('5s')
  const [jitter, setJitter] = useState(0)
  const [advanced, setAdvanced] = useState(false)
  const [busy, setBusy] = useState(false)
  const [result, setResult] = useState('')
  const [lastFile, setLastFile] = useState('')
  const [lastSize, setLastSize] = useState(0)

  useEffect(() => {
    const p = PRESETS.find((x) => x.id === presetId) || PRESETS[0]
    setAgent(p.agent)
    setFormat(p.format)
    if (p.arch) setArch(p.arch)
    if (p.os) setOs(p.os)
    const prof = AGENT_PROFILES.find((x) => x.agent === p.agent)
    if (prof && !p.arch) setArch(prof.arches[0])
  }, [presetId])

  useEffect(() => {
    if (compatible.length) {
      if (!compatible.find((l) => l.l_name === listener)) setListener(compatible[0].l_name)
    } else setListener('')
  }, [compatible, listener])

  const selectedListener = compatible.find((l) => l.l_name === listener)

  async function onGenerate() {
    if (!listener) {
      pushToast('warn', '请先选择监听器')
      return
    }
    setBusy(true); setResult(''); setLastFile(''); setLastSize(0)
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
            <span className="dim" style={{ fontSize: 12 }}>选预设 → 选监听 → 一键下载</span>
          </div>
          <div className="card-bd gen-body">
            <div className="gen-section-title">1. 投递预设</div>
            <div className="gen-presets">
              {PRESETS.map((p) => (
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
                  <div className="gen-preset-meta mono">{p.agent} · {p.format} · {p.arch || 'auto'}</div>
                </button>
              ))}
            </div>

            <div className="gen-section-title">2. 监听器</div>
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
                    onClick={() => setListener(l.l_name)}
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

            <div className="gen-section-title">
              3. 参数
              <button type="button" className="ghost" style={{ marginLeft: 8, height: 24 }} onClick={() => setAdvanced((v) => !v)}>
                {advanced ? '收起高级' : '高级'}
              </button>
            </div>

            <div className="gen-params">
              <div className="field"><label>格式</label>
                <div className="chip-row">
                  {profile.formats.map((f) => (
                    <button key={f} type="button" className={format === f ? 'primary' : ''} onClick={() => setFormat(f)}>{f}</button>
                  ))}
                </div>
              </div>
              <div className="field-grid-2">
                <div className="field"><label>架构</label>
                  <div className="chip-row">
                    {profile.arches.map((a) => (
                      <button key={a} type="button" className={arch === a ? 'primary' : ''} onClick={() => setArch(a)}>{a}</button>
                    ))}
                  </div>
                </div>
                {profile.osOptions ? (
                  <div className="field"><label>系统</label>
                    <select value={os} onChange={(e) => setOs(e.target.value)}>
                      {profile.osOptions.map((o) => <option key={o} value={o}>{o}</option>)}
                    </select>
                  </div>
                ) : <div />}
              </div>

              {(agent === 'beacon' || advanced) && agent === 'beacon' && (
                <div className="field"><label>Sleep / Jitter</label>
                  <div style={{ display: 'flex', gap: 8 }}>
                    <input style={{ flex: 1 }} value={sleep} onChange={(e) => setSleep(e.target.value)} placeholder="5s / 10s / 1m" />
                    <input type="number" min={0} max={100} style={{ width: 90 }} value={jitter}
                      onChange={(e) => setJitter(Number(e.target.value))} title="抖动%" placeholder="抖动%" />
                  </div>
                </div>
              )}

              {advanced && (
                <div className="field"><label>Agent</label>
                  <div className="chip-row">
                    {AGENT_PROFILES.map((p) => (
                      <button key={p.agent} type="button" className={agent === p.agent ? 'primary' : ''}
                        onClick={() => {
                          setAgent(p.agent)
                          setFormat(p.formats[0])
                          setArch(p.arches[0])
                          if (p.osOptions) setOs(p.osOptions[0])
                        }}>{p.label}</button>
                    ))}
                  </div>
                </div>
              )}
            </div>

            {format === 'Stager' && (
              <div className="hint">
                <b>Stager</b>：小加载器 + 服务端托管 payload（<code>/api/s/&lt;token&gt;</code>，一次下载 / 24h TTL）。
                生成后尽快投递；勿提前预下载 token。
              </div>
            )}
            {agent === 'beacon' && format !== 'Stager' && (
              <div className="hint muted-hint">
                生产建议：gcc + 关闭 iat_hiding。反复扫描同模式会触发云学习；单次投递优先 <b>Stager</b>。
              </div>
            )}

            <div className="btns" style={{ marginTop: 8 }}>
              <button className="primary" style={{ minWidth: 160, height: 36 }} disabled={busy || !listener} onClick={onGenerate}>
                {busy ? '生成中…' : '生成并下载'}
              </button>
              {!listener && <span className="dim" style={{ alignSelf: 'center' }}>请先选择监听器</span>}
            </div>

            {result === 'ok' && lastFile && (
              <div className="result-box ok">
                ✓ {lastFile} · {fmtSize(lastSize)}
                {format === 'Stager' && (
                  <div className="dim" style={{ marginTop: 6 }}>
                    投递后从服务端拉 payload，确保目标机能访问 8080。
                  </div>
                )}
              </div>
            )}
            {result && result !== 'ok' && (
              <div className="result-box err">{result}</div>
            )}
          </div>
        </div>

        <aside className="gen-side card">
          <div className="card-hd"><h3>摘要</h3></div>
          <div className="card-bd" style={{ padding: 14 }}>
            <div className="gen-summary">
              <div><span className="k">Agent</span><b>{agent}</b></div>
              <div><span className="k">格式</span><b>{format}</b></div>
              <div><span className="k">架构</span><b className="mono">{arch}</b></div>
              {profile.osOptions && <div><span className="k">系统</span><b>{os}</b></div>}
              <div><span className="k">监听</span><b className="mono">{listener || '未选'}</b></div>
              {selectedListener && (
                <div><span className="k">绑定</span>
                  <b className="mono">{selectedListener.l_bind_host || '-'}:{selectedListener.l_bind_port || '-'}</b>
                </div>
              )}
              {agent === 'beacon' && (
                <div><span className="k">Sleep</span><b>{sleep}{jitter ? ` · jitter ${jitter}%` : ''}</b></div>
              )}
            </div>
            <div className="hint muted-hint" style={{ marginTop: 14 }}>
              流程：创建监听 → 选预设 → 生成 → 投递执行 → 主机列表上线。
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

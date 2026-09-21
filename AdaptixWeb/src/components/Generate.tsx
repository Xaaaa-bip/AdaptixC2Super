import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import {
  AGENT_PROFILES,
  defaultAgentConfig,
  gopherFormatForOs,
  type AgentProfile,
} from '../api/listeners'
import { ApiClient } from '../api/http'
import { loadBuildSchema, visibleFields } from '../api/buildSchema'
import type { BuildSchema } from '../api/types'
import { downloadBytes, fmtSize } from '../utils'
import { PageTips } from './PageTips'
import { BuildSchemaFields } from './BuildSchemaFields'

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
    desc: 'Beacon / Kharon · Stager、EXE',
    badge: '推荐',
    agents: ['beacon', 'kharon'],
    defaultAgent: 'beacon',
  },
  {
    id: 'linux',
    title: 'Linux',
    desc: 'Native Agent · ELF / SO / Shellcode / Stager',
    agents: ['linux'],
    defaultAgent: 'linux',
  },
  {
    id: 'macos',
    title: 'macOS',
    desc: 'Go Agent · Mach-O (Apple Silicon)',
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

/**
 * Stager 内层载荷由 **teamserver 自己**的 HTTP 端点托管（/api/s/:token），
 * 跟监听器不是一回事，端口也不一样。控制台本来就跑在 teamserver 上，所以
 * location 就是正确答案。
 *
 * 以前这里没有输入框：端点只能靠服务端环境变量猜，猜错（典型是落到 8080，
 * 那是 agent 监听器）就生成一个投出去必然静默失败的载荷，而服务端一个字节
 * 的日志都不会留下。
 */
type StagerEndpoint = { host: string; port: number; ssl: boolean }

function defaultStagerEndpoint(): StagerEndpoint {
  if (typeof window === 'undefined') return { host: '', port: 0, ssl: true }
  const { hostname, port, protocol } = window.location
  const ssl = protocol === 'https:'
  return { host: hostname || '', port: port ? Number(port) : ssl ? 443 : 80, ssl }
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
  // Linux 载荷重连参数（当前在 api/listeners.ts 里硬编码，这里允许覆盖）
  const [reconnTimeout, setReconnTimeout] = useState(10)
  const [reconnCount, setReconnCount] = useState(1000)
  const [opsec, setOpsec] = useState(false)
  // Stager 下载端点（beacon / linux 的 Stager 格式用）
  const [stagerEp, setStagerEp] = useState<StagerEndpoint>(defaultStagerEndpoint)
  const [result, setResult] = useState('')
  const [lastFile, setLastFile] = useState('')
  const [lastSize, setLastSize] = useState(0)

  // ── 插件声明的生成参数表 ────────────────────────────────────────────────
  // null = 这个 agent/listener 组合没有声明（服务端返回 {}），走下面的内置界面。
  // 拉取失败也留 null 但会记一条日志 —— 两者在界面上一样，在日志里必须能分开。
  const [schema, setSchema] = useState<BuildSchema | null>(null)
  // 表里那些**控制台自己没有 state** 的键的值。
  //
  // ★ 「在表里」才下发：没动过的键不进 config，插件按自己的默认值走。这是唯一不会
  //   歪掉的做法 —— 表里没有 default 字段，前端无从知道"插件默认是什么"，而随便填
  //   一个值就会静默改掉它。反例就在这张表上：rotation_mode 的选项是
  //   ["random","sequential"]，插件默认却是 sequential（pl_main.go:756 只判 "random"），
  //   默认选第一项 = 把顺序取用改成随机取用。
  //
  //   顺带也让 iat_hiding 回到它该有的语义：以前这里恒发 false，把 buildenv.mk 的
  //   BEACON_IAT_HIDING_DEFAULT 压死了，而表上的提示还写着"默认值来自 buildenv"。
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
  // 表是按 (agent, listener 注册名) 取的 —— 服务端就是按这两个键预计算并缓存的。
  // 这里是**真正选中的**监听器类型（没选就是空串），只用于下发判定与提示。
  // 取表另用下面的 schemaType —— 那一个不能为空串，见其注释。
  const listenerType = selectedListener?.l_reg_name || ''
  /**
   * 取「插件声明的参数表」用的监听器类型。
   *
   * ★ 没选监听器时**不能留空**：服务端按 (agent, listener_type) 预计算并缓存这张表，
   *   查不到就返回 {}，前端 normalize 判 null，整个参数区退回内置表单 —— 操作员一进来
   *   看到的参数比插件实际支持的少一大截（IAT 隐藏 / 图标伪装 / 自签名 / PE 头 /
   *   代理轮换 / DNS / Stager 端点全不见），会以为功能被砍了。
   *   实际上这只是"还没选监听器"，不是"没有这些参数"。
   *
   *   兜底取本 agent 声明的第一个监听器类型：字段面是完整的，只有 show_if 的结论
   *   按那个类型算 —— 比一片空白强，也比猜一个好（不是猜，是 profile 里写死的）。
   *
   *   下发不受影响：没选监听器时「生成并下载」本来就是禁用的，真生成时 listenerType 非空，
   *   两者相等。
   */
  const schemaType = listenerType || profile.listeners[0] || ''

  // 换 agent：上一个插件的表不能继续用（字段完全不同，会渲染出一屏发不出去的控件），
  // 操作员在它上面填的值也一样。换监听器则**不清**——字段列表其实一样，只有 show_if
  // 的结论不同，清掉会让界面先闪回内置表单再闪回来。
  //
  // exe_pseudo_sign 预置 true 是**跨字段策略②**：Exe / Service Exe 默认勾上伪签名。
  // 它以前写死在 onGenerate 里（`format === 'Exe' || format === 'Service Exe'`），
  // 现在表上有这个控件了，就得让它显示成勾上的样子，否则是"界面没勾、实际发了 true"。
  //
  // 计划书里还有一条**策略①（Stager 时强制 sleep=0s / jitter=0）在这份代码里并不存在**
  // —— 别照计划书补上：插件确实拿 Stager 的 sleep 去生成内层载荷（pl_main.go:650），
  // 强制归零是**改**行为，不是还原行为。
  useEffect(() => {
    setSchema(null)
    // 默认**不**勾选。改成 true 之前先看 buildenv.mk 的 BEACON_PSEUDO_SIGN 注释：
    // 自签名实测 0/12 过，关掉 7/9。
    setSv({ exe_pseudo_sign: false })
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
        // 失败退回内置界面是对的（宁可给操作员一个能用的表单），但**必须留痕** ——
        // 否则"这个 agent 没迁移"和"后端挂了"在界面上长得一模一样。
        pushLog('warn', `生成参数表拉取失败（${agent}/${schemaType || '-'}）：${e?.message || e}`)
      })
    return () => {
      stale = true
    }
  }, [api, agent, schemaType, pushLog])

  const showSleep = agent === 'beacon' || agent === 'kharon'
  const isStager = format === 'Stager'
  const formatOptions =
    agent === 'gopher'
      ? [gopherFormatForOs(os)]
      : profile.formats

  /**
   * 表里的值 → 控制台的状态。
   *
   * 这几个 key 控制台自己就有 state：摘要栏、提示条（isStager）、监听器兼容性、切平台
   * 重置都直接读它们，所以不再另开一份。**每个 key 只有一处存储** —— 不存在
   * "表里选了 Stager、而 isStager 还是 false、于是下载端点的输入框不出现"这种分叉。
   *
   * ★ 代价说清楚：表以后新增一个控制台恰好也有 state 的 key，必须在这里补一根线。
   *   漏了的表现是"界面上改了、配置里没变"，不报错。下面 write 的 switch 和这里
   *   一一对应，改的时候两个一起改。
   *   注意这两个 switch 是**路由**，不是"有哪些字段/取值"的第二份声明 ——
   *   取值域和显隐一律来自表，这里一个字都不重复。
   */
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
      case 'stager_ts_host':
        return setStagerEp((s) => ({ ...s, host: String(v ?? '') }))
      case 'stager_ts_port':
        return setStagerEp((s) => ({ ...s, port: Number(v) || 0 }))
      case 'stager_ts_ssl':
        return setStagerEp((s) => ({ ...s, ssl: !!v }))
      default:
        // undefined = 从表里删掉这个键 ⇒ 不下发。文本框清空、以及「默认」芯片都走这条。
        return setSv((s) => {
          const n = { ...s }
          if (v === undefined) delete n[k]
          else n[k] = v
          return n
        })
    }
  }

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
      let cfg: Record<string, any> = defaultAgentConfig(agent, format, arch, os)
      if (schema) {
        // 这个 agent 声明了字段表 ⇒ 表说了算。
        // 先把表覆盖到的键从内置基线里**删掉**，再按表填回去 —— 否则基线里那些
        // 表上没动过的键（`iat_hiding: false` 就是一个）会继续发出去，
        // 而表上写着"留空 = 用插件默认值"。表声明了它，基线就不该再说话。
        for (const f of schema.fields) delete cfg[f.key]
        // ★ 只收集**当前可见**的键：show_if 收起 = 这个组合不适用 = 不发。
        //   否则在 HTTP 监听器上填的代理，切到 TCP 之后会照样发出去 —— 插件不报错，
        //   只是把一个它用不上的代理配置编进载荷。值仍留在 sv 里，切回去还在。
        for (const f of visibleFields(schema, format, schemaType)) {
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
            // 与 buildenv.mk 的 BEACON_PSEUDO_SIGN 保持一致（默认不签名，见那里的实测记录）。
            // ★ 这条分支只在**参数表拉取失败**时走；以前它硬编码 true，
            //   等于"表没拉到就无视操作员的勾选"，是另一处签名开关失灵。
            exe_pseudo_sign: false,
          }
        } else if (agent === 'kharon') {
          cfg = { ...cfg, sleep, jitter }
        }
        if (agent === 'linux') {
          // 覆盖 listeners.ts 中的硬编码默认值
          cfg = {
            ...cfg,
            reconn_timeout: String(reconnTimeout),
            reconn_count: Number(reconnCount),
            opsec_enabled: opsec,
          }
        }
        // Stager 下载端点。beacon_agent / linux_agent 的 pl_stager 都按 stager_ts_*
        // 读，且以 agent_params 为**第一优先级**（空值才回落到服务端环境变量 /
        // 监听器 callback_addresses）。UI 是操作员唯一能改的地方，所以显式带上。
        // （beacon 走表那条路时，这三个键由表上的 stager_ts_* 字段负责，同样是第一优先级。）
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
            <span className="dim" style={{ fontSize: 12 }}>选平台 → 选监听 → 选格式 → 下载</span>
            <PageTips id="generate">
              <p>选择平台 → 格式/架构 → 兼容监听器 → 生成后浏览器下载 payload。</p>
              <p>Linux 载荷可配置<b>重连间隔/次数与 OPSEC 检测</b>；<b>Stager</b> 为一次性载荷（24 小时 TTL）。</p>
              <p>生成产物为 base64 编码，本地解码后投递。</p>
            </PageTips>
          </div>
          <div className="card-bd gen-body">
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

            {/* 计数行。
                这里原来是「只显示 2/5」被当成 bug 报上来的地方 —— 其实过滤本身是**对的**
                （compatible 只留 l_reg_name 落在 profile.listeners 里的那些），
                缺陷在于页面对"藏起来了几个、为什么藏"一个字都不说：
                操作员看到 5 个监听器只出来 2 个，只能当成页面坏了。
                所以补的是**解释**，不是把过滤放开 —— 放开会让不兼容的监听器可选，
                选下去生成出来的载荷连不上。 */}
            <div className="gen-section-title">
              2. 监听器
              {listenerList.length > 0 && (
                <span className="gen-count">
                  显示 {compatible.length} / 共 {listenerList.length}
                  {compatible.length < listenerList.length &&
                    ` · ${listenerList.length - compatible.length} 个与 ${profile.label} 不兼容`}
                </span>
              )}
            </div>
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
              {/* 有表 ⇒ 按插件声明的字段表渲染；没有 ⇒ 下面那段内置 JSX **一字不动**。
                  未迁移的 agent（kharon / gopher / macos / linux）走的还是老路，
                  界面上和改动前逐项一致。 */}
              {schema ? (
                <BuildSchemaFields
                  schema={schema}
                  format={format}
                  listenerType={schemaType}
                  read={readKey}
                  write={writeKey}
                />
              ) : (
              <>
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

              {isStager && (
                <>
                  <div className="gen-section-title">Stager 下载端点</div>
                  <div className="hint muted-hint" style={{ marginBottom: 4 }}>
                    内层载荷由 <b>teamserver 自己</b>托管（<code>/api/s/&lt;token&gt;</code>），
                    不是监听器端口。默认已填当前控制台地址；目标机若从别的出口访问，
                    改成它够得着的对外地址。
                  </div>
                  <div className="field-grid-2">
                    <div className="field">
                      <label>Teamserver 地址</label>
                      <input
                        value={stagerEp.host}
                        onChange={(e) => setStagerEp((s) => ({ ...s, host: e.target.value }))}
                        placeholder="154.198.162.34"
                        title="stager_ts_host"
                      />
                    </div>
                    <div className="field">
                      <label>端口</label>
                      <input
                        type="number"
                        min={0}
                        max={65535}
                        value={stagerEp.port || ''}
                        onChange={(e) => setStagerEp((s) => ({ ...s, port: Number(e.target.value) }))}
                        placeholder="38212"
                        title="stager_ts_port"
                      />
                    </div>
                  </div>
                  <div className="field field-flat">
                    <label className="check-inline" style={{ color: 'var(--text)' }} title="stager_ts_ssl">
                      <input
                        type="checkbox"
                        checked={stagerEp.ssl}
                        onChange={(e) => setStagerEp((s) => ({ ...s, ssl: e.target.checked }))}
                      />
                      走 TLS（与 Teamserver.ssl 一致）
                    </label>
                  </div>
                  {(!stagerEp.host.trim() || !stagerEp.port) && (
                    <div className="hint warn-hint">
                      地址或端口为空：生成时会回落到服务端环境变量；那里也没配的话，生成直接报错。
                    </div>
                  )}
                </>
              )}

              {agent === 'linux' && (
                <>
                  <div className="field-grid-2">
                    <div className="field">
                      <label>重连间隔(秒)</label>
                      <input
                        type="number"
                        min={0}
                        value={reconnTimeout}
                        onChange={(e) => setReconnTimeout(Number(e.target.value))}
                        title="reconn_timeout"
                      />
                    </div>
                    <div className="field">
                      <label>重连次数</label>
                      <input
                        type="number"
                        min={0}
                        value={reconnCount}
                        onChange={(e) => setReconnCount(Number(e.target.value))}
                        title="reconn_count"
                      />
                    </div>
                  </div>
                  <div className="field field-flat">
                    {/* 线名 opsec_enabled 移到 title：它是复选框自己的说明文字，
                        不是左侧那列字段标签（"重连间隔"/"重连次数"那种）。
                        原来两者都写了，读起来像"OPSEC 检测 启用 OPSEC 检测（opsec_enabled）"。
                        color 保持 --text：.field label 会把颜色压成 --text-2，
                        而这是控件正文，跟全站另外 9 处 .check-inline 一致。 */}
                    <label className="check-inline" style={{ color: 'var(--text)' }} title="opsec_enabled">
                      <input type="checkbox" checked={opsec} onChange={(e) => setOpsec(e.target.checked)} />
                      启用 OPSEC 检测
                    </label>
                  </div>
                </>
              )}

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
              </>
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
            {agent === 'linux' && (
              <div className="hint">
                重连参数仅 Linux 载荷生效；Stager 为一次性载荷（24 小时 TTL）。
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
              {isStager && (
                <div>
                  <span className="k">Stager 端点</span>
                  <b className="mono">
                    {stagerEp.ssl ? 'https' : 'http'}://{stagerEp.host || '?'}:{stagerEp.port || '?'}
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

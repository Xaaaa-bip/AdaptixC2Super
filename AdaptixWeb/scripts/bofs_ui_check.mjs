#!/usr/bin/env node
/* Phase5 批次20：AdaptixWeb BOF 页的**契约验收**（无浏览器）。
 *
 * 为什么需要它：
 *   用户实测的两条诉求是「BOF 页面应该动态识别有多少插件」和「要有上传、编译功能」。
 *   而 `npm run build` 绿**只证明能编译**，不证明：
 *     - 列表真的来自服务端（而不是又抄了一份硬编码数组）
 *     - 前端读的 JSON 键与服务端实际发出的键**对得上**
 *     - 分类键闭合（不闭合 ⇒ 模块静默不渲染，正是本项目反复归档的「填了但没生效」族）
 *     - 错误真的能到操作员眼前（http.ts:92 抛的是 data.message，不是 data.error）
 *
 * 判据：
 *   C1 无硬编码模块清单：不得再有 `const BOFS` / `file: 'x.o'` 这类字面量清单
 *   C2 列表来自服务端：必须调用 /api/bofs/list
 *   C3 上传与编译端点存在：/api/bofs/upload、/api/bofs/compile
 *   C4 基路径不再硬编码：不得有 DEFAULT_BASE；必须有 serverBase 兜底
 *   C5 ★两端 JSON 键对齐（本脚本核心）：契约里每个键，服务端源码里必须真发、前端必须真读
 *   C6 分类键闭合：META 用到的每个 category 必须在 CATS 里（否则该模块**永不渲染**）
 *   C7 错误可见：每个 catch 都要把 message 交出去，不得空吞
 *   C8 离线兜底有显式标注
 *   C9 上传体积上限两端一致（客户端先拦 = 服务端也得是这个数，否则提示与实际能力脱节）
 *   C10 ★自检：脚本必须能对「退化版」判红 —— 否则判据本身可能是恒真的
 *
 * 覆盖面（诚实声明）：**不**验证渲染像素、不启动浏览器、不跑真 HTTP。
 *   验证的是「源码级契约 + 同一份校验逻辑的行为」。真链路见批次20 运行态验收记录。
 *
 * 用法（在 AdaptixWeb/ 下）: node scripts/bofs_ui_check.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const WEB = path.join(__dirname, '..')
const TSX_PATH = process.argv[2] || path.join(WEB, 'src', 'components', 'Bofs.tsx')
const GO_PATH = process.argv[3] ||
  path.join(WEB, '..', 'AdaptixServer', 'extenders', 'linux_agent', 'pl_bofs_api.go')

let fails = 0
let checks = 0
function ok(cond, label, extra = '') {
  checks++
  if (cond) {
    console.log(`  ✓ ${label}${extra ? ` | ${extra}` : ''}`)
  } else {
    fails++
    console.log(`  ✗ ${label}${extra ? ` | ${extra}` : ''}`)
  }
}

function slice(src, from, to) {
  const a = src.indexOf(from)
  if (a < 0) return ''
  const b = to ? src.indexOf(to, a + from.length) : -1
  return src.slice(a, b < 0 ? undefined : b)
}

export function runChecks(tsx, go) {
  const out = { fails: 0, checks: 0, lines: [] }
  // ★ 局部重绑：runChecks 只往 out 里记账，不直接打印 ——
  //   否则自检（C10）拿退化版跑一遍时会把输出搅乱、并重复计入全局计数器。
  //   `const ok = ok2` 顺带把外层那个会打印的 ok 遮蔽掉，防止漏改一处就污染全局。
  const ok2 = (c, l, e = '') => { out.checks++; if (!c) { out.fails++; out.lines.push(`  ✗ ${l}${e ? ` | ${e}` : ''}`) } }
  const ok = ok2

  // ── C1 无硬编码模块清单 ──
  ok2(!/const\s+BOFS\b/.test(tsx), 'C1 无 const BOFS 硬编码数组')
  const fileLits = tsx.match(/file:\s*'[^']+\.o'/g) || []
  ok2(fileLits.length === 0, 'C1 无 file: \'x.o\' 字面量清单', fileLits.length ? `${fileLits.length} 处` : '')
  ok2(/file:\s*`\$\{e\.name\}\.o`/.test(tsx), 'C1 文件名由服务端 name 派生')

  // ── C2/C3 端点必须是**真调用**，不是提过一嘴 ──
  // ★ 为什么不能用 tsx.includes('/api/bofs/list')：页面帮助文本里也写了这个路径
  //   （PageTips 里的 <code>/api/bofs/list</code>），于是「只在说明文字里提一句、
  //   代码里根本不调」也能让判据通过 —— 那是一条恒真的假判据。
  //   这是 C10 自检抓出来的（变异「改掉 list 端点」当时判不红）。
  //   改为要求 api.get/api.post 的**调用形态**。
  const callRe = (ep) =>
    new RegExp(`api\\s*\\.\\s*(?:get|post)\\s*(?:<[^>]*>)?\\s*\\(\\s*['"\`]${ep}['"\`]`)
  ok2(callRe('/api/bofs/list').test(tsx), 'C2 真实调用 api.get(/api/bofs/list)')
  ok2(callRe('/api/bofs/upload').test(tsx), 'C3 真实调用 api.post(/api/bofs/upload)')
  ok2(callRe('/api/bofs/compile').test(tsx), 'C3 真实调用 api.post(/api/bofs/compile)')
  ok2(/all:\s*true/.test(tsx), 'C3 支持全量重编 {all:true}')

  // ── C4 基路径 ──
  ok2(!/DEFAULT_BASE/.test(tsx), 'C4 无 DEFAULT_BASE 硬编码默认值')
  ok2(/serverBase/.test(tsx), 'C4 基路径回退到服务端返回的 base')

  // ── C5 ★两端 JSON 键对齐 ──
  // 两个方向是**不对称**的，各有各的失败模式，所以分开判、且只有一边要求「全读」：
  //
  //   方向 A（前端读 → 服务端必须发）：漏了 ⇒ UI 永远拿到 undefined，
  //           按钮点了没反应、字段显示 '-'，**不报错**。本项目「填了但没生效」族。
  //   方向 B（服务端发 → 前端必须读）：只对 **entry 行** 要求全读 ——
  //           一行的字段就是列表要展示的全部信息，漏读 = 信息静默丢失。
  //           其它响应体（如 compile result 的 obj_size/obj_sha256）前端不读是对的：
  //           编译后前端会重新拉一次 list，新值从列表来，读两处反而会不一致。
  //           所以这里**不**对它们设「必须读」，避免造出一条恒真的假判据。
  const TS_READS = {
    'list 顶层': ['entries', 'base'],
    'entry 行': ['name', 'obj', 'obj_size', 'obj_mtime', 'obj_sha256', 'src', 'src_size', 'src_mtime', 'stale'],
    'upload 响应': ['compiled', 'compile'],
    'compile 响应': ['result'],
    'compile result': ['changed', 'output'],
    'compile all': ['results', 'n_ok', 'n_changed', 'count'],
    // 前端不直接读 data.message：http.ts:92 在 ok===false 时把 data.message **抛成**
    // Error，页面上读到的是 e.message。契约是**传递**成立的，方向 A 依然要判。
    '错误信封': ['message'],
  }
  const GO_MUST_BE_READ = {
    'entry 行': ['name', 'obj', 'obj_size', 'obj_mtime', 'obj_sha256', 'src', 'src_size', 'src_mtime', 'stale'],
  }
  // 服务端「发过的键」= json tag ∪ map 字面量 `"k": v` ∪ 索引赋值 `out["k"] = v`
  // ★ 三种形态缺一不可：上传响应用的是第三种（out["compiled"] = res.OK），
  //   只认前两种会把真实发出的键判成「服务端没发」—— 判据自己制造假红。
  const goTags = new Set([...go.matchAll(/json:"([a-z_0-9]+)"/g)].map((m) => m[1]))
  const goMapKeys = new Set([...go.matchAll(/"([a-z_0-9]+)":/g)].map((m) => m[1]))
  const goIdxKeys = new Set([...go.matchAll(/\["([a-z_0-9]+)"\]\s*=/g)].map((m) => m[1]))
  const goEmits = new Set([...goTags, ...goMapKeys, ...goIdxKeys])
  const tsReads = (k) => new RegExp(`[.\\['"\`]${k}\\b`).test(tsx) || tsx.includes(`'${k}'`)

  ok2(goEmits.size > 0, 'C5 解析到服务端发出的键（防空判据）', `${goEmits.size} 个`)
  for (const [group, keys] of Object.entries(TS_READS)) {
    const missingGo = keys.filter((k) => !goEmits.has(k))
    const missingTs = keys.filter((k) => !tsReads(k))
    ok2(missingGo.length === 0, `C5[A] ${group}：前端读的键服务端确实发`, missingGo.length ? `服务端没发 ${missingGo}` : `${keys.length} 个`)
    ok2(missingTs.length === 0, `C5[A] ${group}：这些键前端确实在读`, missingTs.length ? `前端没读 ${missingTs}` : `${keys.length} 个`)
  }
  for (const [group, keys] of Object.entries(GO_MUST_BE_READ)) {
    const missingTs = keys.filter((k) => !tsReads(k))
    ok2(missingTs.length === 0, `C5[B] ${group}：服务端发的键前端全部读到（无静默丢失）`, missingTs.length ? `漏读 ${missingTs}` : `${keys.length} 个`)
  }

  // ── C6 分类键闭合 ──
  const metaSrc = slice(tsx, 'const META', 'const CATS')
  const catsSrc = slice(tsx, 'const CATS', 'const UNKNOWN_META')
  const unknownSrc = slice(tsx, 'const UNKNOWN_META', 'const BASE_KEY')
  const metaCats = new Set([...metaSrc.matchAll(/category:\s*'([a-z]+)'/g)].map((m) => m[1]))
  const catKeys = new Set([...catsSrc.matchAll(/key:\s*'([a-z]+)'/g)].map((m) => m[1]))
  const unknownCat = (unknownSrc.match(/category:\s*'([a-z]+)'/) || [])[1]
  ok(metaCats.size > 0 && catKeys.size > 0, 'C6 解析到 META/CATS（防空判据）', `meta=${metaCats.size} cats=${catKeys.size}`)
  const unclosed = [...metaCats].filter((c) => !catKeys.has(c))
  ok(unclosed.length === 0, 'C6 META 的每个 category 都在 CATS 里（否则模块永不渲染）', unclosed.length ? `未闭合 ${unclosed}` : `${metaCats.size} 个全闭合`)
  ok(!!unknownCat && catKeys.has(unknownCat), 'C6 未登记模块有归处（UNKNOWN_META 的 category 在 CATS 里）', `unknown=${unknownCat}`)

  // ── C7 错误可见 ──
  const catches = [...tsx.matchAll(/catch\s*\(([^)]*)\)\s*\{([\s\S]*?)\n\s*\}/g)]
  const swallowed = catches.filter((m) => !/message/.test(m[2]))
  ok(catches.length > 0, 'C7 解析到 catch 块（防空判据）', `${catches.length} 个`)
  ok(swallowed.length === 0, 'C7 每个 catch 都把 message 交出去（不得空吞）', swallowed.length ? `${swallowed.length} 个吞了` : `${catches.length} 个都可见`)

  // ── C8 离线兜底显式标注 ──
  ok(/OFFLINE_ROWS/.test(tsx), 'C8 存在离线兜底清单')
  ok(/offline\s*&&/.test(tsx), 'C8 离线状态有显式可见标注')
  ok(/未经(核对|校验)/.test(tsx), 'C8 兜底被明确声明为「未核对」')

  // ── C9 上传上限两端一致 ──
  const num = (s, re) => { const m = s.match(re); return m ? Number(m[1]) * 2 ** Number(m[2]) : NaN }
  const goC = num(go, /bofsMaxSrcBytes\s*=\s*(\d+)\s*<<\s*(\d+)/)
  const goO = num(go, /bofsMaxObjBytes\s*=\s*(\d+)\s*<<\s*(\d+)/)
  const tsC = num(tsx, /MAX_C\s*=\s*(\d+)\s*<<\s*(\d+)/)
  const tsO = num(tsx, /MAX_O\s*=\s*(\d+)\s*<<\s*(\d+)/)
  ok(Number.isFinite(goC) && Number.isFinite(tsC) && goC === tsC, 'C9 .c 上限两端一致', `go=${goC} ts=${tsC}`)
  ok(Number.isFinite(goO) && Number.isFinite(tsO) && goO === tsO, 'C9 .o 上限两端一致', `go=${goO} ts=${tsO}`)

  return out
}

// ──────────────────────────────────────────────── 正式运行
const tsxSrc = fs.readFileSync(TSX_PATH, 'utf8')
let goSrc = ''
try {
  goSrc = fs.readFileSync(GO_PATH, 'utf8')
} catch (e) {
  console.error(`读不到服务端源码：${GO_PATH}\n  ${e.message}`)
  console.error('  （本脚本的核心是两端对齐，缺了服务端就等于空转——直接失败，不降级。）')
  process.exit(2)
}

console.log(`前端：${path.relative(WEB, TSX_PATH)}`)
console.log(`服务端：${GO_PATH}\n`)
const r = runChecks(tsxSrc, goSrc)
for (const l of r.lines) console.log(l)
console.log(`\n判据 ${r.checks - r.fails}/${r.checks} 通过`)

// ──────────────────────────────────────────────── C10 自检：判据必须能判红
// 拿「退化版」跑一遍：把服务端基路径抄回去、把一个 category 写错、把 list 端点换掉。
// 如果这些退化都判不出来，说明判据是恒真的 —— 那它就没有价值。
console.log('\n[C10] 自检：对退化版必须判红')
// ★ 变异一律用 replaceAll（字符串形式即全局）：只换第一处的话，若文件里还有
//   第二处同字样，退化就被掩盖，自检会误报「判据恒真」——反过来也一样会误导。
const mutated = [
  ['抄回硬编码数组', tsxSrc.replaceAll('const META', "const BOFS = [{ file: 'host_recon.o' }]\nconst META")],
  ['改掉 list 端点', tsxSrc.replaceAll('/api/bofs/list', '/api/bofs/nope')],
  ['只在注释里提端点', tsxSrc.replaceAll("api.get<any>('/api/bofs/list')", 'null /* /api/bofs/list */')],
  ['抄回 DEFAULT_BASE', tsxSrc.replaceAll('const BASE_KEY', "const DEFAULT_BASE = '/root/cc/run/x/'\nconst BASE_KEY")],
  ['分类键不闭合', tsxSrc.replaceAll("category: 'recon', desc: '主机信息汇总", "category: 'reconn', desc: '主机信息汇总")],
  ['catch 空吞', tsxSrc.replaceAll(/catch \(e: any\) \{\n      pushToast\('err', String\(e\?\.message \|\| e\)\)/g,
    "catch (e: any) {\n      pushToast('err', 'failed')")],
  ['上传上限脱节', tsxSrc.replaceAll('const MAX_C = 1 << 20', 'const MAX_C = 8 << 20')],
]
let selftestBad = 0
for (const [label, src] of mutated) {
  if (src === tsxSrc) { console.log(`  ✗ 自检「${label}」没改到源码（变异无效，判据未受检）`); selftestBad++; continue }
  const m = runChecks(src, goSrc)
  if (m.fails === 0) { console.log(`  ✗ 自检「${label}」未判红 ⇒ 相关判据恒真`); selftestBad++ }
  else console.log(`  ✓ 自检「${label}」判红 ${m.fails} 条`)
}
ok(selftestBad === 0, 'C10 全部变异都能被判出来（判据非恒真）', selftestBad ? `${selftestBad} 个变异没被判出` : `${mutated.length} 个变异`)

console.log(`\n合计 ${checks - fails}/${checks} 通过`)
process.exit(fails ? 1 : 0)

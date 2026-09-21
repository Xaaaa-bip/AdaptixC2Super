#!/usr/bin/env node
/* 验收：BOF 参数编码 + META 表完整性。**纯离线，不联网、不登录、不碰主机。**
 *
 * 为什么需要它（而不是只靠 `npm run build` 绿）：
 *   参数流的编码错误在本项目里是**静默**的。`BeaconDataParse` 无条件跳过头 4 字节，
 *   少了这 4 字节不会报错——BOF 照跑、照回报，只是每个参数都读到 NULL，表现出来
 *   是「BOF 打出了自己的 usage」。ISS-005 就是这么活到今天的。这类缺陷
 *   **只能靠逐字节断言抓**，编译通过完全说明不了任何事。
 *
 * 判据：
 *   A 组 —— 参数编码：字段个数头、字符串长度前缀、整数裸 4 字节、多值展开、空值语义
 *   B 组 —— META 表：26 条齐全、分类闭合、每条都有 notes、参数表自洽
 *   C 组 —— 自检：把编码改回「无头」的旧写法，A 组必须判红（判据非常真）
 *
 * 覆盖面（诚实声明）：**不**验证渲染像素、不启动浏览器。
 *   浏览器侧见 scripts/verify_bof_meta.mjs。真链路（真下发）见运行态验收记录。
 *
 * 用法（在 AdaptixWeb/ 下）: node scripts/verify_bof_args.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import esbuild from 'esbuild'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const WEB = path.join(__dirname, '..')
const TSX_PATH = path.join(WEB, 'src', 'components', 'Bofs.tsx')
const src = fs.readFileSync(TSX_PATH, 'utf8')

let fails = 0
let checks = 0
const ok = (cond, label, extra = '') => {
  checks++
  if (!cond) fails++
  console.log(`${cond ? '  ok  ' : ' FAIL '} ${label}${extra ? `\n        ${extra}` : ''}`)
}

/** 从源码里按花括号配平切出一个函数体。切不出来就抛——绝不返回空串让判据空转。 */
function extractBody(source, header) {
  const i = source.indexOf(header)
  if (i < 0) throw new Error(`源码里找不到 ${JSON.stringify(header)}（判据会空转，直接失败）`)
  const open = source.indexOf('{', i)
  let depth = 0
  for (let j = open; j < source.length; j++) {
    const c = source[j]
    if (c === '{') depth++
    else if (c === '}') {
      depth--
      if (depth === 0) return source.slice(open + 1, j)
    }
  }
  throw new Error(`${header} 的花括号不配平`)
}

/** 切出 `const NAME: 类型 = 字面量` 这条完整声明（含类型标注），交给 esbuild 剥类型。
 *
 *  不能只找第一个 `{`：`const CATS: { key: Cat; ... }[] = [` 的第一个花括号是
 *  **类型标注**不是字面量 —— 拿它去配平会切出一段类型，eval 报语法错。
 *  所以先找**深度 0 的 `=`**（两侧的类型标注各自配平回 0），再从它后面的
 *  第一个 `[`/`{` 开始配平。 */
function extractDecl(source, name) {
  const start = source.indexOf(`const ${name}`)
  if (start < 0) throw new Error(`源码里找不到 const ${name}`)
  let depth = 0
  let inStr = null
  let eq = -1
  for (let j = start; j < source.length; j++) {
    const c = source[j]
    if (inStr) {
      if (c === '\\') { j++; continue }
      if (c === inStr) inStr = null
      continue
    }
    if (c === "'" || c === '"' || c === '`') { inStr = c; continue }
    if (c === '/' && source[j + 1] === '/') { const nl = source.indexOf('\n', j); j = nl < 0 ? source.length : nl; continue }
    if (c === '/' && source[j + 1] === '*') { const e = source.indexOf('*/', j); j = e < 0 ? source.length : e + 1; continue }
    if (c === '{' || c === '[' || c === '(') depth++
    else if (c === '}' || c === ']' || c === ')') depth--
    else if (c === '=' && depth === 0) { eq = j; break }
  }
  if (eq < 0) throw new Error(`const ${name} 找不到深度 0 的 =`)
  const open = source.slice(eq).search(/[[{]/)
  if (open < 0) throw new Error(`const ${name} 的 = 后面没有字面量`)
  const from = eq + open
  const closer = { '[': ']', '{': '}' }[source[from]]
  depth = 0
  inStr = null
  for (let j = from; j < source.length; j++) {
    const c = source[j]
    if (inStr) {
      if (c === '\\') { j++; continue }
      if (c === inStr) inStr = null
      continue
    }
    if (c === "'" || c === '"' || c === '`') { inStr = c; continue }
    if (c === '/' && source[j + 1] === '/') { const nl = source.indexOf('\n', j); j = nl < 0 ? source.length : nl; continue }
    if (c === '{' || c === '[' || c === '(') depth++
    else if (c === '}' || c === ']' || c === ')') {
      depth--
      if (depth === 0 && c === closer) return source.slice(start, j + 1)
    }
  }
  throw new Error(`const ${name} 的字面量不配平`)
}

/** eval 一条 const 声明并取回它的值（剥掉 TS 类型标注后）。 */
function evalDecl(source, name) {
  const js = toJs(extractDecl(source, name))
  // eslint-disable-next-line no-new-func
  return new Function(`${js}\nreturn ${name}`)()
}

/** TS 函数体 → 可 eval 的 JS。用 esbuild 而不是手写正则去类型标注：
 *  手写剥标注会在泛型/箭头函数返回类型上悄悄剥错，而剥错了的后果是
 *  「测的不是发布的那份代码」——比不测更坏。 */
const toJs = (tsBody) => esbuild.transformSync(tsBody, { loader: 'ts', target: 'es2020' }).code

// ─────────────────────────────────── 被测函数：直接从源码切，不重写一份
const packTs = extractBody(src, 'export function packBofArgs(')
const packSrc = toJs(packTs)
// eslint-disable-next-line no-new-func
const packBofArgs = new Function('items', packSrc)

console.log(`被测源码：${path.relative(WEB, TSX_PATH)}`)
console.log(`packBofArgs 函数体 ${packTs.length} 字节（剥类型标注后 ${packSrc.length} 字节）\n`)

// ─────────────────────────────────── 独立实现的解码端
// 故意**不**复用上面的打包逻辑：两边同源就等于自己证明自己。
const u32 = (b, i) => (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0

/** 照 bof_api.c 的 BeaconDataParse / Extract / Int 逐条实现。
 *  kinds 是**BOF 自己**才知道的类型序列（Extract 读长度前缀，Int 读裸 4 字节），
 *  所以必须由调用方给出——这正说明类型信息不在流里，页面的 kind 标注是必需的。 */
function beaconParse(blob, kinds) {
  const count = u32(blob, 0)
  let p = 4
  const out = []
  for (const k of kinds) {
    if (k === 'int') {
      if (p + 4 > blob.length) throw new Error(`字段 ${out.length} 越界（Int）`)
      out.push(u32(blob, p))
      p += 4
    } else {
      if (p + 4 > blob.length) throw new Error(`字段 ${out.length} 越界（len）`)
      const n = u32(blob, p)
      p += 4
      if (p + n > blob.length) throw new Error(`字段 ${out.length} 越界（内容 ${n} > 剩余 ${blob.length - p}）`)
      out.push(new TextDecoder().decode(blob.subarray(p, p + n)))
      p += n
    }
  }
  return { count, fields: out, consumed: p }
}

const enc = (s) => new TextEncoder().encode(s)
const b64 = (u8) => Buffer.from(u8).toString('base64')

console.log('=== A. 参数编码 ===')

// A1 顶级不变量：blob 头 4 字节 == 字段个数。
//    这是 ISS-005 的唯一判据，也是本页此前唯一缺的东西。
{
  const blob = packBofArgs([{ kind: 'str', value: '/bin/id' }, { kind: 'str', value: '-u' }])
  ok(blob.length >= 4 && u32(blob, 0) === 2, 'A1 blob 以 4 字节 LE 字段个数开头（ISS-005）',
    `head4=${u32(blob, 0)} 期望 2，共 ${blob.length} 字节`)
}
// A2 字符串字段 = [4B len][bytes]，用独立解码器往返
{
  const want = ['/tmp/hide.insns.bin', 'p:ax/x', 'a b']
  const blob = packBofArgs(want.map((v) => ({ kind: 'str', value: v })))
  const got = beaconParse(blob, want.map(() => 'str'))
  ok(got.count === want.length && JSON.stringify(got.fields) === JSON.stringify(want),
    'A2 字符串字段是 [4B 长度][UTF-8]，往返一致', `期望 ${JSON.stringify(want)}\n        得到 ${JSON.stringify(got.fields)}`)
  ok(got.consumed === blob.length, 'A2 无多余字节（解码器走到流末尾）', `消费 ${got.consumed} / 共 ${blob.length}`)
}
// A3 整数 = 裸 4 字节，**没有长度前缀**。
//    这条必须单独判：把 Int 也加长度前缀的话，字段数一样、总长只差 8 字节，
//    用「能不能解出 count」是判不出来的。
{
  const blob = packBofArgs([{ kind: 'int', value: '5' }, { kind: 'int', value: '1' }, { kind: 'str', value: 'ax' }])
  ok(blob.length === 4 + 4 + 4 + 4 + 2, 'A3 整数是裸 4 字节（前两个字段共 8 字节，无长度前缀）',
    `总长 ${blob.length}，期望 ${4 + 4 + 4 + 4 + 2}`)
  const got = beaconParse(blob, ['int', 'int', 'str'])
  ok(JSON.stringify(got.fields) === JSON.stringify([5, 1, 'ax']),
    'A3 ebpf_load 的 prog_type/attach 按整数解出', JSON.stringify(got.fields))
}
// A4 混合流：整数在中间，后面的字符串仍能对齐。
//    这是 ebpf_load 的真实形状（2 串 + 2 整数 + 5 串）；错一个字节后面全错位。
{
  const items = [
    { kind: 'str', value: '/tmp/h.insns.bin' },
    { kind: 'str', value: '/tmp/h.mapreloc.txt' },
    { kind: 'int', value: '5' },
    { kind: 'int', value: '1' },
    { kind: 'str', value: 'syscalls/sys_exit_getdents64' },
    { kind: 'str', value: 'sys-update' },
    { kind: 'str', value: '8443,9001' },
    { kind: 'str', value: 'k' },
    { kind: 'str', value: 'ax-rootkit' },
  ]
  const blob = packBofArgs(items)
  const got = beaconParse(blob, items.map((i) => i.kind))
  const want = items.map((i) => (i.kind === 'int' ? Number(i.value) : i.value))
  ok(got.count === 9 && JSON.stringify(got.fields) === JSON.stringify(want),
    'A4 混合流（2 串 + 2 整数 + 5 串）九字段全部对齐', JSON.stringify(got.fields))
}
// A5 空值 = 字段不存在，而不是「一个空字符串字段」。
//    页面允许留空的可选参数（mapreloc / target / names…）全靠这条。
{
  const blob = packBofArgs([
    { kind: 'str', value: '/tmp/a' },
    { kind: 'str', value: '   ' },
    { kind: 'str', value: '' },
    { kind: 'str', value: 'tail' },
  ])
  const got = beaconParse(blob, ['str', 'str'])
  ok(got.count === 2 && JSON.stringify(got.fields) === JSON.stringify(['/tmp/a', 'tail']),
    'A5 空白值被跳过（可选参数留空 = 少一个字段，不是空字符串字段）',
    `count=${got.count} fields=${JSON.stringify(got.fields)}`)
}
// A6 整数参数写非数字必须**抛**，不能静默变成 NaN/0。
//    NaN|0 = 0，发出去就是个合法的整数 0，BOF 会拿着 prog_type=0 去加载。
{
  let threw = ''
  try { packBofArgs([{ kind: 'int', value: 'five' }]) } catch (e) { threw = String(e.message || e) }
  ok(/需要一个整数/.test(threw), 'A6 整数框填非数字会抛错（不静默变成 0）', `抛出：${JSON.stringify(threw)}`)
}
// A7 大参数不爆栈：页面的 base64 是分块的，这里确认打包本身也是 O(n) 的数组 push
{
  const big = 'A'.repeat(300000)
  let len = 0
  try { len = packBofArgs([{ kind: 'str', value: big }]).length } catch (e) { len = -1 }
  ok(len === 4 + 4 + 300000, 'A7 30 万字节的单个字段正常打包（无展开式爆栈）', `长度 ${len}`)
}
// A8 btoa 那一侧是分块的（bytesToB64），不是 String.fromCharCode(...arr)
{
  ok(/function bytesToB64/.test(src) && /CH\s*=\s*0x8000/.test(src),
    'A8 base64 编码分块（4MB 的 .o 一次性展开会 stack overflow）')
}

console.log('\n=== C. 自检：把编码改回旧写法必须判红 ===')
{
  // 变异：去掉字段个数头（正是本页修复前的写法）
  const mutated = packSrc.replace('[0, 0, 0, 0]', '[]')
    .replace(/out\[0\] = n & 0xff[\s\S]*?out\[3\] = \(n >>> 24\) & 0xff/, '')
  let bad = 0
  if (mutated === packSrc) { console.log('  FAIL 变异没改到源码（自检无效）'); bad++ }
  else {
    // eslint-disable-next-line no-new-func
    const oldPack = new Function('items', mutated)
    const blob = oldPack([{ kind: 'str', value: '/bin/id' }])
    let decoded = null
    try { decoded = beaconParse(blob, ['str']).fields } catch { decoded = null }
    if (decoded && decoded[0] === '/bin/id') { console.log('  FAIL 去掉头之后仍能解出 ⇒ A1 判据恒真'); bad++ }
    else console.log(`  ok   去掉字段个数头后解出 ${JSON.stringify(decoded)}（≠ /bin/id）⇒ A1 有区分度`)
  }
  ok(bad === 0, 'C 编码判据非常真（能判出本页修复前的写法）')
}

console.log('\n=== B. META 表 ===')
const META = evalDecl(src, 'META')
const CATS = evalDecl(src, 'CATS')
const names = Object.keys(META)

// B0 真正的「不漂移」判据：META 的键集必须与磁盘上的 src_agent/bofs/*.c 逐名相等。
//    只判个数是不够的 —— 个数对而名字错（写错一个、漏一个、多一个）会直接
//    表现为「某个模块永远显示未登记、另一个模块永远不出现」，而个数判据全绿。
const BOFS_DIR = path.join(WEB, '..', 'AdaptixServer', 'extenders', 'linux_agent', 'src_agent', 'bofs')
let cFiles = []
try {
  cFiles = fs.readdirSync(BOFS_DIR).filter((f) => f.endsWith('.c')).map((f) => f.slice(0, -2)).sort()
} catch (e) {
  console.error(`读不到 BOF 源码目录：${BOFS_DIR}\n  ${e.message}\n  （这条判据是「本地 META 与服务端源码不漂移」的全部依据，缺了它不如不跑。）`)
  process.exit(2)
}
const metaSorted = [...names].sort()
const onlyMeta = metaSorted.filter((n) => !cFiles.includes(n))
const onlySrc = cFiles.filter((n) => !metaSorted.includes(n))
ok(cFiles.length > 0, 'B0 读到服务端 BOF 源码清单（防空判据）', `${cFiles.length} 个 .c`)
ok(onlyMeta.length === 0 && onlySrc.length === 0, 'B0 META 键集 == 服务端 bofs/*.c 名单（逐名）',
  onlyMeta.length || onlySrc.length
    ? `META 独有 ${JSON.stringify(onlyMeta)}；源码独有 ${JSON.stringify(onlySrc)}（后者会在页面上显示为「未登记」）`
    : `${cFiles.length} 个名字一一对应`)

const catKeys = new Set(CATS.map((c) => c.key))
ok(catKeys.has('stealth'), 'B2 新增「隐匿 · 撤离」分类存在', CATS.map((c) => c.key).join(' / '))
ok(!CATS.some((c) => /未登记/.test(c.label)),
  'B2 分类名不再承担「登记状态」（未登记改由每卡徽标表达）', CATS.map((c) => c.label).join(' / '))

const badCat = names.filter((n) => !catKeys.has(META[n].category))
ok(badCat.length === 0, 'B3 每条 META 的 category 都在 CATS 里（否则卡片永不渲染）', badCat.join(', ') || '全部闭合')

const noNotes = names.filter((n) => !Array.isArray(META[n].notes) || META[n].notes.length === 0)
ok(noNotes.length === 0, 'B4 每条 META 都有 notes（实战落地用法）', noNotes.length ? `缺：${noNotes.join(', ')}` : `共 ${names.reduce((a, n) => a + META[n].notes.length, 0)} 条`)

const noStar = names.filter((n) => META[n].notes.length < 1)
ok(noStar.length === 0, 'B4 每条至少一条用法提示')
// ★ 项的存在性：不要求每条都有（不是每个模块都有不可逆后果），
// 但整体必须存在——否则说明「★ = 不可逆/静默失败」这个约定被写丢了。
const starCount = names.reduce((a, n) => a + META[n].notes.filter((t) => t.startsWith('★')).length, 0)
ok(starCount >= 10, 'B4 ★ 标记项存在且成规模（约定：不可逆或静默失败的那一条）', `${starCount} 条 ★`)

// 参数表自洽
const pbad = []
for (const n of names) {
  for (const p of META[n].params || []) {
    if (!p.key) pbad.push(`${n}: 缺 key`)
    if (!p.label) pbad.push(`${n}.${p.key}: 缺 label`)
    if (typeof p.placeholder !== 'string') pbad.push(`${n}.${p.key}: 缺 placeholder`)
    if (p.kind && p.kind !== 'int' && p.kind !== 'str') pbad.push(`${n}.${p.key}: kind=${p.kind} 非法`)
    const nFlags = [p.multi, p.nulJoin, p.flag, p.kind === 'int'].filter(Boolean).length
    if (nFlags > 1) pbad.push(`${n}.${p.key}: multi/nulJoin/flag/int 只能取一个（现在 ${nFlags} 个）`)
    if (p.kind === 'int' && !/^-?\d+$/.test(p.placeholder)) pbad.push(`${n}.${p.key}: 整数参数的 placeholder ${JSON.stringify(p.placeholder)} 不是整数`)
  }
  const keys = (META[n].params || []).map((p) => p.key)
  if (new Set(keys).size !== keys.length) pbad.push(`${n}: 参数 key 重复`)
}
ok(pbad.length === 0, 'B5 参数表自洽（key/label/placeholder 齐全，编码标志互斥）', pbad.join(' | ') || '26 条全部合规')

// 表格里承诺的用法必须真的能被打包出来：拿 ebpf_load 的九个参数走一遍真实编码
{
  const m = META.ebpf_load
  const items = (m.params || []).map((p) => ({ kind: p.kind === 'int' ? 'int' : 'str', value: p.placeholder }))
  const blob = packBofArgs(items)
  const got = beaconParse(blob, items.map((i) => i.kind))
  ok(got.count === 9, 'B6 ebpf_load 参数表展开成 9 个字段（与 .c 的 9 个位置参数一致）',
    got.count === 9 ? '' : `实际 ${got.count}`)
  ok(got.fields[2] === 5 && got.fields[3] === 1, 'B6 第三/四个字段按整数解出（prog_type / attach）', JSON.stringify(got.fields.slice(0, 4)))
}
// 多值参数的展开：hide_add 的 names 是 N 个字段，不是 1 个
{
  const blob = packBofArgs([
    { kind: 'str', value: 'ax-rootkit' },
    { kind: 'str', value: 'sys-update' },
    { kind: 'str', value: 'sys-maintenance' },
  ])
  const got = beaconParse(blob, ['str', 'str', 'str'])
  ok(got.count === 3, 'B6 hide_add 的多值名展开成独立字段（pin + N 个名字）', JSON.stringify(got.fields))
}

console.log(`\n──────── ${checks - fails}/${checks} 通过 ────────`)
if (fails) console.log('未通过项见上方 FAIL 行')
process.exit(fails ? 1 : 0)

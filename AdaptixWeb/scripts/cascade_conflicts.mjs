#!/usr/bin/env node
/* 列一份「设计层 vs styles.css 主体」的对照表。

   背景：@import 会被内联到 styles.css 顶部，所以 design/*.css 全部排在
   styles.css 主体规则【之前】。同特异度下后写的赢 —— 也就是 styles.css 赢。
   加了 html:root[data-theme="dark"] 前缀的规则能靠特异度抢回来，
   裸选择器的抢不回来，于是 ops-hard.css 里的布局基本是摆设。

   这个脚本找出所有「选择器文本相同 + 特异度相同 + 属性同名但值不同」的对，
   并按 布局 / 上色 分类：布局类要逐个上真机验证，上色类一般可以直接翻转。

   用法: node scripts/cascade_conflicts.mjs [--layout|--paint|--all] */
import { readFileSync, readdirSync } from 'node:fs'

const LAYOUT = new Set(['display', 'grid-template-columns', 'grid-template-rows', 'grid-template-areas',
  'grid-column', 'grid-row', 'flex', 'flex-direction', 'flex-wrap', 'align-items', 'align-self',
  'justify-content', 'justify-self', 'gap', 'row-gap', 'column-gap', 'position', 'top', 'right',
  'bottom', 'left', 'inset', 'width', 'min-width', 'max-width', 'height', 'min-height', 'max-height',
  'padding', 'padding-top', 'padding-right', 'padding-bottom', 'padding-left', 'margin', 'margin-top',
  'margin-right', 'margin-bottom', 'margin-left', 'overflow', 'overflow-x', 'overflow-y', 'order',
  'aspect-ratio', 'place-items', 'place-content'])

/* 递归解析，跳过 @media / @supports / @keyframes 内部 —— 那些不是同层级的冲突，
   之前不跳导致把 @media(max-width:720px) 里的 48px 误报成冲突。 */
function parse(src) {
  const out = []
  const clean = src.replace(/\/\*[\s\S]*?\*\//g, '')
  let i = 0
  const walk = (end, depth) => {
    while (i < clean.length) {
      while (i < clean.length && /\s/.test(clean[i])) i++
      if (i >= clean.length) break
      if (clean[i] === '}') { i++; if (depth > 0) return }
      const at = clean.indexOf('{', i)
      if (at < 0) break
      const head = clean.slice(i, at).trim()
      const close = clean.indexOf('}', at)
      // 找匹配的右括号
      let j = at, d = 0
      while (j < clean.length) { if (clean[j] === '{') d++; else if (clean[j] === '}') { d--; if (!d) break } j++ }
      const body = clean.slice(at + 1, j)
      i = j + 1
      if (head.startsWith('@')) {
        if (/^@(media|supports|container|layer)/.test(head)) {
          const save = i; const saveEnd = i
          // 在块内继续解析（把内部规则当独立规则，但不标注 at-rule 包裹 —— 这里只取顶层）
          // 简化：媒体查询内的规则不计入「同层级冲突」，直接跳过
          void body; void save; void saveEnd
        }
        continue
      }
      const sels = head.split(',').map((s) => s.trim()).filter(Boolean)
      const decls = {}
      for (const d0 of body.split(';')) {
        const k = d0.indexOf(':')
        if (k > 0) decls[d0.slice(0, k).trim()] = d0.slice(k + 1).trim()
      }
      if (Object.keys(decls).length) sels.forEach((s) => out.push({ sel: s, decls }))
      void close
    }
  }
  walk(clean.length, 0)
  return out
}

const spec = (s) => {
  const id = (s.match(/#[\w-]+/g) || []).length
  const cls = (s.match(/\.[\w-]+|\[[^\]]+\]|:(?!:)[\w-]+(\([^)]*\))?/g) || []).length
  const el = (s.match(/(^|[\s>+~])[a-zA-Z][\w-]*/g) || []).length
  return [id, cls, el]
}
const sameSpec = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2]

const base = parse(readFileSync('src/styles.css', 'utf8'))
const design = readdirSync('src/design').filter((f) => f.endsWith('.css'))
  .flatMap((f) => parse(readFileSync('src/design/' + f, 'utf8')).map((r) => ({ ...r, file: f })))

/* tokens.css 里有一批 legacy 别名（--bg: var(--paper) 之类）。不解析的话
   styles.css 写 var(--bg)、design 层写 var(--paper) 会被当成冲突，
   其实算出来是同一个颜色 —— 之前那一版扫描出来的 50 处里大半是这种噪音。 */
const TOKENS = (() => {
  const src = readFileSync('src/design/tokens.css', 'utf8').replace(/\/\*[\s\S]*?\*\//g, '')
  const m = {}
  for (const d of src.split(';')) {
    const i = d.indexOf(':')
    if (i < 0) continue
    const k = d.slice(0, i).trim().replace(/^.*\{/, '').trim()
    if (k.startsWith('--')) m[k] = d.slice(i + 1).trim()
  }
  return m
})()
// var(--x) 反复代换到不再是别名；找不到的保持原样
const resolve = (v, depth = 0) => {
  if (depth > 6) return v
  return v.replace(/var\((--[\w-]+)(?:\s*,\s*([^)]*))?\)/g, (whole, name, fallback) => {
    const t = TOKENS[name]
    if (t === undefined) return whole
    // 值本身还是 var() 就继续往里钻；否则把名字换掉，颜色字面量直接内联
    const inner = /^var\(/.test(t) ? t : (/^#[0-9a-f]{3,8}$/i.test(t) || /^rgba?\(/i.test(t) ? t : `var(${name})`)
    return resolve(inner, depth + 1)
  })
}
// 颜色字面量统一小写、去空格，方便比
const norm = (v) => resolve(v).replace(/\s+/g, '').replace(/(#[0-9a-f]{6})[0-9a-f]{2}\b/gi, '$1').toLowerCase()

const mode = process.argv[2] || '--all'
const hits = []
for (const b of base) {
  for (const d of design) {
    if (b.sel !== d.sel) continue
    if (!sameSpec(spec(b.sel), spec(d.sel))) continue
    const props = Object.keys(b.decls).filter((k) => k in d.decls && norm(b.decls[k]) !== norm(d.decls[k]))
    if (!props.length) continue
    hits.push({ sel: b.sel, file: d.file, props: props.map((k) => ({ k, design: d.decls[k], real: b.decls[k] })) })
  }
}
const seen = new Set()
const uniq = hits.filter((h) => { const k = h.sel; if (seen.has(k)) return false; seen.add(k); return true })

const isLayout = (h) => h.props.some((p) => LAYOUT.has(p.k))
const filtered = uniq.filter((h) => mode === '--all' ? true : mode === '--layout' ? isLayout(h) : !isLayout(h))

let n = 0
for (const h of filtered) {
  const kind = isLayout(h) ? '布局' : '上色'
  console.log(`\n[${kind}] ${h.sel}   (design/${h.file}  ←被压掉 | styles.css 生效)`)
  for (const p of h.props) console.log(`    ${p.k}:  ${p.design}   →   ${p.real}`)
  n++
}
console.log(`\n合计 ${n} 处（布局 ${uniq.filter(isLayout).length} / 上色 ${uniq.filter((h) => !isLayout(h)).length}）`)

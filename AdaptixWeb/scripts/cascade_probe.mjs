#!/usr/bin/env node
/* 冲突清单的真机对照：对每个可疑选择器报
     - 实际计算值（到底渲染成什么）
     - 命中该属性的所有规则（值 + 特异度估算 + 源顺序），用来确认是谁赢的
     - 尺寸 / 是否溢出 / 是否可见

   静态扫描（cascade_conflicts.mjs）只能说明「设计层与 styles.css 写法不同」，
   不能说明「实际渲染成哪个」—— 因为可能还有第三条带 html:root[data-theme] 前缀的
   规则插在中间赢了。这个脚本量的是最终结果。

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/cascade_probe.mjs */

import { chromium } from 'playwright'

/* 选择器 → 关心的属性。属性取自 cascade_conflicts.mjs --layout 的输出。 */
const TARGETS = {
  '.ops': ['grid-template-columns', 'grid-template-rows', 'min-height'],
  '.ops-top': ['grid-template-columns', 'padding-left', 'padding-right', 'column-gap'],
  '.ops-tabs': ['column-gap', 'padding-left', 'padding-right', 'background-color'],
  '.ops-tab': ['color', 'padding-left', 'padding-right', 'font-size'],
  '.ops-search': ['width', 'height', 'background-color', 'border-radius'],
  '.ops-pill': ['font-size', 'background-color', 'padding-left', 'padding-right'],
  '.ops-target': ['font-size', 'padding-left', 'padding-right', 'background-color'],
  '.ops-user': ['font-size', 'font-weight', 'max-width'],
  '.rail-brand': ['color', 'padding-top', 'padding-bottom', 'background-color', 'border-radius'],
  '.rail-foot': ['padding-top', 'padding-bottom', 'border-top-color', 'border-top-width'],
  '.rail-badge': ['border-radius', 'background-color', 'color', 'display', 'font-size'],
  '.kbd': ['padding-left', 'padding-right', 'background-color', 'font-size'],
  '.rail-item': ['width', 'height', 'padding-left'],
  '.sdot': ['width', 'height'],
}

/* ::after 这类伪元素单独标出来 */
const PSEUDO = { '.ops-tab.active::after': ['height', 'background-image', 'border-radius', 'opacity'] }

const b = await chromium.launch()
const ctx = await b.newContext({
  viewport: { width: 1600, height: 950 },
  deviceScaleFactor: 1,
  ignoreHTTPSErrors: true,
})
const page = await ctx.newPage()
if (!process.env.ADAPTIX_USER || !process.env.ADAPTIX_PASS) {
  console.error('需要 ADAPTIX_USER / ADAPTIX_PASS 环境变量')
  process.exit(2)
}
await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(4000)

const report = await page.evaluate(({ TARGETS, PSEUDO }) => {
  /* 特异度估算：[id, class/attr/pseudo-class, element] */
  const spec = (s) => {
    const id = (s.match(/#[\w-]+/g) || []).length
    const cls = (s.match(/\.[\w-]+|\[[^\]]+\]|:(?!:)[\w-]+(\([^)]*\))?/g) || []).length
    const el = (s.match(/(^|[\s>+~])[a-zA-Z][\w-]*/g) || []).length
    return id * 10000 + cls * 100 + el
  }
  /* 把所有样式表的规则拉平，带源顺序序号 */
  const flat = []
  let order = 0
  const walk = (rules, sheetHref) => {
    for (const r of rules) {
      if (r.cssRules) { walk(r.cssRules, sheetHref); continue }
      if (!r.selectorText) continue
      const decls = {}
      for (let i = 0; i < r.style.length; i++) {
        /* CSSStyleDeclaration 的索引取值在 Blink 里是 camelCase（backgroundColor），
           而 getPropertyValue 只认 kebab-case。第一版没转，于是 decls 的键全是
           camelCase，后面按 'background-color' in decls 过滤永远为假 ——
           表现是 hits 数组全空，溯源一段都打不出来。统一转 kebab 存。 */
        const p = r.style[i].replace(/[A-Z]/g, (c) => '-' + c.toLowerCase())
        decls[p] = r.style.getPropertyValue(p) + (r.style.getPropertyPriority(p) ? ' !important' : '')
      }
      flat.push({ sel: r.selectorText, decls, order: order++, href: sheetHref })
    }
  }
  for (const sh of document.styleSheets) {
    let rules
    try { rules = sh.cssRules } catch { continue }
    walk(rules, (sh.href || '').split('/').pop())
  }

  const camel = (p) => p.replace(/-([a-z])/g, (_, c) => c.toUpperCase())
  const out = []
  const all = { ...TARGETS, ...PSEUDO }

  for (const [sel, props] of Object.entries(all)) {
    const isPseudo = sel.includes('::')
    const baseSel = isPseudo ? sel.split('::')[0] : sel
    const pseudoArg = isPseudo ? '::' + sel.split('::')[1] : null
    const el = document.querySelector(baseSel)
    if (!el) { out.push({ sel, missing: true }); continue }
    const cs = getComputedStyle(el, pseudoArg)
    const r = el.getBoundingClientRect()

    /* 哪些规则命中了这个元素、且设置了我们要看的属性 */
    const hits = []
    for (const f of flat) {
      const owns = props.filter((p) => p in f.decls)
      if (!owns.length) continue
      let matches = false
      try { matches = el.matches(f.sel) } catch { matches = false }
      if (!matches) continue
      hits.push({
        sel: f.sel, order: f.order, href: f.href, spec: spec(f.sel),
        decls: Object.fromEntries(owns.map((p) => [p, f.decls[p]])),
      })
    }
    /* 排序：!important 优先，然后特异度，然后源顺序 */
    hits.sort((a, x) => {
      const ai = Object.values(a.decls).some((v) => v.includes('!important')) ? 1 : 0
      const xi = Object.values(x.decls).some((v) => v.includes('!important')) ? 1 : 0
      return ai - xi || a.spec - x.spec || a.order - x.order
    })

    out.push({
      sel,
      rect: [Math.round(r.x), Math.round(r.y), Math.round(r.width), Math.round(r.height)],
      visible: !!(r.width && r.height) && cs.visibility !== 'hidden' && cs.display !== 'none',
      overflowX: el.scrollWidth > el.clientWidth + 1,
      overflowY: el.scrollHeight > el.clientHeight + 1,
      computed: Object.fromEntries(props.map((p) => [p, cs[camel(p)]])),
      hits,
    })
  }
  return out
}, { TARGETS, PSEUDO })

for (const r of report) {
  if (r.missing) { console.log(`\n### ${r.sel}  —— 不在 DOM 里（可能未打开）`); continue }
  console.log(`\n### ${r.sel}   rect=[${r.rect.join(', ')}]  visible=${r.visible}` +
    (r.overflowX ? '  ⚠横向溢出' : '') + (r.overflowY ? '  ⚠纵向溢出' : ''))
  for (const [k, v] of Object.entries(r.computed)) console.log(`    实际 ${k}: ${v}`)
  for (const h of r.hits) {
    console.log(`      · spec=${h.spec} #${h.order} ${h.sel}`)
    for (const [k, v] of Object.entries(h.decls)) console.log(`          ${k}: ${v}`)
  }
}

await b.close()

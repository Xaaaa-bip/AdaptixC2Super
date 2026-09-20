#!/usr/bin/env node
/* 量「生成参数」里各选项控件的选中态是否看得出来：
   .chip-row button.primary（已选）vs 普通 button（未选），
   .gen-listener.active（已选监听）vs 普通。
   只导航 + 点页内按钮 + 读计算样式 + 截图，不做任何主机操作。 */
import { chromium } from 'playwright'

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, ignoreHTTPSErrors: true })
const page = await ctx.newPage()
await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(5000)
await page.evaluate(() => {
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('投递'))
  if (it) it.click()
})
await page.waitForTimeout(3000)

const styleOf = (el) => {
  const cs = getComputedStyle(el)
  return {
    text: (el.textContent || '').trim(),
    color: cs.color,
    bg: cs.backgroundColor,
    bgImage: cs.backgroundImage === 'none' ? 'none' : cs.backgroundImage.slice(0, 60),
    border: `${cs.borderTopWidth} ${cs.borderTopStyle} ${cs.borderTopColor}`,
    boxShadow: cs.boxShadow === 'none' ? 'none' : cs.boxShadow.slice(0, 70),
    fontWeight: cs.fontWeight,
    opacity: cs.opacity,
  }
}

const report = await page.evaluate(() => {
  const pick = (sel) => [...document.querySelectorAll(sel)]
  const s = (el) => {
    const cs = getComputedStyle(el)
    return {
      text: (el.textContent || '').trim(),
      color: cs.color,
      bg: cs.backgroundColor,
      border: `${cs.borderTopWidth} ${cs.borderTopStyle} ${cs.borderTopColor}`,
      shadow: cs.boxShadow === 'none' ? 'none' : cs.boxShadow.slice(0, 60),
      weight: cs.fontWeight,
    }
  }
  const chips = pick('.gen-params .chip-row button')
  const sel = chips.filter((c) => c.classList.contains('primary'))
  const unsel = chips.filter((c) => !c.classList.contains('primary'))
  const lis = pick('.gen-listener')
  const lisOn = lis.filter((c) => c.classList.contains('active'))
  const lisOff = lis.filter((c) => !c.classList.contains('active'))
  const out = {
    chipFromCSS: null,
    chipSel: sel.map(s),
    chipUnsel: unsel.slice(0, 3).map(s),
    listenerSel: lisOn.map(s),
    listenerUnsel: lisOff.slice(0, 2).map(s),
    counts: { chips: chips.length, sel: sel.length, listeners: lis.length, lisOn: lisOn.length },
  }
  // 直接问样式表：.chip-row button.primary 这条规则到底存不存在、是谁定的
  for (const sheet of document.styleSheets) {
    let rules
    try { rules = sheet.cssRules } catch { continue }
    for (const r of rules) {
      if (!r.selectorText) continue
      if (/chip-row|gen-listener/.test(r.selectorText) && /primary|active/.test(r.selectorText)) {
        out.chipFromCSS = out.chipFromCSS || []
        out.chipFromCSS.push(`${r.selectorText} { ${r.style.cssText.slice(0, 120)} }`)
      }
    }
  }
  return out
})

console.log('选项计数:', JSON.stringify(report.counts))
console.log('\n--- .chip-row button.primary（已选）---')
for (const c of report.chipSel) console.log('  ', JSON.stringify(c))
console.log('\n--- .chip-row button（未选，取前 3）---')
for (const c of report.chipUnsel) console.log('  ', JSON.stringify(c))
console.log('\n--- .gen-listener.active（已选监听）---')
for (const c of report.listenerSel) console.log('  ', JSON.stringify(c))
console.log('\n--- .gen-listener（未选监听，取前 2）---')
for (const c of report.listenerUnsel) console.log('  ', JSON.stringify(c))
console.log('\n--- 命中 primary/active 的 CSS 规则 ---')
for (const r of report.chipFromCSS || []) console.log('  ', r)

await page.screenshot({ path: 'shots/opts-params.png', clip: await page.evaluate(() => {
  const el = document.querySelector('.gen-params')
  const r = el.getBoundingClientRect()
  return { x: Math.max(0, r.x - 12), y: Math.max(0, r.y - 40), width: Math.min(560, r.width + 24), height: Math.min(880, r.height + 60) }
}) })
console.log('\n已存 shots/opts-params.png')
await b.close()

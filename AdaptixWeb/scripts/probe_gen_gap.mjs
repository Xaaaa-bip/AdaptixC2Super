#!/usr/bin/env node
/* 量载荷页 Linux 表单里相邻字段的纵向间距，看 OPSEC 那行是不是异类。只导航 + 量。 */
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
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('载荷'))
  if (it) it.click()
})
await page.waitForTimeout(3000)
await page.evaluate(() => {
  const b = [...document.querySelectorAll('.gen-preset')].find((x) =>
    (x.querySelector('b')?.textContent || '').trim() === 'Linux')
  if (b) b.click()
})
await page.waitForTimeout(2500)

const out = await page.evaluate(() => {
  const main = document.querySelector('.gen-main')
  // 只看 Linux 参数区：从"重连间隔"开始往下
  const all = [...main.querySelectorAll('.field')]
  const start = all.findIndex((f) => (f.textContent || '').includes('重连间隔'))
  const rows = []
  for (let i = start - 1; i < Math.min(start + 6, all.length); i++) {
    const f = all[i]
    if (!f) continue
    const r = f.getBoundingClientRect()
    const cs = getComputedStyle(f)
    const lab = f.querySelector(':scope > label')
    const ctl = f.querySelector(':scope > input, :scope > .chip-row, :scope > .check-inline, :scope > textarea')
    rows.push({
      label: (lab?.textContent || f.className).trim().slice(0, 24),
      display: cs.display,
      marginBottom: cs.marginBottom,
      marginTop: cs.marginTop,
      rowTop: Math.round(r.top),
      rowH: Math.round(r.height),
      ctlH: ctl ? Math.round(ctl.getBoundingClientRect().height) : null,
      tag: f.tagName + '.' + f.className,
    })
  }
  const gaps = []
  for (let i = 1; i < rows.length; i++) {
    gaps.push({
      between: `${rows[i - 1].label} → ${rows[i].label}`,
      gap: rows[i].rowTop - (rows[i - 1].rowTop + rows[i - 1].rowH),
    })
  }
  return { rows, gaps }
})

console.log('字段行：')
for (const r of out.rows) console.log(' ', JSON.stringify(r))
console.log('\n相邻行之间的空隙（上一行底边 → 下一行顶边）：')
for (const g of out.gaps) console.log(`  ${String(g.gap).padStart(3)}px   ${g.between}`)
await b.close()

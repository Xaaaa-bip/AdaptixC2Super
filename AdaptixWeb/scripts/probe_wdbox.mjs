#!/usr/bin/env node
/* 量 .wd-cmd 的盒模型细节，定位 2px 纵向溢出的来源。只导航 + 量。 */
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

const out = await page.evaluate(() => {
  const ta = document.querySelector('.wd-cmd')
  const cs = getComputedStyle(ta)
  const lh = parseFloat(cs.lineHeight)
  const pt = parseFloat(cs.paddingTop)
  const pb = parseFloat(cs.paddingBottom)
  const lines = Math.round((ta.scrollHeight - pt - pb) / lh)
  return {
    boxSizing: cs.boxSizing,
    fontSize: cs.fontSize,
    lineHeight: cs.lineHeight,
    paddingTB: `${cs.paddingTop} / ${cs.paddingBottom}`,
    borderTB: `${cs.borderTopWidth} / ${cs.borderBottomWidth}`,
    minHeight: cs.minHeight,
    heightInline: ta.style.height,
    offsetHeight: ta.offsetHeight,
    clientHeight: ta.clientHeight,
    scrollHeight: ta.scrollHeight,
    rectH: +ta.getBoundingClientRect().height.toFixed(3),
    lines,
    computedContentH: +(lines * lh + pt + pb).toFixed(3),
  }
})
console.log(JSON.stringify(out, null, 2))

// 逐行看每个框的差值
const all = await page.evaluate(() => [...document.querySelectorAll('.wd-cmd')].map((ta) => {
  const cs = getComputedStyle(ta)
  return {
    label: (ta.closest('.wd-block')?.querySelector('.wd-block-hd > label')?.textContent || '').trim(),
    rectH: +ta.getBoundingClientRect().height.toFixed(3),
    clientH: ta.clientHeight,
    scrollH: ta.scrollHeight,
    delta: ta.scrollHeight - ta.clientHeight,
    lh: cs.lineHeight,
  }
}))
console.log('\n逐框：')
for (const r of all) console.log(' ', JSON.stringify(r))

await b.close()

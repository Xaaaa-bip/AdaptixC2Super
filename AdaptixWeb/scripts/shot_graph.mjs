#!/usr/bin/env node
/* 拓扑视图截图（整屏 + 画布左上角特写）。只导航 + 截图。 */
import { chromium } from 'playwright'

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, deviceScaleFactor: 1, ignoreHTTPSErrors: true })
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
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('拓扑'))
  if (it) it.click()
})
await page.waitForTimeout(4000)

await page.screenshot({ path: 'shots/graph-full.png' })
const wrap = await page.$('.graph-wrap')
if (wrap) await wrap.screenshot({ path: 'shots/graph-canvas.png' })
// 画布左上角 1:1 特写，看节点卡片本身
await page.screenshot({ path: 'shots/graph-zoom.png', clip: { x: 250, y: 180, width: 760, height: 560 } })
console.log('已保存 shots/graph-full.png / graph-canvas.png / graph-zoom.png')
await b.close()

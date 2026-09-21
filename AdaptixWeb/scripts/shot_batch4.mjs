#!/usr/bin/env node
/* 第四批截图：拓扑卡片特写 + 主机页（确认 store 的 IP 归一没弄坏别处）。只导航 + 截图。 */
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

const goto = async (label) => {
  await page.evaluate((l) => {
    const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes(l))
    if (it) it.click()
  }, label)
  await page.waitForTimeout(3500)
}

await goto('拓扑')
await page.screenshot({ path: 'shots/b4-graph-canvas.png', clip: { x: 250, y: 180, width: 900, height: 620 } })

await goto('主机')
await page.screenshot({ path: 'shots/b4-hosts.png' })
console.log('已保存 shots/b4-graph-canvas.png / b4-hosts.png')
await b.close()

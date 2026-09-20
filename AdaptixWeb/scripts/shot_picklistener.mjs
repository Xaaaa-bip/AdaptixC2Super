#!/usr/bin/env node
/* 截图：Linux Agent + 选中 gopher 监听（修复后的状态）。只导航 + 点击 + 截图。 */
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

const clickChip = async (t) => {
  await page.evaluate((l) => {
    const row = [...document.querySelectorAll('.field')]
      .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
      ?.querySelector('.chip-row')
    const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === l)
    if (btn) btn.click()
  }, t)
  await page.waitForTimeout(1500)
}

await clickChip('Linux Agent')
await page.evaluate(() => {
  const btn = [...document.querySelectorAll('.gen-listener-list .gen-listener')]
    .find((x) => (x.querySelector('.name')?.textContent || '').trim() === 'gopher')
  if (btn) btn.click()
})
await page.waitForTimeout(1500)

// 左栏上半部分：提示 + Agent 芯片 + 监听列表，正好是这次改动涉及的区域
await page.screenshot({ path: 'shots/fix-picklistener.png', clip: { x: 60, y: 90, width: 1010, height: 760 } })
console.log('已保存 shots/fix-picklistener.png')
await b.close()

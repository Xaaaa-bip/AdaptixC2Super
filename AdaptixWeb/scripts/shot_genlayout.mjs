#!/usr/bin/env node
/* 截图：投递页右栏滚到底时，左栏表单仍在原位（两栏各自滚）。
   存两张对比：右栏 scrollTop=0 与 滚到底。只导航 + 滚动 + 截图。 */
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
await page.evaluate(() => {
  const row = [...document.querySelectorAll('.field')]
    .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
    ?.querySelector('.chip-row')
  const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === 'Gopher')
  if (btn) btn.click()
})
await page.waitForTimeout(2500)

// 右栏顶：C2 Host URL 和第一条命令同屏
await page.evaluate(() => {
  const bd = document.querySelector('.gen-side .card-bd')
  if (bd) bd.scrollTop = 0
})
await page.waitForTimeout(500)
await page.screenshot({ path: 'shots/genlayout-top.png' })

// 右栏滚到底：左栏的 C2 Host URL 应该还在原地
await page.evaluate(() => {
  const bd = document.querySelector('.gen-side .card-bd')
  if (bd) bd.scrollTop = bd.scrollHeight
})
await page.waitForTimeout(500)
await page.screenshot({ path: 'shots/genlayout-bottom.png' })

// 顺带量一下左栏的 C2 Host 输入框在两屏里的位置，确认没动
const pos = await page.evaluate(() => {
  const inp = [...document.querySelectorAll('.gen-main input')]
    .find((i) => (i.value || '').includes('/api'))
  if (!inp) return null
  const r = inp.getBoundingClientRect()
  return { top: Math.round(r.top), visible: r.top >= 0 && r.bottom <= window.innerHeight }
})
console.log('左栏 C2 Host 输入框在右栏滚到底后的位置:', JSON.stringify(pos))
console.log('已保存 shots/genlayout-top.png / genlayout-bottom.png')
await b.close()

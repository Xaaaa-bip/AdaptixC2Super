#!/usr/bin/env node
/* 截图投递页左栏（Agent 选项 + 监听器选项 + 生成参数），beacon / linux 两张。
   只导航 + 点页内 Agent 按钮 + 截图，不点「应用并同步」。 */
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
await page.waitForTimeout(3500)

const shotCard = async (name) => {
  const clip = await page.evaluate(() => {
    const el = document.querySelector('.gen-main .card-bd')
    const r = el.getBoundingClientRect()
    return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.min(Math.round(r.height), 940) }
  })
  // 把监听器 + 参数区滚进视野
  await page.evaluate(() => {
    const el = document.querySelector('.gen-main .card-bd')
    el.scrollTop = 120
  })
  await page.waitForTimeout(400)
  const c2 = await page.evaluate(() => {
    const el = document.querySelector('.gen-main .card-bd')
    const r = el.getBoundingClientRect()
    return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.min(Math.round(r.height), 940) }
  })
  await page.screenshot({ path: `shots/${name}.png`, clip: c2 })
  console.log(`已存 shots/${name}.png`, JSON.stringify(c2))
}

await shotCard('opts-beacon')
await page.evaluate(() => {
  const row = [...document.querySelectorAll('.field')]
    .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
    ?.querySelector('.chip-row')
  const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === 'Linux Agent')
  if (btn) btn.click()
})
await page.waitForTimeout(2000)
await shotCard('opts-linux')
await b.close()

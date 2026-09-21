#!/usr/bin/env node
/* 登录后逐视图截图 —— 只导航 + 截图，不触发任何主机/监听器/任务操作。
   凭据从环境变量读，不落盘：
     ADAPTIX_USER=... ADAPTIX_PASS=... node scripts/console_tour.mjs
   用法: node scripts/console_tour.mjs [url] [tag] */
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'

const URL = process.argv[2] || process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/'
const TAG = process.argv[3] || 'con'
const USER = process.env.ADAPTIX_USER
const PASS = process.env.ADAPTIX_PASS
if (!USER || !PASS) {
  console.error('需要 ADAPTIX_USER / ADAPTIX_PASS 环境变量')
  process.exit(2)
}
mkdirSync('shots', { recursive: true })

const browser = await chromium.launch()
const ctx = await browser.newContext({
  viewport: { width: 1600, height: 950 },
  deviceScaleFactor: 2,
  ignoreHTTPSErrors: true,
})
const page = await ctx.newPage()
const errs = []
page.on('pageerror', (e) => errs.push(String(e.message).slice(0, 160)))
page.on('requestfailed', (r) => {
  const u = r.url()
  if (!/favicon/.test(u)) errs.push('FAILED ' + u.slice(0, 110))
})

await page.goto(URL, { waitUntil: 'load', timeout: 30000 })
await page.waitForTimeout(1500)

// ── 登录 ──
const inputs = await page.$$('.lx-input')
if (inputs.length < 2) { console.error('没找到登录框，页面结构变了？'); await browser.close(); process.exit(3) }
await inputs[0].fill(USER)
await inputs[1].fill(PASS)
await page.click('.lx-submit')
// 等控制台外壳出现
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(4000)

const loggedIn = await page.$('.ops')
if (!loggedIn) {
  await page.screenshot({ path: `shots/${TAG}-LOGIN-FAILED.png` })
  console.log(JSON.stringify({ ok: false, errors: errs, note: '登录后没出现 .ops' }, null, 1))
  await browser.close()
  process.exit(1)
}

await page.screenshot({ path: `shots/${TAG}-00-landing.png` })

// ── 逐视图：只点左侧轨道，不做任何其它交互 ──
const rail = await page.$$('.rail-item')
const labels = await page.evaluate(() =>
  [...document.querySelectorAll('.rail-item .rail-label')].map((e) => e.textContent.trim())
)
const shots = []
for (let i = 0; i < rail.length; i++) {
  const name = (labels[i] || `item${i}`).replace(/[^\w一-龥]/g, '')
  try {
    await rail[i].click({ timeout: 4000 })
  } catch { continue }
  await page.waitForTimeout(1800)
  const f = `shots/${TAG}-${String(i + 1).padStart(2, '0')}-${name}.png`
  await page.screenshot({ path: f })
  shots.push({ i, name, file: f })
}

console.log(JSON.stringify({ ok: true, views: shots, errors: errs }, null, 1))
await browser.close()

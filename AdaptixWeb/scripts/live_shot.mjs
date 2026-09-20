#!/usr/bin/env node
/* 对真实部署做端到端确认：直接打开线上控制台截图 */
import { chromium } from 'playwright'
const URL = process.argv[2] || 'https://154.198.162.34:38212/'
const TAG = process.argv[3] || 'live'
const b = await chromium.launch()
const ctx = await b.newContext({
  viewport: { width: 1600, height: 950 },
  deviceScaleFactor: 2,
  ignoreHTTPSErrors: true,
})
const p = await ctx.newPage()
const errs = []
p.on('pageerror', (e) => errs.push(String(e.message).slice(0, 200)))
p.on('requestfailed', (r) => errs.push('FAILED ' + r.url().slice(0, 120)))
await p.goto(URL, { waitUntil: 'load', timeout: 30000 })
await p.waitForTimeout(3000)
await p.screenshot({ path: `shots/${TAG}.png` })

const info = await p.evaluate(() => {
  const cs = getComputedStyle(document.documentElement)
  const card = document.querySelector('.lx-card')
  const submit = document.querySelector('.lx-submit')
  return {
    title: document.title,
    accent: cs.getPropertyValue('--accent').trim(),
    ink3: cs.getPropertyValue('--ink-3').trim(),
    cardBackdrop: card ? getComputedStyle(card).backdropFilter : '（无 .lx-card）',
    submitBg: submit ? getComputedStyle(submit).backgroundColor : '（无 .lx-submit）',
    cssHref: [...document.styleSheets].map((s) => (s.href || '').split('/').pop()).filter(Boolean),
  }
})
console.log(JSON.stringify({ ...info, errors: errs }, null, 1))
await b.close()

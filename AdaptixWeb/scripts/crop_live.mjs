#!/usr/bin/env node
/* 登录后对线上真实页面做局部特写 + 布局诊断
   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/crop_live.mjs <tag> <sel> [sel2 ...] */
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'

const TAG = process.argv[2] || 'crop'
const SELS = process.argv.slice(3)
mkdirSync('shots', { recursive: true })

const b = await chromium.launch()
const ctx = await b.newContext({
  viewport: { width: 1600, height: 950 },
  deviceScaleFactor: 3,
  ignoreHTTPSErrors: true,
})
const page = await ctx.newPage()
await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(4000)

// 布局诊断：溢出、被视口裁掉、与兄弟元素重叠
const diag = await page.evaluate(() => {
  const out = []
  document.querySelectorAll('body *').forEach((el) => {
    const r = el.getBoundingClientRect()
    if (r.width < 4 || r.height < 4) return
    const cs = getComputedStyle(el)
    if (cs.display === 'none' || cs.visibility === 'hidden') return
    const cls = typeof el.className === 'string' ? el.className.trim().split(/\s+/)[0] : el.tagName
    // 内容溢出自身盒子
    if (el.scrollHeight > el.clientHeight + 2 && cs.overflowY !== 'auto' && cs.overflowY !== 'scroll') {
      out.push({ kind: '溢出-纵向', el: cls, client: el.clientHeight, scroll: el.scrollHeight })
    }
    if (el.scrollWidth > el.clientWidth + 2 && cs.overflowX !== 'auto' && cs.overflowX !== 'scroll') {
      out.push({ kind: '溢出-横向', el: cls, client: el.clientWidth, scroll: el.scrollWidth })
    }
    // 被视口边缘裁掉
    if (r.right > innerWidth + 1 || r.bottom > innerHeight + 1 || r.left < -1 || r.top < -1) {
      out.push({ kind: '出视口', el: cls, box: [Math.round(r.left), Math.round(r.top), Math.round(r.right), Math.round(r.bottom)], vp: [innerWidth, innerHeight] })
    }
  })
  // 去重
  const seen = new Set()
  return out.filter((o) => { const k = JSON.stringify(o); if (seen.has(k)) return false; seen.add(k); return true }).slice(0, 25)
})

for (const s of SELS) {
  const el = await page.$(s)
  if (!el) { console.log('MISSING', s); continue }
  const box = await el.boundingBox()
  const name = s.replace(/[^a-z0-9]/gi, '_')
  await page.screenshot({
    path: `shots/${TAG}-${name}.png`,
    clip: { x: Math.max(0, box.x - 12), y: Math.max(0, box.y - 12), width: Math.min(box.width + 24, 1600), height: Math.min(box.height + 24, 950) },
  })
}

console.log(JSON.stringify({ diag }, null, 1))
await b.close()

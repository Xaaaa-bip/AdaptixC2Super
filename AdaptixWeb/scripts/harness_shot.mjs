#!/usr/bin/env node
/* 渲染台截图 —— 本地看控制台各视图，用于迭代配色
 * 用法: node scripts/harness_shot.mjs [tag]
 */
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'

const TAG = process.argv[2] || 'now'
const URL = 'http://127.0.0.1:8899/console.html'
const OUT = 'shots'
mkdirSync(OUT, { recursive: true })

const browser = await chromium.launch()
const ctx = await browser.newContext({
  viewport: { width: 1600, height: 950 },
  deviceScaleFactor: 2,
})
const page = await ctx.newPage()
const errs = []
page.on('pageerror', (e) => errs.push('pageerror: ' + e.message))

await page.goto(URL, { waitUntil: 'load', timeout: 30000 })
await page.waitForTimeout(2600)

// 逐个视图截图
const frames = await page.$$('.hx-frame')
const names = ['overview', 'hosts']
for (let i = 0; i < frames.length; i++) {
  await frames[i].screenshot({ path: `${OUT}/${TAG}-${names[i] || i}.png` })
}

// 概览上半部特写：全屏视图
await page.setViewportSize({ width: 1600, height: 900 })
await page.evaluate(() => window.scrollTo(0, 0))
await page.waitForTimeout(400)
await page.screenshot({ path: `${OUT}/${TAG}-full.png` })

console.log(JSON.stringify({ tag: TAG, frames: frames.length, errors: errs }))
await browser.close()

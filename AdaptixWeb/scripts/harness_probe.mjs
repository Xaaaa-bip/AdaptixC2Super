#!/usr/bin/env node
/* 渲染台探针 —— 把指定选择器的计算样式打出来，用来定位"这条样式到底谁写的" */
import { chromium } from 'playwright'

const URL = 'http://127.0.0.1:8899/console.html'
const SELS = process.argv.slice(2)

const browser = await chromium.launch()
const page = await browser.newPage({ viewport: { width: 1600, height: 950 } })
await page.goto(URL, { waitUntil: 'load' })
await page.waitForTimeout(600)

const out = await page.evaluate((sels) => {
  const KEYS = ['boxShadow', 'borderTopColor', 'borderLeftColor', 'backgroundColor',
    'color', 'borderRadius', 'backdropFilter', 'fontFamily', 'fontSize', 'backgroundImage']
  const r = {}
  for (const s of sels) {
    const el = document.querySelector(s)
    if (!el) { r[s] = 'MISSING'; continue }
    const cs = getComputedStyle(el)
    r[s] = Object.fromEntries(KEYS.map((k) => [k, String(cs[k]).slice(0, 150)]))
  }
  return r
}, SELS)

console.log(JSON.stringify(out, null, 2))
await browser.close()

#!/usr/bin/env node
/* v2 复评对比度实测 —— 覆盖上轮 14 个不达标点位 + 浅色状态栏底/浅色事件四级色/暗色 ink-3 全点位。
   方法与上轮一致：getComputedStyle 取终值，半透明背景逐层向上合成到不透明层，WCAG 公式。 */
import { launchConsole, fakeAgent } from './_harness.mjs'
import { chromium } from 'playwright'
import http from 'node:http'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const DIST = path.join(__dirname, '..', 'dist')

const MEASURE = (points) => {
  const parse = (c) => { const m = c.match(/rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)/)
    return m ? [+m[1], +m[2], +m[3], m[4] === undefined ? 1 : +m[4]] : null }
  const over = (fg, bg) => { const a = fg[3]
    return [fg[0]*a+bg[0]*(1-a), fg[1]*a+bg[1]*(1-a), fg[2]*a+bg[2]*(1-a), 1] }
  const effBg = (el) => {
    const layers = []
    for (let n = el; n; n = n.parentElement) {
      const cs = getComputedStyle(n); const c = parse(cs.backgroundColor)
      if (c && c[3] > 0) { layers.push(c); if (c[3] >= 0.999) break }
      if (cs.backgroundImage !== 'none' && layers.length) break
    }
    let acc = [7, 9, 13, 1]
    for (let i = layers.length - 1; i >= 0; i--) acc = over(layers[i], acc)
    return acc }
  const lum = (c) => { const f = (v) => { v /= 255; return v <= 0.03928 ? v/12.92 : ((v+0.055)/1.055)**2.4 }
    return 0.2126*f(c[0]) + 0.7152*f(c[1]) + 0.0722*f(c[2]) }
  const ratio = (a, b) => { const [x,y] = [lum(a), lum(b)].sort((p,q)=>q-p); return (x+0.05)/(y+0.05) }
  const out = []
  for (const p of points) {
    const el = document.querySelector(p.sel)
    if (!el) { out.push({ id: p.id, sel: p.sel, missing: true }); continue }
    const cs = getComputedStyle(el)
    const fg = parse(cs.color), bg = effBg(el)
    const px = parseFloat(cs.fontSize), bold = parseInt(cs.fontWeight) >= 700
    const large = px >= 24 || (px >= 18.66 && bold)
    const r = ratio(fg, bg)
    out.push({ id: p.id, note: p.note, fg: cs.color, bg: bg.map(v=>Math.round(v)).slice(0,3).join(','),
      px, bold, ratio: Math.round(r*100)/100, pass: r >= (large ? 3 : 4.5) })
  }
  const st = document.querySelector('.ops-status')
  out.push({ id: '_statusbar_bg', raw: st ? getComputedStyle(st).backgroundColor : 'MISSING' })
  return out
}

// 控制台点位（登录后）—— 上轮 14 个不达标点位全覆盖 + 新增顺带项
const CONSOLE_POINTS = [
  { id: 'badge',          sel: '.cl-badge',                        note: '工具条徽标数字' },
  { id: 'status-dim',     sel: '.ops-status .dim',                 note: '状态栏提示 .dim' },
  { id: 'status-counts',  sel: '.ops-status .ops-status-counts',   note: '状态栏计数' },
  { id: 'tab-inactive',   sel: '.cl-tab:not(.active)',             note: '非激活标签' },
  { id: 'tab-active',     sel: '.cl-tab.active',                   note: '激活标签' },
  { id: 'btn-active',     sel: '.cl-btn.active',                   note: '激活工具条按钮' },
  { id: 'ev-info',        sel: '.ev-line.lv-info',                 note: '事件 info' },
  { id: 'ev-ok',          sel: '.ev-line.lv-ok',                   note: '事件 ok' },
  { id: 'ev-warn',        sel: '.ev-line.lv-warn',                 note: '事件 warn' },
  { id: 'ev-err',         sel: '.ev-line.lv-err',                  note: '事件 err' },
  { id: 'ev-primary',     sel: '.ev-bar .chip-row button.primary', note: '事件过滤激活钮 button.primary' },
  { id: 'ev-stats',       sel: '.ev-bar .stats',                   note: '事件栏统计(ink-3)' },
]

// 登录页点位
const LOGIN_POINTS = [
  { id: 'lx-input',  sel: '.lx-input',  note: '登录输入框' },
  { id: 'lx-label',  sel: '.lx label',  note: '登录标签' },
  { id: 'lx-submit', sel: '.lx-submit', note: '登录提交钮' },
]
const HOST_POINTS = [
  { id: 'host-th', sel: '.sess-table th', note: '主机表表头' },
]

const fmt = (rows) => rows.map(r => r.missing ? `  ✗ ${r.id}（${r.sel}）元素不存在`
  : r.raw ? `  _statusbar_bg = ${r.raw}`
  : `  ${r.pass ? '✓' : '✗'} ${r.id.padEnd(14)} ${r.note}: ${String(r.ratio).padEnd(6)} ${r.px}px${r.bold?'B':''} fg=${r.fg} bg=rgb(${r.bg})`).join('\n')

// ── 1. 控制台（暗色 → 切浅色） ──
const h = await launchConsole({ agents: [fakeAgent(0), fakeAgent(1), fakeAgent(2)] })
const { page } = h
// 概览默认独占工作区；点开两个 dock 后再测 dock 内的文字点位。
await page.click('.cl-btn[title^="主机"]')
await page.waitForTimeout(250)
console.log('— 主机工作区表头（暗色） —')
console.log(fmt(await page.evaluate(MEASURE, HOST_POINTS)))
await page.click('.cl-btn[title="事件日志"]')
await page.waitForTimeout(250)
await h.gotoView('载荷')   // 多开一个标签，保证存在非激活标签
await page.evaluate(() => { const b = [...document.querySelectorAll('.ev-bar .chip-row button')]; if (b[0]) b[0].click() })
await page.waitForTimeout(600)

// 注入 lv-err 事件行（harness 假数据不产生 error 级事件）与 muted 徽标克隆
await page.evaluate(() => {
  const list = document.querySelector('.ev-list'); if (list) {
    const d = document.createElement('div'); d.className = 'ev-line lv-err'
    d.innerHTML = '<span class="t">00:00:00 </span><span class="lv">[err]</span> probe'
    list.appendChild(d)
  }
})
console.log('— 暗色（默认）控制台 —')
console.log(fmt(await page.evaluate(MEASURE, CONSOLE_POINTS)))

// muted 徽标（0 主机态）单独测
await page.evaluate(() => document.querySelector('.cl-badge').classList.add('muted'))
console.log(fmt(await page.evaluate(MEASURE, [{ id: 'badge-muted', sel: '.cl-badge.muted', note: '徽标 muted 态(0 主机)' }])))
await page.evaluate(() => document.querySelector('.cl-badge').classList.remove('muted'))

await page.click('.cl-ghost[title="浅色"]')
await page.waitForTimeout(600)
console.log('— 浅色控制台 —')
console.log(fmt(await page.evaluate(MEASURE, CONSOLE_POINTS)))
await page.click('.cl-btn[title^="主机"]')
await page.waitForTimeout(250)
console.log('— 主机工作区表头（浅色） —')
console.log(fmt(await page.evaluate(MEASURE, HOST_POINTS)))
await h.close()

// ── 2. 登录页（不登录直接量） ──
const server = http.createServer((req, res) => {
  const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname)
  let f = path.join(DIST, rel === '/' ? 'index.html' : rel)
  if (!f.startsWith(DIST) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) f = path.join(DIST, 'index.html')
  const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
    '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-ico', '.woff2': 'font/woff2' }
  res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' })
  fs.createReadStream(f).pipe(res)
})
await new Promise(r => server.listen(0, '127.0.0.1', r))
const url = `http://127.0.0.1:${server.address().port}/`
const browser = await chromium.launch(process.env.PW_CHANNEL ? { channel: process.env.PW_CHANNEL } : undefined)
for (const theme of ['dark', 'light']) {
  const ctx = await browser.newContext({ viewport: { width: 1600, height: 950 } })
  const p = await ctx.newPage()
  await p.goto(url, { waitUntil: 'load' })
  await p.evaluate((t) => localStorage.setItem('adaptix.theme', t), theme)
  await p.reload({ waitUntil: 'load' })
  await p.waitForSelector('.lx-submit', { timeout: 15000 })
  console.log(`— 登录页 ${theme} —`)
  console.log(fmt(await p.evaluate(MEASURE, LOGIN_POINTS)))
  await ctx.close()
}
await browser.close(); server.close()
console.log('DONE')

#!/usr/bin/env node
/* 第一批四处改动的真机验证。只导航 + 量尺寸，不做任何主机操作。

   验证项：
     E1  .empty 的行不再被拉伸（量图标/标题/说明三行之间的实际间距）
     E2  PageTips 只在第一个视图自动展开，切到第二个视图不再弹
     E3  顶栏不再重复 ConnStatus（chrome 里只剩底栏那一份）
     E4  .rail-badge 字号从 9px 抬上来

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/verify_batch1.mjs */
import { chromium } from 'playwright'
import { mkdirSync } from 'node:fs'

mkdirSync('shots', { recursive: true })

const b = await chromium.launch()
const ctx = await b.newContext({
  viewport: { width: 1600, height: 950 },
  deviceScaleFactor: 2,
  ignoreHTTPSErrors: true,
})
const page = await ctx.newPage()
const errs = []
page.on('pageerror', (e) => errs.push(String(e)))
page.on('console', (m) => { if (m.type() === 'error') errs.push(m.text()) })

await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(4500)

/* 点侧栏某个视图（按标签文字找），只导航 */
async function goto(label) {
  const ok = await page.evaluate((lab) => {
    const it = [...document.querySelectorAll('.rail-item')]
      .find((e) => (e.textContent || '').includes(lab))
    if (!it) return false
    it.click()
    return true
  }, label)
  await page.waitForTimeout(2200)
  return ok
}

const tipsVisible = () => page.evaluate(() => !!document.querySelector('.page-tips'))
const connCount = () => page.evaluate(() => document.querySelectorAll('.conn-chip').length)

console.log('=== E2  PageTips 自动展开只发生一次 ===')
// 落地页可能已经开过一次（首次进入写入全局键）
console.log(`落地页 tips 存在: ${await tipsVisible()}   localStorage 键:`,
  await page.evaluate(() => Object.keys(localStorage).filter((k) => k.startsWith('adaptix.tips'))))

await goto('凭据')
const tipsOnCreds = await tipsVisible()
console.log(`切到「凭据」后 tips 存在: ${tipsOnCreds}   ${tipsOnCreds ? '（预期 false）' : '✓'}`)

console.log('\n=== E1  .empty 空态行间距 ===')
const emptyInfo = await page.evaluate(() => {
  const e = document.querySelector('.empty')
  if (!e) return null
  const r = e.getBoundingClientRect()
  const kids = [...e.children].map((k) => {
    const b = k.getBoundingClientRect()
    return { text: (k.textContent || '').slice(0, 18), top: Math.round(b.top), bottom: Math.round(b.bottom), h: Math.round(b.height) }
  })
  const gaps = []
  for (let i = 1; i < kids.length; i++) gaps.push(kids[i].top - kids[i - 1].bottom)
  return { box: [Math.round(r.top), Math.round(r.bottom)], h: Math.round(r.height), kids, gaps }
})
if (!emptyInfo) console.log('  「凭据」页没有 .empty 元素')
else {
  console.log(`  空态容器高 ${emptyInfo.h}px  [${emptyInfo.box.join(' .. ')}]`)
  for (const k of emptyInfo.kids) console.log(`    「${k.text}」 高 ${k.h}px  top=${k.top}`)
  console.log(`  相邻行间距: [${emptyInfo.gaps.join(', ')}] px  ${emptyInfo.gaps.every((g) => g >= 0 && g < 40) ? '✓ 已收拢' : '✗ 仍被拉开'}`)
}
await page.screenshot({ path: 'shots/v1-凭据-空态.png', fullPage: false })

console.log('\n=== E3  连接状态芯片只保留一份 ===')
const c1 = await connCount()
console.log(`  当前视图 chrome 里 .conn-chip 数量: ${c1}   ${c1 === 1 ? '✓' : '（预期 1；概览页会多一张卡片）'}`)
console.log('  底栏那份:', await page.evaluate(() => {
  const s = document.querySelector('.ops-status .conn-chip')
  return s ? (s.textContent || '').trim() : '缺失!'
}))
console.log('  顶栏那份:', await page.evaluate(() => {
  const s = document.querySelector('.ops-top .conn-chip')
  return s ? (s.textContent || '').trim() + ' ← 应该是 null' : 'null ✓'
}))

console.log('\n=== E4  .rail-badge 字号 ===')
console.log(' ', await page.evaluate(() => {
  const s = document.querySelector('.rail-badge')
  if (!s) return '没有徽章'
  const cs = getComputedStyle(s)
  const r = s.getBoundingClientRect()
  return `font-size=${cs.fontSize} 尺寸=${Math.round(r.width)}x${Math.round(r.height)} 文本="${(s.textContent || '').trim()}"`
}))

console.log('\n=== 切到「日志」再确认一次 tips 不弹 ===')
await goto('日志')
const tipsOnLogs = await tipsVisible()
console.log(`  tips 存在: ${tipsOnLogs}   ${tipsOnLogs ? '✗ 还在弹' : '✓ 不再弹'}`)
await page.screenshot({ path: 'shots/v1-日志.png' })

console.log('\n=== 手动点 ? 仍能打开（功能没被砍掉）===')
await page.click('.tips-btn').catch(() => console.log('  找不到 .tips-btn'))
await page.waitForTimeout(400)
console.log(`  点 ? 后 tips 存在: ${await tipsVisible()}   ${(await tipsVisible()) ? '✓' : '✗'}`)
await page.screenshot({ path: 'shots/v1-日志-手动展开.png' })

console.log('\n页面错误:', errs.length ? errs.slice(0, 5) : '无')
await b.close()

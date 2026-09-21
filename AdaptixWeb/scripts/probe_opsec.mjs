#!/usr/bin/env node
/* 精确量 OPSEC 字段里每个可见元素的 x 位置与尺寸，找出"间隔"到底在哪一段。 */
import { chromium } from 'playwright'

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, deviceScaleFactor: 2, ignoreHTTPSErrors: true })
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
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('载荷'))
  if (it) it.click()
})
await page.waitForTimeout(3000)
await page.evaluate(() => {
  const b = [...document.querySelectorAll('.gen-preset')].find((x) =>
    (x.querySelector('b')?.textContent || '').trim() === 'Linux')
  if (b) b.click()
})
await page.waitForTimeout(2500)

console.log('=== OPSEC 字段逐元素坐标（相对 field 左边缘）===')
console.log(' ', await page.evaluate(() => {
  const lab = [...document.querySelectorAll('label')].find((x) => (x.textContent || '').trim().startsWith('启用 OPSEC'))
  if (!lab) return '未找到'
  const field = lab.closest('.field')
  const fr = field.getBoundingClientRect()
  const rel = (el) => {
    const r = el.getBoundingClientRect()
    return { x: Math.round(r.left - fr.left), w: Math.round(r.width), h: Math.round(r.height) }
  }
  const inline = field.querySelector('.check-inline')
  const cb = field.querySelector('input[type=checkbox]')
  const cs = getComputedStyle(cb)
  return {
    'field 总宽': Math.round(fr.width),
    'field 模板列': getComputedStyle(field).gridTemplateColumns,
    '① 字段标签 OPSEC 检测': rel(lab),
    '② check-inline 容器': rel(inline),
    '③ 复选框': rel(cb),
    '复选框计算样式': {
      width: cs.width, height: cs.height,
      accentColor: cs.accentColor,
      appearance: cs.appearance,
      marginRight: cs.marginRight,
      backgroundColor: cs.backgroundColor,
    },
    '④ check-inline 的文字节点': (() => {
      const t = [...inline.childNodes].filter((n) => n.nodeType === 3 && n.textContent.trim())
      return t.map((n) => n.textContent.trim())
    })(),
    'check-inline 的 justify/align': `${getComputedStyle(inline).justifyContent} / ${getComputedStyle(inline).alignItems}`,
    'check-inline 的 gap': getComputedStyle(inline).gap,
  }
}))

console.log('\n=== 对照：上一行的 重连间隔 字段 ===')
console.log(' ', await page.evaluate(() => {
  const lab = [...document.querySelectorAll('label')].find((x) => (x.textContent || '').includes('重连间隔'))
  if (!lab) return '未找到'
  const field = lab.closest('.field')
  const fr = field.getBoundingClientRect()
  const i = field.querySelector('input')
  const ir = i.getBoundingClientRect()
  return {
    'field 总宽': Math.round(fr.width),
    '标签占位': `${Math.round(lab.getBoundingClientRect().left - fr.left)} → ${Math.round(lab.getBoundingClientRect().right - fr.left)}`,
    '输入框起点': Math.round(ir.left - fr.left),
    '输入框宽': Math.round(ir.width),
  }
}))

// 高分辨率特写：只截 OPSEC 那一行，放大 2 倍
const field = await page.$('.gen-main .field:has(label:text-is("启用 OPSEC 检测"))')
if (field) await field.screenshot({ path: 'shots/b5-opsec-2x.png' })
console.log('\n已保存 shots/b5-opsec-2x.png（deviceScaleFactor 2）')
await b.close()

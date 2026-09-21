#!/usr/bin/env node
/* 第三批（监听器 + 任务）真机验证。只导航 + 量尺寸，不做任何主机操作。

   验证项：
     L1  运行中的监听器：「暂停」可用、「恢复」置灰
     L2  .tag.listen 边框不再是暗红
     L3  「停止」不再常驻实心红
     T1  任务「完成」用中性色，不再复用主机在线的绿

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/verify_batch3.mjs */
import { chromium } from 'playwright'

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, deviceScaleFactor: 1, ignoreHTTPSErrors: true })
const page = await ctx.newPage()
const errs = []
page.on('pageerror', (e) => errs.push(String(e)))
await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(4500)

async function goto(label) {
  await page.evaluate((lab) => {
    const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes(lab))
    if (it) it.click()
  }, label)
  await page.waitForTimeout(2500)
}

await goto('监听器')
console.log('=== L1  暂停 / 恢复 按状态启用 ===')
const rows = await page.evaluate(() => [...document.querySelectorAll('tbody tr')].slice(0, 3).map((tr) => {
  const status = (tr.children[5]?.textContent || '').trim()
  return {
    status,
    buttons: [...tr.querySelectorAll('.lst-actions button')].map((x) => ({
      text: (x.textContent || '').trim(),
      disabled: x.disabled,
      title: x.title,
      bg: getComputedStyle(x).backgroundColor,
      color: getComputedStyle(x).color,
      border: getComputedStyle(x).borderColor,
    })),
  }
}))
for (const r of rows) {
  console.log(`  状态=${r.status}`)
  for (const x of r.buttons) {
    console.log(`    「${x.text}」 disabled=${x.disabled}  bg=${x.bg}  边框=${x.border}`)
    if (x.title) console.log(`        title: ${x.title}`)
  }
}
const running = rows[0]?.status?.toLowerCase().includes('listen')
if (rows[0]) {
  const pause = rows[0].buttons.find((x) => x.text === '暂停')
  const resume = rows[0].buttons.find((x) => x.text === '恢复')
  const ok = running ? (pause && !pause.disabled) && (resume && resume.disabled)
    : (pause && pause.disabled) && (resume && !resume.disabled)
  console.log(`  → 运行中=${running}；暂停/恢复 互斥启用: ${ok ? '✓' : '✗'}`)
}

console.log('\n=== L2 / L3  徽章边框 & 停止按钮权重 ===')
console.log(' ', await page.evaluate(() => {
  const tag = document.querySelector('.tag.listen')
  const stop = document.querySelector('.lst-actions button.danger')
  const cs = tag ? getComputedStyle(tag) : null
  const ss = stop ? getComputedStyle(stop) : null
  return {
    徽章: cs ? `${cs.color} / bg ${cs.backgroundColor} / border ${cs.borderColor}` : '无',
    徽章边框是否透明: cs ? cs.borderColor.includes('rgba(0, 0, 0, 0)') || cs.borderTopColor === 'rgba(0, 0, 0, 0)' : null,
    停止按钮: ss ? `bg ${ss.backgroundColor} / 文字 ${ss.color} / 边框 ${ss.borderColor}` : '无',
    停止是否仍是实心红: ss ? ss.backgroundColor === 'rgb(255, 77, 94)' : null,
  }
}))

await goto('任务')
console.log('\n=== T1  任务状态徽章 ===')
console.log(' ', await page.evaluate(() => {
  const tags = [...document.querySelectorAll('tbody .tag')]
  const kinds = {}
  for (const t of tags) kinds[t.className] = (kinds[t.className] || 0) + 1
  const first = tags[0]
  const cs = first ? getComputedStyle(first) : null
  return {
    行数: document.querySelectorAll('tbody tr').length,
    徽章种类: kinds,
    样本: first ? {
      cls: first.className, text: (first.textContent || '').trim(),
      color: cs.color, bg: cs.backgroundColor,
    } : null,
    还有绿色正文吗: tags.some((t) => getComputedStyle(t).color === 'rgb(52, 211, 153)'),
  }
}))

console.log('\n页面错误:', errs.length ? errs.slice(0, 3) : '无')
await b.close()

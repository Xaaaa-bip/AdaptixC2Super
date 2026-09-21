#!/usr/bin/env node
/* 第 5 批基线探测：载荷(Generate) Linux 的 OPSEC 字段间距 + 投递(WebDelivery) 命令块宽度。
   只导航 + 量，不做任何主机操作。

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/probe_batch5.mjs */
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
await page.waitForTimeout(5000)

const goto = async (label) => {
  await page.evaluate((l) => {
    const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes(l))
    if (it) it.click()
  }, label)
  await page.waitForTimeout(3000)
}

/* ── 1. 载荷 → Linux → OPSEC 字段 ── */
await goto('载荷')
console.log('=== 1. 目标平台 预设卡 ===')
console.log(' ', await page.evaluate(() =>
  [...document.querySelectorAll('.gen-preset')].map((b) => ({
    title: (b.querySelector('b')?.textContent || '').trim(),
    meta: (b.querySelector('.gen-preset-meta')?.textContent || '').trim(),
    active: b.className.includes('active'),
  }))))

// 点 "Linux" 平台卡（用户说的"载荷页面->linux"就是这张）
await page.evaluate(() => {
  const b = [...document.querySelectorAll('.gen-preset')].find((x) =>
    (x.querySelector('b')?.textContent || '').trim() === 'Linux')
  if (b) b.click()
})
await page.waitForTimeout(2500)
console.log('\n=== 切到 Linux 后的 agent 芯片 ===')
console.log(' ', await page.evaluate(() =>
  [...document.querySelectorAll('.gen-agent-row button')].map((b) => (b.textContent || '').trim())))

console.log('\n=== OPSEC 字段的几何 ===')
console.log(' ', await page.evaluate(() => {
  const labs = [...document.querySelectorAll('label')].filter((l) => (l.textContent || '').includes('OPSEC'))
  if (!labs.length) return '页面上没有含 OPSEC 的 label（可能没切到 linux）'
  const out = labs.map((l) => {
    const r = l.getBoundingClientRect()
    const cs = getComputedStyle(l)
    const field = l.closest('.field')
    return {
      文本: (l.textContent || '').replace(/\s+/g, ' ').trim(),
      cls: l.className,
      盒子: `${Math.round(r.width)}×${Math.round(r.height)}`,
      top: Math.round(r.top),
      bottom: Math.round(r.bottom),
      display: cs.display,
      marginTop: cs.marginTop,
      marginBottom: cs.marginBottom,
      paddingTop: cs.paddingTop,
      所在field的高度: field ? Math.round(field.getBoundingClientRect().height) : null,
    }
  })
  // 两个 label 之间的垂直间隙
  const gaps = []
  for (let i = 1; i < out.length; i++) gaps.push(out[i].top - out[i - 1].bottom)
  const field = labs[0].closest('.field')
  const fcs = field ? getComputedStyle(field) : null
  return {
    labels: out,
    '相邻 label 间距px': gaps,
    field的display: fcs ? fcs.display : null,
    field的gap: fcs ? fcs.gap : null,
    'field 里除 label 外还有什么': field ? [...field.children].map((c) => c.tagName + '.' + (c.className || '')) : null,
  }
}))

console.log('\n=== 对照：同一页其他正常字段（重连间隔）===')
console.log(' ', await page.evaluate(() => {
  const l = [...document.querySelectorAll('label')].find((x) => (x.textContent || '').includes('重连间隔'))
  if (!l) return '未找到'
  const field = l.closest('.field')
  const r = l.getBoundingClientRect()
  const input = field?.querySelector('input')
  const ir = input?.getBoundingClientRect()
  return {
    label高: Math.round(r.height),
    'label底到input顶的距离': ir ? Math.round(ir.top - r.bottom) : null,
    field总高: field ? Math.round(field.getBoundingClientRect().height) : null,
  }
}))

/* ── 2. 投递 → 右栏命令块 ── */
await goto('投递')
await page.waitForTimeout(1500)
console.log('\n=== 投递页右栏命令块 ===')
console.log(' ', await page.evaluate(() => {
  const side = document.querySelector('.gen-side')
  if (!side) return '未找到 .gen-side'
  const sr = side.getBoundingClientRect()
  const cs = getComputedStyle(side)
  const cmds = [...document.querySelectorAll('.wd-cmd')].map((t) => {
    const r = t.getBoundingClientRect()
    const s = getComputedStyle(t)
    const line = parseFloat(s.lineHeight) || 16
    const rows = Math.round((r.height - 16) / line)
    return {
      label: (t.closest('.field')?.querySelector('label')?.textContent || '').trim().slice(0, 30),
      字符数: t.value.length,
      盒子: `${Math.round(r.width)}×${Math.round(r.height)}`,
      行高: s.lineHeight,
      可见行数: rows,
      '每行字符数(估)': +(t.value.length / Math.max(rows, 1)).toFixed(1),
      whiteSpace: s.whiteSpace,
      wordBreak: s.wordBreak,
      出现横向滚动条: t.scrollWidth > t.clientWidth + 1,
    }
  })
  return {
    '右栏宽度': Math.round(sr.width),
    '右栏内边距': `${cs.paddingLeft}/${cs.paddingRight}`,
    '命令块数量': cmds.length,
    命令: cmds,
  }
}))

console.log('\n=== 实际命令长度 ===')
console.log(' ', await page.evaluate(() =>
  [...document.querySelectorAll('.wd-cmd')].map((t) => ({
    label: (t.closest('.field')?.querySelector('label')?.textContent || '').trim().slice(0, 26),
    n: t.value.length,
  }))))

console.log('\n页面错误:', errs.length ? errs.slice(0, 3) : '无')
await b.close()

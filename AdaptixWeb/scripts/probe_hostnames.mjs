#!/usr/bin/env node
/* 查主机标识的唯一性：454 张卡片到底能不能区分开。只读 store，不做任何主机操作。 */
import { chromium } from 'playwright'

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, deviceScaleFactor: 1, ignoreHTTPSErrors: true })
const page = await ctx.newPage()
await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(6000)
await page.evaluate(() => {
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('拓扑'))
  if (it) it.click()
})
await page.waitForTimeout(4000)

const out = await page.evaluate(() => {
  // zustand store 挂在模块里，从 DOM 拿不到 —— 改从拓扑页渲染出的卡片文本反推，
  // 但更准的是读 window 上的调试句柄；没有就退回 DOM 文本统计。
  const nodes = [...document.querySelectorAll('.graph-node')]
  const rows = nodes.map((n) => {
    const lab = n.querySelector('.graph-label')
    const sub = n.querySelector('.graph-sub')
    const lis = n.querySelector('.graph-listener')
    return {
      label: (lab?.textContent || '').trim(),
      sub: (sub?.textContent || '').trim(),
      listener: (lis?.textContent || '').trim(),
    }
  })
  const tally = (f) => {
    const m = {}
    for (const r of rows) m[f(r)] = (m[f(r)] || 0) + 1
    return Object.entries(m).sort((a, b) => b[1] - a[1])
  }
  return {
    卡片数: rows.length,
    '按 label 去重': tally((r) => r.label),
    '按 sub 去重': tally((r) => r.sub),
    '按 label+sub 去重': tally((r) => r.label + '|' + r.sub),
    '按 label+sub+listener 去重': tally((r) => r.label + '|' + r.sub + '|' + r.listener),
  }
})
console.log('卡片数:', out.卡片数)
console.log('\n按 label（主机名）去重 —— 前 8:')
for (const [k, v] of out['按 label 去重'].slice(0, 8)) console.log(`  ${v.toString().padStart(4)}  ${k}`)
console.log(`  ...共 ${out['按 label 去重'].length} 种不同值`)
console.log('\n按 sub（user@ip）去重 —— 前 8:')
for (const [k, v] of out['按 sub 去重'].slice(0, 8)) console.log(`  ${v.toString().padStart(4)}  ${k}`)
console.log(`  ...共 ${out['按 sub 去重'].length} 种不同值`)
console.log('\n按 label+sub 去重 种类数:', out['按 label+sub 去重'].length)
console.log('按 label+sub+listener 去重 种类数:', out['按 label+sub+listener 去重'].length)
await b.close()

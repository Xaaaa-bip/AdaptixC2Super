#!/usr/bin/env node
/* 拓扑视图真机探测。只导航 + 量，不做任何主机操作。

   要回答的问题：
     1. 节点实际落在哪几个 x（列）上，每列多少个
     2. 同列内相邻节点的垂直间距是多少，和节点高度 64 比是什么关系（重叠？）
     3. SVG 的 viewBox 与渲染尺寸的比值（是不是被等比缩放了）
     4. 图例颜色 vs 节点圆点实际颜色
     5. 画布是否可滚（scrollHeight vs clientHeight）

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/probe_graph.mjs */
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

await page.evaluate(() => {
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('拓扑'))
  if (it) it.click()
})
await page.waitForTimeout(4000)

console.log('=== 1. 画布容器 ===')
console.log(' ', await page.evaluate(() => {
  const wrap = document.querySelector('.graph-wrap')
  const svg = document.querySelector('.graph-svg')
  if (!wrap || !svg) return '未找到 .graph-wrap / .graph-svg'
  const ws = getComputedStyle(wrap)
  const w = wrap.getBoundingClientRect()
  const s = svg.getBoundingClientRect()
  const vb = (svg.getAttribute('viewBox') || '').split(/\s+/).map(Number)
  return {
    容器: `${Math.round(w.width)}×${Math.round(w.height)}`,
    overflow: `${ws.overflowX}/${ws.overflowY}`,
    可滚动: wrap.scrollHeight > wrap.clientHeight + 1,
    scrollH: wrap.scrollHeight, clientH: wrap.clientHeight,
    svg渲染尺寸: `${Math.round(s.width)}×${Math.round(s.height)}`,
    viewBox: vb,
    'viewBox 宽高比': +(vb[2] / vb[3]).toFixed(3),
    '渲染宽高比': +(s.width / s.height).toFixed(3),
    缩放倍数: +(s.width / vb[2]).toFixed(3),
    preserveAspectRatio: svg.getAttribute('preserveAspectRatio') || '(默认 xMidYMid meet)',
  }
}))

console.log('\n=== 2. 节点分布 / 同列间距 ===')
const dist = await page.evaluate(() => {
  const nodes = [...document.querySelectorAll('.graph-node')]
  // SVG 元素的 className 是 SVGAnimatedString，必须走 getAttribute
  const items = nodes.map((n) => {
    const r = n.getBoundingClientRect()
    const bg = n.querySelector('.graph-node-bg')
    const dot = n.querySelector('.graph-dot')
    const cs = dot ? getComputedStyle(dot) : null
    return {
      cls: n.getAttribute('class') || '',
      x: +(+n.getAttribute('transform').match(/translate\(([-\d.]+)/)[1]).toFixed(1),
      y: +(+n.getAttribute('transform').match(/,\s*([-\d.]+)\)/)[1]).toFixed(1),
      screenX: Math.round(r.x), screenY: Math.round(r.y),
      dotFill: cs ? cs.fill : null,
      live: (n.getAttribute('class') || '').includes('live'),
    }
  })
  const byX = {}
  for (const it of items) (byX[it.x] = byX[it.x] || []).push(it)
  const cols = Object.entries(byX).map(([x, list]) => {
    const ys = list.map((n) => n.y).sort((a, b) => a - b)
    const gaps = ys.slice(1).map((v, i) => +(v - ys[i]).toFixed(2))
    const uniq = [...new Set(gaps)]
    return {
      x: +x, 个数: list.length,
      首末y: [ys[0], ys[ys.length - 1]],
      间距种类: uniq.length <= 4 ? uniq : `${uniq.length} 种`,
      最小间距: gaps.length ? Math.min(...gaps) : null,
      最大间距: gaps.length ? Math.max(...gaps) : null,
      全同y: new Set(ys).size,
    }
  }).sort((a, b) => a.x - b.x)
  return { 节点总数: items.length, 列: cols, 样本: items.slice(0, 3) }
})
console.log('  节点总数:', dist.节点总数)
for (const c of dist.列) {
  console.log(`  x=${c.x}  节点 ${c.个数} 个  y: ${c.首末y[0]} → ${c.首末y[1]}  间距 ${c.最小间距}~${c.最大间距}  不同y值 ${c.全同y} 个`)
}
console.log('  样本:', JSON.stringify(dist.样本, null, 1).replace(/\n/g, '\n  '))
console.log('  → 节点高度 64；若最小间距 << 64 则节点互相重叠')

console.log('\n=== 3. 重叠检测（屏幕坐标） ===')
console.log(' ', await page.evaluate(() => {
  const nodes = [...document.querySelectorAll('.graph-node')].map((n) => n.getBoundingClientRect())
  let overlapPairs = 0
  let worst = 0
  // 同列相邻两两比（按 y 排序）
  const sorted = nodes.slice().sort((a, b) => a.y - b.y)
  for (let i = 1; i < sorted.length; i++) {
    const a = sorted[i - 1], b = sorted[i]
    const ox = Math.min(a.right, b.right) - Math.max(a.left, b.left)
    const oy = Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top)
    if (ox > 1 && oy > 1) {
      overlapPairs++
      worst = Math.max(worst, Math.min(ox, oy))
    }
  }
  return {
    节点数: nodes.length,
    屏幕坐标下互相重叠的相邻对: overlapPairs,
    最大重叠深度px: +worst.toFixed(1),
    单个节点屏幕尺寸: nodes[0] ? `${Math.round(nodes[0].width)}×${Math.round(nodes[0].height)}` : null,
  }
}))

console.log('\n=== 4. 图例 vs 实际圆点 ===')
console.log(' ', await page.evaluate(() => {
  const lgd = {}
  for (const s of document.querySelectorAll('.graph-legend span')) {
    const i = s.querySelector('i')
    const t = (s.textContent || '').trim()
    const cs = i ? getComputedStyle(i) : null
    lgd[t] = cs ? cs.backgroundColor : null
  }
  const dots = [...document.querySelectorAll('.graph-dot')].map((d) => getComputedStyle(d).fill)
  const tally = {}
  for (const d of dots) tally[d] = (tally[d] || 0) + 1
  const live = document.querySelectorAll('.graph-node.live').length
  const dead = document.querySelectorAll('.graph-node.dead').length
  return { 图例: lgd, 圆点颜色分布: tally, 'live 节点': live, 'dead 节点': dead }
}))

console.log('\n=== 5. 工具栏统计 vs 实际 ===')
console.log(' ', await page.evaluate(() => {
  const st = document.querySelector('.graph-page .stats')
  return {
    工具栏文本: st ? (st.textContent || '').replace(/\s+/g, ' ').trim() : '无',
    画出的节点数: document.querySelectorAll('.graph-node').length,
    图例文本: (document.querySelector('.graph-legend')?.textContent || '').replace(/\s+/g, ' ').trim(),
  }
}))

console.log('\n页面错误:', errs.length ? errs.slice(0, 3) : '无')
await b.close()

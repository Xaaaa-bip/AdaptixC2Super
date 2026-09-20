#!/usr/bin/env node
/* 第四批（拓扑）真机验证。只导航 + 量，不做任何主机操作。

   修复前的基线（同一条脚本，454 台主机 / 0 pivot）：
     节点全部落在 x=170，相邻间距 1.1~1.2px（节点高 64）
     453/453 对相邻节点互相重叠，最大重叠 75.4px
     画布不可滚，SVG 被等比缩放 1.365×
     图例"在线"是青色，实际圆点是绿色
     图例有"pivot 链路"，但 pivot 数是 0

   验证项：
     G1  节点不再重叠，垂直间距有硬下限
     G2  454 台折成多列块，不再是 1×454 的一条
     G3  画布按内容算尺寸、可滚动，且 SVG 1:1 不再缩放
     G4  图例"在线"色 == 实际 live 圆点色
     G5  0 pivot 时不显示"pivot 链路"图例
     G6  层标出现且带台数
     G7  滚动时图例不被带走

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/verify_batch4.mjs */
import { chromium } from 'playwright'

const NODE_H = 64
const GAP_Y = 18
const STEP_Y = NODE_H + GAP_Y // 82
let fail = 0
const ck = (name, ok, detail) => {
  console.log(`  ${ok ? '✓' : '✗'} ${name}${detail ? '  ' + detail : ''}`)
  if (!ok) fail++
}

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

console.log('=== G1 / G2  节点排布 ===')
const lay = await page.evaluate(() => {
  const items = [...document.querySelectorAll('.graph-node')].map((n) => {
    const t = (n.getAttribute('transform') || '').match(/translate\(([-\d.]+),\s*([-\d.]+)\)/)
    return { x: +t[1], y: +t[2] }
  })
  const byX = {}
  for (const it of items) (byX[it.x] = byX[it.x] || []).push(it.y)
  const cols = Object.entries(byX).map(([x, ys]) => {
    ys.sort((a, b) => a - b)
    const gaps = ys.slice(1).map((v, i) => +(v - ys[i]).toFixed(2))
    return { x: +x, n: ys.length, minGap: gaps.length ? Math.min(...gaps) : null }
  }).sort((a, b) => a.x - b.x)
  return { total: items.length, cols, distinctX: Object.keys(byX).length }
})
console.log(`  节点总数 ${lay.total}，占用 ${lay.distinctX} 个不同的 x（子列）`)
const minGap = Math.min(...lay.cols.filter((c) => c.minGap != null).map((c) => c.minGap))
const maxPerCol = Math.max(...lay.cols.map((c) => c.n))
console.log(`  子列数 ${lay.cols.length}，单列最多 ${maxPerCol} 个节点，全局最小垂直间距 ${minGap}px`)
ck('G1 垂直间距不低于节点高+间隙（无重叠）', minGap >= STEP_Y - 0.01, `${minGap} >= ${STEP_Y}`)
ck('G2 454 台已折成多列（不再是 1×454 的一条）', lay.distinctX > 1, `${lay.distinctX} 列`)

console.log('\n=== G3  画布尺寸与滚动 ===')
const canvas = await page.evaluate(() => {
  const wrap = document.querySelector('.graph-wrap')
  const scroll = document.querySelector('.graph-scroll')
  const svg = document.querySelector('.graph-svg')
  const r = svg.getBoundingClientRect()
  const vb = (svg.getAttribute('viewBox') || '').split(/\s+/).map(Number)
  return {
    wrap: `${Math.round(wrap.getBoundingClientRect().width)}×${Math.round(wrap.getBoundingClientRect().height)}`,
    scroll: scroll ? `${Math.round(scroll.getBoundingClientRect().width)}×${Math.round(scroll.getBoundingClientRect().height)}` : '无 .graph-scroll',
    scrollOverflow: scroll ? `${getComputedStyle(scroll).overflowX}/${getComputedStyle(scroll).overflowY}` : null,
    svgRender: [Math.round(r.width), Math.round(r.height)],
    viewBox: [vb[2], vb[3]],
    scaleX: +(r.width / vb[2]).toFixed(3),
    scaleY: +(r.height / vb[3]).toFixed(3),
    canScrollX: scroll ? scroll.scrollWidth > scroll.clientWidth + 1 : false,
    canScrollY: scroll ? scroll.scrollHeight > scroll.clientHeight + 1 : false,
    scrollDims: scroll ? `${scroll.scrollWidth}×${scroll.scrollHeight} vs ${scroll.clientWidth}×${scroll.clientHeight}` : null,
  }
})
console.log(' ', canvas)
ck('G3a SVG 按 1:1 渲染（不再等比缩放塞进容器）',
  Math.abs(canvas.scaleX - 1) < 0.02 && Math.abs(canvas.scaleY - 1) < 0.02,
  `scaleX=${canvas.scaleX} scaleY=${canvas.scaleY}`)
ck('G3b 画布真的可滚动', canvas.canScrollX || canvas.canScrollY, canvas.scrollDims)

console.log('\n=== G4 / G5  图例 ===')
const lgd = await page.evaluate(() => {
  const map = {}
  for (const s of document.querySelectorAll('.graph-legend span')) {
    const i = s.querySelector('i')
    map[(s.textContent || '').trim()] = i ? getComputedStyle(i).backgroundColor : null
  }
  const dotColors = [...document.querySelectorAll('.graph-node.live .graph-dot')].map((d) => getComputedStyle(d).fill)
  return { legend: map, legendText: (document.querySelector('.graph-legend')?.textContent || '').replace(/\s+/g, ' ').trim(), liveDot: dotColors[0] || null, liveCount: dotColors.length }
})
console.log('  图例:', lgd.legend)
console.log('  live 圆点实际色:', lgd.liveDot, `(${lgd.liveCount} 个)`)
const hex2rgb = (h) => {
  const m = h.match(/\d+/g).map(Number)
  return `rgb(${m[0]}, ${m[1]}, ${m[2]})`
}
ck('G4 图例"在线"与 live 圆点同色',
  !!lgd.liveDot && lgd.legend['在线'] === lgd.liveDot,
  `图例 ${lgd.legend['在线']} vs 圆点 ${lgd.liveDot}`)
const pivotCount = await page.evaluate(() => {
  const t = document.querySelector('.graph-page .stats')?.textContent || ''
  const m = t.match(/(\d+)\s*pivot/)
  return m ? +m[1] : null
})
ck('G5 无 pivot 时不摆"pivot 链路"图例',
  pivotCount !== 0 || !lgd.legendText.includes('pivot'),
  `pivot=${pivotCount}，图例文本="${lgd.legendText}"`)

console.log('\n=== G6  层标 ===')
const labels = await page.evaluate(() =>
  [...document.querySelectorAll('.graph-layer-label')].map((t) => (t.textContent || '').trim()))
console.log('  层标:', labels)
ck('G6 层标存在且带台数', labels.length > 0 && labels.some((l) => /\d/.test(l)), `${labels.length} 个`)

console.log('\n=== G7  滚动时图例钉住不动 ===')
const pin = await page.evaluate(async () => {
  const scroll = document.querySelector('.graph-scroll')
  const legend = document.querySelector('.graph-legend')
  const before = legend.getBoundingClientRect()
  scroll.scrollTop = 600
  scroll.scrollLeft = 400
  await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))
  const after = legend.getBoundingClientRect()
  return {
    moved: Math.round(Math.abs(after.x - before.x) + Math.abs(after.y - before.y)),
    scrollTop: scroll.scrollTop, scrollLeft: scroll.scrollLeft,
  }
})
ck('G7 图例不随画布滚动', pin.moved <= 1 && (pin.scrollTop > 0 || pin.scrollLeft > 0),
  `位移 ${pin.moved}px（画布已滚到 ${pin.scrollLeft},${pin.scrollTop}）`)

console.log('\n=== G8  卡片可区分性 ===')
const ident = await page.evaluate(() => {
  const rows = [...document.querySelectorAll('.graph-node')].map((n) => ({
    label: (n.querySelector('.graph-label')?.textContent || '').trim(),
    sub: (n.querySelector('.graph-sub')?.textContent || '').trim(),
    listener: (n.querySelector('.graph-listener')?.textContent || '').trim(),
    sid: (n.querySelector('.graph-sid')?.textContent || '').trim(),
  }))
  const uniq = (f) => new Set(rows.map(f)).size
  return {
    n: rows.length,
    label: uniq((r) => r.label),
    labelSub: uniq((r) => r.label + '|' + r.sub),
    withSid: uniq((r) => r.label + '|' + r.sub + '|' + r.listener + '|' + r.sid),
    sampleSid: rows.slice(0, 4).map((r) => r.sid),
    anyBlankSid: rows.some((r) => !r.sid),
  }
})
console.log(`  主机名去重 ${ident.label} 种；加上 user@ip 仍只有 ${ident.labelSub} 种；加上短 ID 后 ${ident.withSid} 种`)
console.log('  短 ID 样本:', ident.sampleSid)
ck('G8 每张卡片都带唯一短 ID 且互不相同',
  ident.withSid === ident.n && !ident.anyBlankSid,
  `${ident.withSid}/${ident.n} 种`)

console.log('\n=== G9  0.0.0.0 不再冒充地址 ===')
const zero = await page.evaluate(() => {
  const txt = document.body.innerText
  return {
    countInBody: (txt.match(/0\.0\.0\.0/g) || []).length,
    subs: [...new Set([...document.querySelectorAll('.graph-sub')].map((t) => (t.textContent || '').trim()))].slice(0, 8),
  }
})
console.log('  卡片 sub 的取值集合:', zero.subs)
ck('G9 页面上不再出现 0.0.0.0', zero.countInBody === 0, `出现 ${zero.countInBody} 次`)

console.log('\n=== 重叠抽样（屏幕坐标，修复前是 453/453）===')
console.log(' ', await page.evaluate(() => {
  const r = [...document.querySelectorAll('.graph-node')].map((n) => n.getBoundingClientRect()).sort((a, b) => a.y - b.y)
  let pairs = 0, worst = 0
  for (let i = 1; i < r.length; i++) {
    const a = r[i - 1], b = r[i]
    const ox = Math.min(a.right, b.right) - Math.max(a.left, b.left)
    const oy = Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top)
    if (ox > 1 && oy > 1) { pairs++; worst = Math.max(worst, Math.min(ox, oy)) }
  }
  return { 节点数: r.length, 重叠相邻对: pairs, 最大重叠px: +worst.toFixed(1) }
}))

console.log('\n页面错误:', errs.length ? errs.slice(0, 3) : '无')
console.log(fail === 0 ? '\n全部通过 ✓' : `\n${fail} 项未通过 ✗`)
await b.close()
process.exit(fail === 0 ? 0 : 1)

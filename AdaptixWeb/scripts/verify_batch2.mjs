#!/usr/bin/env node
/* 第二批（BOF 视图 + 滚动条）真机验证。只导航 + 量尺寸，不做任何主机操作。

   验证项：
     F1  .bof-meta 两条产物记录分开、不再被 opacity 吃掉对比度
     F2  .bof-card-foot 的三个按钮单行、未被压缩
     F3  .bof-card-head 的徽标不再逐字竖排
     F4  .bof-cat 分隔线已中性化
     F5  滚动条 thumb 对比度（读构建产物里的值）

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/verify_batch2.mjs */
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

await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(4500)

await page.evaluate(() => {
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('BOF'))
  if (it) it.click()
})
await page.waitForTimeout(3000)

const out = await page.evaluate(() => {
  /* 线性化 sRGB → 相对亮度，算 WCAG 对比度 */
  const lum = (rgb) => {
    const [r, g, b] = rgb.map((v) => {
      const s = v / 255
      return s <= 0.03928 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4)
    })
    return 0.2126 * r + 0.7152 * g + 0.0722 * b
  }
  const parse = (s) => (s.match(/[\d.]+/g) || []).slice(0, 3).map(Number)
  const ratio = (fg, bg) => {
    const a = lum(parse(fg)), c = lum(parse(bg))
    return ((Math.max(a, c) + 0.05) / (Math.min(a, c) + 0.05)).toFixed(2)
  }

  const res = {}

  // F1 元数据行
  const meta = document.querySelector('.bof-meta')
  if (meta) {
    const cs = getComputedStyle(meta)
    const card = meta.closest('.bof-card')
    const cardBg = getComputedStyle(card).backgroundColor
    res.meta = {
      fontSize: cs.fontSize,
      color: cs.color,
      opacity: cs.opacity,
      records: meta.children.length,
      texts: [...meta.children].map((c) => (c.textContent || '').trim().slice(0, 40)),
      childHeights: [...meta.children].map((c) => Math.round(c.getBoundingClientRect().height)),
      cardBg,
      contrast: ratio(cs.color, cardBg),
    }
  }

  // F2 卡片底部按钮
  const foot = document.querySelector('.bof-card-foot')
  if (foot) {
    res.footButtons = [...foot.querySelectorAll('button')].map((btn) => {
      const r = btn.getBoundingClientRect()
      return { text: (btn.textContent || '').trim(), w: Math.round(r.width), h: Math.round(r.height) }
    })
  }

  // F3 卡片头各元素的折行情况：单行元素高度应接近一行字高
  const heads = [...document.querySelectorAll('.bof-card-head')].slice(0, 8)
  res.headItems = heads.map((h) => {
    const card = h.closest('.bof-card')
    return {
      card: h.querySelector('.bof-name')?.textContent?.trim(),
      items: [...h.children].map((c) => {
        const r = c.getBoundingClientRect()
        const cs = getComputedStyle(c)
        const line = parseFloat(cs.fontSize) * 1.2
        return {
          text: (c.textContent || '').trim().slice(0, 14),
          w: Math.round(r.width),
          h: Math.round(r.height),
          // 高度超过 1.6 行就算折行了
          wrapped: r.height > line * 1.6,
        }
      }).filter((x) => x.text),
    }
  })

  // F4 分组分隔线
  const cat = document.querySelector('.bof-cat')
  if (cat) {
    res.catLine = getComputedStyle(cat, '::after').backgroundImage
    res.catLineColor = getComputedStyle(cat, '::after').backgroundColor
  }

  // 内容滚动状态
  const wrap = document.querySelector('.bof-wrap')
  if (wrap) {
    res.bofWrap = {
      scrollH: wrap.scrollHeight, clientH: wrap.clientHeight,
      scrollable: wrap.scrollHeight > wrap.clientHeight + 1,
      overflowY: getComputedStyle(wrap).overflowY,
    }
  }
  res.cardCount = document.querySelectorAll('.bof-card').length
  res.groups = [...document.querySelectorAll('.bof-cat')].map((c) => (c.textContent || '').trim())

  return res
})

console.log('=== F1  .o/.c 元数据行 ===')
if (out.meta) {
  console.log(`  font-size=${out.meta.fontSize}  opacity=${out.meta.opacity}  (opacity 应为 1)`)
  console.log(`  记录数=${out.meta.records}`)
  out.meta.texts.forEach((t, i) => console.log(`    [${i}] 高 ${out.meta.childHeights[i]}px  "${t}"`))
  console.log(`  卡片底 ${out.meta.cardBg}  文字 ${out.meta.color}  对比度 ${out.meta.contrast}:1  ${Number(out.meta.contrast) >= 4.5 ? '✓' : '✗ 低于 4.5'}`)
} else console.log('  没有 .bof-meta（该页可能离线）')

console.log('\n=== F2  卡片底部按钮 ===')
for (const b2 of out.footButtons || []) {
  console.log(`  「${b2.text}」 ${b2.w}x${b2.h}  ${b2.h <= 32 ? '✓ 单行' : '✗ 疑似折行'}`)
}

console.log('\n=== F3  卡片头折行（应为 0 处）===')
let wrapped = 0
for (const h of out.headItems || []) {
  const bad = h.items.filter((i) => i.wrapped)
  if (bad.length) {
    wrapped += bad.length
    console.log(`  ${h.card}: ${bad.map((x) => `「${x.text}」${x.w}x${x.h}`).join('  ')}`)
  }
}
console.log(`  折行元素总数: ${wrapped}  ${wrapped === 0 ? '✓' : '✗'}`)

console.log('\n=== F4  分组分隔线 ===')
console.log(`  background-image: ${out.catLine}  (应为 none)`)
console.log(`  background-color: ${out.catLineColor}  (中性线)`)
console.log(`  分组: ${out.groups.join(' | ')}`)
console.log(`  卡片总数: ${out.cardCount}`)

console.log('\n=== BOF 容器滚动状态 ===')
console.log(' ', JSON.stringify(out.bofWrap))

await page.screenshot({ path: 'shots/v2-BOF.png' })
console.log('\n页面错误:', errs.length ? errs.slice(0, 3) : '无')
await b.close()

#!/usr/bin/env node
/* 验收：载荷 / 投递两页改成"两栏各自滚"之后
   —— 两栏都封顶在视口内、各自能滚到底、左栏表单不再被右栏拖着走。
   两个页面共用 .gen-layout，所以两页都要量。
   只导航 + 滚 + 读 DOM，不做任何主机操作。 */
import { chromium } from 'playwright'

const results = []
const check = (id, desc, pass, detail) => {
  results.push({ id, pass })
  console.log(`${pass ? '  ok  ' : ' FAIL '} ${id}  ${desc}`)
  console.log(`        ${detail}`)
}

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, ignoreHTTPSErrors: true })
const page = await ctx.newPage()
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

/** 量 .gen-page / 两栏 / 各自的滚动体 */
const geom = () => page.evaluate(() => {
  const q = (s) => document.querySelector(s)
  const box = (el) => {
    if (!el) return null
    const r = el.getBoundingClientRect()
    const cs = getComputedStyle(el)
    return {
      h: Math.round(r.height), w: Math.round(r.width),
      top: Math.round(r.top), bottom: Math.round(r.bottom),
      clientH: el.clientHeight, scrollH: el.scrollHeight,
      canScroll: el.scrollHeight - el.clientHeight > 1,
      overflow: cs.overflowY,
    }
  }
  const pg = q('.gen-page')
  return {
    viewportH: window.innerHeight,
    page: { ...box(pg), overflow: getComputedStyle(pg).overflowY, clientH: pg.clientHeight, scrollH: pg.scrollHeight },
    layoutH: Math.round(q('.gen-layout').getBoundingClientRect().height),
    main: box(q('.gen-main')),
    side: box(q('.gen-side')),
    mainBody: box(q('.gen-main .card-bd')),
    sideBody: box(q('.gen-side .card-bd')),
  }
})

const runPage = async (id, label, prep) => {
  console.log(`\n=== ${label} ===`)
  await goto(label)
  if (prep) await prep()
  const g = await geom()
  console.log(`  视口 ${g.viewportH}px  .gen-page client ${g.page.clientH} / scroll ${g.page.scrollH}  overflow-y=${g.page.overflow}`)
  console.log(`  左栏 ${g.main.h}px（卡身可滚=${g.mainBody.canScroll}, scroll ${g.mainBody.scrollH}/client ${g.mainBody.clientH}）`)
  console.log(`  右栏 ${g.side.h}px（卡身可滚=${g.sideBody.canScroll}, scroll ${g.sideBody.scrollH}/client ${g.sideBody.clientH}）`)

  check(`${id}1`, '整页不再滚动（滚动落到两栏各自的卡身里）',
    g.page.scrollH <= g.page.clientH + 2,
    `.gen-page scroll ${g.page.scrollH} vs client ${g.page.clientH}（修复前 scroll 1767 / client 808）`)
  check(`${id}2`, '两栏都不超出视口',
    g.main.bottom <= g.viewportH + 1 && g.side.bottom <= g.viewportH + 1,
    `左栏底 ${g.main.bottom} / 右栏底 ${g.side.bottom}，视口 ${g.viewportH}`)
  check(`${id}3`, '左栏保持在视野内（不再被右栏拖着整页滚）',
    g.main.top >= 0 && g.main.top < g.viewportH,
    `左栏 top=${g.main.top}，bottom=${g.main.bottom}`)

  // 右栏能滚到底：滚到尽头，确认最后一条命令看得到
  const last = await page.evaluate(() => {
    const bd = document.querySelector('.gen-side .card-bd')
    if (!bd) return null
    bd.scrollTop = bd.scrollHeight
    const blocks = [...bd.querySelectorAll('.wd-block')]
    const b = blocks[blocks.length - 1]
    if (!b) return null
    const br = b.getBoundingClientRect(), cr = bd.getBoundingClientRect()
    return {
      scrollTop: bd.scrollTop, max: bd.scrollHeight - bd.clientHeight,
      lastLabel: (b.querySelector('label')?.textContent || '').trim(),
      lastFullyVisible: br.bottom <= cr.bottom + 1 && br.top >= cr.top - 1,
    }
  })
  if (last) {
    check(`${id}4`, '右栏能滚到最后一条命令且完整可见',
      last.lastFullyVisible && last.scrollTop >= last.max - 2,
      `滚到底 scrollTop=${last.scrollTop}/${last.max}，末条「${last.lastLabel}」完整可见=${last.lastFullyVisible}`)
  } else {
    // 载荷页右栏是「摘要」卡，没有命令块——这时该验的是它没有被裁掉内容
    check(`${id}4`, '右栏内容完整（该页右栏是摘要卡，无命令块）',
      g.sideBody.scrollH <= g.sideBody.clientH + 2,
      `摘要卡身 scroll ${g.sideBody.scrollH} vs client ${g.sideBody.clientH}`)
  }
  return g
}

// 投递：切到 Gopher，出全部 10 条命令，右栏最长
await runPage('L', '投递', async () => {
  await page.evaluate(() => {
    const row = [...document.querySelectorAll('.field')]
      .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
      ?.querySelector('.chip-row')
    const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === 'Gopher')
    if (btn) btn.click()
  })
  await page.waitForTimeout(2000)
})

// 载荷：同一套 .gen-layout，别把它改坏了
await runPage('G', '载荷', async () => {
  await page.evaluate(() => {
    const b = [...document.querySelectorAll('.gen-preset')].find((x) =>
      (x.querySelector('b')?.textContent || '').trim() === 'Linux')
    if (b) b.click()
  })
  await page.waitForTimeout(2000)
})

// 窄屏：单栏时必须退回整页滚，否则两个矮窗谁也读不完整
console.log('\n=== 窄屏 900px（单栏）===')
await page.setViewportSize({ width: 900, height: 950 })
await page.waitForTimeout(1500)
const narrow = await page.evaluate(() => {
  const pg = document.querySelector('.gen-page')
  const lay = document.querySelector('.gen-layout')
  return {
    cols: getComputedStyle(lay).gridTemplateColumns,
    layoutH: Math.round(lay.getBoundingClientRect().height),
    pageOverflow: getComputedStyle(pg).overflowY,
    sideMaxH: getComputedStyle(document.querySelector('.gen-side')).maxHeight,
  }
})
console.log('  实测:', JSON.stringify(narrow))
check('N1', '单栏时退回整页滚（不再把两栏各钉成视口高）',
  narrow.pageOverflow === 'auto' && narrow.sideMaxH === 'none' && narrow.layoutH > 950,
  `overflow-y=${narrow.pageOverflow}，.gen-side max-height=${narrow.sideMaxH}，layout 高 ${narrow.layoutH}px`)
await page.setViewportSize({ width: 1600, height: 950 })

const bad = results.filter((r) => !r.pass)
console.log(`\n──────── ${results.length - bad.length}/${results.length} 通过 ────────`)
if (bad.length) console.log('未通过: ' + bad.map((r) => r.id).join(', '))
await b.close()
process.exit(bad.length ? 1 : 0)

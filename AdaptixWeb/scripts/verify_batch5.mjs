#!/usr/bin/env node
/* 第 5 批验收：载荷 Linux 的 OPSEC 字段间隔 + 投递右栏命令块可读性。
   只导航 + 量，不做任何主机操作。 */
import { chromium } from 'playwright'

const URL = process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/'
const results = []
const check = (id, desc, pass, detail) => {
  results.push({ id, desc, pass, detail })
  console.log(`${pass ? '  ok  ' : ' FAIL '} ${id}  ${desc}`)
  console.log(`        ${detail}`)
}

const b = await chromium.launch()
const ctx = await b.newContext({
  viewport: { width: 1600, height: 950 }, deviceScaleFactor: 1, ignoreHTTPSErrors: true,
})
const page = await ctx.newPage()
await page.goto(URL, { waitUntil: 'load' })
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

/* ───────────────── A. 载荷 → Linux → OPSEC 检测 ───────────────── */
console.log('\n=== A. 载荷 / Linux / OPSEC 检测字段 ===')
await goto('载荷')
await page.evaluate(() => {
  const b = [...document.querySelectorAll('.gen-preset')].find((x) =>
    (x.querySelector('b')?.textContent || '').trim() === 'Linux')
  if (b) b.click()
})
await page.waitForTimeout(2500)

const a = await page.evaluate(() => {
  const lab = [...document.querySelectorAll('label')].find(
    (x) => (x.textContent || '').trim().startsWith('启用 OPSEC'))
  if (!lab) return { err: '未找到 OPSEC 复选框标签' }
  const field = lab.closest('.field')
  const fr = field.getBoundingClientRect()
  const cb = field.querySelector('input[type=checkbox]')
  const cr = cb.getBoundingClientRect()
  const cs = getComputedStyle(cb)

  // 文字节点用 Range 量，text node 本身没有 getBoundingClientRect
  const tn = [...lab.childNodes].find((n) => n.nodeType === 3 && n.textContent.trim())
  let textLeft = null
  if (tn) {
    const rg = document.createRange()
    rg.selectNodeContents(tn)
    textLeft = rg.getBoundingClientRect().left
  }
  return {
    fieldW: Math.round(fr.width),
    fieldCols: getComputedStyle(field).gridTemplateColumns,
    inlineLeft: Math.round(lab.getBoundingClientRect().left - fr.left),
    cb: { x: Math.round(cr.left - fr.left), w: +cr.width.toFixed(1), h: +cr.height.toFixed(1) },
    cbCssW: cs.width,
    gapCbToText: textLeft == null ? null : Math.round(textLeft - cr.right),
    visibleText: (lab.textContent || '').trim(),
    title: lab.getAttribute('title'),
    color: getComputedStyle(lab).color,
  }
})

if (a.err) {
  check('A0', 'OPSEC 字段存在', false, a.err)
} else {
  console.log('  实测:', JSON.stringify(a))
  check('A1', '复选框本体是 14px，不是被 .field input 撑满整行',
    a.cb.w <= 16 && a.cbCssW !== '100%', `checkbox w=${a.cb.w}px cssWidth=${a.cbCssW}`)
  check('A2', '复选框与其说明文字之间无异常空隙',
    a.gapCbToText != null && a.gapCbToText >= 0 && a.gapCbToText <= 20,
    `gap=${a.gapCbToText}px（修复前约 1090px）`)
  check('A3', '复选框不再被 100px 的空标签列推开',
    a.inlineLeft <= 4, `check-inline 距 field 左边缘 ${a.inlineLeft}px（修复前 114px）`)
  check('A4', '线名 opsec_enabled 只出现在 title，不在可见文字里重复',
    a.title === 'opsec_enabled' && !a.visibleText.includes('opsec_enabled'),
    `title="${a.title}"  可见文字="${a.visibleText}"`)
}

/* ───────────────── B. 投递 → gopher（Windows + Linux 共 10 块）───────────────── */
console.log('\n=== B. 投递 / 右栏命令块 ===')
await goto('投递')
await page.waitForTimeout(1500)
// Agent 选择器上的按钮文字是 AGENT_PROFILES 的 label（"Gopher"），不是 agent 名（"gopher"）。
// Gopher 同时出 Windows 和 Linux 两组，一次能覆盖全部 10 个命令块。
const switched = await page.evaluate(() => {
  const lab = [...document.querySelectorAll('.field > label')].find((x) => (x.textContent || '').trim() === 'Agent')
  const row = lab && lab.parentElement.querySelector('.chip-row')
  const chip = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === 'Gopher')
  if (chip) chip.click()
  return !!chip
})
await page.waitForTimeout(2500)
if (!switched) check('B0', '切到 Gopher（同时出 Windows + Linux 两组命令）', false, '未找到 Agent 选择器里的 Gopher 按钮')

const bres = await page.evaluate(() => {
  const side = document.querySelector('.gen-side')
  if (!side) return { err: '未找到 .gen-side' }
  const blocks = [...side.querySelectorAll('.wd-block')]
  const cv = document.createElement('canvas').getContext('2d')
  const rows = blocks.map((blk) => {
    const ta = blk.querySelector('.wd-cmd')
    const lab = blk.querySelector('.wd-block-hd > label')
    const btn = blk.querySelector('.wd-copy')
    if (!ta) return { err: '块内无 .wd-cmd' }
    const cs = getComputedStyle(ta)
    cv.font = `${cs.fontStyle} ${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`
    const charW = cv.measureText('M').width
    const inner = ta.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight)
    // 一个"词"里最长的那段，决定 break-word 会不会在词中间断
    const longestToken = ta.value.split(/\s+/).reduce((m, t) => Math.max(m, t.length), 0)
    const r = ta.getBoundingClientRect()
    const lr = lab ? lab.getBoundingClientRect() : null
    const br = btn ? btn.getBoundingClientRect() : null
    return {
      label: (lab?.textContent || '').trim(),
      chars: ta.value.length,
      boxW: Math.round(r.width),
      boxH: Math.round(r.height),
      whiteSpace: cs.whiteSpace,
      wordBreak: cs.wordBreak,
      overflowWrap: cs.overflowWrap,
      hOverflow: ta.scrollWidth - ta.clientWidth,
      vOverflow: ta.scrollHeight - ta.clientHeight,
      charsPerLine: Math.floor(inner / charW),
      longestToken,
      lines: Math.round((ta.scrollHeight - parseFloat(cs.paddingTop) - parseFloat(cs.paddingBottom)) / parseFloat(cs.lineHeight)),
      hdRowAligned: lr && br ? Math.abs((lr.top + lr.height / 2) - (br.top + br.height / 2)) <= 2 : null,
      btnRightOfLabel: lr && br ? br.left >= lr.right : null,
    }
  })
  const page_ = document.querySelector('.gen-page')
  return {
    sideW: Math.round(side.getBoundingClientRect().width),
    sideH: Math.round(side.getBoundingClientRect().height),
    mainH: Math.round((document.querySelector('.gen-main')?.getBoundingClientRect().height) || 0),
    scrollH: page_ ? page_.scrollHeight : null,
    viewH: page_ ? page_.clientHeight : null,
    rows,
  }
})

if (bres.err) {
  check('B0', '投递右栏存在', false, bres.err)
} else {
  console.log(`  右栏 ${bres.sideW}px 宽 / ${bres.sideH}px 高，左栏 ${bres.mainH}px 高，命令块 ${bres.rows.length} 个`)
  console.log(`  页面内容 ${bres.scrollH}px，视口 ${bres.viewH}px → 需滚动 ${(bres.scrollH / bres.viewH).toFixed(1)} 屏\n`)
  for (const r of bres.rows) {
    console.log(`  · ${r.label}`)
    console.log(`      ${r.chars} 字符 / 每行 ${r.charsPerLine} 字符 / 折成 ${r.lines} 行`)
    console.log(`      框 ${r.boxW}×${r.boxH}  white-space=${r.whiteSpace} word-break=${r.wordBreak} overflow-wrap=${r.overflowWrap}`)
    console.log(`      横向溢出 ${r.hOverflow}px  纵向裁剪 ${r.vOverflow}px  最长 token ${r.longestToken} 字符`)
  }
  console.log('')

  const widths = bres.rows.map((r) => r.boxW)
  const cpl = bres.rows.map((r) => r.charsPerLine)

  check('B1', '右栏不再是 280px 的"摘要宽度"',
    bres.sideW >= 340, `右栏 ${bres.sideW}px（修复前 280px）`)
  check('B2', '命令框宽度足够',
    Math.min(...widths) >= 300, `最窄 ${Math.min(...widths)}px，最宽 ${Math.max(...widths)}px（修复前 140px）`)
  check('B3', '断行只发生在词边界（不是 break-all 从任意字符处劈开）',
    bres.rows.every((r) => r.wordBreak === 'normal' && r.overflowWrap === 'break-word'),
    [...new Set(bres.rows.map((r) => `word-break:${r.wordBreak}/overflow-wrap:${r.overflowWrap}`))].join(', '))
  check('B4', '每行能放下至少 40 个字符（20 字符/行是原来不可读的根源）',
    Math.min(...cpl) >= 40, `最少 ${Math.min(...cpl)} 字符/行，最多 ${Math.max(...cpl)}（修复前约 20）`)
  check('B5', '命令不横向溢出、不被纵向裁剪',
    bres.rows.every((r) => r.hOverflow <= 1 && r.vOverflow <= 1),
    `横向溢出 max=${Math.max(...bres.rows.map((r) => r.hOverflow))}px，纵向裁剪 max=${Math.max(...bres.rows.map((r) => r.vOverflow))}px`)
  check('B6', '盒子高度跟着命令长度长（不是固定 3 行的窗口）',
    new Set(bres.rows.map((r) => r.boxH)).size > 1,
    `出现了 ${new Set(bres.rows.map((r) => r.boxH)).size} 种高度：${[...new Set(bres.rows.map((r) => r.boxH))].sort((x, y) => x - y).join(' / ')}px`)
  check('B7', '标签与"复制"按钮同一行，按钮在标签右侧',
    bres.rows.every((r) => r.hdRowAligned === true && r.btnRightOfLabel === true),
    `对齐偏差 max=${Math.max(...bres.rows.map((r) => (r.hdRowAligned ? 0 : 99)))}px，全部按钮在标签右侧=${bres.rows.every((r) => r.btnRightOfLabel)}`)
}

const bad = results.filter((r) => !r.pass)
console.log(`\n──────── ${results.length - bad.length}/${results.length} 通过 ────────`)
if (bad.length) console.log('未通过: ' + bad.map((r) => r.id).join(', '))
await b.close()
process.exit(bad.length ? 1 : 0)

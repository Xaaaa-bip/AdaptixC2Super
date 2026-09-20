#!/usr/bin/env node
/* 逐个 Agent 把投递页的「生成参数」选项面全量 dump 出来，
   和 api/listeners.ts 的 AGENT_PROFILES 对照，找客观不一致。
   只导航 + 点击页内按钮 + 读 DOM，不点「应用并同步」（那会写服务端配置）。 */
import { chromium } from 'playwright'

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
await page.evaluate(() => {
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('投递'))
  if (it) it.click()
})
await page.waitForTimeout(3000)

/** 把每个 .field 的标签 + 控件类型 + 可选项 + 选中项都读出来 */
const dump = () => page.evaluate(() => {
  const fields = [...document.querySelectorAll('.gen-params .field')].map((f) => {
    const label = (f.querySelector(':scope > label')?.textContent || '').trim()
    const chips = [...f.querySelectorAll('.chip-row button')]
    const inputs = [...f.querySelectorAll(':scope > input, :scope > div > input')]
    return {
      label,
      chips: chips.length ? chips.map((c) => `${c.textContent.trim()}${c.classList.contains('primary') ? '★' : ''}`) : null,
      inputs: inputs.length ? inputs.map((i) => ({ value: i.value, ph: i.placeholder || '', title: i.title || '' })) : null,
    }
  })
  const summary = [...document.querySelectorAll('.gen-summary > div')].map((d) => (d.textContent || '').trim())
  return { fields, summary }
})

const chip = async (t) => {
  await page.evaluate((l) => {
    const row = [...document.querySelectorAll('.field')]
      .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
      ?.querySelector('.chip-row')
    const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === l)
    if (btn) btn.click()
  }, t)
  await page.waitForTimeout(1500)
}

for (const a of ['Beacon', 'Kharon', 'Linux Agent', 'macOS Agent', 'Gopher']) {
  await chip(a)
  const d = await dump()
  console.log(`\n═══ Agent = ${a}`)
  console.log(`  摘要: ${d.summary.join(' | ')}`)
  for (const f of d.fields) {
    console.log(`  · ${f.label}`)
    if (f.chips) console.log(`      选项: ${f.chips.join('  ')}`)
    if (f.inputs) console.log(`      输入: ${f.inputs.map((i) => `"${i.value}"${i.ph ? ` (占位 ${i.ph})` : ''}${i.title ? ` [${i.title}]` : ''}`).join('  ')}`)
  }
}
await b.close()

#!/usr/bin/env node
/* 复现：投递 → 点 Linux Agent → 点 gopher 监听 → agent 是否被顶成 gopher。
   只导航 + 读 DOM 文本，不做任何主机操作（不创建/不删除监听器）。 */
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

/** 读当前状态：选中的 agent 芯片、选中的监听、可选监听清单 */
const state = () => page.evaluate(() => {
  const chipRow = [...document.querySelectorAll('.field')]
    .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
    ?.querySelector('.chip-row')
  const activeAgent = chipRow && [...chipRow.querySelectorAll('button')].find((x) => x.classList.contains('primary'))
  const list = [...document.querySelectorAll('.gen-listener-list .gen-listener')]
  const activeL = list.find((x) => x.classList.contains('active'))
  const sum = [...document.querySelectorAll('.gen-summary > div')].map((d) => (d.textContent || '').trim())
  return {
    agent: (activeAgent?.textContent || '(无)').trim(),
    listeners: list.map((x) => {
      const nm = (x.querySelector('.name')?.textContent || '').trim()
      const sub = (x.querySelector('.sub')?.textContent || '').trim()
      return `${nm}${x.classList.contains('active') ? ' ←选中' : ''}  [${sub}]`
    }),
    selected: (activeL?.querySelector('.name')?.textContent || '(无)').trim(),
    summary: sum,
  }
})

const chip = async (label) => {
  await page.evaluate((l) => {
    const row = [...document.querySelectorAll('.field')]
      .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
      ?.querySelector('.chip-row')
    const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === l)
    if (btn) btn.click()
  }, label)
  await page.waitForTimeout(1200)
}
const pickL = async (name) => {
  await page.evaluate((n) => {
    const btn = [...document.querySelectorAll('.gen-listener-list .gen-listener')]
      .find((x) => (x.querySelector('.name')?.textContent || '').trim() === n)
    if (btn) btn.click()
  }, name)
  await page.waitForTimeout(1200)
}

const show = async (tag) => {
  const s = await state()
  console.log(`\n── ${tag}`)
  console.log(`   agent = ${s.agent}   选中监听 = ${s.selected}`)
  console.log(`   可选监听 ${s.listeners.length} 个：`)
  for (const l of s.listeners) console.log(`     · ${l}`)
  return s
}

await show('初始状态')
await chip('Linux Agent')
const afterLinux = await show('① 点了 Linux Agent 之后')
await pickL('gopher')
const afterPick = await show('② 又点了 gopher 这个监听之后')

console.log('\n──── 结论 ────')
if (afterLinux.agent.includes('Linux') && !afterPick.agent.includes('Linux')) {
  console.log(`复现成功：agent 从 "${afterLinux.agent}" 被顶成了 "${afterPick.agent}"，用户的选择被覆盖。`)
} else if (!afterLinux.agent.includes('Linux')) {
  console.log(`注意：点 Linux Agent 后 agent 就不是 Linux 了（"${afterLinux.agent}"）—— 在①这一步就已经被顶掉了。`)
} else {
  console.log(`未复现：agent 保持 "${afterPick.agent}"。`)
}
await b.close()

#!/usr/bin/env node
/* 投递页「监听器」这个选项面到底选中了什么。
   怀疑：applyStatus 会把服务端存的 listener 直接灌进 state，
   而这个值不在当前 agent 的 compatible 列表里 —— 于是没有任何一个监听高亮，
   且那个校验 effect 只依赖 compatible，不会再跑一次去纠正它。

   只导航 + 点页内按钮（含「刷新状态」，只读接口）+ 读 DOM。
   绝不点「应用并同步」（那会写服务端配置）。 */
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
await page.waitForTimeout(4000)

/** 读「监听器」选项面 + 摘要（摘要里的「监听 X」就是 listener 这个 state 的活值） */
const read = () => page.evaluate(() => {
  const lis = [...document.querySelectorAll('.gen-listener')]
  const summary = [...document.querySelectorAll('.gen-summary > div')].map((d) => (d.textContent || '').trim())
  const applyBtn = [...document.querySelectorAll('.btns button')].find((x) => (x.textContent || '').includes('应用'))
  const warn = document.querySelector('.hint.warn-hint')
  return {
    stateListener: (summary.find((s) => s.startsWith('监听')) || '').replace(/^监听/, ''),
    rendered: lis.map((l) => ({
      name: (l.querySelector('.name')?.textContent || '').trim(),
      reg: (l.querySelector('.sub')?.textContent || '').split('·')[0].trim(),
      active: l.classList.contains('active'),
    })),
    activeCount: lis.filter((l) => l.classList.contains('active')).length,
    applyDisabled: applyBtn ? applyBtn.disabled : null,
    noCompatible: !!warn,
    statusText: (document.querySelector('.btns')?.parentElement?.textContent || '').slice(-160),
  }
})

const clickAgent = async (label) => {
  await page.evaluate((l) => {
    const row = [...document.querySelectorAll('.field')]
      .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
      ?.querySelector('.chip-row')
    const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === l)
    if (btn) btn.click()
  }, label)
  await page.waitForTimeout(2000)
}

console.log('=== 1. 刚进页面（未碰任何 Agent）===')
console.log(JSON.stringify(await read(), null, 2))

console.log('\n=== 2. 点「刷新状态」之后 ===')
await page.evaluate(() => {
  const btn = [...document.querySelectorAll('.btns button')].find((x) => (x.textContent || '').includes('刷新'))
  if (btn) btn.click()
})
await page.waitForTimeout(3000)
console.log(JSON.stringify(await read(), null, 2))

console.log('\n=== 3. 依次点各 Agent，看选中态是否出现 ===')
for (const a of ['Kharon', 'Linux Agent', 'macOS Agent', 'Gopher', 'Beacon']) {
  await clickAgent(a)
  const r = await read()
  console.log(`  ${a.padEnd(12)} state监听=${JSON.stringify(r.stateListener)}  渲染 ${r.rendered.length} 个  高亮 ${r.activeCount} 个  应用键禁用=${r.applyDisabled}`)
  console.log(`      选项: ${r.rendered.map((x) => `${x.name}${x.active ? '★' : ''}(${x.reg})`).join('  ') || '（无）'}`)
}
await b.close()

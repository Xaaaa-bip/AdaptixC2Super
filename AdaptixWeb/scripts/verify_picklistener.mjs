#!/usr/bin/env node
/* 验收：投递页选 Agent / 选监听不再互相顶掉。
   覆盖 GopherTCP（gopher+linux+macos 共用）与 BeaconWS（beacon+linux 共用）两条路径。
   只导航 + 点击页内按钮 + 读 DOM 文本，不创建/删除/改动任何监听器。 */
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
await page.evaluate(() => {
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('投递'))
  if (it) it.click()
})
await page.waitForTimeout(3000)

const state = () => page.evaluate(() => {
  const row = [...document.querySelectorAll('.field')]
    .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
    ?.querySelector('.chip-row')
  const act = row && [...row.querySelectorAll('button')].find((x) => x.classList.contains('primary'))
  const list = [...document.querySelectorAll('.gen-listener-list .gen-listener')]
  const sel = list.find((x) => x.classList.contains('active'))
  return {
    agent: (act?.textContent || '(无)').trim(),
    selected: (sel?.querySelector('.name')?.textContent || '(无)').trim(),
    visible: list.map((x) => (x.querySelector('.name')?.textContent || '').trim()),
  }
})
const chip = async (l) => {
  await page.evaluate((t) => {
    const row = [...document.querySelectorAll('.field')]
      .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
      ?.querySelector('.chip-row')
    const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === t)
    if (btn) btn.click()
  }, l)
  await page.waitForTimeout(1500)
}
const pickL = async (n) => {
  const hit = await page.evaluate((t) => {
    const btn = [...document.querySelectorAll('.gen-listener-list .gen-listener')]
      .find((x) => (x.querySelector('.name')?.textContent || '').trim() === t)
    if (btn) btn.click()
    return !!btn
  }, n)
  await page.waitForTimeout(1500)
  return hit
}

console.log('=== 投递页 Agent / 监听 联动 ===\n')

// ① 换 Agent 本身要生效（这是被 bug 顶掉的那一步）
await chip('Linux Agent')
let s = await state()
check('C1', '点「Linux Agent」后 agent 真的变成 Linux Agent（不再被反推顶回 Beacon）',
  s.agent.includes('Linux'), `agent = ${s.agent}，可见监听 = [${s.visible.join(', ')}]`)

// ② 共用监听 GopherTCP：选它不能把 agent 顶成 gopher
const hadGopher = s.visible.includes('gopher')
if (hadGopher) {
  await pickL('gopher')
  s = await state()
  check('C2', 'Linux Agent 下选 gopher（GopherTCP 三属共用）仍保持 Linux Agent',
    s.agent.includes('Linux') && s.selected === 'gopher',
    `agent = ${s.agent}，选中 = ${s.selected}`)
} else {
  check('C2', 'Linux Agent 下可见 gopher 监听', false, `服务端没有对 linux 可见的 gopher 监听，可见 = [${s.visible.join(', ')}]`)
}

// ③ 共用监听 BeaconWS：切到 Linux Agent 时不能被顶回 beacon
await chip('Beacon')
await page.waitForTimeout(800)
await chip('Linux Agent')
s = await state()
const hasWs = s.visible.some((n) => /TestWS|TestWSS/.test(n))
check('C3', 'BeaconWS 共用监听不会把 Linux Agent 顶回 beacon',
  s.agent.includes('Linux') && (!hasWs || !!s.selected),
  `agent = ${s.agent}，选中 = ${s.selected}，可见 = [${s.visible.join(', ')}]`)

// ④ 另一个共用者 macOS Agent
await chip('macOS Agent')
s = await state()
check('C4', '点「macOS Agent」后保持 macOS Agent',
  s.agent.includes('macOS'), `agent = ${s.agent}，可见 = [${s.visible.join(', ')}]`)

// ⑤ 反向：点监听不能改 agent（gopher 监听三属共用，最容易触发反推）
await chip('Gopher')
s = await state()
const beforeAgent = s.agent
if (s.visible.length) {
  await pickL(s.visible[0])
  s = await state()
  check('C5', '单独点监听不会改 agent（箭头只从 Agent 指向监听）',
    s.agent === beforeAgent, `点监听前 agent = ${beforeAgent}，点监听后 = ${s.agent}`)
} else {
  check('C5', 'Gopher 下可见监听', false, '可见监听为空')
}

// ⑥ Kharon 没有兼容监听时，不能留下一个孤立的选中监听
await chip('Kharon')
s = await state()
check('C6', '切到无兼容监听的 agent 时清空选中监听',
  s.visible.length === 0 ? s.selected === '(无)' : true,
  `agent = ${s.agent}，可见 ${s.visible.length} 个，选中 = ${s.selected}`)

const bad = results.filter((r) => !r.pass)
console.log(`\n──────── ${results.length - bad.length}/${results.length} 通过 ────────`)
if (bad.length) console.log('未通过: ' + bad.map((r) => r.id).join(', '))
await b.close()
process.exit(bad.length ? 1 : 0)

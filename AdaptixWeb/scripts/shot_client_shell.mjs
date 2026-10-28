/* 客户端壳（client.css 布局）全量截图 —— 登录 / 概览 / 各面板 / 收合 dock / 浅色档 */
import { mkdirSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { launchConsole, fakeAgent, fakeListener } from './_harness.mjs'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const OUT = path.join(__dirname, '..', 'screenshots')
mkdirSync(OUT, { recursive: true })

const agents = [
  fakeAgent(0, { a_computer: 'hx-node-01', a_username: 'root', a_tags: '生产' }),
  fakeAgent(1, { a_os: 0, a_computer: 'WIN-DC01', a_username: 'administrator', a_domain: 'corp', a_listener: 'BeaconHTTP' }),
  fakeAgent(2, { a_computer: 'hx-node-03', a_username: 'ubuntu', a_last_tick: Math.floor(Date.now() / 1000) - 700, a_tags: '测试' }),
  fakeAgent(3, { a_os: 1, a_computer: 'macbook-pro', a_username: 'dev', a_listener: 'GopherTCP' }),
  fakeAgent(4, { a_computer: 'hx-node-05', a_username: 'root', a_tags: '生产' }),
  fakeAgent(5, { a_os: 0, a_computer: 'WIN-FIN-07', a_username: 'finance', a_listener: 'BeaconWS' }),
]
const listeners = [
  fakeListener('LinuxTCP'),
  fakeListener('BeaconWS', { l_type: 'ws' }),
]

const h = await launchConsole({ agents, listeners })
const { page } = h

async function shot(name) {
  await page.screenshot({ path: path.join(OUT, `client_${name}.png`) })
  console.log('shot', name)
}

/** 按工具条按钮 title 前缀点导航 */
async function tool(label) {
  const ok = await page.evaluate((label) => {
    const btn = [...document.querySelectorAll('.cl-btn')]
      .find((e) => (e.getAttribute('title') || '').startsWith(label))
    if (!btn) return false
    btn.click()
    return true
  }, label)
  if (!ok) throw new Error(`工具条没有「${label}」`)
  await page.waitForTimeout(700)
}

await shot('01_hosts')

// 概览是轻量状态页，主机工作区保持完整可用。
await tool('概览')
await shot('02_overview')

for (const [label, name] of [
  ['载荷', '03_generate'],
  ['投递', '04_delivery'],
  ['监听器', '05_listeners'],
  ['任务', '06_tasks'],
]) {
  await tool(label)
  await shot(name)
}

// 事件 dock 按需打开，再收起
await page.click('.cl-btn[title="事件日志"]')
await page.waitForTimeout(300)
await shot('10_events_open')
await page.evaluate(() => { const b = [...document.querySelectorAll('.cl-dock-btn')].find((e) => e.title.includes('事件')); b && b.click() })
await page.waitForTimeout(300)
await shot('11_events_collapsed')

// 回概览，切浅色档
await tool('概览')
await page.evaluate(() => { const b = [...document.querySelectorAll('.cl-ghost')].find((e) => (e.title === '浅色')); b && b.click() })
await page.waitForTimeout(500)
await shot('12_light_theme')

// 回暗色，退出登录 → 登录页
await page.evaluate(() => { const b = [...document.querySelectorAll('.cl-ghost')].find((e) => (e.title === '深色')); b && b.click() })
await page.waitForTimeout(300)
await page.evaluate(() => { const b = [...document.querySelectorAll('.cl-ghost')].find((e) => e.textContent === '退出'); b && b.click() })
await page.waitForSelector('.lx-card', { timeout: 10000 })
await shot('13_login')

await h.close()
console.log('done →', OUT)

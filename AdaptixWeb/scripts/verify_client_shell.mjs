/* 客户端壳结构审计 —— 可计算断言（不靠目测）
 * 覆盖：壳结构 / dock 行为 / 标签页 / 明暗两档都有样式（主题作用域陷阱回归）
 *      / 横向溢出 / 紧凑密度 / 工具条导航完整性 */
import { launchConsole, fakeAgent, fakeListener } from './_harness.mjs'

let pass = 0, fail = 0
function ok(cond, name, extra = '') {
  if (cond) { pass++; console.log(`  ✓ ${name}`) }
  else { fail++; console.error(`  ✗ ${name} ${extra}`) }
}

const agents = [fakeAgent(0), fakeAgent(1, { a_os: 0 }), fakeAgent(2)]
const h = await launchConsole({ agents, listeners: [fakeListener('LinuxTCP'), fakeListener('BeaconWS', { l_type: 'ws' })] })
const { page } = h

console.log('— 壳结构 —')
ok(await page.$('.cl-bar'), '顶部工具条存在')
ok(await page.$('.cl-dock.hosts') === null, '概览默认不被主机 dock 挤压')
ok(await page.$('.cl-dock.events') === null, '概览默认不被事件 dock 挤压')
ok(await page.$('.sess'), '登录后直接进入主机工作区')
ok(await page.$('.ops-status'), '状态栏存在')
ok(await page.$('.cl-tabs .cl-tab.active'), '工作区标签条 + 激活标签')
const btnCount = await page.locator('.cl-btn').count()
ok(btnCount >= 7, `高频工具条入口齐全（${btnCount} ≥ 7）`)
ok(await page.$('.rail') === null, '旧侧栏已移除')
ok(await page.$('.ink-scape') === null, '水墨装饰层已移除')

console.log('— 壳几何（导入顺序陷阱回归：styles.css 遗留 .ops grid 压垮 flex） —')
const geo = await page.evaluate(() => {
  const ops = getComputedStyle(document.querySelector('.ops'))
  const bar = document.querySelector('.cl-bar').getBoundingClientRect()
  return { display: ops.display, barW: Math.round(bar.width), innerW: window.innerWidth }
})
// display=flex + 工具条铺满 = 遗留 grid 壳失效的充分判据。
// （不能断言 gridTemplateColumns==='none'：Chrome 对非应用属性也计算声明值）
ok(geo.display === 'flex', `.ops display=flex（实际 ${geo.display}）`)
ok(geo.barW >= geo.innerW - 4, `工具条铺满视口（${geo.barW}/${geo.innerW}）`)

// 概览是独占工作区；事件流从工具条按需打开。
await page.click('.cl-btn[title="事件日志"]')
await page.waitForTimeout(250)
ok(await page.$('.cl-dock.events') !== null, '事件 dock 可按需打开')

console.log('— 主机工作区 —')
const rows = await page.locator('.cl-dock .sess-table tbody tr').count()
const hostRows = await page.locator('.sess-table tbody tr').count()
ok(hostRows === agents.length, `主机表行数 = ${hostRows}（应为 ${agents.length}）`)
const thPos = await page.$eval('.sess-table th', (el) => getComputedStyle(el).position)
ok(thPos === 'sticky', `表头 sticky（${thPos}）`)
ok(rows === 0, '旧主机 dock 已移除，避免重复显示')

console.log('— 事件 dock —')
ok(await page.$('.ev-list') !== null, '事件流容器存在')
const evH = await page.$eval('.cl-dock.events', (el) => el.getBoundingClientRect().height)
ok(evH > 80, `事件 dock 高度合理（${Math.round(evH)}px）`)

console.log('— 横向溢出 —')
for (const view of ['载荷', '投递', '监听器', '扩展命令', '拓扑']) {
  await page.evaluate((label) => {
    const btn = [...document.querySelectorAll('.cl-btn')].find((e) => (e.getAttribute('title') || '').startsWith(label))
    btn?.click()
  }, view)
  await page.waitForTimeout(500)
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth)
  ok(overflow <= 1, `${view}：无横向溢出（${overflow}px）`)
}

console.log('— 主题作用域陷阱（历史两次踩坑的回归） —')
// 切浅色，关键组件必须仍有样式（背景/颜色不塌）
await page.click('.cl-btn[title^="主机"]')
await page.waitForTimeout(250)
await page.evaluate(() => { const b = [...document.querySelectorAll('.cl-ghost')].find((e) => e.title === '浅色'); b?.click() })
await page.waitForTimeout(400)
const lightAudit = await page.evaluate(() => {
  const th = document.querySelector('.sess-table th')
  const ev = document.querySelector('.ev-line') || document.querySelector('.ev-empty')
  const tab = document.querySelector('.cl-tab.active')
  const pill = document.querySelector('.cl-pill')
  const probe = (el) => {
    if (!el) return null
    const s = getComputedStyle(el)
    return s
  }
  return {
    thBg: th ? getComputedStyle(th).backgroundColor : null,
    tabBg: tab ? getComputedStyle(tab).backgroundColor : null,
    pillColor: pill ? getComputedStyle(pill).color : null,
    evColor: ev ? getComputedStyle(ev).color : null,
  }
})
ok(lightAudit.thBg && lightAudit.thBg !== 'rgba(0, 0, 0, 0)' && lightAudit.thBg !== 'transparent', `浅色档表头有背景（${lightAudit.thBg}）`)
ok(lightAudit.tabBg && lightAudit.tabBg !== 'rgba(0, 0, 0, 0)', `浅色档激活标签有背景（${lightAudit.tabBg}）`)
ok(!!lightAudit.pillColor, `浅色档计数 pill 有颜色（${lightAudit.pillColor}）`)

// CSSOM：作用域规则允许存在（旧文件残留），但每个组件类必须有**无作用域**兜底
// —— 没有兜底才是「切浅色整片裸 HTML」的陷阱（2026-09-21 踩过两次）
const scopedAudit = await page.evaluate(() => {
  const scoped = new Set(), unscoped = new Set()
  for (const sheet of document.styleSheets) {
    let rules
    try { rules = sheet.cssRules } catch { continue }
    for (const r of rules) {
      if (!r.selectorText) continue
      const sel = r.selectorText
      if (!/\.cl-|\.ev-line|\.ev-bar|\.lx-card|\.lx-input|\.lx-submit/.test(sel)) continue
      for (const s of sel.split(',')) {
        const t = s.trim()
        if (t.includes('[data-theme')) scoped.add(t)
        else unscoped.add(t)
      }
    }
  }
  const missing = []
  for (const s of scoped) {
    const cls = [...s.matchAll(/\.(cl-[\w-]+|ev-[\w-]+|lx-[\w-]+)/g)].map((m) => m[1])
    for (const c of new Set(cls)) {
      if (![...unscoped].some((u) => u.includes('.' + c))) missing.push(`${s} [${c}]`)
    }
  }
  return { scoped: scoped.size, missing }
})
ok(scopedAudit.missing.length === 0,
  `作用域规则均有无作用域兜底（scoped ${scopedAudit.scoped} 条，缺兜底 ${scopedAudit.missing.length}${scopedAudit.missing.length ? ': ' + scopedAudit.missing[0] : ''}）`)

// 回暗色
await page.evaluate(() => { const b = [...document.querySelectorAll('.cl-ghost')].find((e) => e.title === '深色'); b?.click() })
await page.waitForTimeout(200)

console.log('— 紧凑密度 —')
await page.evaluate(() => { const b = [...document.querySelectorAll('.cl-ghost')].find((e) => e.title === '密度'); b?.click() })
await page.waitForTimeout(200)
const dens = await page.$eval('.ops', (el) => el.getAttribute('data-density'))
ok(dens === 'compact', `密度切换生效（${dens}）`)

console.log('— 键盘：G H 聚焦主机搜索 —')
await page.keyboard.press('g')
await page.waitForTimeout(150)
await page.keyboard.press('h')
await page.waitForTimeout(400)
const focusCls = await page.evaluate(() => document.activeElement?.className || '')
ok(focusCls.includes('sess-search'), `G H 聚焦主机搜索（focus=${focusCls.slice(0, 30)}）`)

await h.close()
console.log(`\n结果：${pass} 通过 / ${fail} 失败`)
process.exit(fail ? 1 : 0)

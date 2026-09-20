#!/usr/bin/env node
/* 第 5 批截图：载荷 Linux 的 OPSEC 字段 + 投递右栏命令块。只导航 + 截图。 */
import { chromium } from 'playwright'

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, deviceScaleFactor: 1, ignoreHTTPSErrors: true })
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

await goto('载荷')
await page.evaluate(() => {
  const b = [...document.querySelectorAll('.gen-preset')].find((x) =>
    (x.querySelector('b')?.textContent || '').trim() === 'Linux')
  if (b) b.click()
})
await page.waitForTimeout(2500)

// 滚到 OPSEC 字段并截图。
// 截"重连间隔/重连次数 + OPSEC"这一整块而不是 OPSEC 单行：
// 用户报的是"间隔过大"，单行 958×30 的细条看不出间距，要连着上一行才有参照。
const ok = await page.evaluate(() => {
  const l = [...document.querySelectorAll('label')].find((x) => (x.textContent || '').trim().startsWith('启用 OPSEC'))
  if (!l) return null
  const f = l.closest('.field')
  f.scrollIntoView({ block: 'center' })
  // 从 OPSEC 往上找到它的兄弟网格行，一起框进来
  const prev = f.previousElementSibling
  const top = (prev && prev.classList.contains('field-grid-2') ? prev : f).getBoundingClientRect().top
  const r = f.getBoundingClientRect()
  return { x: Math.round(r.left) - 10, y: Math.round(top) - 10, width: Math.round(r.width) + 20, height: Math.round(r.bottom - top) + 20 }
})
await page.waitForTimeout(800)
if (!ok) console.log('!! 没定位到 OPSEC 字段，检查标签文字是否又改了')
else await page.screenshot({ path: 'shots/b5-opsec-field.png', clip: ok })
await page.screenshot({ path: 'shots/b5-generate.png' })

await goto('投递')
await page.waitForTimeout(1500)
// 切到 Gopher：Windows + Linux 两组命令一起出，一屏能看到全部 10 块
await page.evaluate(() => {
  const lab = [...document.querySelectorAll('.field > label')].find((x) => (x.textContent || '').trim() === 'Agent')
  const row = lab && lab.parentElement.querySelector('.chip-row')
  const chip = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === 'Gopher')
  if (chip) chip.click()
})
await page.waitForTimeout(2000)
const side = await page.$('.gen-side')
if (side) await side.screenshot({ path: 'shots/b5-delivery-side.png' })
await page.screenshot({ path: 'shots/b5-delivery.png' })
console.log('已保存 shots/b5-opsec-field.png / b5-generate.png / b5-delivery-side.png / b5-delivery.png')
await b.close()

#!/usr/bin/env node
/* 把三份截图审查里对 投递 / 监听器 / 拓扑 / 任务 的具体断言逐条量一遍。

   审查员是看图说话的，量出来才知道哪些是真缺陷、哪些是截图误读。
   这里只测量，不做任何主机操作。

   用法: ADAPTIX_USER=.. ADAPTIX_PASS=.. node scripts/claims_probe.mjs */
import { chromium } from 'playwright'

const b = await chromium.launch()
const ctx = await b.newContext({
  viewport: { width: 1600, height: 950 }, deviceScaleFactor: 1, ignoreHTTPSErrors: true,
})
const page = await ctx.newPage()
await page.goto(process.env.ADAPTIX_URL || 'https://154.198.162.34:38212/', { waitUntil: 'load' })
await page.waitForTimeout(1200)
const ins = await page.$$('.lx-input')
await ins[0].fill(process.env.ADAPTIX_USER)
await ins[1].fill(process.env.ADAPTIX_PASS)
await page.click('.lx-submit')
await page.waitForSelector('.ops', { timeout: 20000 }).catch(() => {})
await page.waitForTimeout(4500)

async function goto(label) {
  const ok = await page.evaluate((lab) => {
    const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes(lab))
    if (!it) return false
    it.click(); return true
  }, label)
  await page.waitForTimeout(2500)
  return ok
}

const R = (s) => Math.round(s)

/* ---------- 投递 ---------- */
console.log('\n########## 投递 ##########')
if (await goto('投递')) {
  console.log(JSON.stringify(await page.evaluate(() => {
    const out = {}
    // 找所有 code/pre 块，看宽度与是否横向可滚
    const codes = [...document.querySelectorAll('code, pre')].filter((e) => {
      const r = e.getBoundingClientRect()
      return r.width > 0 && (e.textContent || '').length > 40
    })
    out.codeBlocks = codes.slice(0, 6).map((e) => {
      const r = e.getBoundingClientRect()
      const cs = getComputedStyle(e)
      return {
        w: Math.round(r.width), h: Math.round(r.height),
        scrollW: e.scrollWidth, clientW: e.clientWidth,
        canScrollX: e.scrollWidth > e.clientWidth + 1,
        overflowX: cs.overflowX, whiteSpace: cs.whiteSpace,
        textHead: (e.textContent || '').trim().slice(0, 46),
      }
    })
    // 有没有以 ttps:// 开头的（审查员怀疑缺 h）
    const all = document.body.innerText || ''
    out.looksLikeMissingH = (all.match(/\bttps:\/\//g) || []).length
    out.hasHttps = (all.match(/https:\/\//g) || []).length
    // 左右栏宽度
    const cols = [...document.querySelectorAll('*')].filter((e) => e.children.length > 2)
    out.widestCols = cols.slice(0, 3).map((e) => ({
      cls: e.className?.toString?.().slice(0, 40), w: Math.round(e.getBoundingClientRect().width),
    }))
    return out
  }), null, 2))
}

/* ---------- 监听器 ---------- */
console.log('\n########## 监听器 ##########')
if (await goto('监听器')) {
  console.log(JSON.stringify(await page.evaluate(() => {
    const rows = [...document.querySelectorAll('tbody tr')]
    const first = rows[0]
    const out = { rowCount: rows.length }
    if (first) {
      out.cells = [...first.children].map((td) => ({
        text: (td.textContent || '').trim().slice(0, 22),
        w: Math.round(td.getBoundingClientRect().width),
      }))
      const btns = [...first.querySelectorAll('button')]
      out.firstRowButtons = btns.map((x) => {
        const r = x.getBoundingClientRect()
        const cs = getComputedStyle(x)
        return {
          text: (x.textContent || '').trim(), w: Math.round(r.width), h: Math.round(r.height),
          x: Math.round(r.left), y: Math.round(r.top),
          bg: cs.backgroundColor, color: cs.color,
          disabled: x.disabled,
        }
      })
      out.rowHeight = Math.round(first.getBoundingClientRect().height)
    }
    // 表头
    out.headers = [...document.querySelectorAll('thead th')].map((th) => (th.textContent || '').trim())
    // 状态徽章
    const badge = document.querySelector('tbody .badge, tbody .lv, tbody .tag')
    if (badge) {
      const cs = getComputedStyle(badge)
      out.statusBadge = { text: (badge.textContent || '').trim(), cls: badge.className, color: cs.color, border: cs.borderColor, bg: cs.backgroundColor }
    }
    return out
  }), null, 2))
}

/* ---------- 拓扑 ---------- */
console.log('\n########## 拓扑 ##########')
if (await goto('拓扑')) {
  console.log(JSON.stringify(await page.evaluate(() => {
    const out = {}
    const nodes = [...document.querySelectorAll('.gnode, .graph-node, [class*="node"]')].filter((e) => {
      const r = e.getBoundingClientRect(); return r.width > 20 && r.height > 10
    })
    out.nodeCount = nodes.length
    out.nodes = nodes.slice(0, 6).map((e) => {
      const r = e.getBoundingClientRect()
      const cs = getComputedStyle(e)
      return {
        cls: (e.className?.toString?.() || '').slice(0, 34),
        rect: [Math.round(r.x), Math.round(r.y), Math.round(r.width), Math.round(r.height)],
        bg: cs.backgroundColor, border: cs.borderColor,
        text: (e.textContent || '').trim().slice(0, 34),
      }
    })
    // 图例文字
    const body = document.body.innerText || ''
    out.legendHit = (body.match(/[^\n]*(在线|离线)[^\n]*/g) || []).slice(0, 4)
    // 画布尺寸 vs 节点占用
    const svg = document.querySelector('svg')
    if (svg) {
      const r = svg.getBoundingClientRect()
      out.svg = [Math.round(r.width), Math.round(r.height)]
    }
    return out
  }), null, 2))
}

/* ---------- 任务 ---------- */
console.log('\n########## 任务 ##########')
if (await goto('任务')) {
  console.log(JSON.stringify(await page.evaluate(() => {
    const rows = [...document.querySelectorAll('tbody tr')]
    const out = { rowCount: rows.length }
    out.headers = [...document.querySelectorAll('thead th')].map((th) => (th.textContent || '').trim())
    // 状态徽章颜色统计
    const lvs = [...document.querySelectorAll('tbody .lv')]
    out.lvClasses = {}
    for (const l of lvs) {
      const k = l.className
      out.lvClasses[k] = (out.lvClasses[k] || 0) + 1
    }
    if (lvs[0]) {
      const cs = getComputedStyle(lvs[0])
      out.lvSample = { text: (lvs[0].textContent || '').trim(), color: cs.color, bg: cs.backgroundColor, border: cs.borderColor, cls: lvs[0].className }
    }
    // 复选框
    out.checkboxes = [...document.querySelectorAll('input[type=checkbox]')].slice(0, 4).map((c) => {
      const r = c.getBoundingClientRect()
      return { w: Math.round(r.width), h: Math.round(r.height), accent: getComputedStyle(c).accentColor }
    })
    // 主机列第二行字号
    if (rows[0]) {
      const small = [...rows[0].querySelectorAll('span,div')].filter((e) => {
        const fs = parseFloat(getComputedStyle(e).fontSize); return fs > 0 && fs <= 11
      })
      out.smallText = small.slice(0, 3).map((e) => ({ text: (e.textContent || '').trim().slice(0, 16), fs: getComputedStyle(e).fontSize, color: getComputedStyle(e).color }))
    }
    return out
  }), null, 2))
}

await b.close()

#!/usr/bin/env node
/* 对比度审计 —— 遍历渲染台所有可见文字节点，算它相对"实际合成背景"的对比度。
   不是查 CSS 变量，是查最终像素：逐层向上找第一个不透明背景并合成。
   WCAG AA：正文 4.5:1，大字(≥18.66px 或 ≥14px 粗体) 3:1。 */
import { chromium } from 'playwright'

const browser = await chromium.launch()
const page = await browser.newPage({ viewport: { width: 1600, height: 950 } })
await page.goto('http://127.0.0.1:8899/console.html', { waitUntil: 'load' })
await page.waitForTimeout(2600)

const rows = await page.evaluate(() => {
  const parse = (c) => {
    const m = c.match(/rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)/)
    return m ? [+m[1], +m[2], +m[3], m[4] === undefined ? 1 : +m[4]] : null
  }
  const over = (fg, bg) => {
    const a = fg[3]
    return [fg[0] * a + bg[0] * (1 - a), fg[1] * a + bg[1] * (1 - a), fg[2] * a + bg[2] * (1 - a), 1]
  }
  // 从元素往上找，把每层背景按 alpha 依次合成，直到遇到不透明层
  const effBg = (el) => {
    const layers = []
    for (let n = el; n; n = n.parentElement) {
      const cs = getComputedStyle(n)
      const c = parse(cs.backgroundColor)
      if (c && c[3] > 0) {
        layers.push(c)
        if (c[3] >= 0.999) break
      }
      // 有背景图/滤镜的层视为不透明屏障（无法可靠合成），
      // 但继续向上累加会让结果偏乐观，所以遇到就停
      if (cs.backgroundImage !== 'none' && layers.length) break
    }
    let acc = [7, 9, 13, 1] // --paper 兜底
    for (let i = layers.length - 1; i >= 0; i--) acc = over(layers[i], acc)
    return acc
  }
  const lum = (c) => {
    const f = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4 }
    return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2])
  }
  const ratio = (a, b) => {
    const [x, y] = [lum(a), lum(b)].sort((p, q) => q - p)
    return (x + 0.05) / (y + 0.05)
  }

  const out = []
  const seen = new Set()
  document.querySelectorAll('body *').forEach((el) => {
    // 只取直接含文字的叶子
    const txt = [...el.childNodes].filter((n) => n.nodeType === 3).map((n) => n.textContent.trim()).join('')
    if (!txt) return
    const r = el.getBoundingClientRect()
    if (r.width < 2 || r.height < 2) return
    const cs = getComputedStyle(el)
    if (cs.visibility === 'hidden' || cs.display === 'none') return
    if (parseFloat(cs.opacity) < 0.15) return
    const fg = parse(cs.color)
    if (!fg) return
    if (txt.length > 24) return // 长正文另算，先看标签
    const bg = effBg(el)
    const eff = over(fg, bg)
    const cr = ratio(eff, bg)
    const size = parseFloat(cs.fontSize)
    const bold = parseInt(cs.fontWeight, 10) >= 700
    const large = size >= 18.66 || (size >= 14 && bold)
    const need = large ? 3 : 4.5
    if (cr < need) {
      const key = `${cs.color}|${Math.round(cr * 10)}|${txt.slice(0, 12)}`
      if (seen.has(key)) return
      seen.add(key)
      out.push({
        txt: txt.slice(0, 18),
        sel: el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).join('.') : el.tagName,
        color: cs.color,
        bg: `rgb(${bg.slice(0, 3).map(Math.round).join(',')})`,
        size: Math.round(size * 10) / 10,
        bold,
        ratio: Math.round(cr * 100) / 100,
        need,
      })
    }
  })
  return out
})

rows.sort((a, b) => a.ratio - b.ratio)
console.log(JSON.stringify({ fails: rows.length, rows }, null, 1))
await browser.close()

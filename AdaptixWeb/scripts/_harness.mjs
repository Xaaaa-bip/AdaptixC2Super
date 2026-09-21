/* 离线控制台 harness —— 给所有布局/行为探查脚本共用。
 *
 * ★ 完全离线：起个静态服务器供 dist/，把控制台会打的每个接口都拦成假数据，
 *   不登录任何真实控制台、不带任何凭据、不向任何主机下发任何东西。
 *
 * 为什么必须经 HTTP 而不是 file://：登录表单的 baseUrl 留空时接口走相对路径
 * （http.ts:282），file:// 下会解析成 file:///api/login，fetch 直接抛，
 * 拦都拦不到。起静态服务器让 origin 变成 http://127.0.0.1:PORT，
 * 相对路径才有意义，WS 的 connectWsUrl 也自然落到同一个 host 上。
 *
 * 为什么不打在真控制台上量布局：真机数据每天不同，量出来的基线不可复现，
 * 而且「点执行」会真下发。假数据的名字/数量是我定的，断言才能精确到值。
 * 代价（诚实声明）：**不覆盖**真实服务端返回的字段差异 —— 那条由
 * scripts/bofs_ui_check.mjs 这类源码级契约检查负责。
 *
 * 安全约束：任何脚本都**不得**让 /api/agent/command/** 走 abort ——
 * runBof 的 catch 会回退到 sendCommand(cmdline)（另一个端点），
 * 等于把本该拦下的下发换个门送出去。这里一律伪造 200。
 */
import { chromium } from 'playwright'
import http from 'node:http'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const DIST = path.join(__dirname, '..', 'dist')

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon',
  '.json': 'application/json', '.woff2': 'font/woff2', '.woff': 'font/woff' }

/** 造一台假主机。os: 2 = Linux（isLinuxAgent 认这个），0 = Windows，1 = macOS */
export function fakeAgent(i = 0, over = {}) {
  const os = over.a_os ?? 2
  const osName = { 0: 'Windows', 1: 'macOS', 2: 'Linux' }[os] || 'Linux'
  const now = Math.floor(Date.now() / 1000)
  return {
    a_id: (over.a_id || `aabbccdd${String(i).padStart(8, '0')}`),
    a_name: over.a_name || 'linux_agent',
    a_listener: over.a_listener || 'LinuxTCP',
    a_os: os, a_os_desc: over.a_os_desc || `${osName} 22.04`, a_arch: over.a_arch || 'x86_64',
    a_computer: over.a_computer || `hx-node-${String(i + 1).padStart(2, '0')}`,
    a_username: over.a_username || 'root',
    a_process: over.a_process || '/usr/bin/apt-helper',
    a_pid: String(over.a_pid || 4200 + i), a_tid: String(over.a_tid || 4200 + i),
    a_internal_ip: over.a_internal_ip || `10.0.4.${17 + i}`,
    a_external_ip: over.a_external_ip || `203.0.113.${9 + i}`,
    a_domain: over.a_domain ?? '', a_tags: over.a_tags ?? '', a_mark: over.a_mark ?? '',
    a_elevated: over.a_elevated ?? true,
    a_create_time: now - 600, a_last_tick: now, a_sleep: 5, a_jitter: 0,
    a_acp: 0, a_oemcp: 0,
    ...over,
  }
}

/** 造一个假监听器 */
export function fakeListener(name, over = {}) {
  return { l_name: name, l_type: 'linux', l_status: 'Listen', l_data: '{}',
    l_create_time: Math.floor(Date.now() / 1000) - 3600, ...over }
}

/**
 * 起一套离线控制台。
 * @param {object} o
 * @param {any[]} o.agents     假主机（默认 1 台 linux）
 * @param {any[]} o.listeners  假监听器
 * @param {object} o.routes    额外/覆盖的接口处理：pathname → (route, body) => void|data
 * @param {object} o.bofs      /api/bofs/list 的返回（默认 null = 返回空清单）
 */
export async function launchConsole(o = {}) {
  if (!fs.existsSync(path.join(DIST, 'index.html'))) {
    throw new Error(`dist/index.html 不存在 —— 先 npm run build（找的是 ${DIST}）`)
  }
  const agents = o.agents ?? [fakeAgent(0)]
  const listeners = o.listeners ?? [fakeListener('LinuxTCP')]
  const extraRoutes = o.routes || {}
  const sent = []   // 拦到的下发
  const seen = []   // 所有被拦到的 pathname

  const server = http.createServer((req, res) => {
    const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname)
    let f = path.join(DIST, rel === '/' ? 'index.html' : rel)
    if (!f.startsWith(DIST) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) f = path.join(DIST, 'index.html')
    res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' })
    fs.createReadStream(f).pipe(res)
  })
  await new Promise((r) => server.listen(0, '127.0.0.1', r))
  const url = `http://127.0.0.1:${server.address().port}/`

  const browser = await chromium.launch()
  // permissions 走显式开关：默认不给。要给的话得是真的需要
  //（比如量剪贴板保真 —— 不授权时 navigator.clipboard.writeText 会抛，
  //  代码会静默退到 execCommand 兜底，于是「剪贴板内容对不对」这条断言
  //  测的其实是另一条代码路径，绿得毫无意义）。
  const ctx = await browser.newContext({
    viewport: o.viewport || { width: 1600, height: 950 },
    ...(o.permissions ? { permissions: o.permissions } : {}),
  })
  const page = await ctx.newPage()

  // WS：接受连接后保持静默。必须有 —— finishConnect 会 await 建连，
  // 不接的话页面一直停在 syncing。
  await page.routeWebSocket(o.wsPattern || /\/api\/connect/, () => { /* 静默 */ })

  await page.route('**/api/**', async (route) => {
    const p = new URL(route.request().url()).pathname
    seen.push(p)
    const json = (v) => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(v) })
    let body = {}
    try { body = JSON.parse(route.request().postData() || '{}') } catch { /* 保持 {} */ }

    if (extraRoutes[p]) return extraRoutes[p](route, body, json)

    if (p === '/api/login') return json({ ok: true, message: '', access_token: 'fake.access', refresh_token: 'fake.refresh' })
    if (p === '/api/otp/generate') return json({ ok: true, message: 'fake-otp' })
    if (p === '/api/agent/list') return json(agents)
    if (p === '/api/listener/list') return json(listeners)
    if (p === '/api/download/list') return json([])
    if (p === '/api/agent/task/list') return json([])
    if (p === '/api/targets/list') return json(o.targets ?? [])
    if (p === '/api/bofs/list') return json(o.bofs ?? { base: '', entries: [] })
    // ★ 下发：记录 + 伪造成功。绝不 abort（见文件头）。
    if (p.startsWith('/api/agent/command')) { sent.push({ path: p, body }); return json({ message: '', ok: true }) }
    return json({})
  })

  await page.goto(url, { waitUntil: 'load' })
  await page.waitForSelector('.lx-input', { timeout: 15000 })
  await page.fill('.lx-input >> nth=0', 'harness')
  await page.fill('.lx-input >> nth=1', 'harness')
  await page.click('.lx-submit')
  await page.waitForSelector('.ops', { timeout: 20000 })
  await page.waitForTimeout(1200)

  return {
    page, sent, seen, url,
    /** 点左侧导航进某个视图（按文案匹配） */
    async gotoView(label) {
      const ok = await page.evaluate((label) => {
        const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes(label))
        if (!it) return false
        it.click()
        return true
      }, label)
      if (!ok) throw new Error(`左栏没有「${label}」`)
      await page.waitForTimeout(900)
    },
    async close() { await browser.close(); server.close() },
  }
}

/** 通用的「量一个元素盒」——避免各处重复写 getBoundingClientRect */
export const BOX = `(el) => { if (!el) return null; const r = el.getBoundingClientRect();
  return { x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height) } }`

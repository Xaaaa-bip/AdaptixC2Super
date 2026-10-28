/* 本地预览服务器：dist/ 静态 + 假数据 API + 静默 WS —— 不连任何真实 teamserver
 * 用法：node scripts/preview_server.mjs [port]   →  http://127.0.0.1:<port>/
 * 登录：任意用户名/密码都放行（本地假数据，无真实凭据）
 */
import http from 'node:http'
import crypto from 'node:crypto'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const DIST = path.join(__dirname, '..', 'dist')
const PORT = Number(process.argv[2]) || 5199

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.ttf': 'font/ttf', '.woff2': 'font/woff2',
  '.json': 'application/json', '.ipdb': 'application/octet-stream' }

const now = Math.floor(Date.now() / 1000)
const A = (i, o = {}) => ({
  a_id: `aabbccdd${String(i).padStart(8, '0')}`, a_name: 'linux_agent', a_listener: 'LinuxTCP',
  a_os: 2, a_os_desc: 'Ubuntu 22.04', a_arch: 'x86_64',
  a_computer: `hx-node-0${i + 1}`, a_username: 'root', a_process: '/usr/bin/apt-helper',
  a_pid: String(4200 + i), a_tid: String(4200 + i),
  a_internal_ip: `10.0.4.${17 + i}`, a_external_ip: `203.0.113.${9 + i}`,
  a_domain: '', a_tags: i % 2 ? '生产' : '', a_mark: '', a_elevated: true,
  a_create_time: now - 3600, a_last_tick: now - (i === 2 ? 700 : 3), a_sleep: 5, a_jitter: 0,
  a_acp: 0, a_oemcp: 0, ...o,
})
const agents = [
  A(0), A(1, { a_os: 0, a_computer: 'WIN-DC01', a_username: 'administrator', a_domain: 'corp', a_listener: 'BeaconHTTP', a_os_desc: 'Windows Server 2022' }),
  A(2, { a_username: 'ubuntu', a_tags: '测试' }),
  A(3, { a_os: 1, a_computer: 'macbook-pro', a_username: 'dev', a_listener: 'GopherTCP', a_os_desc: 'macOS 14' }),
  A(4, { a_tags: '生产' }),
  A(5, { a_os: 0, a_computer: 'WIN-FIN-07', a_username: 'finance', a_listener: 'BeaconWS', a_os_desc: 'Windows 11' }),
]
const listeners = [
  { l_name: 'LinuxTCP', l_type: 'linux', l_status: 'Listen', l_data: '{}', l_create_time: now - 7200 },
  { l_name: 'BeaconWS', l_type: 'ws', l_status: 'Listen', l_data: '{}', l_create_time: now - 3600 },
]

const server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x')
  const p = decodeURIComponent(u.pathname)

  if (p.startsWith('/api/')) {
    let body = ''
    req.on('data', (c) => { body += c })
    req.on('end', () => {
      const json = (v) => res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify(v))
      if (p === '/api/login') return json({ ok: true, message: '', access_token: 'preview.access', refresh_token: 'preview.refresh' })
      if (p === '/api/refresh') return json({ ok: true, message: '', access_token: 'preview.access2', refresh_token: 'preview.refresh2' })
      if (p === '/api/agent/list') return json(agents)
      if (p === '/api/listener/list') return json(listeners)
      if (p === '/api/download/list' || p === '/api/agent/task/list' || p === '/api/targets/list') return json([])
      if (p === '/api/bofs/list') return json({ base: '', entries: [] })
      if (p === '/api/agent/build/schema') return json({ version: 1, fields: [] })
      if (p === '/api/ip/geo') return json({})
      if (p.startsWith('/api/agent/command')) return json({ message: '', ok: true })
      return json({})
    })
    return
  }

  // 静态：SPA 回退 index.html
  let f = path.join(DIST, p === '/' ? 'index.html' : p)
  if (!f.startsWith(DIST) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) f = path.join(DIST, 'index.html')
  res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' })
  fs.createReadStream(f).pipe(res)
})

// WS：完成握手后静默（对齐 _harness.mjs 的 routeWebSocket 行为——
// finishConnect 会 await 建连，握手不接受页面会一直停在 syncing）
server.on('upgrade', (req, socket) => {
  const key = req.headers['sec-websocket-key']
  if (!key) return socket.destroy()
  const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64')
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
    `Sec-WebSocket-Accept: ${accept}\r\n\r\n`)
  // 不发任何帧；浏览器侧 SyncSocket 保持打开
  socket.on('error', () => {})
})

server.listen(PORT, '127.0.0.1', () => {
  console.log(`预览: http://127.0.0.1:${PORT}/  （登录任意用户名/密码；6 台假主机，本地假数据，无真实连接）`)
})

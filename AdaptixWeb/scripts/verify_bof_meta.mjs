#!/usr/bin/env node
/* 验收：BOF 页的「实战用法」面 —— 渲染 + 端到端参数打包。
 *
 * ★ 这套验收**完全离线**：不起真控制台、不用任何凭据、不碰任何主机。
 *   做法是把**真组件**（dist 里的构建产物）跑起来，把它会打的每一个后端
 *   请求都拦成假数据：
 *     /api/login             → 假 JWT
 *     /api/agent/list        → 一台假的 linux 在线主机
 *     /api/listener/list     → 假的 LinuxTCP 监听
 *     /api/bofs/list         → 假清单（名字故意多给一个，验证「未登记」徽标）
 *     /api/otp/generate      → 假 otp，并接受 WS 连接后保持静默
 *     /api/agent/command/**  → **记录并伪造成功**，绝不真下发
 *
 *   为什么不在真控制台上跑：真控制台上「点执行」要么真下发（不可接受），
 *   要么被 abort 拦下 —— 而 `runBof` 的 catch 会**回退到 sendCommand(cmdline)**，
 *   那是另一个端点，等于把本该拦下的下发换个门送出去。伪造成功返回才是
 *   唯一既拦住下发、又走完真实代码路径的办法。
 *   附带好处：假数据的名字/数量是**我定的**，所以断言可以精确到字段值，
 *   不受当天服务端状态影响（真控制台上跑不出可复现的结果）。
 *
 * 判据：
 *   D 组 —— 渲染：notes / ★ / 未登记徽标 / 分类名 / 参数控件形态 / 不撑破卡片
 *   E 组 —— 端到端：点「执行」后拦到的 param_data，按 bof_api.c 解出来必须等于输入
 *   F 组 —— 自检：请求确实被拦到了，且没有任何请求走 /command/raw 回退
 *
 * 覆盖面（诚实声明）：假数据意味着**不覆盖**真实服务端返回的字段差异；
 *   那条由 scripts/bofs_ui_check.mjs（两端 JSON 键对齐）负责。
 *
 * 用法（在 AdaptixWeb/ 下）: node scripts/verify_bof_meta.mjs
 */
import { chromium } from 'playwright'
import http from 'node:http'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const WEB = path.join(__dirname, '..')
const DIST = path.join(WEB, 'dist')

// 用**构建产物**而不是 dev server：验的就是要部署出去的那份东西。
// 但必须经 HTTP 起，不能直接 file:// —— 登录表单的 baseUrl 留空时接口走相对
// 路径（http.ts:282），file:// 下会解析成 file:///api/login，fetch 直接抛，
// 拦都拦不到。起个静态服务器让 origin 变成 http://127.0.0.1:PORT，
// 相对路径才有意义，顺便让 WS 的 connectWsUrl 落到同一个 host 上。
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon', '.json': 'application/json' }
const server = http.createServer((req, res) => {
  const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname)
  let f = path.join(DIST, rel === '/' ? 'index.html' : rel)
  if (!f.startsWith(DIST) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) f = path.join(DIST, 'index.html')
  res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' })
  fs.createReadStream(f).pipe(res)
})
await new Promise((r) => server.listen(0, '127.0.0.1', r))
const INDEX = `http://127.0.0.1:${server.address().port}/`

const results = []
const check = (id, desc, pass, detail) => {
  results.push({ id, pass })
  console.log(`${pass ? '  ok  ' : ' FAIL '} ${id}  ${desc}`)
  if (detail) console.log(`        ${detail}`)
}

// ────────────────────────────────── 独立实现的解码端（照 bof_api.c，不引用前端代码）
const u32 = (b, i) => (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0
function beaconParse(blob, kinds) {
  const count = u32(blob, 0)
  let p = 4
  const out = []
  for (const k of kinds) {
    if (k === 'int') {
      if (p + 4 > blob.length) throw new Error(`字段 ${out.length} 越界（Int）`)
      out.push(u32(blob, p)); p += 4
    } else {
      if (p + 4 > blob.length) throw new Error(`字段 ${out.length} 越界（len）`)
      const n = u32(blob, p); p += 4
      if (p + n > blob.length) throw new Error(`字段 ${out.length} 越界（内容 ${n} > 剩余 ${blob.length - p}）`)
      out.push(new TextDecoder().decode(blob.subarray(p, p + n))); p += n
    }
  }
  return { count, fields: out, consumed: p }
}

// ────────────────────────────────── 假后端
const FAKE_AGENT = {
  a_id: 'aabbccdd11223344', a_name: 'linux_agent', a_listener: 'LinuxTCP',
  a_os: 2, a_os_desc: 'Ubuntu 22.04', a_arch: 'x86_64',
  a_computer: 'hx-node-01', a_username: 'root', a_process: '/usr/bin/apt-helper',
  a_pid: '4242', a_tid: '4242', a_internal_ip: '10.0.4.17', a_external_ip: '203.0.113.9',
  a_domain: '', a_tags: '', a_mark: '', a_elevated: true,
  a_create_time: Math.floor(Date.now() / 1000) - 600,
  a_last_tick: Math.floor(Date.now() / 1000), a_sleep: 5, a_jitter: 0,
  a_acp: 0, a_oemcp: 0,
}
// 26 个真实名字 + 一个不在本地 META 里的，用来验「未登记」徽标
const FAKE_BOF_NAMES = [
  'argprobe', 'container_detect', 'cred_harvest', 'cron_persist', 'ebpf_load', 'ebpf_unload',
  'evacuate', 'hide_add', 'hide_del', 'host_recon', 'kernel_exploit_check', 'ko_load',
  'ld_preload_check', 'ld_preload_install', 'matrix_pick', 'memfd_exec', 'net_enum',
  'proc_enum', 'service_enum', 'shadow_dump', 'spawn_detached', 'ssh_keys', 'ssh_persist',
  'sudo_check', 'suid_scan', 'systemd_persist', 'zzz_unlisted_probe',
]
const FAKE_BOF_LIST = {
  base: '/root/cc/run/extenders/linux_agent/bofs/',
  entries: FAKE_BOF_NAMES.map((name, i) => ({
    name, obj: true, obj_size: 4096 + i * 37, obj_mtime: 1758000000 + i * 60,
    obj_sha256: String(i).padStart(2, '0').repeat(32),
    src: name !== 'zzz_unlisted_probe', src_size: 2048 + i * 11, src_mtime: 1758000000 + i * 60 + 30,
    stale: false,
  })),
}

const sent = []
const seen = []

const b = await chromium.launch()
const ctx = await b.newContext({ viewport: { width: 1600, height: 950 }, ignoreHTTPSErrors: true })
const page = await ctx.newPage()

// WS：接受连接后保持静默。必须有 —— finishConnect 会 await 建连，
// 不接的话它一直挂着，页面停在 syncing 状态。
await page.routeWebSocket(/\/api\/connect/, (ws) => { /* 不发不关，保持静默 */ })

await page.route('**/api/**', async (route) => {
  const url = route.request().url()
  const p = new URL(url).pathname
  seen.push(p)
  const json = (o) => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(o) })

  if (p === '/api/login') return json({ ok: true, message: '', access_token: 'fake.access', refresh_token: 'fake.refresh' })
  if (p === '/api/otp/generate') return json({ ok: true, message: 'fake-otp' })
  if (p === '/api/agent/list') return json([FAKE_AGENT])
  if (p === '/api/listener/list') return json([{ l_name: 'LinuxTCP', l_type: 'linux', l_status: 'Listen', l_data: '{}' }])
  if (p === '/api/download/list') return json([])
  if (p === '/api/agent/task/list') return json([])
  if (p === '/api/bofs/list') return json(FAKE_BOF_LIST)
  // ★ 下发：记录 + 伪造成功。绝不 abort（abort 会让 runBof 回退到 sendCommand）。
  if (p.startsWith('/api/agent/command')) {
    let body = {}
    try { body = JSON.parse(route.request().postData() || '{}') } catch { /* 保持 {} */ }
    sent.push({ path: p, body })
    return json({ message: '', ok: true })
  }
  return json({})
})

try {
  await page.goto(INDEX, { waitUntil: 'load' })
  await page.waitForSelector('.lx-input', { timeout: 15000 })
  await page.fill('.lx-input >> nth=0', 'harness')
  await page.fill('.lx-input >> nth=1', 'harness')
  await page.click('.lx-submit')
  await page.waitForSelector('.ops', { timeout: 20000 })
  await page.waitForTimeout(1500)

  await page.evaluate(() => {
    const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('BOF'))
    if (it) it.click()
  })
  await page.waitForSelector('.bof-wrap', { timeout: 15000 })
  await page.waitForTimeout(1200)

  const state = await page.evaluate(() => {
    const rows = [...document.querySelectorAll('.bof-card')]
    const sel = document.querySelector('.toolbar select')
    const opts = sel ? [...sel.options].map((o) => o.value).filter(Boolean) : []
    const cards = rows.map((c) => {
      const name = (c.querySelector('.bof-name')?.textContent || '').trim()
      const notes = [...c.querySelectorAll('.bof-notes > li')]
      return {
        name,
        notes: notes.length,
        stars: notes.filter((n) => n.classList.contains('hot')).length,
        noteTexts: notes.map((n) => (n.textContent || '').trim()),
        params: [...c.querySelectorAll('.bof-param')].map((p) => ({
          label: (p.querySelector('label')?.textContent || '').trim(),
          hint: (p.querySelector('.bof-hint')?.textContent || '').trim(),
          ph: p.querySelector('input')?.placeholder || '',
          w: Math.round(p.querySelector('input')?.getBoundingClientRect().width || 0),
        })),
        flags: [...c.querySelectorAll('.bof-flag')].map((f) => ({
          label: (f.textContent || '').trim(),
          box: Math.round(f.querySelector('input')?.getBoundingClientRect().width || 0),
        })),
        unknown: !!c.querySelector('.bof-unknown'),
        usage: (c.querySelector('.bof-usage')?.textContent || '').trim(),
        overflowX: c.scrollWidth - c.clientWidth,
        notesOverflow: (() => {
          const u = c.querySelector('.bof-notes')
          return u ? u.scrollWidth - u.clientWidth : 0
        })(),
        // notes 块的实测宽度与行数：用来确认「中文能换行、没被压成一条竖线」
        notesBox: (() => {
          const u = c.querySelector('.bof-notes')
          if (!u) return null
          const r = u.getBoundingClientRect()
          return { w: Math.round(r.width), h: Math.round(r.height) }
        })(),
      }
    })
    const cats = [...document.querySelectorAll('.bof-cat')].map((e) => (e.textContent || '').trim())
    const tip = (document.querySelector('.toolbar .dim')?.textContent || '').trim()
    return { cards, cats, tip, opts, hasAgent: opts.length > 0 }
  })

  console.log(`\n工具条：${state.tip}`)
  console.log(`分类：${state.cats.join('  ')}`)
  console.log(`假主机：${state.opts.length} 台在线\n`)

  // ───────── D. 渲染 ─────────
  console.log('=== D. 渲染 ===')
  const registered = state.cards.filter((c) => c.name !== 'zzz_unlisted_probe')
  check('D1', '26 个已登记模块全部渲染出卡片（假清单多给的那一个另算）',
    registered.length === 26, `已登记 ${registered.length} 张，共 ${state.cards.length} 张`)
  check('D2', '每张已登记的卡都有 notes 块（实战用法）',
    registered.every((c) => c.notes > 0),
    registered.filter((c) => !c.notes).map((c) => c.name).join(', ') || `${registered.reduce((a, c) => a + c.notes, 0)} 条`)
  check('D3', '★ 项按 hot 类渲染，且与文本里的 ★ 逐条对上',
    registered.some((c) => c.stars > 0) &&
    state.cards.every((c) => c.noteTexts.filter((t) => t.startsWith('★')).length === c.stars),
    `${state.cards.reduce((a, c) => a + c.stars, 0)} 条 hot`)
  check('D4', '分类里没有「未登记 / 上传」，有「隐匿 · 撤离」',
    state.cats.every((c) => !/未登记/.test(c)) && state.cats.some((c) => /隐匿/.test(c)),
    state.cats.join('  |  '))
  const nOther = Number((state.tip.match(/未登记\s*(\d+)/) || [])[1] || 0)
  const nBadge = state.cards.filter((c) => c.unknown).length
  const unlisted = state.cards.find((c) => c.name === 'zzz_unlisted_probe')
  check('D5', '「未登记」徽标只出现在没登记的那张卡上，且与工具条计数自洽',
    nBadge === nOther && nOther === 1 && !!unlisted?.unknown,
    `工具条未登记=${nOther}，渲染出的徽标=${nBadge}（${state.cards.filter((c) => c.unknown).map((c) => c.name).join(',') || '无'}）`)

  const byName = Object.fromEntries(state.cards.map((c) => [c.name, c]))
  const eb = byName.ebpf_load
  check('D6', 'ebpf_load 渲染 9 个参数行，其中恰好 2 个标「整数」',
    !!eb && eb.params.length === 9 && eb.params.filter((p) => p.hint === '整数').length === 2,
    eb ? eb.params.map((p) => p.label + (p.hint ? `[${p.hint}]` : '')).join(' ') : '没找到 ebpf_load 卡片')
  const ha = byName.hide_add
  check('D7', 'hide_add 的 names 标「多个」（打包成 N 个字段）',
    !!ha && ha.params.some((p) => p.hint === '多个'),
    ha ? ha.params.map((p) => `${p.label}${p.hint ? `[${p.hint}]` : ''}`).join(' ') : '没找到 hide_add')
  const me = byName.memfd_exec
  check('D8', 'memfd_exec 的 --unlink 是独立复选框（不是被拉满的暗色文本框）',
    !!me && me.flags.length === 1 && me.flags[0].box > 0 && me.flags[0].box <= 20,
    me ? `开关「${me.flags[0]?.label}」复选框宽 ${me.flags[0]?.box}px` : '没找到 memfd_exec')
  const narrow = state.cards.flatMap((c) => c.params.filter((p) => p.w < 120).map((p) => `${c.name}.${p.label}=${p.w}px`))
  check('D9', '所有参数输入框宽度 ≥120px（标签加徽标后没把输入框挤没）',
    narrow.length === 0, narrow.join(', ') || '全部达标')
  const spill = state.cards.filter((c) => c.overflowX > 1 || c.notesOverflow > 1)
  check('D10', '没有卡片被内容顶破（横向溢出 0）',
    spill.length === 0, spill.map((c) => `${c.name} card+${c.overflowX}/notes+${c.notesOverflow}`).join(', ') || `${state.cards.length} 张全部无溢出`)
  // 中文换行：notes 块必须是有实际宽度的块，且高度明显大于单行（说明真的换行了）
  const flatNotes = state.cards.filter((c) => c.notesBox && c.notesBox.w < 200)
  check('D11', 'notes 块有正常宽度（中文在卡宽内换行，没被压成竖条）',
    flatNotes.length === 0,
    flatNotes.map((c) => `${c.name}=${c.notesBox.w}px`).join(', ') ||
    `最窄 ${Math.min(...state.cards.filter((c) => c.notesBox).map((c) => c.notesBox.w))}px / 最宽 ${Math.max(...state.cards.filter((c) => c.notesBox).map((c) => c.notesBox.w))}px`)

  // ───────── E. 端到端参数打包 ─────────
  console.log('\n=== E. 端到端：点「执行」后拦到的 param_data ===')
  check('E0', '有在线主机可测（缺了则 E 组全部恒真）', state.hasAgent, `${state.opts.length} 台`)

  const setField = (name, label, v) => page.evaluate(({ name, label, v }) => {
    const card = [...document.querySelectorAll('.bof-card')]
      .find((c) => (c.querySelector('.bof-name')?.textContent || '').trim() === name)
    if (!card) throw new Error(`没有卡片 ${name}`)
    const row = [...card.querySelectorAll('.bof-param')]
      .find((p) => (p.querySelector('label')?.textContent || '').trim().startsWith(label))
    if (!row) throw new Error(`${name} 没有参数 ${label}`)
    const input = row.querySelector('input')
    // React 的受控输入：必须走原型上的 value setter 再派发 input 事件，
    // 直接改 .value 不会触发 onChange（页面读到的还是旧值）。
    const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set
    setter.call(input, v)
    input.dispatchEvent(new Event('input', { bubbles: true }))
  }, { name, label, v })

  const toggleFlag = (name, label) => page.evaluate(({ name, label }) => {
    const card = [...document.querySelectorAll('.bof-card')]
      .find((c) => (c.querySelector('.bof-name')?.textContent || '').trim() === name)
    const fl = [...card.querySelectorAll('.bof-flag')].find((f) => (f.textContent || '').includes(label))
    if (!fl) throw new Error(`${name} 没有开关 ${label}`)
    fl.querySelector('input').click()
  }, { name, label })

  const clickRun = (name) => page.evaluate((name) => {
    const card = [...document.querySelectorAll('.bof-card')]
      .find((c) => (c.querySelector('.bof-name')?.textContent || '').trim() === name)
    const btn = [...card.querySelectorAll('.bof-card-foot button')].find((b) => (b.textContent || '').trim() === '执行')
    if (!btn) throw new Error(`${name} 没有「执行」按钮`)
    if (btn.disabled) throw new Error(`${name} 的「执行」被禁用`)
    btn.click()
  }, name)

  const runCard = async (name, fields = {}, flags = []) => {
    for (const [label, v] of Object.entries(fields)) await setField(name, label, v)
    for (const label of flags) await toggleFlag(name, label)
    await page.waitForTimeout(200)
    const before = sent.length
    await clickRun(name)
    await page.waitForTimeout(500)
    if (sent.length <= before) throw new Error(`${name} 点了执行但没拦到下发请求`)
    return argObjOf(sent[sent.length - 1].body)
  }

  // 请求体是 { id, ui, cmdline, data, ... }，参数对象被 **JSON 串**塞进 `data`
  // （http.ts:260 commandExecute）。解析它才是「服务端会收到的那个对象」：
  // 真正决定行为的是 args（服务端 TsAgentCommand → agent.Command(args)），
  // cmdline 只用于显示/回显。
  const argObjOf = (body) => {
    const d = body?.data
    if (typeof d === 'string') { try { return JSON.parse(d) } catch { /* 落到空对象 */ } }
    return d && typeof d === 'object' ? d : {}
  }
  const decode = (args) => {
    const raw = args.param_data
    if (raw === '' || raw == null) return { empty: true, blob: new Uint8Array(0) }
    return { empty: false, blob: new Uint8Array(Buffer.from(raw, 'base64')) }
  }

  try {
    const a = await runCard('host_recon')
    const d = decode(a)
    check('E1', '无参数模块：param_data 为空串（服务端会兜底成 count=0 的合法空 blob）',
      d.empty && a.command === 'execute' && a.subcommand === 'bof' && /host_recon\.o$/.test(a.bof || ''),
      `bof=${a.bof} param_data=${JSON.stringify(a.param_data)}`)
  } catch (e) { check('E1', '无参数模块执行', false, String(e.message)) }

  try {
    const a = await runCard('cron_persist', { '命令': '/tmp/x', '任务名': 'agent_update' })
    const got = beaconParse(decode(a).blob, ['str', 'str'])
    check('E2', 'cron_persist：blob 头 4 字节 = 2，两个字段逐字相等（ISS-005 回归）',
      got.count === 2 && JSON.stringify(got.fields) === JSON.stringify(['/tmp/x', 'agent_update']),
      `head4=${got.count} fields=${JSON.stringify(got.fields)}`)
  } catch (e) { check('E2', 'cron_persist 参数打包', false, String(e.message)) }

  try {
    const a = await runCard('hide_add', { 'pin 名': 'ax-rootkit', '要隐藏的名': 'sys-update sys-maintenance' })
    const got = beaconParse(decode(a).blob, ['str', 'str', 'str'])
    check('E3', 'hide_add：names 里的空格拆成独立字段（pin + 2 名 = 3 字段）',
      got.count === 3 && JSON.stringify(got.fields) === JSON.stringify(['ax-rootkit', 'sys-update', 'sys-maintenance']),
      `head4=${got.count} fields=${JSON.stringify(got.fields)}`)
  } catch (e) { check('E3', 'hide_add 多值展开', false, String(e.message)) }

  try {
    const a = await runCard('ebpf_load', {
      '指令文件': '/tmp/h.insns.bin', 'mapreloc': '/tmp/h.mapreloc.txt',
      'prog_type': '5', 'attach': '1', 'attach 目标': 'syscalls/sys_exit_getdents64',
      '隐藏名': 'sys-update sys-maintenance', '隐藏端口': '8443,9001',
      'knock 密钥': '0123456789abcdef', 'pin 名': 'ax-rootkit',
    })
    const got = beaconParse(decode(a).blob, ['str', 'str', 'int', 'int', 'str', 'str', 'str', 'str', 'str'])
    const want = ['/tmp/h.insns.bin', '/tmp/h.mapreloc.txt', 5, 1, 'syscalls/sys_exit_getdents64',
      'sys-update\0sys-maintenance', '8443,9001', '0123456789abcdef', 'ax-rootkit']
    check('E4', 'ebpf_load：9 字段全对（整数裸 4 字节 + names 以 NUL 连成一个字段）',
      got.count === 9 && JSON.stringify(got.fields) === JSON.stringify(want),
      `head4=${got.count}\n        得到 ${JSON.stringify(got.fields)}\n        期望 ${JSON.stringify(want)}`)
  } catch (e) { check('E4', 'ebpf_load 参数打包', false, String(e.message)) }

  try {
    const a = await runCard('memfd_exec', { '目标机 ELF 路径': '/tmp/payload.elf' }, ['--unlink'])
    const got = beaconParse(decode(a).blob, ['str', 'str'])
    check('E5', 'memfd_exec：勾上开关后 --unlink 作为一个字段发出',
      got.count === 2 && JSON.stringify(got.fields) === JSON.stringify(['/tmp/payload.elf', '--unlink']),
      `head4=${got.count} fields=${JSON.stringify(got.fields)}`)
  } catch (e) { check('E5', 'memfd_exec 开关参数', false, String(e.message)) }

  // E6 整数框填非数字：必须被拦下（不发请求），而不是把 0 发出去
  try {
    await setField('ebpf_load', 'prog_type', 'five')
    await page.waitForTimeout(150)
    const before = sent.length
    await clickRun('ebpf_load')
    await page.waitForTimeout(400)
    const toast = await page.evaluate(() => {
      const t = [...document.querySelectorAll('[class*="toast"]')].map((e) => (e.textContent || '').trim())
      return t.join(' | ')
    })
    check('E6', '整数框填非数字时不下发，并给出可读提示',
      sent.length === before && /整数/.test(toast),
      `拦到新请求 ${sent.length - before} 个；提示：${JSON.stringify(toast.slice(0, 80))}`)
    await setField('ebpf_load', 'prog_type', '5')
  } catch (e) { check('E6', '整数校验', false, String(e.message)) }

  // E7 异步：argObj.async 才是真正决定行为的字段；命令行末尾的标记名随 agent 变
  try {
    await page.evaluate(() => {
      const card = [...document.querySelectorAll('.bof-card')]
        .find((c) => (c.querySelector('.bof-name')?.textContent || '').trim() === 'suid_scan')
      card.querySelector('.bof-async input').click()
    })
    await page.waitForTimeout(200)
    const usage = byName.suid_scan.usage
    const a = await runCard('suid_scan')
    const usageNow = await page.evaluate(() => {
      const card = [...document.querySelectorAll('.bof-card')]
        .find((c) => (c.querySelector('.bof-name')?.textContent || '').trim() === 'suid_scan')
      return (card.querySelector('.bof-usage')?.textContent || '').trim()
    })
    // 假主机是 linux（a_os=2）⇒ 标记必须是 `async`（linux_agent 的 ax_config 是
    // addArgBool("async")），不是 beacon 的 `-a`。写错不会报错，
    // 而会被 AxScript 的 isWideArgs 分支并进前一个参数里。
    check('E7', '勾异步后 argObj.async=true，且命令行末尾用 linux agent 认得的 `async`',
      a.async === true && / async$/.test(usageNow) && !/ -a$/.test(usageNow),
      `async=${JSON.stringify(a.async)} 命令行=${JSON.stringify(usageNow)}（勾选前 ${JSON.stringify(usage)}）`)
  } catch (e) { check('E7', '异步开关', false, String(e.message)) }

  // ───────── F. 自检 ─────────
  console.log('\n=== F. 自检 ===')
  check('F1', '确实拦到了下发请求（否则 E 组恒真）', sent.length > 0, `拦到 ${sent.length} 个 /api/agent/command/**`)
  const rawPath = sent.filter((s) => s.path.includes('/command/raw'))
  check('F2', '没有任何请求走到 /command/raw（回退路径没被触发）',
    rawPath.length === 0, rawPath.length ? `有 ${rawPath.length} 个回退：${JSON.stringify(rawPath.map((r) => r.body.cmdline))}` : '全部走结构化路径')
  check('F3', '假后端确实被用上了（走了 /api/login + /api/bofs/list）',
    seen.includes('/api/login') && seen.includes('/api/bofs/list'),
    `共 ${seen.length} 个 API 调用`)
  // 判据敏感性：E 组的判据必须能认出「漏了 count 头」的那种 blob，
  // 否则它只是在复述自己期望的东西。手工造一个缺头的流灌进同一个解码器，
  // 结果必须与期望**不等**（要么抛越界，要么解出错的字段）。
  let caughtNoHeader = false, how = ''
  try {
    const bad = []
    for (const s of ['/tmp/x', 'agent_update']) {
      const e = new TextEncoder().encode(s)
      bad.push(e.length & 0xff, (e.length >> 8) & 0xff, 0, 0, ...e)  // 直接 [len][bytes]，没有 4 字节头
    }
    const got = beaconParse(new Uint8Array(bad), ['str', 'str'])
    caughtNoHeader = JSON.stringify(got.fields) !== JSON.stringify(['/tmp/x', 'agent_update'])
    how = `解出 ${JSON.stringify(got.fields)}（head4=${got.count}）`
  } catch (e) { caughtNoHeader = true; how = `抛错：${e.message}` }
  check('F4', '判据对「漏 count 头」的 blob 判红（E 组不是恒真）',
    caughtNoHeader, how)
} catch (e) {
  console.error(`\n  ✗ 脚本中断：${e?.stack || e}`)
  results.push({ id: 'X0', pass: false })
} finally {
  const bad = results.filter((r) => !r.pass)
  console.log(`\n──────── ${results.length - bad.length}/${results.length} 通过 ────────`)
  if (bad.length) console.log('未通过: ' + bad.map((r) => r.id).join(', '))
  await b.close()
  server.close()
  process.exit(bad.length ? 1 : 0)
}

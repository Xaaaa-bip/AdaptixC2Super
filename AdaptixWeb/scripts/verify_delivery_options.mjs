#!/usr/bin/env node
/* 验收：投递页「选项」面。
   A 组 —— 监听器选项：任何时候都必须恰好一个高亮，且与右栏摘要的「监听」一致。
            （修复前：刚进页面 listener="http"，一个都不高亮，摘要却写「监听 http」，
             而「应用并同步」可点。）
   B 组 —— 生成参数：五个 agent 都有参数栏；linux/macos/gopher 是「重连间隔（秒）」
            且值不再是 beacon 带过来的 '5s'。
   C 组 —— 不回归：第 5 批的命令块宽度 + 第 6 批的两栏各自滚。

   只导航 + 点页内 Agent/监听按钮 + 读 DOM。
   **绝不点「应用并同步」**（那会写服务端配置），也绝不点「刷新状态」以外的接口。

   跑法：ADAPTIX_USER=... ADAPTIX_PASS=... node scripts/verify_delivery_options.mjs */
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
// 抓住服务端存的原值。B8 的区分度取决于它 —— 如果服务端存的本来就是个合法秒数，
// 那条断言就只是白过；把原值打出来，读的人能自己判断。
let srvCfg = null
page.on('response', async (r) => {
  if (r.url().includes('/api/webdelivery/status')) {
    try { srvCfg = await r.json() } catch { /* 非 JSON 就是插件没加载 */ }
  }
})
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
await page.waitForTimeout(4000)

/** 监听器选项面的状态：state 活值（摘要）vs 渲染出的高亮项 */
const listenerState = () => page.evaluate(() => {
  const lis = [...document.querySelectorAll('.gen-listener')]
  const summary = [...document.querySelectorAll('.gen-summary > div')].map((d) => (d.textContent || '').trim())
  const applyBtn = [...document.querySelectorAll('.btns button')].find((x) => (x.textContent || '').includes('应用'))
  const active = lis.filter((l) => l.classList.contains('active'))
  return {
    stateValue: (summary.find((s) => s.startsWith('监听')) || '').replace(/^监听/, ''),
    rendered: lis.map((l) => (l.querySelector('.name')?.textContent || '').trim()),
    activeNames: active.map((l) => (l.querySelector('.name')?.textContent || '').trim()),
    activeCount: active.length,
    applyDisabled: applyBtn ? applyBtn.disabled : null,
  }
})

/** 「生成参数」栏：标签 + 值 */
const params = () => page.evaluate(() => {
  const out = []
  for (const f of document.querySelectorAll('.gen-params .field')) {
    const label = (f.querySelector(':scope > label')?.textContent || '').trim()
    const inputs = [...f.querySelectorAll(':scope > div > input, :scope > input')]
    out.push({ label, values: inputs.map((i) => i.value), ph: inputs.map((i) => i.placeholder || '') })
  }
  return out
})

const clickAgent = async (label) => {
  await page.evaluate((l) => {
    const row = [...document.querySelectorAll('.field')]
      .find((f) => (f.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
      ?.querySelector('.chip-row')
    const btn = row && [...row.querySelectorAll('button')].find((x) => (x.textContent || '').trim() === l)
    if (btn) btn.click()
  }, label)
  await page.waitForTimeout(1800)
}

// ───────── A. 监听器选项 ─────────
console.log('\n=== A. 监听器选项（刚进页面，未碰任何 Agent）===')
const first = await listenerState()
console.log(`  摘要监听=${JSON.stringify(first.stateValue)}  渲染=${JSON.stringify(first.rendered)}  高亮=${JSON.stringify(first.activeNames)}  应用键禁用=${first.applyDisabled}`)
check('A1', '刚进页面就恰好一个监听高亮（修复前 0 个）',
  first.activeCount === 1,
  `高亮 ${first.activeCount} 个：${JSON.stringify(first.activeNames)}`)
check('A2', '摘要的「监听」与高亮项是同一个（不再各说各话）',
  first.activeCount === 1 && first.stateValue === first.activeNames[0],
  `摘要 ${JSON.stringify(first.stateValue)} vs 高亮 ${JSON.stringify(first.activeNames)}`)
check('A3', '摘要的监听名是真实监听器（不再是服务端残留的 "http"）',
  first.rendered.includes(first.stateValue) && first.stateValue !== 'http',
  `stateValue=${JSON.stringify(first.stateValue)}，渲染列表=${JSON.stringify(first.rendered)}`)

// 自动选中的默认项必须是确定的：compatible 的顺序来自 listeners 记录的插入顺序
// （Object.values），那是按服务端返回 / WS 到达顺序攒的，实测两次打开会不一样。
// 重新载入一次页面，默认选中必须还是同一个。
console.log('\n=== A6. 重新载入后默认选中项不变（确定性）===')
await page.reload({ waitUntil: 'load' })
await page.waitForTimeout(6500)
await page.evaluate(() => {
  const it = [...document.querySelectorAll('.rail-item')].find((e) => (e.textContent || '').includes('投递'))
  if (it) it.click()
})
await page.waitForTimeout(4000)
const second = await listenerState()
check('A6', '两次载入自动选中的是同一个监听（不随消息到达顺序变）',
  second.activeCount === 1 && second.activeNames[0] === first.activeNames[0],
  `第一次=${JSON.stringify(first.activeNames)} 重新载入后=${JSON.stringify(second.activeNames)}（渲染顺序=${JSON.stringify(second.rendered)}）`)

console.log('\n=== A4. 逐个 Agent：compatible 非空就必须恰好一个高亮 ===')
let a4bad = []
for (const a of ['Kharon', 'Linux Agent', 'macOS Agent', 'Gopher', 'Beacon']) {
  await clickAgent(a)
  const s = await listenerState()
  const expectEmpty = s.rendered.length === 0
  const ok = expectEmpty
    ? s.activeCount === 0 && s.stateValue === '未选' && s.applyDisabled === true
    : s.activeCount === 1 && s.stateValue === s.activeNames[0]
  if (!ok) a4bad.push(`${a}: 高亮${s.activeCount} 摘要=${s.stateValue} 渲染${s.rendered.length}`)
  console.log(`  ${a.padEnd(12)} 渲染 ${String(s.rendered.length).padStart(2)} 个  高亮 ${s.activeCount}  摘要=${JSON.stringify(s.stateValue)}  禁用=${s.applyDisabled}  ${ok ? 'ok' : 'FAIL'}`)
}
check('A4', '每个 agent 下的监听选中态都自洽（含兼容数为 0 的 Kharon）',
  a4bad.length === 0, a4bad.length ? a4bad.join(' | ') : '5 个 agent 全部自洽')

console.log('\n=== A5. 点「刷新状态」后不回退 ===')
await page.evaluate(() => {
  const btn = [...document.querySelectorAll('.btns button')].find((x) => (x.textContent || '').includes('刷新'))
  if (btn) btn.click()
})
await page.waitForTimeout(3000)
const after = await listenerState()
check('A5', '刷新状态（重新拉服务端配置）后仍恰好一个高亮且与摘要一致',
  after.activeCount === 1 && after.stateValue === after.activeNames[0],
  `高亮 ${after.activeCount} ${JSON.stringify(after.activeNames)}，摘要=${JSON.stringify(after.stateValue)}`)

// ───────── B. 生成参数 ─────────
console.log('\n=== B. 生成参数：每个 agent 的参数栏 ===')
const seen = {}
for (const a of ['Beacon', 'Kharon', 'Linux Agent', 'macOS Agent', 'Gopher']) {
  await clickAgent(a)
  const ps = await params()
  seen[a] = ps
  const sleepField = ps.find((f) => /Sleep|重连|Reconnect/.test(f.label))
  console.log(`  ${a.padEnd(12)} 参数栏: ${ps.map((f) => `${f.label}=[${f.values.join(',')}]`).join('  ')}`)
  if (!sleepField) console.log(`       ⚠ 没有参数栏`)
}
const labels = Object.fromEntries(Object.entries(seen).map(([k, v]) => [k, (v.find((f) => /Sleep|重连|Reconnect/.test(f.label)) || {}).label]))
const vals = Object.fromEntries(Object.entries(seen).map(([k, v]) => [k, (v.find((f) => /Sleep|重连|Reconnect/.test(f.label)) || { values: [] }).values]))

check('B1', '五个 agent 都有参数栏（修复前 macos / kharon 没有）',
  Object.values(labels).every((l) => !!l),
  Object.entries(labels).map(([k, v]) => `${k}=${v || '（无）'}`).join('  '))
check('B2', '标签不再承诺设不了的 count（"Reconnect timeout / count 相关" 已消失）',
  !Object.values(labels).some((l) => /Reconnect timeout/.test(l || '')),
  Object.entries(labels).map(([k, v]) => `${k}=${v}`).join('  '))
check('B3', 'linux/macos/gopher 的值是纯数字秒数（不再是 beacon 带过来的 5s）',
  ['Linux Agent', 'macOS Agent', 'Gopher'].every((a) => /^\d+(\.\d+)?$/.test(String(vals[a]?.[0] ?? ''))),
  ['Linux Agent', 'macOS Agent', 'Gopher'].map((a) => `${a}=${JSON.stringify(vals[a]?.[0])}`).join('  '))
// 注意：只断言"值的类型对"，不断言 kharon 一定是 3s。
// beacon 的 5s 切到 kharon 后**故意保留** —— 两者都是时长，5s 对 kharon 是合法取值，
// 强行改回 3s 等于把操作员自己设的值悄悄丢掉。跨类（时长↔秒数）才重置成该 agent 的默认，
// 和旁边 format 的"只在不属于本 agent 时才校正"是同一条规则。跨类重置见 B3。
check('B4', 'beacon / kharon 的参数值是带单位的时长（不是纯数字秒数）',
  /[a-z]/i.test(String(vals['Beacon']?.[0] ?? '')) && /[a-z]/i.test(String(vals['Kharon']?.[0] ?? '')),
  `beacon=${JSON.stringify(vals['Beacon']?.[0])} kharon=${JSON.stringify(vals['Kharon']?.[0])}`)

// 占位符 = 该 agent 自己的默认值（真值来自 defaultAgentConfig），不是写死的 '10'/'5s'
const phOf = (a) => (seen[a]?.find((f) => /Sleep|重连/.test(f.label)) || { ph: [] }).ph[0]
check('B7', '每个 agent 的占位符显示的是它自己的默认值',
  phOf('Kharon') === '3s' && phOf('Beacon') === '5s' && phOf('Linux Agent') === '10' && phOf('Gopher') === '10',
  `beacon=${JSON.stringify(phOf('Beacon'))} kharon=${JSON.stringify(phOf('Kharon'))} linux=${JSON.stringify(phOf('Linux Agent'))} gopher=${JSON.stringify(phOf('Gopher'))}`)
check('B5', '只有 Sleep 类 agent 显示抖动输入（重连类只有一个输入）',
  (seen['Beacon']?.find((f) => /Sleep/.test(f.label))?.values.length === 2) &&
  (seen['Gopher']?.find((f) => /重连/.test(f.label))?.values.length === 1),
  `beacon 输入 ${seen['Beacon']?.find((f) => /Sleep/.test(f.label))?.values.length} 个，gopher 输入 ${seen['Gopher']?.find((f) => /重连/.test(f.label))?.values.length} 个`)

// 往返不污染：beacon → linux → beacon
await clickAgent('Beacon')
const roundTrip = (await params()).find((f) => /Sleep|重连/.test(f.label))
check('B6', 'Agent 来回切不残留：beacon→linux→beacon 后回到 5s',
  roundTrip?.values[0] === '5s',
  `往返后 beacon 的值 = ${JSON.stringify(roundTrip?.values[0])}`)

// ───────── C. 不回归 ─────────
console.log('\n=== C. 不回归（第 5 / 6 批的东西）===')
await clickAgent('Gopher')
const layout = await page.evaluate(() => {
  const pg = document.querySelector('.gen-page')
  const sideBd = document.querySelector('.gen-side .card-bd')
  const cmd = document.querySelector('.wd-cmd')
  const r = cmd.getBoundingClientRect()
  return {
    pageScroll: pg.scrollHeight, pageClient: pg.clientHeight,
    sideCanScroll: sideBd.scrollHeight - sideBd.clientHeight > 1,
    cmdW: Math.round(r.width), cmdH: Math.round(r.height),
    cmdClipped: cmd.scrollHeight > cmd.clientHeight + 2,
    cmdHOverflow: cmd.scrollWidth > cmd.clientWidth + 1,
  }
})
check('C1', '整页仍不滚（滚动在两栏卡身里，第 6 批）',
  layout.pageScroll <= layout.pageClient + 2,
  `.gen-page scroll ${layout.pageScroll} vs client ${layout.pageClient}，右栏卡身可滚=${layout.sideCanScroll}`)
check('C2', '命令块仍够宽、不横向溢出、不纵向裁剪（第 5 批）',
  layout.cmdW > 400 && !layout.cmdClipped && !layout.cmdHOverflow,
  `命令框 ${layout.cmdW}×${layout.cmdH}px，横向溢出=${layout.cmdHOverflow}，纵向裁剪=${layout.cmdClipped}`)

// 刷新会把 agent_name / format / sleep 整份重载（这是「刷新状态」的本分），
// 所以不能断言"刷完还是 Linux"。要断言的是**自洽性**：显示出来的值与显示出来的
// agent 必须同类。占位符就是该 agent 自己的默认值（app 从 defaultAgentConfig 推的），
// 拿它当参照物，测试里就不用再抄一份 agent→类别 的表。
// 修复前的状态正是违反这条：值 "5s" 配占位符 "10"。
console.log('\n=== B8. 刷新状态后表单仍自洽（值必须与占位符同类）===')
await clickAgent('Linux Agent')
await page.evaluate(() => {
  const btn = [...document.querySelectorAll('.btns button')].find((x) => (x.textContent || '').includes('刷新'))
  if (btn) btn.click()
})
await page.waitForTimeout(3000)
const rd = await page.evaluate(() => {
  const f = [...document.querySelectorAll('.gen-params .field')].find((x) => /Sleep|重连/.test(x.querySelector(':scope > label')?.textContent || ''))
  const i = f?.querySelector(':scope > div > input, :scope > input')
  const agentBtn = [...document.querySelectorAll('.field')]
    .find((x) => (x.querySelector(':scope > label')?.textContent || '').trim() === 'Agent')
    ?.querySelector('.chip-row button.primary')
  return { value: i?.value ?? '', ph: i?.placeholder ?? '', agent: (agentBtn?.textContent || '').trim() }
})
const sameKind = (a, b) => /[a-z]/i.test(String(a).trim()) === /[a-z]/i.test(String(b).trim())
check('B8', '刷新后 值与占位符同类（自洽，不出现"5s 配占位 10"）',
  sameKind(rd.value, rd.ph),
  `刷新后：Agent=${rd.agent} 值=${JSON.stringify(rd.value)} 占位=${JSON.stringify(rd.ph)}；` +
  `服务端存的原值 sleep=${JSON.stringify(srvCfg?.sleep ?? null)} agent_name=${JSON.stringify(srvCfg?.agent_name ?? null)} listener=${JSON.stringify(srvCfg?.listener ?? null)}`)

// 反向也要能纠正，而且默认值必须来自**该 agent 自己**的 defaultAgentConfig：
// linux(秒数) → kharon(时长) 应该得到 kharon 的 3s，而不是硬编码的 5s。
console.log('\n=== B9. linux → kharon：换成 kharon 自己的默认值，不是通用常数 ===')
await clickAgent('Linux Agent')
const linuxVal = (await params()).find((f) => /Sleep|重连/.test(f.label))?.values[0]
await clickAgent('Kharon')
const kharonVal = (await params()).find((f) => /Sleep|重连/.test(f.label))?.values[0]
check('B9', '跨类切换取该 agent 自己的默认值（linux 10 → kharon 3s）',
  linuxVal === '10' && kharonVal === '3s',
  `linux=${JSON.stringify(linuxVal)} → kharon=${JSON.stringify(kharonVal)}（硬编码 5s 会在这里露馅）`)

// ───────── D. 状态行 ─────────
console.log('\n=== D. 底部状态行 ===')
const hint = await page.evaluate(() => {
  const btns = document.querySelector('.btns')
  const el = btns && btns.nextElementSibling
  return el ? (el.textContent || '').trim() : ''
})
console.log(`  实测: ${JSON.stringify(hint)}`)
check('D1', '状态行标明它是「服务端」的快照（不再读成表单当前值）',
  hint.startsWith('服务端当前配置：'),
  JSON.stringify(hint.slice(0, 40)) + '…')
check('D2', '状态行里不再混入表单的实时值（选中监听的 reg_name / 重复的缓存数）',
  !/BeaconWS|GopherTCP|LinuxTCP|KharonHTTP|BeaconHTTP/.test(hint) && (hint.match(/缓存/g) || []).length === 1,
  `含监听 reg_name=${/BeaconWS|GopherTCP|LinuxTCP/.test(hint)}，出现「缓存」${(hint.match(/缓存/g) || []).length} 次`)

const bad = results.filter((r) => !r.pass)
console.log(`\n──────── ${results.length - bad.length}/${results.length} 通过 ────────`)
if (bad.length) console.log('未通过: ' + bad.map((r) => r.id).join(', '))
await b.close()
process.exit(bad.length ? 1 : 0)

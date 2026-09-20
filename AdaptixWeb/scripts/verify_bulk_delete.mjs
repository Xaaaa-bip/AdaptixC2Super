#!/usr/bin/env node
/* 主机页批量删除行为验收。
 *
 * ★ 安全：本脚本**只做导航、输入与 DOM 量测**，不登录任何真实控制台、
 *   不带任何凭据、不向任何主机下发任何东西。它起一个本地静态服务器供 dist/，
 *   把控制台会打的每个接口都拦成假数据。`/api/agent/command/**` 一律**伪造 200
 *   并记录**，绝不 abort —— abort 会让 runBof 的 catch 回退到 sendCommand，
 *   等于把本该拦下的下发换个门送出去（见 _harness.mjs 文件头）。
 *
 * 为什么不能只靠 `npm run build` 绿：build 只证明能编译。它不证明
 *   - 点行内复选框**不会**顺手开出一个控制台页签（stopPropagation 回归守卫）
 *   - 服务端报「部分失败」时本地仍然清理。`/api/agent/remove` 的失败语义是
 *     「只要有一个 id 失败就整体 ok:false」（tc_agents.go:311-331），而清理
 *     写在 await 之后 ⇒ 不 catch 的话删 50 台有 1 台失败，UI 上 50 台**全都还在**
 *   - 焦点主机不在勾选集里时，操作员的控制台不会被从脚底下抽走
 *   - 勾选不会被搜索收窄悄悄丢掉（操作员绝不能删掉比自己被告知的更多的机器）
 *
 * 判据：
 *   B1  勾选列存在（表头 + 每一行）
 *   B2  点行内复选框 ⇒ 该行 checked + 批量条说出真实数量
 *   B3  ★点行内复选框**不得**开出控制台页签
 *   B4  表头框：部分勾选时 indeterminate；点击后全选
 *   B5  确认框内容：在线/离线拆分 + 前 5 个主机名 + 「…等 N 台」
 *   B6  ★捕获的 agent_id_array 与勾选集合**精确相等**（不是 includes）
 *   B7  ★部分失败（服务端 ok:false 且拒收 1 台）⇒ 行仍消失、报错，
 *       且被拒的那台在 refreshRest 后**回来**（对账）
 *   B8  右键菜单单选删除仍正常（重构 removeSelected 后的回归）
 *   B9  ★焦点主机不在勾选集时，控制台页签与焦点主机都保住
 *   B10 打开文件页签后批量删掉该主机 ⇒ 页签消失
 *   B11 ★全选后收窄搜索 ⇒ 真实数量 + 「N 台不在当前筛选里」
 *   B12 「清除」清空勾选并收起批量条
 *
 * 覆盖面（诚实声明）：**不**验证 browserFiles / browserProcs / interactBoost
 *   的 map 键真的被删掉了 —— 那三个键从 DOM 观测不到。B10 断言的是可观测后果
 *   （页签消失）。要直接断言 map 键得在生产代码里加 VITE_E2E 门控的 window.__store，
 *   那是为可测性改动生产代码，不采纳。
 *
 * 用法（在 AdaptixWeb/ 下，先 npm run build）: node scripts/verify_bulk_delete.mjs
 */
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { fakeAgent, launchConsole } from './_harness.mjs'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

let fails = 0
let checks = 0
function check(id, desc, pass, detail = '') {
  checks++
  console.log(`  ${pass ? '✓' : '✗'} ${id} ${desc}${detail ? ` | ${detail}` : ''}`)
  if (!pass) fails++
}

// ── 夹具 ──
// 主机名刻意分成 web-* / db-* 两组：B11 要靠搜索 "web" 收窄到 3 台。
// 心跳时间逐个递减，让表格排序（按 a_last_tick 倒序）是确定的。
const HOSTS = ['web-01', 'web-02', 'web-03', 'db-01', 'db-02', 'db-03']
const idOf = (i) => `aabbccdd${String(i).padStart(8, '0')}`   // 与 _harness.fakeAgent 同构
const OFFLINE = 5                                            // db-03 离线
const now = () => Math.floor(Date.now() / 1000)

function mkAgents() {
  const t = now()
  return HOSTS.map((h, i) => fakeAgent(i, {
    a_computer: h,
    // i === OFFLINE 的那台给一个远早于 sleep 窗口的 tick ⇒ isAgentOnline 判离线
    a_last_tick: i === OFFLINE ? t - 86400 : t - i,
  }))
}

/**
 * 会真的「删掉」的假服务端。
 * ★ 必须让 /api/agent/list 反映删除结果，否则 refreshRest 会把刚删掉的主机
 *   原样复活，B6/B7 全都会假通过（这正是「假服务端比真服务端还宽松」的经典坑）。
 * reject 里的 id：服务端**拒收**（保留），并整体回 ok:false —— 复现真实的部分失败。
 */
function mkBackend(agents, reject = []) {
  const st = { alive: [...agents], calls: [], reject }
  const routes = {
    '/api/agent/list': (route, body, json) => json(st.alive),
    '/api/agent/remove': (route, body, json) => {
      st.calls.push(body)
      const ids = Array.isArray(body.agent_id_array) ? body.agent_id_array : []
      for (const id of ids) {
        if (st.reject.includes(id)) continue
        const i = st.alive.findIndex((a) => a.a_id === id)
        if (i >= 0) st.alive.splice(i, 1)
      }
      const hit = ids.some((id) => st.reject.includes(id))
      return json(hit
        ? { ok: false, message: '1' }            // 真实服务端：编号列表 + ok:false（HTTP 200）
        : { ok: true, message: '' })
    },
  }
  return { st, routes }
}

const ROW = '.sess-table tbody tr'
const BAR = '.sess-bulk'
const HDR = 'th.sess-cell-check input'
const TERM_TABS = '.term-tabs .t[title]'   // 带 title 的才是主机页签；空态占位按钮没有
const rowOf = (page, host) => page.locator(ROW, { hasText: host }).first()
const rowCount = (page) => page.$$eval(ROW, (e) => e.length)
const checkedRows = (page) => page.$$eval(`${ROW}.checked`, (e) => e.length)
const agentTabs = (page) => page.$$eval(TERM_TABS, (e) => e.map((x) => x.getAttribute('title')))
const barText = (page) => page.$eval(BAR, (e) => e.textContent.replace(/\s+/g, ' ').trim())
  .catch(() => '')
const uiTabs = (page) => page.$$eval('.ops-tabs .ops-tab', (e) => e.map((x) => x.textContent.replace(/×$/, '').trim()))

/** 点某行的复选框。点的是 <input> 本身：事件冒泡到 <label> 时被 stopPropagation 拦下。 */
async function tick(page, host) {
  await rowOf(page, host).locator('td.sess-cell-check input[type=checkbox]').click()
  await page.waitForTimeout(200)
}

/** 等表格行数落到 n。等不到就让下面的断言去报错，不在这里吞。 */
const waitRows = (page, n, timeout = 8000) =>
  page.waitForFunction((n) => document.querySelectorAll('.sess-table tbody tr').length === n, n, { timeout })
    .catch(() => {})

async function openHosts(h) {
  await h.gotoView('主机')
  // 等表格真的画出来，别拿「还没渲染」当成「没有勾选列」
  await h.page.waitForSelector(ROW, { timeout: 10000 })
}

const sorted = (a) => [...a].sort()

// ══════════════════════════════════════════════════════════════
async function main() {
  console.log('\n── 主机页批量删除验收 ──\n')

  // ── S1：勾选 / 全选 / 确认框 / 精确下发集合 ──
  {
    const { st, routes } = mkBackend(mkAgents())
    const h = await launchConsole({ agents: mkAgents(), routes })
    try {
      const page = h.page
      await openHosts(h)

      // B1 勾选列存在
      const hdr = await page.$(HDR)
      const cells = await page.$$eval(`${ROW} td.sess-cell-check input`, (e) => e.length)
      check('B1', '勾选列存在（表头 + 每一行）',
        !!hdr && cells === HOSTS.length, `表头=${!!hdr} 行内复选框=${cells}/${HOSTS.length}`)

      // B2 点一行 ⇒ 该行 checked + 批量条
      const tabs0 = (await agentTabs(page)).length
      await tick(page, 'web-02')
      const bar1 = await barText(page)
      check('B2', '点行内复选框 ⇒ 该行 checked + 批量条说出真实数量',
        (await checkedRows(page)) === 1 && bar1.includes('已选 1 台') && bar1.includes('删除 1 台'),
        `checked=${await checkedRows(page)} 批量条="${bar1}"`)

      // B3 ★ 回归守卫：点复选框不得开出控制台页签
      //    （这一行上有 onClick={() => pick(a)}，不拦传播每点一次就多一个页签）
      const tabs1 = await agentTabs(page)
      check('B3', '★点行内复选框**不得**开出控制台页签',
        tabs1.length === tabs0 && tabs1.length === 0,
        `页签 ${tabs0}→${tabs1.length} [${tabs1.join(', ')}]`)

      // B4 表头框：部分勾选 ⇒ indeterminate；点击 ⇒ 全选
      await tick(page, 'db-01')
      const mid = await page.$eval(HDR, (el) => ({ c: el.checked, i: el.indeterminate }))
      await page.click(HDR)
      await page.waitForTimeout(250)
      const all = await page.$eval(HDR, (el) => ({ c: el.checked, i: el.indeterminate }))
      check('B4', '表头框：部分勾选时 indeterminate，点击后全选',
        mid.c === false && mid.i === true && all.c === true && all.i === false &&
        (await checkedRows(page)) === HOSTS.length,
        `部分={checked:${mid.c},ind:${mid.i}} 全选={checked:${all.c},ind:${all.i}} 行=${await checkedRows(page)}`)

      // B5 确认框内容：6 台全选，其中 db-03 离线 ⇒ 必须说出 5 在线 / 1 离线
      await page.click(`${BAR} button.danger`)
      await page.waitForTimeout(300)
      const dlg = await page.$eval('.dlg', (e) => e.textContent.replace(/\s+/g, ' ').trim()).catch(() => '')
      const listed = HOSTS.slice(0, 5).filter((x) => dlg.includes(x)).length
      check('B5', '确认框：在线/离线拆分 + 前 5 个主机名 + 「…等 N 台」',
        dlg.includes('删除 6 台主机') && dlg.includes('5 台在线') && dlg.includes('1 台离线') &&
        listed === 5 && dlg.includes('…等 6 台'),
        `在线/离线=${dlg.includes('5 台在线') && dlg.includes('1 台离线')} 列出=${listed}/5 截断=${dlg.includes('…等 6 台')}`)

      await page.click('.dlg .btns button:has-text("取消")')
      await page.waitForTimeout(250)

      // B6 ★ 精确下发集合。先全部取消，再只勾两台。
      await page.click(HDR)
      await page.waitForTimeout(250)
      check('B6a', '取消全选后批量条收起', (await checkedRows(page)) === 0 && (await barText(page)) === '',
        `checked=${await checkedRows(page)}`)

      await tick(page, 'web-01')
      await tick(page, 'db-02')
      await page.click(`${BAR} button.danger`)
      await page.waitForTimeout(300)
      await page.click('.dlg .btns button.danger')
      await waitRows(page, HOSTS.length - 2)

      const got = st.calls.length === 1 ? sorted(st.calls[0].agent_id_array || []) : []
      const want = sorted([idOf(0), idOf(4)])
      check('B6', '★捕获的 agent_id_array 与勾选集合**精确相等**',
        got.length === 2 && got.join(',') === want.join(','),
        `下发=[${got.join(', ')}] 期望=[${want.join(', ')}]（调用 ${st.calls.length} 次）`)

      const names = await page.$$eval(`${ROW} .cell-main`, (e) => e.map((x) => x.textContent.trim()))
      check('B6b', '删除后行消失、批量条收起、服务端真删了',
        !names.includes('web-01') && !names.includes('db-02') && names.length === 4 &&
        (await barText(page)) === '' && st.alive.length === 4,
        `剩=[${names.join(', ')}] 服务端剩 ${st.alive.length}`)
    } finally {
      await h.close()
    }
  }

  // ── S2：部分失败（约束 B 的修复）+ 对账 ──
  // 服务端**拒收** web-02（保留），但整体回 ok:false。
  // 期望：本地照样清理（4→1 行暂时全没了），然后 refreshRest 把被拒的那台捞回来。
  {
    const agents = mkAgents()
    const { st, routes } = mkBackend(agents, [idOf(1)])
    const h = await launchConsole({ agents, routes })
    try {
      const page = h.page
      await openHosts(h)
      await page.click(HDR)                       // 全选 6 台
      await page.waitForTimeout(250)
      await page.click(`${BAR} button.danger`)
      await page.waitForTimeout(300)
      await page.click('.dlg .btns button.danger')

      await waitRows(page, 1)                     // 对账后应只剩被拒的那一台
      const names = await page.$$eval(`${ROW} .cell-main`, (e) => e.map((x) => x.textContent.trim()))
      check('B7a', '★服务端报部分失败 ⇒ 本地仍然清理（不是 50 台全都还在）',
        st.calls.length === 1 && names.length === 1 && names[0] === 'web-02',
        `下发 ${(st.calls[0]?.agent_id_array || []).length} 个 id ⇒ 表格剩 [${names.join(', ')}]`)

      // 对账：服务端拒收的那台必须回来，不能凭空消失
      check('B7b', '被服务端拒收的那台在 refreshRest 后**回来**（对账）',
        names.length === 1 && names[0] === 'web-02' && st.alive.length === 1,
        `表格=[${names.join(', ')}] 服务端=[${st.alive.map((a) => a.a_computer).join(', ')}]`)

      // 报错要说出来，不能静默
      await h.gotoView('日志')
      const logs = await page.$$eval('.log-page .it', (e) => e.map((x) => x.textContent))
      check('B7c', '部分失败要报错（不是静默）',
        logs.some((l) => l.includes('未全部成功')) && logs.some((l) => l.includes('部分失败')),
        `命中=${logs.filter((l) => /未全部成功|部分失败/.test(l)).map((l) => l.trim().slice(0, 46)).join(' ｜ ')}`)
    } finally {
      await h.close()
    }
  }

  // ── S3：右键菜单单选删除（removeSelected 重构后的回归）──
  {
    const agents = mkAgents().slice(0, 3)
    const { st, routes } = mkBackend(agents)
    const h = await launchConsole({ agents, routes })
    try {
      const page = h.page
      page.on('dialog', (d) => d.accept())        // 右键删除走原生 confirm
      await openHosts(h)
      await rowOf(page, 'web-02').click({ button: 'right' })
      await page.waitForTimeout(300)
      await page.locator('.sess-ctx button', { hasText: '删除主机' }).first().click()
      await waitRows(page, 2)
      const got = st.calls.length === 1 ? sorted(st.calls[0].agent_id_array || []) : []
      const names = await page.$$eval(`${ROW} .cell-main`, (e) => e.map((x) => x.textContent.trim()))
      check('B8', '右键菜单单选删除仍正常（且只发一个 id）',
        got.length === 1 && got[0] === idOf(1) && names.length === 2 && !names.includes('web-02'),
        `下发=[${got.join(', ')}] 剩=[${names.join(', ')}]`)
    } finally {
      await h.close()
    }
  }

  // ── S4：焦点主机不在勾选集 ⇒ 控制台不能被抽走 ──
  {
    const agents = mkAgents().slice(0, 4)
    const { st, routes } = mkBackend(agents)
    const h = await launchConsole({ agents, routes })
    try {
      const page = h.page
      await openHosts(h)
      await rowOf(page, 'web-01').click()          // 聚焦 + 开控制台页签
      await page.waitForTimeout(400)
      const before = await agentTabs(page)
      await tick(page, 'web-02')
      await tick(page, 'db-01')
      await page.click(`${BAR} button.danger`)
      await page.waitForTimeout(300)
      await page.click('.dlg .btns button.danger')
      await waitRows(page, 2)

      const after = await agentTabs(page)
      const target = await page.$eval('.ops-target', (e) => e.textContent.trim()).catch(() => '')
      check('B9', '★焦点主机不在勾选集时，控制台页签与焦点主机都保住',
        before.includes(idOf(0)) && after.join(',') === [idOf(0)].join(',') &&
        target.includes('web-01') && st.alive.length === 2,
        `页签 ${before.length}→${after.length} [${after.join(', ')}] 标题栏="${target}"`)
    } finally {
      await h.close()
    }
  }

  // ── S5：批量删掉开着文件页签的主机 ⇒ 页签一起关 ──
  {
    const agents = mkAgents().slice(0, 3)
    const { st, routes } = mkBackend(agents)
    const h = await launchConsole({ agents, routes })
    try {
      const page = h.page
      await openHosts(h)
      await rowOf(page, 'web-02').click()
      await page.waitForTimeout(300)
      await page.click('.sess-actions button:has-text("文件")')
      await page.waitForTimeout(600)
      const opened = await uiTabs(page)
      const hadTab = opened.some((t) => t.includes('web-02'))

      // 开文件页签会把视图切走（App 按 current 渲染），所以得先导航回主机页 ——
      // 这也正是这条判据想要的场景：**后台**开着的文件页签随主机一起被清掉。
      await openHosts(h)
      await page.waitForSelector(ROW, { timeout: 10000 })

      await tick(page, 'web-02')
      await page.click(`${BAR} button.danger`)
      await page.waitForTimeout(300)
      await page.click('.dlg .btns button.danger')
      await waitRows(page, 2)

      const left = await uiTabs(page)
      check('B10', '批量删掉开着文件页签的主机 ⇒ 文件页签一并消失',
        hadTab && !left.some((t) => t.includes('web-02')) && (await agentTabs(page)).length === 0,
        `开过=[${opened.join(' | ')}] 删后=[${left.join(' | ')}] 控制台页签=${(await agentTabs(page)).length}`)
    } finally {
      await h.close()
    }
  }

  // ── S6：勾选与搜索筛选的交互 ──
  {
    const agents = mkAgents()
    const { st, routes } = mkBackend(agents)
    const h = await launchConsole({ agents, routes })
    try {
      const page = h.page
      await openHosts(h)
      await page.click(HDR)                        // 全选 6 台
      await page.waitForTimeout(250)

      await page.fill('.sess-search', 'web')       // 收窄到 3 台
      await page.waitForTimeout(400)
      const bar = await barText(page)
      const rows = await rowCount(page)
      check('B11', '★全选后收窄搜索 ⇒ 真实数量 + 「N 台不在当前筛选里」',
        rows === 3 && bar.includes('已选 6 台') && bar.includes('3 台不在当前筛选里'),
        `可见 ${rows} 行 批量条="${bar}"`)

      // 勾选没有被静默丢掉：直接删，下发的应该是 6 个 id 而不是 3 个
      await page.click(`${BAR} button.danger`)
      await page.waitForTimeout(300)
      const dlg = await page.$eval('.dlg h2', (e) => e.textContent.trim()).catch(() => '')
      await page.click('.dlg .btns button.danger')
      await waitRows(page, 0)
      const got = st.calls.length === 1 ? (st.calls[0].agent_id_array || []).length : 0
      check('B12', '★筛选外被隐藏的勾选也要删（不是只删看得见的 3 台）',
        dlg.includes('删除 6 台主机') && got === 6 && st.alive.length === 0,
        `确认框="${dlg}" 下发 ${got} 个 id，服务端剩 ${st.alive.length}`)

      // 清除按钮
      const d = await launchConsole({ agents: mkAgents(), routes: mkBackend(mkAgents()).routes })
      try {
        await openHosts(d)
        await tick(d.page, 'web-01')
        await tick(d.page, 'web-03')
        const had = await barText(d.page)
        await d.page.click(`${BAR} button:has-text("清除")`)
        await d.page.waitForTimeout(300)
        check('B12b', '「清除」清空勾选并收起批量条',
          had.includes('已选 2 台') && (await checkedRows(d.page)) === 0 && (await barText(d.page)) === '',
          `清除前="${had}" 清除后 checked=${await checkedRows(d.page)}`)
      } finally {
        await d.close()
      }
    } finally {
      await h.close()
    }
  }

  console.log(`\n${fails === 0 ? '✅' : '❌'} ${checks - fails}/${checks} 通过\n`)
  process.exit(fails === 0 ? 0 : 1)
}

main().catch((e) => { console.error(e); process.exit(1) })

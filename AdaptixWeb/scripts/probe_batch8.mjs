#!/usr/bin/env node
/* 第 8 批量基线：载荷 (Generate) + 目标 (Targets)。
 *
 * ★ 完全离线（见 scripts/_harness.mjs 文件头）：
 *   只导航与测量，不登录真实控制台、不带凭据、不向任何主机下发任何东西，
 *   /api/agent/command/** 一律伪造 200。
 *
 * 这个脚本**只量不改**，输出的是客观数值，用来决定改什么 ——
 * 不靠截图判断（见记忆 screenshot-review-verification：子代理给的像素值不可信，
 * 动手前必须 DOM 实测）。
 *
 * 用法（在 AdaptixWeb/ 下）: node scripts/probe_batch8.mjs
 */
import { launchConsole, fakeAgent, fakeListener } from './_harness.mjs'

// 5 个监听器：默认 profile 只认其中一部分 —— 复现用户报的「只显示 2/5」
const LISTENERS = [
  fakeListener('BeaconHTTP', { l_reg_name: 'BeaconHTTP', l_type: 'beacon' }),
  fakeListener('BeaconWS', { l_reg_name: 'BeaconWS', l_type: 'beacon' }),
  fakeListener('LinuxTCP', { l_reg_name: 'LinuxTCP', l_type: 'linux' }),
  fakeListener('GopherTCP', { l_reg_name: 'GopherTCP', l_type: 'gopher' }),
  fakeListener('KharonHTTP', { l_reg_name: 'KharonHTTP', l_type: 'kharon' }),
]

// 目标：故意混长短内容，dead space 只有在内容短时才看得出来
const TARGETS = [
  { t_target_id: 't1', t_computer: 'DC01', t_domain: 'corp.local', t_address: '10.0.4.5', t_os_desk: 'Windows Server 2019', t_tag: 'dc', t_info: '域控，勿动' },
  { t_target_id: 't2', t_computer: 'WEB-02', t_domain: 'corp.local', t_address: '10.0.4.31', t_os_desk: 'Ubuntu 22.04', t_tag: '', t_info: '' },
  { t_target_id: 't3', t_computer: 'FS01', t_domain: '', t_address: '10.0.4.9', t_os_desk: '', t_tag: 'fileserver', t_info: '' },
]

const h = await launchConsole({
  agents: [fakeAgent(0), fakeAgent(1, { a_os: 0, a_name: 'beacon', a_listener: 'BeaconHTTP' })],
  listeners: LISTENERS,
  targets: TARGETS,
})

try {
  // ───────────────────────── 目标 (Targets)
  await h.gotoView('目标')
  const t = await h.page.evaluate(() => {
    const g = (el) => { if (!el) return null; const r = el.getBoundingClientRect()
      return { x: Math.round(r.x), w: Math.round(r.width) } }
    const cs = (el) => el ? getComputedStyle(el) : null
    const table = document.querySelector('.ops table')
    const ths = [...document.querySelectorAll('.ops table thead th')]
    const firstRow = document.querySelector('.ops table tbody tr')
    const tds = firstRow ? [...firstRow.querySelectorAll('td')] : []
    const delBtn = document.querySelector('.ops table tbody tr button')
    const dcs = cs(delBtn)
    // 表格列的「死区」：单元格内容右边到下一个单元格左边之间没用上的宽度
    const cells = tds.map((td, i) => {
      const r = td.getBoundingClientRect()
      // 量内容实际占多宽（用一个行内探针，避免受 td 的 padding 影响判断）
      const range = document.createRange()
      range.selectNodeContents(td.firstChild && td.firstChild.nodeType === 1 ? td.firstChild : td)
      const cr = range.getBoundingClientRect()
      return { i, col: Math.round(r.width), content: Math.round(cr.width),
        text: (td.textContent || '').trim().slice(0, 24) }
    })
    const wrap = document.querySelector('.ops')
    return {
      toolbar: [...document.querySelectorAll('.toolbar > *')].map((e) => ({
        tag: e.tagName.toLowerCase(), cls: e.className,
        text: (e.textContent || '').trim().slice(0, 20),
        x: Math.round(e.getBoundingClientRect().x),
        w: Math.round(e.getBoundingClientRect().width),
      })),
      ths: ths.map((e) => ({ text: (e.textContent || '').trim(), w: Math.round(e.getBoundingClientRect().width) })),
      cells,
      tableW: table ? Math.round(table.getBoundingClientRect().width) : 0,
      wrapW: wrap ? Math.round(wrap.getBoundingClientRect().width) : 0,
      del: delBtn ? {
        text: (delBtn.textContent || '').trim(),
        bg: dcs.backgroundColor, border: dcs.borderColor, color: dcs.color,
        w: Math.round(delBtn.getBoundingClientRect().width),
        h: Math.round(delBtn.getBoundingClientRect().height),
        aria: delBtn.getAttribute('aria-label'), title: delBtn.getAttribute('title'),
      } : null,
      colHeaderForActions: ths.length ? (ths[ths.length - 1].textContent || '').trim() : null,
      rowCount: document.querySelectorAll('.ops table tbody tr').length,
    }
  })

  console.log('════════ 目标 (Targets) ════════')
  console.log(`表格宽 ${t.tableW} / 容器宽 ${t.wrapW}（差 ${t.wrapW - t.tableW}px）`)
  console.log(`行数 ${t.rowCount}`)
  console.log('\n表头：')
  t.ths.forEach((h2, i) => console.log(`  [${i}] ${JSON.stringify(h2.text)}  宽 ${h2.w}px`))
  console.log(`\n最后一列表头 = ${JSON.stringify(t.colHeaderForActions)}${t.colHeaderForActions === '' ? '  ← 空表头（无标签）' : ''}`)
  console.log('\n首行各列（宽度 vs 内容实际宽度 = 死区）：')
  t.cells.forEach((c) => console.log(
    `  [${c.i}] 列宽 ${String(c.col).padStart(4)}  内容 ${String(c.content).padStart(4)}  死区 ${String(c.col - c.content).padStart(4)}  ${JSON.stringify(c.text)}`))
  console.log('\n删除按钮：', JSON.stringify(t.del, null, 2))
  console.log('\n工具条顺序：')
  t.toolbar.forEach((e) => console.log(`  x=${String(e.x).padStart(4)} w=${String(e.w).padStart(4)}  ${e.tag}.${e.cls}  ${JSON.stringify(e.text)}`))

  // ───────────────────────── 载荷 (Generate)
  await h.gotoView('载荷')
  const g = await h.page.evaluate(() => {
    const list = document.querySelector('.gen-listener-list')
    const items = [...document.querySelectorAll('.gen-listener')]
    const cs = list ? getComputedStyle(list) : null
    // 页面上的 agent 卡片（每个 profile 一个）
    const cards = [...document.querySelectorAll('.gen-agent, .gen-preset, [class*="gen-agent"]')].map((e) => ({
      cls: e.className, text: (e.textContent || '').trim().slice(0, 30),
      active: /active|on\b/.test(e.className),
    }))
    const sel = [...document.querySelectorAll('.gen-listener.active')].map((e) => (e.textContent || '').trim())
    return {
      listFound: !!list,
      listBox: list ? { h: Math.round(list.getBoundingClientRect().height),
        client: list.clientHeight, scroll: list.scrollHeight } : null,
      overflowY: cs ? cs.overflowY : null,
      maxH: cs ? cs.maxHeight : null,
      itemCount: items.length,
      items: items.map((e) => ({ text: (e.textContent || '').trim().slice(0, 30),
        h: Math.round(e.getBoundingClientRect().height) })),
      selected: sel,
      cards,
      // 页面上任何提到「N / M」或计数的文案
      counts: [...document.querySelectorAll('.gen-page *')]
        .map((e) => (e.children.length === 0 ? (e.textContent || '').trim() : ''))
        .filter((s) => /监听|个/.test(s)).slice(0, 12),
    }
  })

  console.log('\n════════ 载荷 (Generate) ════════')
  console.log(`listener 列表存在：${g.listFound}`)
  console.log(`渲染出的监听项：${g.itemCount} 个 / 假后端共 ${LISTENERS.length} 个`)
  g.items.forEach((it) => console.log(`  · ${JSON.stringify(it.text)}  高 ${it.h}px`))
  console.log(`选中：${JSON.stringify(g.selected)}`)
  console.log(`列表盒：${JSON.stringify(g.listBox)}  overflowY=${g.overflowY} maxHeight=${g.maxH}`)
  console.log(`页面上跟「监听/个」有关的文案：${JSON.stringify(g.counts)}`)
  console.log(`\nAgent 卡片 ${g.cards.length} 张：`)
  g.cards.forEach((c) => console.log(`  ${c.active ? '●' : '○'} ${JSON.stringify(c.text)}  .${c.cls}`))
} catch (e) {
  console.error(`✗ 探查中断：${e?.stack || e}`)
  process.exitCode = 1
} finally {
  await h.close()
}

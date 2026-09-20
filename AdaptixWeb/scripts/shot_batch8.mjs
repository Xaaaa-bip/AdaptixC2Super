#!/usr/bin/env node
/* 第 8 批截图：目标 + 载荷。离线 harness，见 scripts/_harness.mjs 文件头。
 * 只用于肉眼复核 —— 数值判据在 probe_batch8.mjs / verify_batch8.mjs 里，
 * 截图只是补充（像素级结论一律以 DOM 实测为准）。
 * 用法: node scripts/shot_batch8.mjs [outdir]
 */
import { launchConsole, fakeAgent, fakeListener } from './_harness.mjs'
import fs from 'node:fs'
import path from 'node:path'

const OUT = process.argv[2] || 'shots/batch8'
fs.mkdirSync(OUT, { recursive: true })

const LISTENERS = [
  fakeListener('BeaconHTTP', { l_reg_name: 'BeaconHTTP', l_type: 'beacon', l_bind_port: 8080 }),
  fakeListener('BeaconWS', { l_reg_name: 'BeaconWS', l_type: 'beacon', l_bind_port: 8443 }),
  fakeListener('LinuxTCP', { l_reg_name: 'LinuxTCP', l_type: 'linux', l_bind_port: 4444 }),
  fakeListener('GopherTCP', { l_reg_name: 'GopherTCP', l_type: 'gopher', l_bind_port: 5555 }),
  fakeListener('KharonHTTP', { l_reg_name: 'KharonHTTP', l_type: 'kharon', l_bind_port: 6666 }),
]
const TARGETS = [
  { t_target_id: 't1', t_computer: 'DC01', t_domain: 'corp.local', t_address: '10.0.4.5', t_os_desk: 'Windows Server 2019', t_tag: 'dc', t_info: '域控，勿动' },
  { t_target_id: 't2', t_computer: 'WEB-02', t_domain: 'corp.local', t_address: '10.0.4.31', t_os_desk: 'Ubuntu 22.04', t_tag: '', t_info: '' },
  { t_target_id: 't3', t_computer: 'FS01', t_domain: '', t_address: '10.0.4.9', t_os_desk: '', t_tag: 'fileserver', t_info: '归档服务器，每周日 02:00 备份窗口，动手前确认备份已完成' },
]

const h = await launchConsole({
  agents: [fakeAgent(0), fakeAgent(1, { a_os: 0, a_name: 'beacon', a_listener: 'BeaconHTTP' })],
  listeners: LISTENERS, targets: TARGETS,
})
try {
  await h.gotoView('目标')
  await h.page.screenshot({ path: path.join(OUT, 'targets.png') })
  console.log(`写到 ${OUT}/targets.png`)
  await h.gotoView('载荷')
  await h.page.screenshot({ path: path.join(OUT, 'generate.png') })
  console.log(`写到 ${OUT}/generate.png`)
} finally { await h.close() }

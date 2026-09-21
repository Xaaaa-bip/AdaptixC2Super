import type { ApiClient } from './http'
import type { FileItem, ProcItem } from '../utils/browserParse'
import { decodeWindowsText, decodeWithEncoding } from '../utils/textDecode'
import { isLinuxAgent } from '../utils'
import { useStore } from '../store'
import { resolveOp, type AgentCtx, type BrowserOp, type Invocation } from './cmdTree'

/**
 * File/Process browser ops:
 * - ALWAYS use commandExecute(..., ui=true) → TASK_TYPE_BROWSER
 * - Results arrive as BROWSER_* WS packets into store.browserFiles / browserProcs
 * - Do NOT use command/raw (that prints full listings into the agent console)
 */

function sleep(ms: number) {
  return new Promise((r) => setTimeout(r, ms))
}

export type BrowserProgress = { sec: number; pending: boolean }

function stamp() {
  return Date.now()
}

/** loading 占位会写进 store 并带新 _ts；wait 绝不能把它当成真实 BROWSER_* 回显 */
function isPendingBrowserMessage(message?: string) {
  const m = String(message || '').trim().toLowerCase()
  return m.startsWith('loading')
}

function isFreshBrowserResult(cur: { message?: string; _ts?: number } | null | undefined, since: number) {
  if (!cur || !(cur as any)._ts || (cur as any)._ts < since) return false
  if (isPendingBrowserMessage(cur.message)) return false
  return true
}

async function waitBrowserFiles(
  agentId: string,
  since: number,
  wantPath?: string,
  timeoutMs = 22000,
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ path: string; items: FileItem[]; message: string }> {
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    const sec = Math.floor((Date.now() - start) / 1000)
    onProgress?.({ sec, pending: true })
    const cur = useStore.getState().browserFiles[agentId]
    if (isFreshBrowserResult(cur as any, since)) {
      // path soft-match: empty path for disks; otherwise accept any update after stamp
      if (wantPath == null || wantPath === '' || !cur.path || cur.path === wantPath || wantPath === '.' || cur.path.startsWith(wantPath) || wantPath.startsWith(cur.path)) {
        return {
          path: cur.path || wantPath || '.',
          items: (cur.items || []) as FileItem[],
          message: cur.message || '',
        }
      }
    }
    await sleep(250)
  }
  // last snapshot even if path imperfect（仍忽略 loading 占位）
  const cur = useStore.getState().browserFiles[agentId]
  if (isFreshBrowserResult(cur as any, since)) {
    return {
      path: cur.path || wantPath || '.',
      items: (cur.items || []) as FileItem[],
      message: cur.message || '',
    }
  }
  throw new Error('等待文件列表回显超时（检查主机是否在线 / Sleep 是否过长）')
}

async function waitBrowserProcs(
  agentId: string,
  since: number,
  timeoutMs = 25000,
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ items: ProcItem[]; message: string }> {
  const start = Date.now()
  while (Date.now() - start < timeoutMs) {
    const sec = Math.floor((Date.now() - start) / 1000)
    onProgress?.({ sec, pending: true })
    const cur = useStore.getState().browserProcs[agentId]
    if (isFreshBrowserResult(cur as any, since)) {
      return {
        items: (cur.items || []) as ProcItem[],
        message: cur.message || '',
      }
    }
    await sleep(250)
  }
  throw new Error('等待进程列表回显超时（检查主机是否在线 / Sleep 是否过长）')
}

/** 从 store 取 agent（部分调用只拿到 agentId） */
function agentOf(agentId: string) {
  return (useStore.getState().agents || {})[agentId] as { a_os?: number; a_listener?: string } | undefined
}

/**
 * 会话上下文 → 命令树查找键。
 *
 * a_listener 是监听器**显示名**（操作员随便起，如 khtest-1），而命令树是按
 * **注册名**（l_reg_name，如 KharonHTTP）分组的 —— kharon 只有 KharonHTTP 注册了
 * 命令，名字对不上就退化到「该 agent 的第一个非空批次」，所以这层映射必须做。
 */
function ctxOf(agentId: string): AgentCtx {
  const a = agentOf(agentId) as { a_name?: string; a_listener?: string; a_os?: number } | undefined
  const ls = (useStore.getState().listeners || {}) as Record<string, { l_reg_name?: string }>
  const reg = a?.a_listener ? ls[a.a_listener]?.l_reg_name || '' : ''
  return { agent: String(a?.a_name || ''), listenerReg: reg, os: Number(a?.a_os || 0) }
}

/**
 * 解析并下发一个浏览器操作。
 * 该 (agent, listener) 组合下没有对应命令时**报错**，而不是发一条注定失败的命令
 * 让窗口转到超时 —— 后者从现象上完全看不出是命令不在。
 */
async function sendOp(
  api: ApiClient,
  agentId: string,
  op: BrowserOp,
  values: unknown[],
  message: string,
): Promise<Invocation> {
  const ctx = ctxOf(agentId)
  const inv = await resolveOp(api, ctx, op, values)
  if (!inv) {
    throw new Error(
      `该主机没有可用的「${op}」命令（agent=${ctx.agent || '?'} listener=${ctx.listenerReg || '?'}）`,
    )
  }
  await api.commandBrowser(agentId, inv.cmdline, { ...inv.args, message })
  return inv
}

function fixWinPath(p: string, agent?: { a_os?: number; a_listener?: string } | null) {
  const s = String(p || '').trim()
  if (!s) return '.'
  // Linux 主机：路径原样透传，不做盘符/反斜杠归一化
  if (isLinuxAgent(agent)) return s
  let out = s
  // C: / C:\ / C:/
  const driveOnly = out.match(/^([a-zA-Z]):[\\/]*$/)
  if (driveOnly) return driveOnly[1].toUpperCase() + ':\\'
  if (/^[a-zA-Z]:/.test(out) || out.includes('\\')) {
    out = out.replace(/\//g, '\\').replace(/\\+/g, '\\')
    // C:\C:\foo → C:\foo
    out = out.replace(/^([a-zA-Z]:\\)\1+/i, '$1')
    out = out.replace(/^([a-zA-Z]:\\)([a-zA-Z]:\\)/i, '$2')
  }
  return out
}

export async function browserListFiles(
  api: ApiClient,
  agentId: string,
  path = '.',
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ path: string; items: FileItem[]; message: string }> {
  const agent = agentOf(agentId)
  const p = fixWinPath(path || '.', agent)
  const since = stamp()
  // mark pending so we don't accept stale store data
  useStore.getState().setBrowserFiles(agentId, p, useStore.getState().browserFiles[agentId]?.items || [], 'loading…')
  // 命令与参数名全部取自命令树：beacon/linux/macos/gopher 是 `ls <path>`，
  // kharon 是 `fs ls <directory>` —— 参数名不同，由树决定。
  await sendOp(api, agentId, 'listDir', [p], 'Task: list files')
  const res = await waitBrowserFiles(agentId, since, p === '.' ? undefined : p, 22000, onProgress)
  if (!res.items.length && /error/i.test(res.message)) {
    throw new Error(res.message || '列目录失败')
  }
  return {
    ...res,
    path: fixWinPath(res.path || p, agent),
  }
}

export async function browserListDisks(
  api: ApiClient,
  agentId: string,
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ path: string; items: FileItem[]; message: string }> {
  const agent = agentOf(agentId)
  const linux = isLinuxAgent(agent)
  // 只有 beacon 注册了 disks 命令。其余 agent（含 kharon —— 它的
  // SyncBrowserDisks 是死代码，从没被调用过）硬发一条不存在的命令只会让窗口
  // 转到超时，不如直接说清楚，让操作员在地址栏手输路径。
  const inv = await resolveOp(api, ctxOf(agentId), 'disks')
  if (!inv) {
    return { path: '', items: [], message: '该主机不支持枚举驱动器，请在地址栏直接输入路径' }
  }
  const since = stamp()
  useStore.getState().setBrowserFiles(agentId, '', [], 'loading disks…')
  await api.commandBrowser(agentId, inv.cmdline, { ...inv.args, message: 'Task: show mounted disks' })
  const res = await waitBrowserFiles(agentId, since, '', 22000, onProgress)
  // normalize disk names to C:\ style（Linux 挂载点原样保留）
  const items = (res.items || []).map((x) => {
    let name = String(x.name || '')
    if (!linux) {
      if (/^[a-zA-Z]:$/.test(name)) name = name + '\\'
      if (/^[a-zA-Z]:\\?$/.test(name) && !name.endsWith('\\')) name = name + '\\'
    }
    return { ...x, name, isDir: true }
  }).filter((x) => x.name)
  if (!items.length) {
    // Linux 没有盘符概念：不回退到假的 ['C:\\','D:\\']
    if (linux) return { path: '', items: [], message: '无挂载点' }
    ;['C:\\', 'D:\\'].forEach((n) => items.push({ name: n, isDir: true, size: 0, date: 0 }))
  }
  return { path: '', items, message: res.message || 'Disks' }
}

export async function browserListProcs(
  api: ApiClient,
  agentId: string,
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ items: ProcItem[]; message: string }> {
  const since = stamp()
  useStore.getState().setBrowserProcs(agentId, [], 'loading…')
  // beacon=`ps list`，linux/macos/gopher=裸 `ps`（linux 的 ps 只挂了 kill
  // 子命令，列表动作就是裸命令），kharon=`process list`。
  await sendOp(api, agentId, 'listProcs', [], 'Task: show process list')
  return waitBrowserProcs(agentId, since, 25000, onProgress)
}

export async function browserKillProc(
  api: ApiClient,
  agentId: string,
  pid: number,
  _onProgress?: (p: BrowserProgress) => void,
): Promise<{ message: string }> {
  // ui=true → TASK_TYPE_BROWSER：不写 agent 命令控制台。
  // 解析器优先选独立顶层 `kill`：macos/gopher 的 ps 不读子命令，发 `ps kill`
  // 会被它们自己的 `case "ps"` 吞掉、静默变成列进程。
  await sendOp(api, agentId, 'killProc', [Number(pid)], 'Task: kill process')
  return { message: `已请求结束 PID ${pid}` }
}

export async function browserDownloadFile(api: ApiClient, agentId: string, path: string) {
  const remote = fixWinPath(path, agentOf(agentId))
  // 参数名由树决定，三家都不同：beacon=file（pl_main.go:1749）、
  // kharon=remote_path（pl_agent.go:1443）、linux/macos/gopher=path。
  await sendOp(api, agentId, 'download', [remote], 'Task: download file')
}

export async function browserRm(api: ApiClient, agentId: string, path: string) {
  const remote = fixWinPath(path, agentOf(agentId))
  // 参数名两边都叫 path，只有命令树不同：kharon 是 fs rm
  await sendOp(api, agentId, 'rm', [remote], 'Task: remove path')
}

/**
 * Read file head (beacon cat ≈ first 2048 bytes).
 * ui=true so it does not spam the agent console; we still poll task list for cleartext.
 */
export async function browserCat(
  api: ApiClient,
  agentId: string,
  path: string,
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ message: string; text: string }> {
  const agent = agentOf(agentId)
  const remote = fixWinPath(path, agent)
  if (!isLinuxAgent(agent) && /^[a-zA-Z]:\\[a-zA-Z]:\\/i.test(remote)) {
    throw new Error(`路径异常（盘符重复）: ${remote}`)
  }
  // kharon 是 fs cat，其余是 cat；参数名都叫 path。
  // 下面的回显匹配用的是 cmd.includes('cat')，"fs cat ..." 也命中。
  // snapshot task ids then poll for cleartext without forcing console UI open
  const before = await api.listTasks(agentId).catch(() => [])
  const known = new Set((Array.isArray(before) ? before : []).map((t: any) => String(t?.a_task_id || t?.task_id || '')).filter(Boolean))
  await sendOp(api, agentId, 'cat', [remote], 'Task: cat file')

  const start = Date.now()
  let delay = 300
  while (Date.now() - start < 20000) {
    const sec = Math.floor((Date.now() - start) / 1000)
    onProgress?.({ sec, pending: true })
    const arr = await api.listTasks(agentId).catch(() => [])
    const list = Array.isArray(arr) ? arr : []
    const hit = list.find((t: any) => {
      const id = String(t?.a_task_id || t?.task_id || '')
      if (!id || known.has(id)) return false
      const cmd = String(t?.a_cmdline || '').toLowerCase()
      if (!cmd.includes('cat')) return false
      if (t?.a_completed === false && !t?.a_finish_time) return false
      return true
    })
    if (hit) {
      const message = String(hit.a_message || hit.message || '')
      let text = String(hit.a_text || hit.text || '')
      if (!text && /error/i.test(message)) throw new Error(message || '读取失败')
      if (isLinuxAgent(agent)) {
        // Linux 主机：按 UTF-8 解码，跳过 Windows GBK/ACP 猜测
        text = decodeWithEncoding(text, 'utf-8')
      } else {
        // Fix GBK/ACP file content mojibake when server returned raw bytes as string
        const acp = Number((agent as any)?.a_acp || (agent as any)?.ACP || 0)
        const oemcp = Number((agent as any)?.a_oemcp || (agent as any)?.OemCP || 0)
        text = decodeWindowsText(text, { acp: acp || 936, oemcp: oemcp || acp || 936 })
      }
      return { message: message || `cat ${remote}`, text }
    }
    await sleep(delay)
    delay = Math.min(900, delay + 80)
  }
  throw new Error('读取超时：未捕获到 cat 回显')
}

export async function browserUploadFile(
  api: ApiClient,
  agentId: string,
  file: File,
  remotePath: string,
  onProgress?: (pct: number) => void,
) {
  const fileId = await api.uploadTempFile(file, onProgress)
  await api.uploadToAgent(agentId, fileId, file.name, remotePath)
}

export async function browserBoost(api: ApiClient, agentId: string) {
  await api.commandRaw(agentId, 'sleep 0')
}

export async function browserRestoreSleep(api: ApiClient, agentId: string, sleepSec: number, jitter = 0) {
  const s = Math.max(0, Math.floor(Number(sleepSec) || 0))
  const j = Math.max(0, Math.min(100, Math.floor(Number(jitter) || 0)))
  if (j > 0) await api.commandRaw(agentId, `sleep ${s} ${j}`)
  else await api.commandRaw(agentId, `sleep ${s}`)
}

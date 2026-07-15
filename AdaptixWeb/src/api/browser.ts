import type { ApiClient } from './http'
import type { FileItem, ProcItem } from '../utils/browserParse'
import { decodeWindowsText } from '../utils/textDecode'
import { useStore } from '../store'

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

function fixWinPath(p: string) {
  let s = String(p || '').trim()
  if (!s) return '.'
  // C: / C:\ / C:/
  const driveOnly = s.match(/^([a-zA-Z]):[\\/]*$/)
  if (driveOnly) return driveOnly[1].toUpperCase() + ':\\'
  if (/^[a-zA-Z]:/.test(s) || s.includes('\\')) {
    s = s.replace(/\//g, '\\').replace(/\\+/g, '\\')
    // C:\C:\foo → C:\foo
    s = s.replace(/^([a-zA-Z]:\\)\1+/i, '$1')
    s = s.replace(/^([a-zA-Z]:\\)([a-zA-Z]:\\)/i, '$2')
  }
  return s
}

export async function browserListFiles(
  api: ApiClient,
  agentId: string,
  path = '.',
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ path: string; items: FileItem[]; message: string }> {
  const p = fixWinPath(path || '.')
  const cmdline = p === '.' ? 'ls .' : `ls ${p}`
  const since = stamp()
  // mark pending so we don't accept stale store data
  useStore.getState().setBrowserFiles(agentId, p, useStore.getState().browserFiles[agentId]?.items || [], 'loading…')
  await api.commandBrowser(agentId, cmdline, {
    command: 'ls',
    path: p,
    message: 'Task: list files',
  })
  const res = await waitBrowserFiles(agentId, since, p === '.' ? undefined : p, 22000, onProgress)
  if (!res.items.length && /error/i.test(res.message)) {
    throw new Error(res.message || '列目录失败')
  }
  return {
    ...res,
    path: fixWinPath(res.path || p),
  }
}

export async function browserListDisks(
  api: ApiClient,
  agentId: string,
  onProgress?: (p: BrowserProgress) => void,
): Promise<{ path: string; items: FileItem[]; message: string }> {
  const since = stamp()
  useStore.getState().setBrowserFiles(agentId, '', [], 'loading disks…')
  await api.commandBrowser(agentId, 'disks', {
    command: 'disks',
    message: 'Task: show mounted disks',
  })
  const res = await waitBrowserFiles(agentId, since, '', 22000, onProgress)
  // normalize disk names to C:\ style
  const items = (res.items || []).map((x) => {
    let name = String(x.name || '')
    if (/^[a-zA-Z]:$/.test(name)) name = name + '\\'
    if (/^[a-zA-Z]:\\?$/.test(name) && !name.endsWith('\\')) name = name + '\\'
    return { ...x, name, isDir: true }
  }).filter((x) => x.name)
  if (!items.length) {
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
  await api.commandBrowser(agentId, 'ps list', {
    command: 'ps',
    subcommand: 'list',
    message: 'Task: show process list',
  })
  return waitBrowserProcs(agentId, since, 25000, onProgress)
}

export async function browserKillProc(
  api: ApiClient,
  agentId: string,
  pid: number,
  _onProgress?: (p: BrowserProgress) => void,
): Promise<{ message: string }> {
  // ui=true → TASK_TYPE_BROWSER：不写 agent 命令控制台
  await api.commandBrowser(agentId, `ps kill ${pid}`, {
    command: 'ps',
    subcommand: 'kill',
    pid: Number(pid),
    message: 'Task: kill process',
  })
  return { message: `已请求结束 PID ${pid}` }
}

export async function browserDownloadFile(api: ApiClient, agentId: string, path: string) {
  const remote = fixWinPath(path)
  await api.commandBrowser(agentId, `download ${remote}`, {
    command: 'download',
    file: remote,
    message: 'Task: download file',
  })
}

export async function browserRm(api: ApiClient, agentId: string, path: string) {
  const remote = fixWinPath(path)
  await api.commandBrowser(agentId, `rm ${remote}`, {
    command: 'rm',
    path: remote,
    message: 'Task: remove path',
  })
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
  const remote = fixWinPath(path)
  if (/^[a-zA-Z]:\\[a-zA-Z]:\\/i.test(remote)) {
    throw new Error(`路径异常（盘符重复）: ${remote}`)
  }
  const cmdline = `cat ${remote}`
  // snapshot task ids then poll for cleartext without forcing console UI open
  const before = await api.listTasks(agentId).catch(() => [])
  const known = new Set((Array.isArray(before) ? before : []).map((t: any) => String(t?.a_task_id || t?.task_id || '')).filter(Boolean))
  await api.commandBrowser(agentId, cmdline, {
    command: 'cat',
    path: remote,
    message: 'Task: cat file',
  })

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
      // Fix GBK/ACP file content mojibake when server returned raw bytes as string
      const agent = useStore.getState().agents?.[agentId] as any
      const acp = Number(agent?.a_acp || agent?.ACP || 0)
      const oemcp = Number(agent?.a_oemcp || agent?.OemCP || 0)
      text = decodeWindowsText(text, { acp: acp || 936, oemcp: oemcp || acp || 936 })
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

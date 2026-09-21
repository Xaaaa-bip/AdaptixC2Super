import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import {
  browserCat, browserDownloadFile, browserListDisks, browserListFiles, browserRm, browserUploadFile,
} from '../api/browser'
import { isAbsPath, joinPath, normalizePath, parentPath, type FileItem } from '../utils/browserParse'
import {
  TEXT_ENCODING_OPTIONS,
  decodeWindowsText,
  decodeWithEncoding,
  detectEncodingId,
  type TextEncodingId,
} from '../utils/textDecode'
import { copyText, isLinuxAgent } from '../utils'
import { PageTips } from './PageTips'

type SortKey = 'name' | 'size' | 'date' | 'type'
type Ctx = { x: number; y: number; item: FileItem } | null
type TreeNode = { path: string; name: string; children?: string[]; loaded?: boolean; open?: boolean }

function isWinPath(p: string, linux = false) {
  // Linux 主机不做盘符/反斜杠判定
  if (linux) return false
  return /\\/.test(p) || /^[a-zA-Z]:/.test(p)
}

function crumbsOf(path: string, linux = false): { label: string; path: string }[] {
  // Linux 无盘符概念：面包屑根显示「根目录」（树根为「挂载点」）
  const rootLabel = linux ? '根目录' : '此电脑'
  if (!path || path === '.') return [{ label: '当前目录', path: '.' }]
  if (path === '' || path === '(disks)') return [{ label: rootLabel, path: '' }]
  const win = isWinPath(path, linux)
  const sep = win ? '\\' : '/'
  // keep root like C:\ or /
  if (win) {
    const m = path.match(/^([a-zA-Z]:\\?)(.*)$/)
    if (!m) return [{ label: path, path }]
    const root = m[1].endsWith('\\') ? m[1] : m[1] + '\\'
    const rest = (m[2] || '').replace(/^\\+/, '')
    const parts = rest ? rest.split(/\\+/).filter(Boolean) : []
    const out = [{ label: root.replace(/\\$/, ''), path: root }]
    let cur = root
    for (const part of parts) {
      cur = cur.endsWith('\\') ? cur + part : cur + '\\' + part
      out.push({ label: part, path: cur })
    }
    return out
  }
  if (path === '/') return [{ label: '/', path: '/' }]
  const parts = path.replace(/\/+$/, '').split('/').filter(Boolean)
  const out = [{ label: '/', path: '/' }]
  let cur = ''
  for (const part of parts) {
    cur += '/' + part
    out.push({ label: part, path: cur })
  }
  return out
}

function extOf(name: string) {
  const i = name.lastIndexOf('.')
  if (i <= 0) return ''
  return name.slice(i + 1).toLowerCase()
}

function fileIcon(it: FileItem, isDisk = false, linux = false) {
  // Linux 挂载点用中性文件夹图标，不做盘符判定
  if (isDisk) return linux ? '📁' : '💾'
  if (!linux && /^[a-zA-Z]:\\?$/.test(it.name)) return '💾'
  if (it.isDir) return '📁'
  const e = extOf(it.name)
  if (['exe', 'msi', 'bat', 'cmd', 'ps1', 'com', 'scr'].includes(e)) return '⚙️'
  if (['dll', 'sys', 'ocx'].includes(e)) return '🧩'
  if (['txt', 'log', 'md', 'ini', 'cfg', 'conf', 'json', 'xml', 'yml', 'yaml'].includes(e)) return '📄'
  if (['png', 'jpg', 'jpeg', 'gif', 'bmp', 'ico', 'webp', 'svg'].includes(e)) return '🖼️'
  if (['zip', 'rar', '7z', 'tar', 'gz', 'cab'].includes(e)) return '📦'
  if (['pdf'].includes(e)) return '📕'
  if (['doc', 'docx', 'xls', 'xlsx', 'ppt', 'pptx', 'csv'].includes(e)) return '📊'
  if (['mp3', 'wav', 'flac', 'aac'].includes(e)) return '🎵'
  if (['mp4', 'avi', 'mkv', 'mov', 'wmv'].includes(e)) return '🎬'
  return '📃'
}

function displaySize(it: FileItem) {
  if (it.isDir) return '—'
  return it.sizeText || (it.size ? String(it.size) : '—')
}

export function FileManager({ agentId }: { agentId: string }) {
  const api = useStore((s) => s.api)
  const agents = useStore((s) => s.agents) || {}
  const closeUiTab = useStore((s) => s.closeUiTab)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  const openConsole = useStore((s) => s.openConsole)
  const interactBoost = useStore((s) => s.interactBoost)
  const acquireInteractBoost = useStore((s) => s.acquireInteractBoost)
  const agent = agents[agentId]
  const boosted = !!interactBoost[agentId]?.ref
  // Linux 主机：无盘符 / 反斜杠 / GBK 概念，界面与路径处理走 Unix 分支
  const isLinux = isLinuxAgent(agent)
  const diskLabel = isLinux ? '挂载点' : '磁盘'
  const rootLabel = isLinux ? '挂载点' : '此电脑'

  const [path, setPath] = useState('.')
  const [pathInput, setPathInput] = useState('.')
  const [items, setItems] = useState<FileItem[]>([])
  const [message, setMessage] = useState('')
  const [busy, setBusy] = useState(false)
  const [progress, setProgress] = useState('')
  const [err, setErr] = useState('')
  const [q, setQ] = useState('')
  const [sortKey, setSortKey] = useState<SortKey>('name')
  const [sortAsc, setSortAsc] = useState(true)
  const [selected, setSelected] = useState<string | null>(null)
  const [selectedSet, setSelectedSet] = useState<Set<string>>(new Set())
  const [lastClicked, setLastClicked] = useState<string | null>(null)
  const [ctx, setCtx] = useState<Ctx>(null)
  const [mode, setMode] = useState<'list' | 'disks'>('list')
  const [history, setHistory] = useState<string[]>(['.'])
  const [histIdx, setHistIdx] = useState(0)
  const [tree, setTree] = useState<Record<string, TreeNode>>({
    '': { path: '', name: isLinux ? '挂载点' : '此电脑', open: true, loaded: true, children: [] },
  })
  const [uploading, setUploading] = useState(false)
  const [preview, setPreview] = useState<{
    path: string
    title: string
    /** original payload from server (may be mojibake / byte-preserving) */
    raw: string
    message?: string
    text: string
    encoding: TextEncodingId
    detected?: TextEncodingId
    busy?: boolean
  } | null>(null)
  const seq = useRef(0)
  const tableRef = useRef<HTMLDivElement>(null)
  const fileInputRef = useRef<HTMLInputElement>(null)

  const crumbs = useMemo(() => crumbsOf(mode === 'disks' ? '' : path, isLinux), [path, mode, isLinux])

  const view = useMemo(() => {
    let arr = [...items]
    if (q.trim()) {
      const s = q.trim().toLowerCase()
      arr = arr.filter((x) => x.name.toLowerCase().includes(s))
    }
    const dirFirst = (a: FileItem, b: FileItem) => {
      if (a.isDir !== b.isDir) return a.isDir ? -1 : 1
      return 0
    }
    arr.sort((a, b) => {
      const d = dirFirst(a, b)
      if (d) return d
      let cmp = 0
      if (sortKey === 'name') cmp = a.name.localeCompare(b.name, undefined, { sensitivity: 'base' })
      else if (sortKey === 'size') cmp = (a.sizeText || '').localeCompare(b.sizeText || '')
      else if (sortKey === 'date') cmp = (a.dateText || '').localeCompare(b.dateText || '')
      else cmp = Number(b.isDir) - Number(a.isDir)
      return sortAsc ? cmp : -cmp
    })
    return arr
  }, [items, q, sortKey, sortAsc])

  const selectedItem = useMemo(
    () => view.find((x) => x.name === selected) || null,
    [view, selected],
  )
  const multiItems = useMemo(
    () => view.filter((x) => selectedSet.has(x.name)),
    [view, selectedSet],
  )

  function pushHistory(p: string) {
    setHistory((h) => {
      const next = h.slice(0, histIdx + 1)
      if (next[next.length - 1] === p) return next
      const arr = [...next, p].slice(-40)
      setHistIdx(arr.length - 1)
      return arr
    })
  }

  function treeLabel(p: string) {
    if (p === '') return isLinux ? '挂载点' : '此电脑'
    if (p === '.') return '当前目录'
    // C:\ or C:
    if (!isLinux && /^[a-zA-Z]:\\?$/.test(p)) return p.replace(/\\$/, '')
    if (isLinux && p === '/') return '/'
    return p.split(/[\\/]/).filter(Boolean).pop() || p
  }

  function treeParent(p: string): string | null {
    if (!p || p === '.' || p === '/') return null
    // disk root belongs under "此电脑"；Linux 挂载点根（/xxx）挂在「挂载点」下
    if (!isLinux && /^[a-zA-Z]:\\?$/.test(p)) return ''
    if (isLinux && /^\/[^/]+$/.test(p)) return ''
    const parent = parentPath(p)
    // parentPath may return self for roots — treat as cycle
    if (!parent || parent === p) return ''
    return parent
  }

  function rememberTree(dirPath: string, list: FileItem[]) {
    const dirs = list
      .filter((x) => x.isDir)
      .map((x) => {
        if (!dirPath || dirPath === '.') {
          // disk list items are already full roots like C:\（Linux 挂载点原样保留）
          if (!isLinux && /^[a-zA-Z]:/.test(x.name)) return x.name.endsWith('\\') ? x.name : (x.name + '\\')
          return x.name
        }
        return joinPath(dirPath, x.name)
      })
      // never point to self
      .filter((c) => c && c !== dirPath)

    setTree((t) => {
      const n: Record<string, TreeNode> = { ...t }
      n[dirPath] = {
        ...(n[dirPath] || { path: dirPath, name: treeLabel(dirPath) }),
        path: dirPath,
        name: treeLabel(dirPath),
        children: dirs,
        loaded: true,
        open: true,
      }
      for (const c of dirs) {
        if (!n[c]) {
          n[c] = {
            path: c,
            name: treeLabel(c),
            children: [],
            loaded: false,
            open: false,
          }
        }
      }
      // attach under parent (if any), never self
      const parent = treeParent(dirPath)
      if (parent !== null && parent !== dirPath) {
        const prev = n[parent] || { path: parent, name: treeLabel(parent), children: [], loaded: false, open: true }
        const kids = Array.from(new Set([...(prev.children || []), dirPath].filter((x) => x && x !== parent)))
        n[parent] = { ...prev, children: kids, open: true }
      }
      return n
    })
  }

  async function load(target: string, opts?: { fromHistory?: boolean }) {
    const my = ++seq.current
    const dest = normalizePath(target || '.')
    setBusy(true)
    setErr('')
    setProgress('')
    setMessage('正在下发 ls…')
    setMode('list')
    setSelected(null)
    setSelectedSet(new Set())
    setLastClicked(null)
    setCtx(null)
    setPath(dest)
    setPathInput(dest)
    if (!opts?.fromHistory) pushHistory(dest)
    try {
      const res = await browserListFiles(api, agentId, dest || '.', ({ sec, pending }) => {
        if (my !== seq.current) return
        setProgress(pending ? `已下发，等待回显 ${sec}s…` : `轮询任务 ${sec}s…`)
        setMessage(pending ? `等待主机回显 ${sec}s` : `查询结果 ${sec}s`)
      })
      if (my !== seq.current) return
      const resolved = normalizePath(res.path || dest)
      setItems(res.items)
      setPath(resolved)
      setPathInput(resolved)
      setMessage(`${res.items.length} 项`)
      setProgress('')
      rememberTree(resolved, res.items)
      pushLog('ok', `文件 ${resolved}: ${res.items.length}`, false)
    } catch (e: any) {
      if (my !== seq.current) return
      const msg = e?.message || String(e)
      setErr(msg)
      setMessage('')
      setProgress('')
      setItems([])
      pushToast('err', msg)
    } finally {
      if (my === seq.current) setBusy(false)
    }
  }

  async function loadDisks(opts?: { fromHistory?: boolean }) {
    const my = ++seq.current
    setBusy(true)
    setErr('')
    setProgress('')
    setMessage(isLinux ? '正在读取挂载点…' : '正在读取磁盘…')
    setMode('disks')
    setSelected(null)
    setSelectedSet(new Set())
    setLastClicked(null)
    setCtx(null)
    if (!opts?.fromHistory) pushHistory('')
    try {
      const res = await browserListDisks(api, agentId, ({ sec, pending }) => {
        if (my !== seq.current) return
        setProgress(pending ? `已下发，等待回显 ${sec}s…` : `轮询任务 ${sec}s…`)
      })
      if (my !== seq.current) return
      setItems(res.items)
      setPath('')
      setPathInput('')
      setMessage(`${res.items.length} 个${diskLabel}`)
      setProgress('')
      setTree((t) => ({
        ...t,
        '': {
          path: '',
          name: isLinux ? '挂载点' : '此电脑',
          open: true,
          loaded: true,
          children: res.items.map((x) => x.name),
        },
        ...Object.fromEntries(res.items.map((x) => [x.name, {
          path: x.name,
          name: x.name,
          children: [],
          loaded: false,
          open: false,
        }])),
      }))
    } catch (e: any) {
      if (my !== seq.current) return
      setErr(e?.message || String(e))
      setProgress('')
      setItems([])
    } finally {
      if (my === seq.current) setBusy(false)
    }
  }

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      // 先加速心跳，再列目录（产品级：打开即 interact）
      try { await acquireInteractBoost(agentId) } catch { /* toast 已在 store */ }
      if (cancelled) return
      await load('.')
      if (cancelled) return
      // preload disks into tree quietly
      browserListDisks(api, agentId).then((res) => {
        if (cancelled) return
        setTree((t) => ({
          ...t,
          '': {
            path: '',
            name: isLinux ? '挂载点' : '此电脑',
            open: true,
            loaded: true,
            children: res.items.map((x) => x.name),
          },
          ...Object.fromEntries(res.items.map((x) => [x.name, t[x.name] || {
            path: x.name,
            name: x.name,
            children: [],
            loaded: false,
            open: false,
          }])),
        }))
      }).catch(() => {})
    })()
    return () => { cancelled = true }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [agentId])

  useEffect(() => {
    const close = () => setCtx(null)
    window.addEventListener('click', close)
    window.addEventListener('scroll', close, true)
    return () => {
      window.removeEventListener('click', close)
      window.removeEventListener('scroll', close, true)
    }
  }, [])

  function toggleSort(k: SortKey) {
    if (sortKey === k) setSortAsc((v) => !v)
    else { setSortKey(k); setSortAsc(true) }
  }

  /** Build full remote path for a list row without doubling drive letters. */
  function itemPath(it: FileItem) {
    const name = String(it?.name || '').trim()
    if (!name) return normalizePath(path || '.')
    // disks mode or empty cwd: entry name is already a root/full path
    if (mode === 'disks' || !path || path === '') return normalizePath(name)
    // server sometimes returns absolute names
    if (isAbsPath(name)) return normalizePath(name)
    return joinPath(path || '.', name)
  }

  function openItem(it: FileItem) {
    if (mode === 'disks' || it.isDir) {
      load(itemPath(it))
      return
    }
    // file: double-click opens content preview
    setSelected(it.name)
    setSelectedSet(new Set([it.name]))
    void doCat(it)
  }

  async function doDownload(it: FileItem) {
    const p = itemPath(it)
    try {
      await browserDownloadFile(api, agentId, p)
      pushToast('ok', `已下发下载：${it.name}`)
    } catch (e: any) { pushToast('err', e.message || String(e)) }
  }

  function agentCodePages() {
    // Linux 主机默认 UTF-8（65001），不回退 GBK/936
    const fallback = isLinux ? 65001 : 936
    const acp = Number((agent as any)?.a_acp || (agent as any)?.ACP || fallback) || fallback
    const oemcp = Number((agent as any)?.a_oemcp || (agent as any)?.OemCP || acp) || acp
    return { acp, oemcp }
  }

  function renderPreviewText(raw: string, encoding: TextEncodingId, message?: string) {
    const { acp, oemcp } = agentCodePages()
    const body = encoding === 'auto'
      ? (decodeWindowsText(raw || '', { acp, oemcp }) || '(空内容 / 仅返回消息)')
      : (decodeWithEncoding(raw || '', encoding) || '(空内容 / 仅返回消息)')
    return message ? `${message}\n\n${body}` : body
  }

  function setPreviewEncoding(encoding: TextEncodingId) {
    setPreview((prev) => {
      if (!prev || prev.busy) return prev
      return {
        ...prev,
        encoding,
        text: renderPreviewText(prev.raw, encoding, prev.message),
      }
    })
  }

  async function doCat(it: FileItem) {
    if (it.isDir) {
      pushToast('warn', '目录无法直接查看内容，请双击进入')
      return
    }
    const p = itemPath(it)
    const { acp, oemcp } = agentCodePages()
    setPreview({
      path: p,
      title: it.name,
      raw: '',
      text: '',
      encoding: 'auto',
      busy: true,
    })
    try {
      const res = await browserCat(api, agentId, p, ({ sec, pending }) => {
        setPreview((prev) => prev ? {
          ...prev,
          text: pending ? `已下发 cat，等待回显 ${sec}s…` : `轮询任务 ${sec}s…`,
          busy: true,
        } : prev)
      })
      // Prefer server-decoded text as raw payload for re-encoding attempts
      const raw = res.text || ''
      const detected = detectEncodingId(raw, { acp, oemcp })
      const title = decodeWindowsText(it.name, { acp, oemcp }) || it.name
      const encoding: TextEncodingId = 'auto'
      setPreview({
        path: p,
        title,
        raw,
        message: res.message || undefined,
        encoding,
        detected,
        text: renderPreviewText(raw, encoding, res.message || undefined),
        busy: false,
      })
      pushToast('ok', `已读取 ${title}`)
    } catch (e: any) {
      const msg = e?.message || String(e)
      setPreview({
        path: p,
        title: it.name,
        raw: msg,
        text: msg,
        encoding: 'auto',
        busy: false,
      })
      pushToast('err', msg)
    }
  }

  async function doRm(it: FileItem) {
    const p = itemPath(it)
    if (!confirm(`确认删除？\n${p}`)) return
    try {
      await browserRm(api, agentId, p)
      pushToast('warn', `已请求删除 ${it.name}`)
      setTimeout(() => (mode === 'disks' ? loadDisks({ fromHistory: true }) : load(path || '.', { fromHistory: true })), 1200)
    } catch (e: any) { pushToast('err', e.message || String(e)) }
  }

  async function doRmMulti(items: FileItem[]) {
    if (!items.length || mode === 'disks') return
    if (!confirm(`确认删除选中的 ${items.length} 项？`)) return
    try {
      for (const it of items) {
        const p = itemPath(it)
        await browserRm(api, agentId, p)
      }
      pushToast('warn', `已请求删除 ${items.length} 项`)
      setSelectedSet(new Set())
      setTimeout(() => load(path || '.', { fromHistory: true }), 1200)
    } catch (e: any) { pushToast('err', e.message || String(e)) }
  }

  async function doDownloadMulti(items: FileItem[]) {
    const files = items.filter((x) => !x.isDir)
    if (!files.length) {
      pushToast('warn', '请选择文件（目录不支持批量下载）')
      return
    }
    try {
      for (const it of files) {
        const p = itemPath(it)
        await browserDownloadFile(api, agentId, p)
      }
      pushToast('ok', `已下发 ${files.length} 个下载任务`)
    } catch (e: any) { pushToast('err', e.message || String(e)) }
  }

  async function doUpload(files: FileList | null) {
    if (!files?.length) return
    if (mode === 'disks') {
      pushToast('warn', '请先进入目录再上传')
      return
    }
    const base = path && path !== '.' ? path : '.'
    setUploading(true)
    setProgress('')
    try {
      for (const f of Array.from(files)) {
        const remote = base === '.' ? f.name : joinPath(base, f.name)
        await browserUploadFile(api, agentId, f, normalizePath(remote), (pct) => {
          setProgress(`上传 ${f.name} · ${pct}%`)
        })
        pushLog('ok', `上传任务已下发: ${f.name} → ${remote}`, false)
      }
      pushToast('ok', `已下发 ${files.length} 个上传任务`)
      setTimeout(() => load(path || '.', { fromHistory: true }), 1500)
    } catch (e: any) {
      pushToast('err', e?.message || String(e))
      pushLog('err', `上传失败: ${e?.message || e}`)
    } finally {
      setUploading(false)
      setProgress('')
      if (fileInputRef.current) fileInputRef.current.value = ''
    }
  }

  function onRowClick(it: FileItem, e: React.MouseEvent) {
    const names = view.map((x) => x.name)
    if (e.shiftKey && lastClicked) {
      const a = names.indexOf(lastClicked)
      const b = names.indexOf(it.name)
      if (a >= 0 && b >= 0) {
        const [lo, hi] = a < b ? [a, b] : [b, a]
        const range = names.slice(lo, hi + 1)
        setSelectedSet(new Set(range))
        setSelected(it.name)
        return
      }
    }
    if (e.ctrlKey || e.metaKey) {
      setSelectedSet((prev) => {
        const n = new Set(prev)
        if (n.has(it.name)) n.delete(it.name)
        else n.add(it.name)
        return n
      })
      setSelected(it.name)
      setLastClicked(it.name)
      return
    }
    setSelected(it.name)
    setSelectedSet(new Set([it.name]))
    setLastClicked(it.name)
  }

  async function copyPath(it?: FileItem | null) {
    const p = !it
      ? (mode === 'disks' ? '' : path)
      : itemPath(it)
    const ok = await copyText(p || '')
    pushToast(ok ? 'ok' : 'err', ok ? `已复制路径` : '复制失败')
  }

  /** Linux 上一级：/a/b → /a、/a → /（parentPath 对单段 Unix 路径会返回自身） */
  function parentOf(p: string) {
    if (!isLinux) return parentPath(p || '.')
    const s = String(p || '').replace(/\/+$/, '')
    if (!s || s === '/' || s === '.') return p || '.'
    const i = s.lastIndexOf('/')
    return i <= 0 ? '/' : s.slice(0, i)
  }

  function goBack() {
    if (histIdx <= 0) return
    const i = histIdx - 1
    setHistIdx(i)
    const p = history[i]
    if (p === '') loadDisks({ fromHistory: true })
    else load(p, { fromHistory: true })
  }
  function goForward() {
    if (histIdx >= history.length - 1) return
    const i = histIdx + 1
    setHistIdx(i)
    const p = history[i]
    if (p === '') loadDisks({ fromHistory: true })
    else load(p, { fromHistory: true })
  }

  function renderTree(nodePath: string, depth = 0, visited?: Set<string>): any {
    // hard stop against cyclic tree data
    if (depth > 24) return null
    const seen = visited || new Set<string>()
    if (seen.has(nodePath)) return null
    seen.add(nodePath)

    const node = tree[nodePath]
    if (!node) return null
    const kids = (node.children || []).filter((c) => c !== nodePath && !seen.has(c))
    const active = (mode === 'disks' && nodePath === '') || (mode === 'list' && path === nodePath)
    return (
      <div key={nodePath || 'root'}>
        <div
          className={`fm-tree-item ${active ? 'active' : ''}`}
          style={{ paddingLeft: 8 + depth * 14 }}
          onClick={() => {
            if (nodePath === '') loadDisks()
            else load(nodePath)
          }}
        >
          <button
            className="fm-tree-caret"
            onClick={(e) => {
              e.stopPropagation()
              setTree((t) => ({
                ...t,
                [nodePath]: { ...t[nodePath], open: !t[nodePath]?.open },
              }))
              if (!node.loaded && nodePath) load(nodePath)
            }}
          >
            {kids.length ? (node.open ? '▾' : '▸') : '·'}
          </button>
          <span className="fm-tree-ico">{nodePath === '' ? '💻' : ((!isLinux && /^[a-zA-Z]:\\?$/.test(nodePath)) ? '💾' : '📁')}</span>
          <span className="fm-tree-name" title={nodePath || rootLabel}>{nodePath === '' ? rootLabel : node.name}</span>
        </div>
        {node.open && kids.map((c) => renderTree(c, depth + 1, seen))}
      </div>
    )
  }

  const hostTitle = agent?.a_computer || agentId
  // Linux 根目录 / 挂载点根无上一级（Windows 为盘符根）
  const canUp = mode !== 'disks' && path && path !== '.' && (isLinux ? path !== '/' : !/^[a-zA-Z]:\\?$/.test(path))

  return (
    <div className="fm">
      {/* Title bar */}
      <div className="fm-titlebar">
        <div className="fm-title">
          <span className="fm-appico">📁</span>
          <div>
            <div className="fm-appname">文件管理</div>
            <div className="fm-appsub mono">{hostTitle}{agent?.a_internal_ip ? ` · ${agent.a_internal_ip}` : ''} · {agentId}</div>
          </div>
        </div>
        <div className="fm-title-actions">
          {isLinux ? (
            <span className="fm-boost" title="Linux beacon：编译期固定节拍，无 sleep 0 交互加速">Linux</span>
          ) : (
            <span className={`fm-boost ${boosted || agent?.a_sleep === 0 ? 'on' : ''}`} title="打开本页会自动 sleep 0；关闭页签后恢复">
              {boosted || agent?.a_sleep === 0 ? '交互加速 · sleep 0' : `Sleep ${agent?.a_sleep ?? '-'}s`}
            </span>
          )}
          <input
            ref={fileInputRef}
            type="file"
            multiple
            style={{ display: 'none' }}
            onChange={(e) => doUpload(e.target.files)}
          />
          <button
            className="primary"
            disabled={busy || uploading || mode === 'disks'}
            title={mode === 'disks' ? '请先进入目录' : '上传到当前目录'}
            onClick={() => fileInputRef.current?.click()}
          >
            {uploading ? '上传中…' : '上传'}
          </button>
          <button className="ghost" disabled={busy || uploading} onClick={() => (mode === 'disks' ? loadDisks({ fromHistory: true }) : load(path || '.', { fromHistory: true }))}>
            {busy ? '刷新中…' : '刷新'}
          </button>
          <button className="ghost" onClick={() => closeUiTab(`files:${agentId}` as any)}>关闭</button>
          <PageTips id="files">
            <p>双击目录进入；面包屑/上级按钮导航；右键下载/删除/复制路径。</p>
            <p><b>Linux 主机显示挂载点</b>（无盘符）；预览自动识别编码（UTF-8 优先）。</p>
            <p>多选支持 Ctrl/Shift，可批量下载/删除；「上传」投递本地文件到当前目录。</p>
          </PageTips>
        </div>
      </div>

      {/* Nav toolbar */}
      <div className="fm-nav">
        <button className="ico" disabled={histIdx <= 0 || busy} onClick={goBack} title="后退">←</button>
        <button className="ico" disabled={histIdx >= history.length - 1 || busy} onClick={goForward} title="前进">→</button>
        <button className="ico" disabled={!canUp || busy} onClick={() => load(parentOf(path || '.'))} title="上级">↑</button>
        <button className="ico" disabled={busy} onClick={() => (mode === 'disks' ? loadDisks({ fromHistory: true }) : load(path || '.', { fromHistory: true }))} title="刷新">↻</button>
        <div className="sep" />
        <button disabled={busy} onClick={() => loadDisks()}>{isLinux ? '挂载点' : '此电脑'}</button>
        {!isLinux && <button disabled={busy} onClick={() => load('C:\\')}>C:\</button>}
        <button disabled={busy} onClick={() => load('.')}>当前目录</button>
        <div className="sep" />
        <div className="fm-crumbs">
          {crumbs.map((c, i) => (
            <span key={c.path + i} className="fm-crumb">
              {i > 0 && <span className="fm-crumb-sep">›</span>}
              <button
                className="ghost fm-crumb-btn"
                disabled={busy}
                onClick={() => (c.path === '' ? loadDisks() : load(c.path))}
                title={c.path || (isLinux ? '挂载点' : '此电脑')}
              >
                {c.label}
              </button>
            </span>
          ))}
        </div>
        {/*
          ★ 这里**不能**加 disabled={busy}：disabled 的元素浏览器会立刻收回焦点，
            而 load() 要等主机回显（可能好几秒），表现就是"敲路径回车跳转一次之后，
            必须再用鼠标点一下输入框才能敲下一个路径"。
            连按回车不会乱：load() 自己有 `const my = ++seq.current` 的序号防重入，
            后发的赢、先发的作废（见 load 里的 `if (my !== seq.current) return`）。
        */}
        <input
          className="fm-path"
          value={pathInput}
          onChange={(e) => setPathInput(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              const v = pathInput.trim()
              if (!v) loadDisks()
              else load(v)
            }
          }}
          placeholder="输入路径后回车"
        />
        <button className="primary" disabled={busy} onClick={() => {
          const v = pathInput.trim()
          if (!v) loadDisks()
          else load(v)
        }}>转到</button>
        <input
          className="fm-search"
          type="search"
          placeholder="过滤当前列表"
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
      </div>

      {/* Body: tree + table + detail */}
      <div className="fm-body">
        <aside className="fm-tree">
          <div className="fm-tree-hd">导航</div>
          <div className="fm-tree-bd">
            {renderTree('')}
          </div>
          <div className="fm-tree-ft dim">双击目录进入 · 右键更多操作</div>
        </aside>

        <section className="fm-main">
          <div className="fm-table-wrap" ref={tableRef}>
            {busy && (
              <div className="fm-loading">
                <div className="fm-spinner" />
                <div>正在读取主机文件…</div>
                <div className="dim">{progress || '下发命令并轮询任务结果'}</div>
                <div className="dim">超时约 20s；若 sleep 较长请稍等或把 sleep 调小</div>
              </div>
            )}
            {!busy && err && (
              <div className="fm-empty">
                <div className="big">!</div>
                <div className="err">{err}</div>
                <button className="primary" onClick={() => (mode === 'disks' ? loadDisks() : load(path || '.'))}>重试</button>
              </div>
            )}
            {!busy && !err && view.length === 0 && (
              <div className="fm-empty">
                <div className="big">📁</div>
                <div>空目录</div>
                <div className="dim">{mode === 'disks' ? (isLinux ? '未发现挂载点' : '未发现磁盘') : (path || '.')}</div>
              </div>
            )}
            {!busy && !err && view.length > 0 && (
              <table className="fm-table">
                <thead>
                  <tr>
                    <th style={{ width: 34 }}>
                      <input
                        type="checkbox"
                        checked={view.length > 0 && view.every((x) => selectedSet.has(x.name))}
                        onChange={(e) => {
                          if (e.target.checked) setSelectedSet(new Set(view.map((x) => x.name)))
                          else setSelectedSet(new Set())
                        }}
                        title="全选当前列表"
                      />
                    </th>
                    <th style={{ width: 44 }}></th>
                    <th className="sortable" onClick={() => toggleSort('name')}>
                      名称 {sortKey === 'name' ? (sortAsc ? '↑' : '↓') : ''}
                    </th>
                    <th style={{ width: 110 }} className="sortable" onClick={() => toggleSort('size')}>
                      大小 {sortKey === 'size' ? (sortAsc ? '↑' : '↓') : ''}
                    </th>
                    <th style={{ width: 150 }} className="sortable" onClick={() => toggleSort('date')}>
                      修改时间 {sortKey === 'date' ? (sortAsc ? '↑' : '↓') : ''}
                    </th>
                    <th style={{ width: 90 }}>类型</th>
                  </tr>
                </thead>
                <tbody>
                  {view.map((it) => {
                    const active = selectedSet.has(it.name) || selected === it.name
                    const disk = mode === 'disks'
                    return (
                      <tr
                        key={it.name}
                        className={active ? 'selected' : ''}
                        onClick={(e) => onRowClick(it, e)}
                        onDoubleClick={() => openItem(it)}
                        onContextMenu={(e) => {
                          e.preventDefault()
                          if (!selectedSet.has(it.name)) {
                            setSelected(it.name)
                            setSelectedSet(new Set([it.name]))
                          }
                          setCtx({ x: e.clientX, y: e.clientY, item: it })
                        }}
                      >
                        <td onClick={(e) => e.stopPropagation()}>
                          <input
                            type="checkbox"
                            checked={selectedSet.has(it.name)}
                            onChange={(e) => {
                              setSelectedSet((prev) => {
                                const n = new Set(prev)
                                if (e.target.checked) n.add(it.name)
                                else n.delete(it.name)
                                return n
                              })
                              setSelected(it.name)
                              setLastClicked(it.name)
                            }}
                          />
                        </td>
                        <td className="fm-ico">{fileIcon(it, disk, isLinux)}</td>
                        <td>
                          <div className="fm-name">{it.name}</div>
                        </td>
                        <td className="mono dim">{displaySize(it)}</td>
                        <td className="mono dim">{it.dateText || '—'}</td>
                        <td className="dim">{disk ? diskLabel : it.isDir ? '文件夹' : (extOf(it.name).toUpperCase() || '文件')}</td>
                      </tr>
                    )
                  })}
                </tbody>
              </table>
            )}
          </div>

          <div className="fm-statusbar">
            <span>{view.length} 个对象{q ? `（已过滤）` : ''}</span>
            <span className="dim mono">{mode === 'disks' ? (isLinux ? '挂载点' : '此电脑') : (path || '.')}</span>
            <span className="dim">{message || progress}</span>
            {selectedSet.size > 1 ? (
              <span className="mono">已选 {selectedSet.size} 项</span>
            ) : selectedItem ? (
              <span className="mono">选中：{selectedItem.name}</span>
            ) : null}
            {selectedSet.size > 1 && mode !== 'disks' && (
              <span className="fm-multi-actions">
                <button className="ghost" disabled={busy || uploading} onClick={() => doDownloadMulti(multiItems)}>批量下载</button>
                <button className="danger" disabled={busy || uploading} onClick={() => doRmMulti(multiItems)}>批量删除</button>
              </span>
            )}
          </div>
        </section>

        <aside className="fm-detail">
          <div className="fm-detail-hd">详细信息</div>
          {!selectedItem ? (
            <div className="fm-detail-empty dim">选择文件或文件夹查看详情</div>
          ) : (
            <div className="fm-detail-bd">
              <div className="fm-detail-ico">{fileIcon(selectedItem, mode === 'disks', isLinux)}</div>
              <div className="fm-detail-name" title={selectedItem.name}>{selectedItem.name}</div>
              <div className="fm-kv">
                <div><span>类型</span><b>{mode === 'disks' ? diskLabel : selectedItem.isDir ? '文件夹' : (extOf(selectedItem.name) || '文件')}</b></div>
                <div><span>大小</span><b>{displaySize(selectedItem)}</b></div>
                <div><span>修改</span><b>{selectedItem.dateText || '—'}</b></div>
                <div><span>路径</span><b className="mono">{itemPath(selectedItem)}</b></div>
              </div>
              <div className="fm-detail-actions">
                {(selectedItem.isDir || mode === 'disks') ? (
                  <button className="primary" disabled={busy} onClick={() => openItem(selectedItem)}>打开</button>
                ) : (
                  <>
                    <button className="primary" disabled={busy} onClick={() => doDownload(selectedItem)}>下载</button>
                    <button disabled={busy} onClick={() => doCat(selectedItem)}>查看</button>
                  </>
                )}
                <button disabled={busy} onClick={() => copyPath(selectedItem)}>复制路径</button>
                {mode !== 'disks' && (
                  <button className="danger" disabled={busy} onClick={() => doRm(selectedItem)}>删除</button>
                )}
              </div>
            </div>
          )}
        </aside>
      </div>

      {preview && (
        <div className="mask" onMouseDown={(e) => { if (e.target === e.currentTarget && !preview.busy) setPreview(null) }}>
          <div className="dlg fm-preview-dlg" onMouseDown={(e) => e.stopPropagation()}>
            <div className="dlg-hd">
              <div>
                <h2>查看 · {preview.title}</h2>
                <div className="dim mono" style={{ fontSize: 12 }}>{preview.path}</div>
              </div>
              <div className="fm-preview-tools">
                <label className="fm-enc-label">
                  <span>编码</span>
                  <select
                    className="fm-enc-select"
                    disabled={!!preview.busy}
                    value={preview.encoding}
                    onChange={(e) => setPreviewEncoding(e.target.value as TextEncodingId)}
                    title="切换文本编码以修复乱码"
                  >
                    {TEXT_ENCODING_OPTIONS.map((opt) => (
                      <option key={opt.id} value={opt.id}>
                        {opt.id === 'auto' && preview.detected
                          ? `自动（检测: ${TEXT_ENCODING_OPTIONS.find((x) => x.id === preview.detected)?.label || preview.detected}）`
                          : opt.label}
                      </option>
                    ))}
                  </select>
                </label>
                <button className="ghost" disabled={!!preview.busy} onClick={() => setPreview(null)}>关闭</button>
              </div>
            </div>
            <div className="fm-preview-body mono">
              {preview.busy && !preview.raw ? '读取中…' : (preview.text || '(无内容)')}
            </div>
            <div className="dlg-foot dim">
              完整读取文件内容（上限约 16MB，更大请用下载）；乱码时切换上方编码。
              <button
                className="ghost"
                style={{ marginLeft: 8 }}
                disabled={!!preview.busy || !preview.text}
                onClick={async () => {
                  // copy decoded body only (strip message header if present)
                  const full = preview.text || ''
                  const body = preview.message && full.startsWith(preview.message)
                    ? full.slice(preview.message.length).replace(/^\n+/, '')
                    : full
                  const ok = await copyText(body)
                  pushToast(ok ? 'ok' : 'err', ok ? '已复制内容' : '复制失败')
                }}
              >
                复制内容
              </button>
            </div>
          </div>
        </div>
      )}

      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.min(ctx.x, window.innerWidth - 200), top: Math.min(ctx.y, window.innerHeight - 300) }}
          onClick={(e) => e.stopPropagation()}
        >
          {selectedSet.size > 1 ? (
            <>
              <div className="dim" style={{ padding: '4px 10px', fontSize: 11 }}>已选 {selectedSet.size} 项</div>
              <button onClick={() => { doDownloadMulti(multiItems); setCtx(null) }}>批量下载文件</button>
              {mode !== 'disks' && (
                <button className="danger" onClick={() => { doRmMulti(multiItems); setCtx(null) }}>批量删除</button>
              )}
            </>
          ) : (
            <>
              {(ctx.item.isDir || mode === 'disks') && (
                <button onClick={() => { openItem(ctx.item); setCtx(null) }}>打开</button>
              )}
              {!ctx.item.isDir && mode !== 'disks' && (
                <>
                  <button onClick={() => { doDownload(ctx.item); setCtx(null) }}>下载</button>
                  <button onClick={() => { doCat(ctx.item); setCtx(null) }}>查看内容</button>
                </>
              )}
              <button onClick={() => { copyPath(ctx.item); setCtx(null) }}>复制路径</button>
              <button onClick={async () => {
                const ok = await copyText(ctx.item.name)
                pushToast(ok ? 'ok' : 'err', ok ? '已复制名称' : '复制失败')
                setCtx(null)
              }}>复制名称</button>
              {mode !== 'disks' && (
                <>
                  <div className="sep" />
                  <button disabled={uploading} onClick={() => { fileInputRef.current?.click(); setCtx(null) }}>上传到此目录</button>
                  <button className="danger" onClick={() => { doRm(ctx.item); setCtx(null) }}>删除</button>
                </>
              )}
            </>
          )}
        </div>
      )}
    </div>
  )
}

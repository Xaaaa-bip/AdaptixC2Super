import { memo, useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
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
import { qtAsset } from '../design/qt-themes'

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

function fileIconSrc(it: FileItem, isDisk = false, linux = false): string {
  if (isDisk || (!linux && /^[a-zA-Z]:\\?$/.test(it.name))) return qtAsset('icons/filesystem/ssd_64dp.png')
  if (it.isDir) return qtAsset('icons/filesystem/folder_64dp.png')
  return qtAsset('icons/filesystem/document_64dp.png')
}

function fmtBytes(n: number): string {
  if (!Number.isFinite(n) || n < 0) return '—'
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`
  if (n < 1024 * 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(2)} MB`
  return `${(n / (1024 * 1024 * 1024)).toFixed(2)} GB`
}

function displaySize(it: FileItem) {
  if (it.isDir) return ''
  if (it.size && it.size > 0) return fmtBytes(it.size)
  if (it.sizeText && /^\d+$/.test(it.sizeText.trim())) return fmtBytes(Number(it.sizeText.trim()))
  return it.sizeText || '0 B'
}

export const FileManager = memo(function FileManager({ agentId }: { agentId: string }) {
  const api = useStore((s) => s.api)
  const agent = useStore((s) => s.agents[agentId])
  const closeUiTab = useStore((s) => s.closeUiTab)
  const pushToast = useStore((s) => s.pushToast)
  const pushLog = useStore((s) => s.pushLog)
  const openConsole = useStore((s) => s.openConsole)
  const boosted = useStore((s) => !!s.interactBoost[agentId]?.ref)
  const acquireInteractBoost = useStore((s) => s.acquireInteractBoost)
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
  const [treePct, setTreePct] = useState(24)
  const [draggingSplit, setDraggingSplit] = useState(false)
  const [dragOver, setDragOver] = useState(false)
  const seq = useRef(0)
  const wrapRef = useRef<HTMLDivElement>(null)
  const tableRef = useRef<HTMLDivElement>(null)
  const fileInputRef = useRef<HTMLInputElement>(null)
  const filterRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    if (!draggingSplit) return
    const onMove = (e: MouseEvent) => {
      const box = wrapRef.current?.getBoundingClientRect()
      if (!box || box.width <= 0) return
      const pct = Math.max(15, Math.min(48, ((e.clientX - box.left) / box.width) * 100))
      setTreePct(pct)
    }
    const onUp = () => setDraggingSplit(false)
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [draggingSplit])

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
      else if (sortKey === 'size') cmp = (a.size || 0) - (b.size || 0)
      else if (sortKey === 'date') cmp = (a.date || 0) - (b.date || 0) || (a.dateText || '').localeCompare(b.dateText || '')
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
    pushToast(ok ? 'ok' : 'err', ok ? `已复制路径` : t('复制失败'))
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
    const treeIcon = nodePath === '' || (!isLinux && /^[a-zA-Z]:\\?$/.test(nodePath))
      ? qtAsset('icons/filesystem/ssd_64dp.png')
      : (node.open || node.loaded ? qtAsset('icons/filesystem/open_folder_64dp.png') : qtAsset('icons/filesystem/folder_64dp.png'))

    return (
      <div key={nodePath || 'root'}>
        <div
          className={`qt-tree-row ${active ? 'active' : ''}`}
          style={{ paddingLeft: 6 + depth * 14 }}
          onClick={() => {
            if (nodePath === '') loadDisks()
            else load(nodePath)
          }}
        >
          <button
            type="button"
            className="qt-tree-caret"
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
          <img className="qt-ico qt-tree-ico" src={treeIcon} alt="" />
          <span className="qt-tree-lbl" title={nodePath || rootLabel}>{nodePath === '' ? rootLabel : node.name}</span>
        </div>
        {node.open && kids.map((c) => renderTree(c, depth + 1, seen))}
      </div>
    )
  }

  const hostTitle = agent?.a_computer || agentId
  const canUp = mode !== 'disks' && path && path !== '.' && (isLinux ? path !== '/' : !/^[a-zA-Z]:\\?$/.test(path))
  const dirCount = view.filter((x) => x.isDir).length
  const fileCount = view.length - dirCount
  const sortArrow = (k: SortKey) => (sortKey === k ? (sortAsc ? ' ▲' : ' ▼') : '')

  function onWrapKey(e: React.KeyboardEvent) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'f') {
      e.preventDefault()
      filterRef.current?.focus()
      filterRef.current?.select()
    } else if (e.key === 'Backspace' && canUp) {
      const tag = (e.target as HTMLElement | null)?.tagName
      if (tag !== 'INPUT' && tag !== 'TEXTAREA') {
        e.preventDefault()
        load(parentOf(path || '.'))
      }
    }
  }

  return (
    <div
      className="qt-browser"
      ref={wrapRef}
      tabIndex={0}
      onKeyDown={onWrapKey}
    >
      <input
        ref={fileInputRef}
        type="file"
        multiple
        style={{ display: 'none' }}
        onChange={(e) => doUpload(e.target.files)}
      />

      {/* Left Pane: Directory Tree (QTreeWidget) */}
      <aside className="qt-browser-tree" style={{ width: `${treePct}%` }}>
        <div className="qt-tree-hd">
          <span>Directory Tree</span>
          <span className="qt-tree-hd-actions">
            <button type="button" className="qt-tree-hd-btn" title={diskLabel} onClick={() => loadDisks()}>
              {isLinux ? '/' : 'Disks'}
            </button>
            <button type="button" className="qt-tree-hd-btn" title={t('当前目录')} onClick={() => load('.')}>
              .
            </button>
          </span>
        </div>
        <div className="qt-tree-bd">
          {renderTree('')}
        </div>
      </aside>

      {/* Draggable Horizontal QSplitter */}
      <div
        className={`qt-vsplit ${draggingSplit ? 'dragging' : ''}`}
        role="separator"
        aria-orientation="vertical"
        title={t('拖拽调整目录树宽度，双击还原')}
        onMouseDown={() => setDraggingSplit(true)}
        onDoubleClick={() => setTreePct(24)}
      />

      {/* Right Pane: Qt Toolbar + Breadcrumbs + QTableView + Compact Footer */}
      <section
        className="qt-browser-main"
        onDragOver={(e) => {
          if (mode === 'disks') return
          e.preventDefault()
          if (!dragOver) setDragOver(true)
        }}
        onDragLeave={(e) => {
          if (e.currentTarget === e.target) setDragOver(false)
        }}
        onDrop={(e) => {
          e.preventDefault()
          setDragOver(false)
          if (mode !== 'disks' && e.dataTransfer?.files?.length) {
            void doUpload(e.dataTransfer.files)
          }
        }}
      >
        {dragOver && (
          <div className="qt-drop-overlay">
            <img className="qt-ico qt-ico-mono" src={qtAsset('icons/upload_file_64dp.png')} alt="" style={{ width: 32, height: 32 }} />
            <div>{t('释放文件以上传到')} {path || '.'}</div>
          </div>
        )}

        {/* Primary Qt Browser Toolbar */}
        <div className="qt-browser-bar">
          <button type="button" className="qt-b-ico-btn" disabled={histIdx <= 0 || busy} onClick={goBack} title={t('后退')}>
            ←
          </button>
          <button type="button" className="qt-b-ico-btn" disabled={histIdx >= history.length - 1 || busy} onClick={goForward} title={t('前进')}>
            →
          </button>
          <button
            type="button"
            className="qt-b-ico-btn"
            disabled={!canUp || busy}
            onClick={() => load(parentOf(path || '.'))}
            title="Up folder (Backspace)"
          >
            <img className="qt-ico qt-ico-mono" src={qtAsset('icons/arrow_drop_up_64dp.png')} alt="" />
          </button>
          <button
            type="button"
            className="qt-b-ico-btn"
            disabled={busy}
            onClick={() => (mode === 'disks' ? loadDisks({ fromHistory: true }) : load(path || '.', { fromHistory: true }))}
            title="Reload"
          >
            <img className="qt-ico qt-ico-mono" src={qtAsset('icons/autorenew_64dp.png')} alt="" />
          </button>

          {/* Editable Path Bar (never disabled while loading so Enter focus never drops) */}
          <div className="qt-b-path-wrap">
            <input
              className="qt-b-path"
              value={pathInput}
              onChange={(e) => setPathInput(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  const v = pathInput.trim()
                  if (!v) loadDisks()
                  else load(v)
                }
              }}
              placeholder={isLinux ? '/etc' : 'C:\\Users'}
              spellCheck={false}
            />
            <button
              type="button"
              className="qt-b-ico-btn"
              disabled={busy}
              title="Go"
              onClick={() => {
                const v = pathInput.trim()
                if (!v) loadDisks()
                else load(v)
              }}
            >
              <img className="qt-ico qt-ico-mono" src={qtAsset('icons/arrow_right_alt_64dp.png')} alt="" />
            </button>
          </div>

          <span className="qt-tb-vline" />

          <button
            type="button"
            className="qt-b-btn primary"
            disabled={busy || uploading || mode === 'disks'}
            title={mode === 'disks' ? t('请先进入目录') : t('上传本地文件到当前目录（支持拖拽）')}
            onClick={() => fileInputRef.current?.click()}
          >
            <img className="qt-ico" src={qtAsset('icons/upload_file_64dp.png')} alt="" style={{ filter: 'brightness(10)' }} />
            <span>{uploading ? t('上传中…') : t('上传')}</span>
          </button>
          <button
            type="button"
            className="qt-b-btn"
            disabled={busy}
            title="Disks list"
            onClick={() => loadDisks()}
          >
            <img className="qt-ico qt-ico-mono" src={qtAsset('icons/storage_64dp.png')} alt="" />
            <span>{diskLabel}</span>
          </button>

          <input
            ref={filterRef}
            className="qt-b-filter"
            type="search"
            placeholder={t('过滤当前目录 (Ctrl+F)')}
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />

          <span className="qt-tb-vline" />

          <span className={`qt-b-status ${err ? 'err' : busy || uploading ? 'busy' : 'ok'}`}>
            {err ? err : busy ? (progress || t('读取中…')) : uploading ? t('上传中…') : (message || 'Status: OK')}
          </span>

          <PageTips id="files">
            <p>{t('左侧为目录树，右侧为文件表；双击目录进入、双击文件预览、支持拖拽本地文件直接上传。')}</p>
            <p>{t('Ctrl/Shift 多选可批量下载或删除；Backspace 返回上级目录；预览支持 UTF-8 / GBK 编码切换。')}</p>
          </PageTips>
        </div>

        {/* Ultra-compact Breadcrumb + Quick Jump + Sleep Boost strip */}
        <div className="qt-b-crumbs">
          {crumbs.map((c, i) => (
            <span key={c.path + i} style={{ display: 'inline-flex', alignItems: 'center', gap: 2 }}>
              {i > 0 && <span className="qt-b-crumb-sep">›</span>}
              <button
                type="button"
                className={`qt-b-crumb-btn ${i === crumbs.length - 1 ? 'last' : ''}`}
                disabled={busy}
                onClick={() => (c.path === '' ? loadDisks() : load(c.path))}
                title={c.path || rootLabel}
              >
                {c.label}
              </button>
            </span>
          ))}
          {!isLinux && (
            <button type="button" className="qt-b-crumb-btn" style={{ marginLeft: 8 }} onClick={() => load('C:\\')}>
              [C:\]
            </button>
          )}
          <button type="button" className="qt-b-crumb-btn" onClick={() => openConsole(agentId)}>
            [Console]
          </button>
          <span className={`qt-b-boost ${!isLinux && (boosted || agent?.a_sleep === 0) ? 'on' : ''}`}>
            {isLinux ? `${hostTitle} · Linux` : `${hostTitle} · ${boosted || agent?.a_sleep === 0 ? 'sleep 0' : `${agent?.a_sleep ?? '-'}s`}`}
          </span>
        </div>

        {/* Main QTableView */}
        <div className="qt-table-wrap" ref={tableRef}>
          {busy && view.length === 0 ? (
            <div className="qt-table-empty">
              <div>{t('正在读取主机文件…')}</div>
              <div style={{ fontSize: 11 }}>{progress || t('下发命令并等待主机回显')}</div>
            </div>
          ) : !busy && err ? (
            <div className="qt-table-empty">
              <div style={{ color: 'var(--qt-err)' }}>{err}</div>
              <button type="button" className="primary" onClick={() => (mode === 'disks' ? loadDisks() : load(path || '.'))}>
                {t('重试')}
              </button>
            </div>
          ) : !busy && view.length === 0 ? (
            <div className="qt-table-empty">
              <img className="qt-ico qt-ico-mono" src={qtAsset('icons/filesystem/folder_64dp.png')} alt="" style={{ width: 32, height: 32, opacity: 0.5 }} />
              <div>{t('空目录')} ({mode === 'disks' ? rootLabel : (path || '.')})</div>
            </div>
          ) : (
            <table className="qt-table">
              <thead>
                <tr>
                  <th className="align-l" onClick={() => toggleSort('name')}>Name{sortArrow('name')}</th>
                  {isLinux && mode !== 'disks' && (
                    <>
                      <th style={{ width: 100 }}>Mode</th>
                      <th style={{ width: 88 }}>User</th>
                      <th style={{ width: 88 }}>Group</th>
                    </>
                  )}
                  <th style={{ width: 108 }} onClick={() => toggleSort('size')}>Size{sortArrow('size')}</th>
                  <th style={{ width: 165 }} onClick={() => toggleSort('date')}>Last Modified{sortArrow('date')}</th>
                  {!isLinux && <th style={{ width: 84 }}>{mode === 'disks' ? 'Disk Type' : 'Type'}</th>}
                </tr>
              </thead>
              <tbody>
                {view.map((it) => {
                  const active = selectedSet.has(it.name) || selected === it.name
                  const disk = mode === 'disks'
                  const ext = !it.isDir && !disk ? extOf(it.name).toUpperCase() : ''
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
                      <td className="align-l" title={it.name}>
                        <span className="qt-file-cell">
                          <img className="qt-ico qt-file-ico" src={fileIconSrc(it, disk, isLinux)} alt="" />
                          <span className={it.isDir || disk ? 'qt-file-dir' : ''}>{it.name}</span>
                        </span>
                      </td>
                      {isLinux && mode !== 'disks' && (
                        <>
                          <td className="mono" style={{ color: 'var(--qt-text-muted)' }}>{it.mode || ''}</td>
                          <td>{it.user || ''}</td>
                          <td>{it.group || ''}</td>
                        </>
                      )}
                      <td className="mono">{displaySize(it)}</td>
                      <td className="mono" style={{ color: 'var(--qt-text-muted)' }}>{it.dateText || ''}</td>
                      {!isLinux && (
                        <td>
                          {disk
                            ? (it.diskType || diskLabel)
                            : it.isDir
                              ? <span style={{ color: 'var(--qt-text-muted)' }}>DIR</span>
                              : (ext ? <span className="qt-file-ext">{ext}</span> : '')}
                        </td>
                      )}
                    </tr>
                  )
                })}
              </tbody>
            </table>
          )}
        </div>

        {/* Compact Qt Footer Status + Inline Selection Actions */}
        <footer className="qt-browser-foot">
          <span className="mono">
            {mode === 'disks' ? `${view.length} ${diskLabel}` : `${view.length} items (${dirCount} dirs, ${fileCount} files)`}
          </span>
          <span className="qt-south-sep" />
          <span className="qt-browser-foot-path" title={selectedItem ? itemPath(selectedItem) : path}>
            {selectedSet.size > 1
              ? `${selectedSet.size} selected`
              : selectedItem
                ? itemPath(selectedItem)
                : (mode === 'disks' ? rootLabel : (path || '.'))}
          </span>

          {selectedSet.size > 1 && mode !== 'disks' ? (
            <span className="qt-browser-foot-actions">
              <button type="button" className="primary" disabled={busy || uploading} onClick={() => doDownloadMulti(multiItems)}>
                {t('批量下载')} ({selectedSet.size})
              </button>
              <button type="button" className="danger" disabled={busy || uploading} onClick={() => doRmMulti(multiItems)}>
                {t('批量删除')} ({selectedSet.size})
              </button>
            </span>
          ) : selectedItem ? (
            <span className="qt-browser-foot-actions">
              {(selectedItem.isDir || mode === 'disks') ? (
                <button type="button" className="primary" disabled={busy} onClick={() => openItem(selectedItem)}>
                  {t('打开')}
                </button>
              ) : (
                <>
                  <button type="button" className="primary" disabled={busy} onClick={() => doDownload(selectedItem)}>
                    {t('下载')}
                  </button>
                  <button type="button" disabled={busy} onClick={() => doCat(selectedItem)}>
                    {t('查看')}
                  </button>
                </>
              )}
              <button type="button" disabled={busy} onClick={() => copyPath(selectedItem)}>
                {t('复制路径')}
              </button>
              {mode !== 'disks' && (
                <button type="button" className="danger" disabled={busy} onClick={() => doRm(selectedItem)}>
                  {t('删除')}
                </button>
              )}
            </span>
          ) : null}
        </footer>
      </section>

      {/* Qt Text File Preview Dialog */}
      {preview && (
        <div className="qt-modal-mask" onMouseDown={(e) => { if (e.target === e.currentTarget && !preview.busy) setPreview(null) }}>
          <div className="qt-dialog qt-preview-dlg" onMouseDown={(e) => e.stopPropagation()}>
            <div className="qt-dialog-hd">
              <span className="mono" style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                {preview.title} — {preview.path}
              </span>
              <div style={{ display: 'inline-flex', alignItems: 'center', gap: 6, flexShrink: 0 }}>
                <select
                  style={{ height: 22, padding: '0 6px', fontSize: 11 }}
                  disabled={!!preview.busy}
                  value={preview.encoding}
                  onChange={(e) => setPreviewEncoding(e.target.value as TextEncodingId)}
                  title={t('切换文本编码')}
                >
                  {TEXT_ENCODING_OPTIONS.map((opt) => (
                    <option key={opt.id} value={opt.id}>
                      {opt.id === 'auto' && preview.detected
                        ? `Auto (${TEXT_ENCODING_OPTIONS.find((x) => x.id === preview.detected)?.label || preview.detected})`
                        : opt.label}
                    </option>
                  ))}
                </select>
                <button type="button" className="qt-dock-btn" disabled={!!preview.busy} onClick={() => setPreview(null)}>×</button>
              </div>
            </div>
            <pre className="qt-preview-body">
              {preview.busy && !preview.raw ? t('读取中…') : (preview.text || '(empty)')}
            </pre>
            <div className="qt-dialog-foot">
              <button
                type="button"
                disabled={!!preview.busy || !preview.text}
                onClick={async () => {
                  const full = preview.text || ''
                  const body = preview.message && full.startsWith(preview.message)
                    ? full.slice(preview.message.length).replace(/^\n+/, '')
                    : full
                  const ok = await copyText(body)
                  pushToast(ok ? 'ok' : 'err', ok ? t('已复制内容') : t('复制失败'))
                }}
              >
                {t('复制内容')}
              </button>
              <button type="button" className="primary" disabled={!!preview.busy} onClick={() => setPreview(null)}>
                {t('关闭')}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Right-click Qt Context Menu */}
      {ctx && (
        <div
          className="ctx"
          style={{ left: Math.max(8, Math.min(ctx.x, window.innerWidth - 225)), top: Math.max(8, Math.min(ctx.y, window.innerHeight - 320)) }}
          onClick={(e) => e.stopPropagation()}
        >
          {selectedSet.size > 1 ? (
            <>
              <div className="ctx-hd mono">{selectedSet.size} items selected</div>
              <button type="button" onClick={() => { doDownloadMulti(multiItems); setCtx(null) }}>
                <span>{t('批量下载文件')}</span>
              </button>
              {mode !== 'disks' && (
                <button type="button" className="danger" onClick={() => { doRmMulti(multiItems); setCtx(null) }}>
                  <span>{t('批量删除')}</span>
                </button>
              )}
            </>
          ) : (
            <>
              <div className="ctx-hd mono">{ctx.item.name}</div>
              {(ctx.item.isDir || mode === 'disks') && (
                <button type="button" onClick={() => { openItem(ctx.item); setCtx(null) }}>
                  <span>{t('打开')}</span>
                </button>
              )}
              {!ctx.item.isDir && mode !== 'disks' && (
                <>
                  <button type="button" onClick={() => { doDownload(ctx.item); setCtx(null) }}>
                    <span>{t('下载')}</span>
                  </button>
                  <button type="button" onClick={() => { doCat(ctx.item); setCtx(null) }}>
                    <span>{t('查看内容')}</span>
                  </button>
                </>
              )}
              <button type="button" onClick={() => { copyPath(ctx.item); setCtx(null) }}>
                <span>{t('复制路径')}</span>
              </button>
              <button type="button" onClick={async () => {
                const ok = await copyText(ctx.item.name)
                pushToast(ok ? 'ok' : 'err', ok ? t('已复制名称') : t('复制失败'))
                setCtx(null)
              }}>
                <span>{t('复制名称')}</span>
              </button>
              {mode !== 'disks' && (
                <>
                  <div className="sep" />
                  <button type="button" disabled={uploading} onClick={() => { fileInputRef.current?.click(); setCtx(null) }}>
                    <span>{t('上传到此目录')}</span>
                  </button>
                  <button type="button" className="danger" onClick={() => { doRm(ctx.item); setCtx(null) }}>
                    <span>{t('删除')}</span>
                  </button>
                </>
              )}
            </>
          )}
        </div>
      )}
    </div>
  )
})

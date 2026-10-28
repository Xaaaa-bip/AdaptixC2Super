import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore, type TabId } from '../store'
import { isAgentOnline } from '../utils'

export type PaletteAction = {
  id: string
  group: string
  label: string
  hint?: string
  keywords?: string
  run: () => void
}

type Props = {
  open: boolean
  onClose: () => void
  go: (id: TabId) => void
}

export function CommandPalette({ open, onClose, go }: Props) {
  const agents = useStore((s) => s.agents) || {}
  const openConsole = useStore((s) => s.openConsole)
  const openFilesBrowser = useStore((s) => s.openFilesBrowser)
  const openProcsBrowser = useStore((s) => s.openProcsBrowser)
  const openPty = useStore((s) => s.openPty)
  const openVnc = useStore((s) => s.openVnc)
  const selectAgent = useStore((s) => s.selectAgent)
  const refreshRest = useStore((s) => s.refreshRest)
  const disconnect = useStore((s) => s.disconnect)
  const pushToast = useStore((s) => s.pushToast)

  const [q, setQ] = useState('')
  const [idx, setIdx] = useState(0)
  const inputRef = useRef<HTMLInputElement>(null)
  const listRef = useRef<HTMLDivElement>(null)

  const actions = useMemo<PaletteAction[]>(() => {
    const nav: PaletteAction[] = [
      { id: 'nav-dash', group: '导航', label: '仪表盘', hint: 'G D', keywords: 'dashboard home', run: () => go('dashboard') },
      { id: 'nav-hosts', group: '导航', label: '主机列表', hint: 'G H', keywords: 'sessions hosts agents', run: () => go('sessions') },
      { id: 'nav-delivery', group: '导航', label: '投递一键上线', hint: 'G W', keywords: 'webdelivery deliver oneliner powershell curl', run: () => go('delivery') },
      { id: 'nav-listeners', group: '导航', label: '监听器', keywords: 'listener', run: () => go('listeners') },
      { id: 'nav-gen', group: '导航', label: '生成载荷', keywords: 'generate payload beacon stager', run: () => go('generate') },
      { id: 'nav-tasks', group: '导航', label: '任务中心', keywords: 'tasks jobs', run: () => go('tasks') },
      { id: 'nav-bofs', group: '导航', label: 'BOF 模块', hint: 'G B', keywords: 'bof execute in-memory', run: () => go('bofs') },
      { id: 'nav-argf', group: '导航', label: '参数混淆', hint: 'G A', keywords: 'argfuscator obfuscate powershell commandline evade', run: () => go('argfuscator') },
      { id: 'nav-extkit', group: '导航', label: '扩展命令', hint: 'G X', keywords: 'extkit extension-kit bof ad adcs kerberos ldap creds sal sar 命令 清单 参考', run: () => go('extkit') },
      { id: 'nav-dl', group: '导航', label: '文件下载', keywords: 'downloads', run: () => go('downloads') },
      { id: 'nav-tunnels', group: '导航', label: '隧道代理', keywords: 'tunnels proxy socks', run: () => go('tunnels') },
      { id: 'nav-screens', group: '导航', label: '截图中心', keywords: 'screenshots', run: () => go('screens') },
      { id: 'nav-creds', group: '导航', label: '凭据仓库', keywords: 'credentials', run: () => go('creds') },
      { id: 'nav-targets', group: '导航', label: '目标资产', keywords: 'targets', run: () => go('targets') },
      { id: 'nav-logs', group: '导航', label: '系统日志', keywords: 'logs events', run: () => go('logs') },
      { id: 'nav-ai', group: '协作', label: 'AI 协作平台', hint: 'G I', keywords: 'ai assistant copilot collaboration context analysis', run: () => go('ai') },
    ]

    const sys: PaletteAction[] = [
      {
        id: 'sys-refresh',
        group: '系统',
        label: '刷新 REST 数据',
        hint: 'R',
        keywords: 'refresh sync',
        run: () => {
          refreshRest()
            .then(() => pushToast('ok', '数据已刷新'))
            .catch((e: any) => pushToast('err', e?.message || String(e)))
        },
      },
      {
        id: 'sys-logout',
        group: '系统',
        label: '退出登录',
        keywords: 'logout disconnect exit',
        run: () => disconnect(),
      },
    ]

    const hostActs: PaletteAction[] = Object.values(agents)
      .sort((a, b) => (b.a_last_tick || 0) - (a.a_last_tick || 0))
      .slice(0, 40)
      .flatMap((a) => {
        const live = isAgentOnline(a)
        const name = a.a_computer || a.a_id
        const base = `${name} ${a.a_id} ${a.a_username || ''} ${a.a_internal_ip || ''} ${a.a_external_ip || ''}`
        const acts: PaletteAction[] = [
          {
            id: `host-term-${a.a_id}`,
            group: live ? '在线主机' : '离线主机',
            label: `终端 · ${name}`,
            hint: a.a_internal_ip || a.a_id.slice(0, 8),
            keywords: base + ' term console shell',
            run: () => {
              selectAgent(a.a_id)
              openConsole(a.a_id)
            },
          },
        ]
        // 离线主机不提供文件/进程入口
        if (live) {
          acts.push(
            {
              id: `host-pty-${a.a_id}`,
              group: '在线主机',
              label: `PTY · ${name}`,
              keywords: base + ' pty terminal interactive shell',
              run: () => openPty(a.a_id),
            },
            {
              id: `host-files-${a.a_id}`,
              group: '在线主机',
              label: `文件 · ${name}`,
              keywords: base + ' files browser',
              run: () => openFilesBrowser(a.a_id),
            },
            {
              id: `host-procs-${a.a_id}`,
              group: '在线主机',
              label: `进程 · ${name}`,
              keywords: base + ' process ps',
              run: () => openProcsBrowser(a.a_id),
            },
            {
              id: `host-vnc-${a.a_id}`,
              group: '在线主机',
              label: `VNC · ${name}`,
              keywords: base + ' vnc desktop screen',
              run: () => openVnc(a.a_id),
            },
          )
        }
        return acts
      })

    return [...nav, ...sys, ...hostActs]
  }, [agents, go, openConsole, openFilesBrowser, openProcsBrowser, openPty, openVnc, selectAgent, refreshRest, disconnect, pushToast])

  const filtered = useMemo(() => {
    const query = q.trim().toLowerCase()
    if (!query) return actions.slice(0, 60)
    return actions
      .filter((a) => {
        const hay = `${a.label} ${a.group} ${a.hint || ''} ${a.keywords || ''}`.toLowerCase()
        return query.split(/\s+/).every((t) => hay.includes(t))
      })
      .slice(0, 60)
  }, [actions, q])

  useEffect(() => {
    if (!open) return
    setQ('')
    setIdx(0)
    const t = window.setTimeout(() => inputRef.current?.focus(), 20)
    return () => clearTimeout(t)
  }, [open])

  useEffect(() => {
    setIdx(0)
  }, [q])

  useEffect(() => {
    if (!open) return
    const el = listRef.current?.querySelector<HTMLElement>(`[data-idx="${idx}"]`)
    el?.scrollIntoView({ block: 'nearest' })
  }, [idx, open, filtered.length])

  if (!open) return null

  function runAt(i: number) {
    const a = filtered[i]
    if (!a) return
    onClose()
    // defer so close paint doesn't eat the action
    window.setTimeout(() => a.run(), 0)
  }

  // group for display
  let lastGroup = ''

  return (
    <div className="palette-mask" onMouseDown={(e) => { if (e.target === e.currentTarget) onClose() }}>
      <div className="palette" role="dialog" aria-modal="true" aria-label="命令面板">
        <div className="palette-input-row">
          <span className="palette-ico">⌘</span>
          <input
            ref={inputRef}
            value={q}
            onChange={(e) => setQ(e.target.value)}
            placeholder="搜索页面、主机、操作…"
            onKeyDown={(e) => {
              if (e.key === 'Escape') { e.preventDefault(); onClose() }
              if (e.key === 'ArrowDown') { e.preventDefault(); setIdx((v) => Math.min(v + 1, Math.max(filtered.length - 1, 0))) }
              if (e.key === 'ArrowUp') { e.preventDefault(); setIdx((v) => Math.max(v - 1, 0)) }
              if (e.key === 'Enter') { e.preventDefault(); runAt(idx) }
            }}
          />
          <kbd className="kbd">Esc</kbd>
        </div>
        <div className="palette-list" ref={listRef}>
          {filtered.length === 0 && <div className="palette-empty">无匹配项</div>}
          {filtered.map((a, i) => {
            const showGroup = a.group !== lastGroup
            lastGroup = a.group
            return (
              <div key={a.id}>
                {showGroup && <div className="palette-group">{a.group}</div>}
                <button
                  type="button"
                  data-idx={i}
                  className={`palette-item ${i === idx ? 'active' : ''}`}
                  onMouseEnter={() => setIdx(i)}
                  onClick={() => runAt(i)}
                >
                  <span className="palette-label">{a.label}</span>
                  {a.hint ? <span className="palette-hint mono">{a.hint}</span> : null}
                </button>
              </div>
            )
          })}
        </div>
        <div className="palette-foot">
          <span><kbd className="kbd">↑↓</kbd> 选择</span>
          <span><kbd className="kbd">Enter</kbd> 执行</span>
          <span><kbd className="kbd">Ctrl</kbd>+<kbd className="kbd">K</kbd> 开关</span>
        </div>
      </div>
    </div>
  )
}

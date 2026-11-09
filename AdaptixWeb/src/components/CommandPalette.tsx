import { useEffect, useMemo, useRef, useState } from 'react'
import { t } from '../i18n'
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
      { id: 'nav-dash', group: t('导航'), label: t('仪表盘'), hint: 'G D', keywords: 'dashboard home', run: () => go('dashboard') },
      { id: 'nav-hosts', group: t('导航'), label: t('主机列表'), hint: 'G H', keywords: 'sessions hosts agents', run: () => go('sessions') },
      { id: 'nav-delivery', group: t('导航'), label: t('投递一键上线'), hint: 'G W', keywords: 'webdelivery deliver oneliner powershell curl', run: () => go('delivery') },
      { id: 'nav-listeners', group: t('导航'), label: t('监听器'), keywords: 'listener', run: () => go('listeners') },
      { id: 'nav-gen', group: t('导航'), label: t('生成载荷'), keywords: 'generate payload beacon stager', run: () => go('generate') },
      { id: 'nav-tasks', group: t('导航'), label: t('任务中心'), keywords: 'tasks jobs', run: () => go('tasks') },
      { id: 'nav-bofs', group: t('导航'), label: t('BOF 模块'), hint: 'G B', keywords: 'bof execute in-memory', run: () => go('bofs') },
      { id: 'nav-argf', group: t('导航'), label: t('参数混淆'), hint: 'G A', keywords: 'argfuscator obfuscate powershell commandline evade', run: () => go('argfuscator') },
      { id: 'nav-extkit', group: t('导航'), label: t('扩展命令'), hint: 'G X', keywords: 'extkit extension-kit bof ad adcs kerberos ldap creds sal sar 命令 清单 参考', run: () => go('extkit') },
      { id: 'nav-dl', group: t('导航'), label: t('文件下载'), keywords: 'downloads', run: () => go('downloads') },
      { id: 'nav-tunnels', group: t('导航'), label: t('隧道代理'), keywords: 'tunnels proxy socks', run: () => go('tunnels') },
      { id: 'nav-screens', group: t('导航'), label: t('截图中心'), keywords: 'screenshots', run: () => go('screens') },
      { id: 'nav-creds', group: t('导航'), label: t('凭据仓库'), keywords: 'credentials', run: () => go('creds') },
      { id: 'nav-targets', group: t('导航'), label: t('目标资产'), keywords: 'targets', run: () => go('targets') },
      { id: 'nav-logs', group: t('导航'), label: t('系统日志'), keywords: 'logs events', run: () => go('logs') },
      { id: 'nav-ai', group: t('协作'), label: t('AI 协作平台'), hint: 'G I', keywords: 'ai assistant copilot collaboration context analysis', run: () => go('ai') },
      // ★ 2026-11-09 补：这三个页面 MENUS 里有、主排按钮没有，之前导航列表
      //   也漏了 —— 拓扑页一度在整个 UI 上无任何可达入口。
      { id: 'nav-graph', group: t('协作'), label: t('拓扑图'), hint: 'G G', keywords: 'graph topology 拓扑 图 pivot 网络关系', run: () => go('graph') },
      { id: 'nav-chat', group: t('协作'), label: t('团队聊天'), hint: 'G C', keywords: 'chat team message 聊天 消息', run: () => go('chat') },
      { id: 'nav-relay', group: t('系统'), label: t('中转节点'), hint: 'G R', keywords: 'relay libp2p 中转 转发', run: () => go('relay') },
    ]

    const sys: PaletteAction[] = [
      {
        id: 'sys-refresh',
        group: t('系统'),
        label: t('刷新 REST 数据'),
        hint: 'R',
        keywords: 'refresh sync',
        run: () => {
          refreshRest()
            .then(() => pushToast('ok', t('数据已刷新')))
            .catch((e: any) => pushToast('err', e?.message || String(e)))
        },
      },
      {
        id: 'sys-logout',
        group: t('系统'),
        label: t('退出登录'),
        keywords: 'logout disconnect exit',
        run: () => disconnect(),
      },
    ]

    // 排序键用恒定的 a_create_time（与 Hosts.tsx 同一修法）：按 a_last_tick 排
    // 会让面板里这份主机清单随 WS 心跳不停换序，↑↓ 刚对准的条目被顶走。
    const hostActs: PaletteAction[] = Object.values(agents)
      .sort((a, b) => {
        const ta = a.a_create_time || 0
        const tb = b.a_create_time || 0
        if (ta !== tb) return tb - ta
        return String(a.a_id).localeCompare(String(b.a_id))
      })
      .slice(0, 40)
      .flatMap((a) => {
        const live = isAgentOnline(a)
        const name = a.a_computer || a.a_id
        const base = `${name} ${a.a_id} ${a.a_username || ''} ${a.a_internal_ip || ''} ${a.a_external_ip || ''}`
        const acts: PaletteAction[] = [
          {
            id: `host-term-${a.a_id}`,
            group: live ? t('在线主机') : t('离线主机'),
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
              group: t('在线主机'),
              label: `PTY · ${name}`,
              keywords: base + ' pty terminal interactive shell',
              run: () => openPty(a.a_id),
            },
            {
              id: `host-files-${a.a_id}`,
              group: t('在线主机'),
              label: `文件 · ${name}`,
              keywords: base + ' files browser',
              run: () => openFilesBrowser(a.a_id),
            },
            {
              id: `host-procs-${a.a_id}`,
              group: t('在线主机'),
              label: `进程 · ${name}`,
              keywords: base + ' process ps',
              run: () => openProcsBrowser(a.a_id),
            },
            {
              id: `host-vnc-${a.a_id}`,
              group: t('在线主机'),
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
            placeholder={t('搜索页面、主机、操作…')}
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
          {filtered.length === 0 && <div className="palette-empty">{t('无匹配项')}</div>}
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
          <span><kbd className="kbd">Enter</kbd> {t('执行')}</span>
          <span><kbd className="kbd">Ctrl</kbd>+<kbd className="kbd">K</kbd> 开关</span>
        </div>
      </div>
    </div>
  )
}

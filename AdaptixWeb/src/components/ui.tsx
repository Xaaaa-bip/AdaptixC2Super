import type { ReactNode } from 'react'

export function EmptyState({
  icon,
  title,
  hint,
  action,
}: {
  icon?: string
  title: string
  hint?: string
  action?: ReactNode
}) {
  return (
    <div className="empty">
      {icon ? <div className="big">{icon}</div> : <div className="empty-mark" aria-hidden />}
      <div>{title}</div>
      {hint ? <div className="dim" style={{ marginTop: 6 }}>{hint}</div> : null}
      {action ? <div style={{ marginTop: 12 }}>{action}</div> : null}
    </div>
  )
}

export function PageShell({
  toolbar,
  title,
  actions,
  children,
  className = '',
}: {
  /** 完整工具栏节点；若提供则优先于 title/actions */
  toolbar?: ReactNode
  title?: ReactNode
  actions?: ReactNode
  children: ReactNode
  className?: string
}) {
  const bar = toolbar ?? (
    <>
      {title ? <div className="page-shell-title">{title}</div> : null}
      <span className="spacer" />
      {actions}
    </>
  )
  return (
    <div className={`page page-shell ${className}`.trim()}>
      <div className="toolbar">{bar}</div>
      <div className="table-wrap">{children}</div>
    </div>
  )
}

export function ShortcutHelp({ open, onClose }: { open: boolean; onClose: () => void }) {
  if (!open) return null
  const rows: Array<[string, string]> = [
    ['Ctrl / ⌘ + K', '命令面板（搜页面 / 主机 / 操作）'],
    ['G 然后 D', '概览'],
    ['G 然后 H', '主机列表'],
    ['G 然后 L', '监听器'],
    ['G 然后 P', '生成载荷'],
    ['G 然后 W', 'Web 投递'],
    ['G 然后 T', '任务中心'],
    ['G 然后 B', 'BOF 模块'],
    ['G 然后 A', '参数混淆'],
    ['G 然后 O', '系统日志'],
    ['/', '主机页聚焦搜索；其它页打开命令面板'],
    ['Ctrl+Shift+F', '当前主机 · 文件管理'],
    ['Ctrl+Shift+P', '当前主机 · 进程管理'],
    ['主机工具栏 PTY / VNC', '交互伪终端 / 远程桌面会话'],
    ['Esc', '关闭文件/进程/PTY/VNC 页签或浮层'],
    ['↑ / ↓ + Enter', '终端命令历史 / 面板选择'],
    ['Ctrl+L', '清空当前终端输出'],
  ]
  return (
    <div className="mask" onMouseDown={(e) => { if (e.target === e.currentTarget) onClose() }}>
      <div className="dlg shortcut-dlg" onMouseDown={(e) => e.stopPropagation()}>
        <div className="dlg-hd">
          <div>
            <h2>键盘快捷键</h2>
            <div className="dim" style={{ fontSize: 12 }}>面向操作员的高效路径</div>
          </div>
          <button className="ghost" onClick={onClose}>关闭</button>
        </div>
        <div className="shortcut-grid">
          {rows.map(([k, v]) => (
            <div key={k} className="shortcut-row">
              <span className="shortcut-keys">{k.split('+').map((p, i) => (
                <span key={i}>{i > 0 ? <span className="dim"> + </span> : null}<kbd className="kbd">{p.trim()}</kbd></span>
              ))}</span>
              <span className="shortcut-desc">{v}</span>
            </div>
          ))}
        </div>
        <div className="dlg-foot dim">侧栏可切换紧凑密度与主题；状态会保存在本机浏览器。</div>
      </div>
    </div>
  )
}

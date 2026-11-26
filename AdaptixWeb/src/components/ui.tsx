import type { ReactNode } from 'react'
import { t } from '../i18n'

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
    ['Ctrl / ⌘ + K', t('命令面板（搜页面 / 主机 / 操作）')],
    ['Ctrl + F', t('当前表格 / 控制台 / 日志页内查找')],
    ['Ctrl + L', t('清空当前终端输出')],
    ['G 然后 H', t('主机列表 (Sessions table)')],
    ['G 然后 P', t('会话拓扑 (Sessions graph)')],
    ['G 然后 T', t('任务中心 (Jobs & Tasks)')],
    ['G 然后 L', t('监听器 (Listeners)')],
    ['G 然后 E', t('系统日志 (Logs)')],
    ['G 然后 G', t('载荷生成 (Generate)')],
    ['G 然后 W', t('一键投递 (Web Delivery)')],
    ['G 然后 B', t('BOF 模块')],
    ['G 然后 A', t('参数混淆')],
    ['G 然后 O', t('状态概览')],
    ['↑ / ↓ + Enter', t('终端命令历史 / 补全选择')],
    ['Esc', t('收起查找栏或关闭弹窗')],
  ]
  return (
    <div className="mask" onMouseDown={(e) => { if (e.target === e.currentTarget) onClose() }}>
      <div className="dlg shortcut-dlg" onMouseDown={(e) => e.stopPropagation()}>
        <div className="dlg-hd">
          <div>
            <h2>{t('键盘快捷键')}</h2>
            <div className="dim" style={{ fontSize: 12 }}>{t('面向操作员的高效路径')}</div>
          </div>
          <button className="ghost" onClick={onClose}>{t('关闭')}</button>
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
        <div className="dlg-foot dim">{t('可在顶部「设置」切换 10 套 Qt 主题、字体字号、列显隐与分屏布局。')}</div>
      </div>
    </div>
  )
}

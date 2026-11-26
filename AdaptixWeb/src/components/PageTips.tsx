import { useState } from 'react'
import { t } from '../i18n'
import type { ReactNode } from 'react'

/**
 * Collapsible per-view tips popover: closed by default so it never covers
 * or crushes the toolbar/canvas; click "?" to toggle floating Qt popover.
 */
export function PageTips({ id: _id, children }: { id: string; children: ReactNode }) {
  const [open, setOpen] = useState(false)

  return (
    <>
      <button
        type="button"
        className="ghost ico tips-btn"
        title={t('操作提示')}
        onClick={() => setOpen((v) => !v)}
      >
        ?
      </button>
      {open && (
        <div className="page-tips" role="dialog" aria-label={t('操作提示')}>
          <div className="page-tips-head">
            <span>{t('操作提示')}</span>
            <button type="button" className="ghost ico" title={t('关闭')} onClick={() => setOpen(false)}>×</button>
          </div>
          <div className="page-tips-body">{children}</div>
        </div>
      )}
    </>
  )
}

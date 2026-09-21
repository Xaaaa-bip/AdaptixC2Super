import { useEffect, useState } from 'react'
import type { ReactNode } from 'react'

/**
 * 每页操作提示：工具栏「?」按钮 + 可折叠面板。
 *
 * 自动展开用的键是【全局一个】，不是每页一个。
 *
 * 原来是 `adaptix.tips.seen.${id}` —— 每个视图各存一个键，于是 16 个视图
 * 会各自动展开一次。而这块面板是 fixed 定位浮在右下角的，展开时正压在
 * 数据区上（实测：任务列表的摘要列被盖住三行、日志行尾、拓扑画布都被盖）。
 * 操作员走一圈要点 16 次 ×，每次还得先把被盖住的内容让出来。
 *
 * 首次使用确实需要一次引导，但「每页引导一次」不是引导，是打扰。
 * 现在只在第一次用这台控制台时自动展开一次，之后一律靠工具栏的 ? 手动开。
 * （老用户身上的 `adaptix.tips.seen.<视图>` 旧键留着不影响 —— 新键不存在，
 *   所以会再自动展开最后一次。）
 */
export function PageTips({ id, children }: { id: string; children: ReactNode }) {
  const seenKey = 'adaptix.tips.seen'
  const [open, setOpen] = useState(false)

  useEffect(() => {
    try {
      if (!localStorage.getItem(seenKey)) {
        setOpen(true)
        localStorage.setItem(seenKey, '1')
      }
    } catch {
      /* localStorage 不可用时静默 */
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  return (
    <>
      <button
        type="button"
        className="ghost ico tips-btn"
        title="操作提示"
        onClick={() => setOpen((v) => !v)}
      >
        ?
      </button>
      {open && (
        <div className="page-tips">
          <div className="page-tips-head">
            <span>操作提示</span>
            <button type="button" className="ghost ico" title="关闭" onClick={() => setOpen(false)}>×</button>
          </div>
          <div className="page-tips-body">{children}</div>
        </div>
      )}
    </>
  )
}

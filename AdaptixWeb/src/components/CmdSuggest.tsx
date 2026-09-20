import { useEffect, useRef } from 'react'
import type { Suggestion } from '../lib/completions'

/**
 * 补全候选弹层。Term 与「执行命令」对话框共用同一个组件 ——
 * 不只是为了少写一遍样式，更是因为**降级必须被看见**这条规则
 * 只能有一个实现处（见下面 degraded 的渲染）。
 */
export function CmdSuggest({
  items,
  index,
  degraded,
  notice,
  onPick,
  onHover,
  onRefresh,
}: {
  items: Suggestion[]
  index: number
  degraded: boolean
  /**
   * 「这批候选为什么长这样」的一句话。与 degraded 不同：
   * degraded 说的是数据源降级，notice 说的是**当前上下文没有可用候选**。
   * 它必须被渲染出来 —— 否则「功能正常但结果为空」和「功能坏了」
   * 在界面上完全同形。
   */
  notice?: string | null
  onPick: (i: number) => void
  onHover: (i: number) => void
  /**
   * 重取候选。给了才渲染刷新入口。
   *
   * 为什么弹层里要有个刷新按钮：BOF 清单是**服务端目录**的映射，
   * 而目录会变（编译产物、丢一个新 `.o` 进去、profile 换了 axscripts）。
   * 「BOF」页的编译入口已经会通知补全，但操作员也可能是在**别的终端 / 别处**
   * 动的文件 —— 那时没有这个按钮，他只能等着或者刷新整个页面。
   */
  onRefresh?: () => void
}) {
  const listRef = useRef<HTMLDivElement>(null)

  // 高亮行跟随键盘滚动。block:'nearest' —— 已经可见的行不动，
  // 否则每按一次方向键整个列表都会跳一下。
  useEffect(() => {
    if (index < 0) return
    const el = listRef.current?.querySelector<HTMLElement>(`[data-idx="${index}"]`)
    el?.scrollIntoView({ block: 'nearest' })
  }, [index])

  return (
    <div className="cmd-suggest">
      {/* 静默降级 = 让操作员以为看到的就是全部，正是这次要修的毛病
          （Bofs.tsx:487-489 同款理由）。所以宁可占一行也要说出来。 */}
      {degraded && (
        <div className="cs-warn" role="status">
          服务端命令表不可用 · 候选来自内置清单（可能不全）
        </div>
      )}
      {/* notice 优先于上面那条：它说的是「眼前这批为什么是空的」，
          比「数据源降级了」更贴近操作员当下的问题。 */}
      {notice ? (
        <div className="cs-warn" role="status">{notice}</div>
      ) : null}
      <div className="cs-list" ref={listRef}>
        {items.map((s, i) => (
          <button
            key={s.name}
            type="button"
            data-idx={i}
            className={`cs-item ${i === index ? 'active' : ''}`}
            // onMouseDown + preventDefault：抢在 blur 之前接受。
            // 用 onClick 的话，mousedown 会先把输入框的焦点带走。
            onMouseDown={(e) => { e.preventDefault(); onPick(i) }}
            onMouseEnter={() => onHover(i)}
          >
            {/* 显示 label、插入 name。BOF 候选的 name 是服务端绝对路径
                （很长），铺在弹层里没人读得下去；插入的必须是全路径。 */}
            <span className="cs-name">
              {/* 产物比源码旧。**只标注、不隐藏**（用户 2026-09-19 的取舍）：
                  「旧」不等于「错」，可能就是要跑那个版本。
                  原因放 title —— 占一整行会把列表挤没，但鼠标一停就能看到
                  到底是哪份源码更新了。 */}
              {s.stale && (
                <span className="cs-stale" title={s.staleReason || '产物比源码旧'}>⚠</span>
              )}
              {s.label ?? s.name}
            </span>
            {s.description ? <span className="cs-desc">{s.description}</span> : null}
            {s.example ? <span className="cs-eg">{s.example}</span> : null}
          </button>
        ))}
      </div>
      {onRefresh ? (
        <div className="cs-foot">
          {/* 与候选行同样的 onMouseDown + preventDefault：这个按钮在输入框
              的 blur 路径上，用 onClick 会在焦点被带走之后才触发。 */}
          <button
            type="button"
            className="cs-refresh"
            onMouseDown={(e) => { e.preventDefault(); onRefresh() }}
            title="重新向服务端索取候选（编译 / 上传新 BOF 之后用）"
          >
            刷新候选
          </button>
        </div>
      ) : null}
    </div>
  )
}

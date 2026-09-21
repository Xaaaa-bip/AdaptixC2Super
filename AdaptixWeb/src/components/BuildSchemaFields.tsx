import { useEffect, useMemo, useRef, useState } from 'react'
import { groupFields, visibleFields } from '../api/buildSchema'
import type { BuildSchema, BuildSchemaField } from '../api/types'

/**
 * 按插件声明的字段表渲染生成参数。
 *
 * ★ 这里只认契约里那几个键（key/label/type/options/min/max/placeholder/hint/
 *   accept/group/show_if），多的一律忽略 —— 表是插件作者写的，前端不能因为多了个
 *   不认识的键就渲染出个半成品控件。
 *
 * ★ **值的存储不在这里**。read/write 由调用方给：Generate.tsx 里有一批 key
 *   （format/arch/os/sleep/jitter/stager_ts_*）控制台自己就有 state，摘要栏、提示条、
 *   监听器兼容性都长在上面，所以不另开一份。**每个 key 只有一处存储**，
 *   不存在"表里选了 Stager、isStager 还是 false"这种分叉。
 *
 * 样式只沿用载荷页既有的 .field / .chip-row / .check-inline / .hint，加上
 * styles.css 里一小段 .bs-* 新类名（见那里的注释：全是新类名，不覆盖任何既有选择器）。
 */
type Props = {
  schema: BuildSchema
  /** 显隐判据之一：当前选中的输出格式 */
  format: string
  /** 显隐判据之二：当前监听器的**注册名**（l_reg_name），没选就是空串 */
  listenerType: string
  read: (key: string) => unknown
  write: (key: string, v: unknown) => void
}

export function BuildSchemaFields({ schema, format, listenerType, read, write }: Props) {
  const shown = useMemo(
    () => visibleFields(schema, format, listenerType),
    [schema, format, listenerType],
  )
  const groups = useMemo(() => groupFields(shown), [shown])

  // 折叠状态按组名记。默认只展开第一组：载荷页的主控件（格式/架构/系统/Sleep）都在
  // 第一组，展开它才和改动前的界面等价；其余组默认收起，免得二十多个控件一次铺开，
  // 把「生成并下载」按钮顶到屏幕外。
  const [closed, setClosed] = useState<Record<string, boolean>>({})
  const isClosed = (name: string, i: number) => (name in closed ? closed[name] : i > 0)

  // ★ 某个组「多出一个可见字段」时必须自动展开。否则操作员把格式切成 Stager，该填的
  //   下载地址藏在一个刚出现的折叠块里 —— 界面上看不出任何变化，只会觉得"选了 Stager
  //   什么都没发生"。只对**新增**的字段生效：收起某个组之后切格式，只要没有新字段落
  //   进去，它就还是收着的。首次挂载不算（那正是"默认收起"的时刻）。
  const prevKeys = useRef<Set<string> | null>(null)
  useEffect(() => {
    const now = new Set(shown.map((f) => f.key))
    const prev = prevKeys.current
    prevKeys.current = now
    if (prev === null) return
    const grew = new Set<string>()
    for (const f of shown) if (!prev.has(f.key)) grew.add(f.group || '基础')
    if (!grew.size) return
    setClosed((c) => {
      let changed = false
      const next = { ...c }
      // 判据是 `!== false` 而不是 `=== true`：默认开着的第一组在表里没有条目，
      // 显式写一个 false 进去不改变结果（幂等），却省掉一套"它现在到底开没开"的推算 ——
      // 那套推算正是最容易和上面的 isClosed 走岔的地方。
      for (const g of grew) {
        if (next[g] !== false) {
          next[g] = false
          changed = true
        }
      }
      return changed ? next : c
    })
  }, [shown])

  return (
    <>
      {groups.map((g, i) => {
        const shut = isClosed(g.name, i)
        return (
          <section className="bs-group" key={g.name} data-group={g.name}>
            <button
              type="button"
              className="bs-head"
              onClick={() => setClosed((c) => ({ ...c, [g.name]: !shut }))}
              aria-expanded={!shut}
              title={shut ? '展开' : '收起'}
            >
              <span className="bs-caret">{shut ? '▸' : '▾'}</span>
              <span>{g.name}</span>
              {/* 收起时也报个数：否则"这组里有没有我要的东西"只能靠点开试 */}
              <span className="bs-count">{g.fields.length} 项</span>
            </button>
            {!shut && (
              <div className="bs-body">
                {g.fields.map((f) => (
                  <Field key={f.key} field={f} value={read(f.key)} write={write} />
                ))}
              </div>
            )}
          </section>
        )
      })}
    </>
  )
}

function Field({
  field,
  value,
  write,
}: {
  field: BuildSchemaField
  value: unknown
  write: (key: string, v: unknown) => void
}) {
  const { key, label } = field
  const hint = field.hint ? <div className="hint muted-hint">{field.hint}</div> : null

  // 复选框走 .field-flat（单列），跟载荷页既有的 OPSEC / stager_ts_ssl 两处一致。
  // 两列网格会把 14px 的复选框摆到 x=110，跟左边 0~60 的标签中间空一大格。
  if (field.type === 'bool') {
    return (
      <div className="field field-flat">
        <label className="check-inline" style={{ color: 'var(--text)' }} title={key}>
          <input
            type="checkbox"
            checked={value === true}
            onChange={(e) => write(key, e.target.checked)}
          />
          {label}
        </label>
        {hint}
      </div>
    )
  }

  return (
    <div className="field">
      {/* 线名（= GenerateConfig 的 json tag）放 title：对照服务端日志和插件源码时要用 */}
      <label title={key}>{label}</label>
      {field.type === 'select' ? (
        <SelectRow field={field} value={value} write={write} />
      ) : field.type === 'file' ? (
        <FileRow field={field} write={write} />
      ) : field.type === 'number' ? (
        <input
          type="number"
          min={field.min}
          max={field.max}
          value={value === undefined || value === null ? '' : String(value)}
          placeholder={field.placeholder}
          onChange={(e) => write(key, e.target.value === '' ? undefined : Number(e.target.value))}
        />
      ) : (
        <input
          value={value === undefined || value === null ? '' : String(value)}
          placeholder={field.placeholder}
          onChange={(e) => write(key, e.target.value === '' ? undefined : e.target.value)}
        />
      )}
      {hint}
    </div>
  )
}

/**
 * 取值域来自插件，所以这里**只做展示，绝不增删** —— 多一个操作员就能选到一个插件
 * 不认的值，少一个就是插件认的值选不到（Qt 那份 combo 漏掉 "Stager" 就是这么来的，
 * 而 pl_main.go 有 `Format == "Stager"` 的真分支）。
 *
 * 「默认」这一项是**不下发这个键**，不是某个具体取值。★ 必须有它：表里没有 default
 * 字段，前端无从知道"插件默认是什么"，而"默认选第一项"是会错的 ——
 * rotation_mode 的选项是 ["random","sequential"]（推导顺序把 `== "random"` 这个特例
 * 分支排在了前面），可插件默认是 sequential（pl_main.go:756 只判 "random"），
 * 默认选第一项等于把默认值静默改成 random。
 */
function SelectRow({
  field,
  value,
  write,
}: {
  field: BuildSchemaField
  value: unknown
  write: (key: string, v: unknown) => void
}) {
  const opts = field.options || []
  const cur = typeof value === 'string' ? value : undefined
  // 当前值不在表里时（插件改了取值域、界面还留着旧值）照样把它画出来并标成选中 ——
  // 否则界面上一个都不亮，看着像没选，实际却会把这个值发出去。
  const extra = cur !== undefined && !opts.includes(cur) ? [cur] : []
  return (
    <div className="chip-row bs-chips">
      <button
        type="button"
        className={`bs-default${cur === undefined ? ' primary' : ''}`}
        onClick={() => write(field.key, undefined)}
        title="不指定：这个键不下发，交给插件自己的默认值（不是取第一个选项）"
      >
        默认
      </button>
      {[...extra, ...opts].map((o) => (
        <button
          key={o}
          type="button"
          className={cur === o ? 'primary' : ''}
          onClick={() => write(field.key, o)}
          title={o}
        >
          {o}
        </button>
      ))}
    </div>
  )
}

// 请求体没有服务端上限（只有 /api/otp 那条有 2GB 护栏），base64 之后还要乘 4/3，
// 所以客户端自己拦一道，免得手滑选了个几百兆的文件把页面卡死。
const MAX_FILE = 16 << 20

/**
 * 文件字段。**线格式是 base64**，不是文件内容本身 —— 插件那边是
 * `base64.StdEncoding.DecodeString(generateConfig.SideloadingContent / IconContent)`
 * （pl_main.go:1060、:1104）。
 *
 * 选中的文件名只留在本地 state：值列里存的是 base64，从它反推不出文件名，
 * 而操作员需要看见"我到底选没选、选的是哪个文件"。
 */
function FileRow({
  field,
  write,
}: {
  field: BuildSchemaField
  write: (key: string, v: unknown) => void
}) {
  const [picked, setPicked] = useState<{ name: string; size: number } | null>(null)
  const [err, setErr] = useState('')

  async function pick(file: File | undefined) {
    setErr('')
    if (!file) {
      setPicked(null)
      write(field.key, undefined)
      return
    }
    if (file.size > MAX_FILE) {
      setErr(`文件 ${fmtBytes(file.size)} 超过上限 ${fmtBytes(MAX_FILE)}，没有读入`)
      return
    }
    try {
      const buf = await file.arrayBuffer()
      setPicked({ name: file.name, size: file.size })
      write(field.key, toBase64(buf))
    } catch (e: any) {
      setErr(`读取失败：${e?.message || e}`)
    }
  }

  return (
    <div>
      <div className="bs-file-row">
        <input
          type="file"
          accept={field.accept || undefined}
          onChange={(e) => pick(e.target.files?.[0])}
          title={field.key}
        />
        {picked && (
          <>
            <span className="dim bs-file-meta">
              {picked.name} · {fmtBytes(picked.size)}
            </span>
            <button type="button" className="ghost" onClick={() => pick(undefined)}>
              清除
            </button>
          </>
        )}
      </div>
      {err && <div className="hint warn-hint">{err}</div>}
    </div>
  )
}

/** 分块拼 base64：`String.fromCharCode(...bytes)` 在几兆的数组上会把调用栈撑爆。 */
function toBase64(buf: ArrayBuffer): string {
  const b = new Uint8Array(buf)
  let s = ''
  const CH = 0x8000
  for (let i = 0; i < b.length; i += CH) {
    s += String.fromCharCode.apply(null, Array.from(b.subarray(i, i + CH)))
  }
  return btoa(s)
}

function fmtBytes(n: number): string {
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`
  return `${(n / 1024 / 1024).toFixed(2)} MB`
}

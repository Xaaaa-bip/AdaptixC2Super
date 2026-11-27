import { useEffect, useMemo, useRef, useState } from 'react'
import { groupFields, visibleFields } from '../api/buildSchema'
import type { BuildSchema, BuildSchemaField } from '../api/types'

type Props = {
  schema: BuildSchema
  format: string
  listenerType: string
  read: (key: string) => unknown
  write: (key: string, v: unknown) => void
  isBuiltin?: boolean
  onResetAdvanced?: () => void
}

const SLEEP_PRESETS = ['0s', '5s', '15s', '60s']

const CURATED_HINTS: Record<string, string> = {
  iat_hiding: '剥离 CRT 依赖并隐藏导入表（部分启发式查杀较敏感）',
  exe_pseudo_sign: '为产物附加随机自签 Authenticode 签名（默认关闭）',
  pe_header_mod: '覆写 PE Rich Header 与编译时间戳特征',
  diversify: '每次构建注入独立代码块与诱饵导入，避免特征收敛',
  disguise: '嵌入常见系统组件风格的版本资源与文件描述',
  svcname: 'Windows 服务可执行文件注册的服务名称',
  is_killdate: '到达指定日期时间后自动停止上线并退出',
  is_workingtime: '仅在每日指定工作时间段内保持回连活动',
  icon_random: '为 EXE/DLL 自动生成随机应用程序图标',
  icon_content: '上传自定义 .ico 文件作为可执行文件图标',
  is_sideloading: '基于合法 DLL 导出表构造白加黑侧载载荷',
  sideloading_content: '上传用于转发导出函数的参考合法 DLL',
  use_proxy: '使 Beacon 经企业 HTTP/HTTPS 代理服务器出口回连',
  opsec_enabled: '启用反调试、常见沙箱特征探测与敏感字符串混淆',
  opsec_vm_exit: '检测到虚拟机环境时放弃上线（云主机目标慎用）',
  bof_api_proxy: '经代理跳板解析并执行 BOF 动态 API 调用',
  mask_heap: '休眠期间对进程堆内存执行混淆加密防护',
  win7_support: '兼容旧版 Windows 7 / Server 2008 R2 系统 API',
  stager_ts_ssl: '按 Teamserver 端口 HTTPS 配置加密拉取二阶段载荷',
  reconn_timeout: '断线后尝试重连的等待间隔（纯数字按秒计算）',
  reconn_count: '最大重连重试次数（填 0 表示无限持久化重连）',
}

function formatHint(key: string, raw?: string): string {
  if (CURATED_HINTS[key]) return CURATED_HINTS[key]
  if (!raw) return ''
  const s = raw
    .replace(/★\s*/g, '')
    .replace(/默认跟随\s*buildenv\.mk\s*的\s*[A-Z0-9_]+(?:=\w+)?[；;，,\s]*/g, '')
    .replace(/插件默认值来自\s*buildenv\.mk\s*的\s*[A-Z0-9_]+[；;，,\s]*/g, '')
    .replace(/（[^）]*pl_[\w.]+\.go:\d+[^）]*）/g, '')
    .replace(/\([^)]*pl_[\w.]+\.go:\d+[^)]*\)/g, '')
    .replace(/^[;；:：,，\s]+/, '')
    .trim()
  return s.length > 52 ? `${s.slice(0, 50).trimEnd()}…` : s
}

function isFieldActive(f: BuildSchemaField, val: unknown): boolean {
  if (f.type === 'bool') return val === true
  if (val === undefined || val === null) return false
  if (typeof val === 'string') return val.trim().length > 0 && val !== 'None'
  return true
}

export function BuildSchemaFields({
  schema,
  format,
  listenerType,
  read,
  write,
}: Props) {
  const shown = useMemo(
    () => visibleFields(schema, format, listenerType),
    [schema, format, listenerType],
  )
  const groups = useMemo(() => groupFields(shown), [shown])

  const coreGroup = groups[0]
  const advGroups = useMemo(() => groups.slice(1), [groups])

  const [activeGroup, setActiveGroup] = useState<string>(() => advGroups[0]?.name || '')
  const [showAll, setShowAll] = useState(false)

  useEffect(() => {
    if (!advGroups.length) {
      setActiveGroup('')
      return
    }
    if (!advGroups.some((g) => g.name === activeGroup)) {
      setActiveGroup(advGroups[0].name)
    }
  }, [advGroups, activeGroup])

  // Auto-focus newly revealed group (e.g. Stager endpoint or DNS options)
  const prevKeys = useRef<Set<string> | null>(null)
  useEffect(() => {
    const now = new Set(shown.map((f) => f.key))
    const prev = prevKeys.current
    prevKeys.current = now
    if (prev === null) return
    for (const f of shown) {
      if (!prev.has(f.key)) {
        const grpName = f.group || '基础'
        if (grpName !== coreGroup?.name && advGroups.some((g) => g.name === grpName)) {
          setActiveGroup(grpName)
          break
        }
      }
    }
  }, [shown, coreGroup?.name, advGroups])

  const displayedAdvGroups = showAll
    ? advGroups
    : advGroups.filter((g) => g.name === activeGroup)

  const coreHasSleep = !!coreGroup?.fields.some((f) => f.key === 'sleep')
  const coreHasJitter = !!coreGroup?.fields.some((f) => f.key === 'jitter')

  return (
    <div className="ax-schema-wrap">
      {coreGroup && (
        <section className="ax-group-card" data-group={coreGroup.name}>
          <div className="ax-group-card-hd">
            <b>核心编译参数</b>
            <span className="mono">{format}{read('arch') ? ` · ${String(read('arch'))}` : ''}</span>
          </div>

          <div className="ax-core-row">
            {coreGroup.fields.map((f) => {
              if ((f.key === 'sleep' || f.key === 'jitter') && coreHasSleep && coreHasJitter) {
                return null
              }
              if (f.key === 'os' && f.type === 'select' && (f.options?.length || 0) <= 1) {
                return null
              }
              return (
                <CoreField
                  key={f.key}
                  field={f}
                  value={read(f.key)}
                  write={write}
                />
              )
            })}
          </div>

          {coreHasSleep && coreHasJitter && (
            <div className="ax-ctrl-grp">
              <div className="ax-ctrl-lbl">
                <span>回连周期 Sleep 与随机抖动</span>
                <span className="ax-ctrl-sub">支持单位 h / m / s</span>
              </div>
              <div className="ax-sleep-line">
                <input
                  className="mono ax-sleep-in"
                  value={String(read('sleep') ?? '')}
                  onChange={(e) => write('sleep', e.target.value)}
                  placeholder="5s / 10s / 1m"
                />
                <div className="ax-seg-track">
                  {SLEEP_PRESETS.map((s) => (
                    <button
                      key={s}
                      type="button"
                      className={`ax-seg-btn mono ${read('sleep') === s ? 'active' : ''}`}
                      onClick={() => write('sleep', s)}
                    >
                      {s}
                    </button>
                  ))}
                </div>
                <label className="ax-jit-wrap">
                  <span>Jitter</span>
                  <div className="ax-unit-input">
                    <input
                      type="number"
                      className="mono"
                      min={0}
                      max={100}
                      value={Number(read('jitter') ?? 0)}
                      onChange={(e) => write('jitter', Number(e.target.value))}
                    />
                    <span className="ax-unit">%</span>
                  </div>
                </label>
              </div>
            </div>
          )}
        </section>
      )}

      {advGroups.length > 0 && (
        <section className="ax-group-card ax-adv-wrap">
          <div className="ax-tabs-bar">
            <div className="ax-tabs-list" role="tablist">
              {advGroups.map((g) => {
                const active = !showAll && activeGroup === g.name
                const activeCount = g.fields.filter((f) => isFieldActive(f, read(f.key))).length
                return (
                  <button
                    key={g.name}
                    type="button"
                    role="tab"
                    aria-selected={active}
                    className={`ax-tab ${active ? 'active' : ''}`}
                    onClick={() => {
                      setShowAll(false)
                      setActiveGroup(g.name)
                    }}
                  >
                    <span>{g.name}</span>
                    <span className="ax-tab-cnt mono">{g.fields.length}</span>
                    {activeCount > 0 && <i className="ax-tab-dot" title={`已开启 ${activeCount} 项`} />}
                  </button>
                )
              })}
            </div>
            <div className="ax-tabs-acts">
              <button
                type="button"
                className={`ax-btn-ghost ax-btn-sm ${showAll ? 'active' : ''}`}
                onClick={() => setShowAll((v) => !v)}
                title={showAll ? '切换回分组标签视图' : '同屏展开所有高级选项'}
              >
                {showAll ? '分组视图' : '全部展开'}
              </button>
            </div>
          </div>

          <div className="ax-adv-sections">
            {displayedAdvGroups.map((g) => {
              const boolFields = g.fields.filter((f) => f.type === 'bool')
              const otherFields = g.fields.filter((f) => f.type !== 'bool')
              return (
                <div className="ax-group-sec" key={g.name} data-group={g.name}>
                  {showAll && (
                    <div className="ax-group-sec-hd">
                      <span>{g.name}</span>
                      <span className="mono">{g.fields.length} 项</span>
                    </div>
                  )}

                  {boolFields.length > 0 && (
                    <div className="ax-setting-list">
                      {boolFields.map((f) => {
                        const checked = read(f.key) === true
                        const note = formatHint(f.key, f.hint)
                        return (
                          <label
                            key={f.key}
                            className={`ax-setting-row ${checked ? 'checked' : ''}`}
                            title={f.hint || f.key}
                          >
                            <input
                              type="checkbox"
                              checked={checked}
                              onChange={(e) => write(f.key, e.target.checked)}
                            />
                            <div className="ax-setting-info">
                              <b>{f.label}</b>
                              {note && <span>{note}</span>}
                            </div>
                          </label>
                        )
                      })}
                    </div>
                  )}

                  {otherFields.length > 0 && (
                    <div className="ax-field-grid">
                      {otherFields.map((f) => (
                        <AdvField
                          key={f.key}
                          field={f}
                          value={read(f.key)}
                          write={write}
                        />
                      ))}
                    </div>
                  )}
                </div>
              )
            })}
          </div>
        </section>
      )}
    </div>
  )
}

function CoreField({
  field,
  value,
  write,
}: {
  field: BuildSchemaField
  value: unknown
  write: (key: string, v: unknown) => void
}) {
  if (field.type === 'select') {
    const opts = field.options || []
    const cur = typeof value === 'string' ? value : undefined
    const extra = cur !== undefined && !opts.includes(cur) ? [cur] : []
    return (
      <div className="ax-ctrl-grp">
        <div className="ax-ctrl-lbl" title={field.hint || field.key}>
          <span>{field.label}</span>
        </div>
        <div className="ax-seg-track">
          {[...extra, ...opts].map((o) => (
            <button
              key={o}
              type="button"
              className={`ax-seg-btn ${cur === o ? 'active' : ''}`}
              onClick={() => write(field.key, o)}
              title={o}
            >
              {o}
            </button>
          ))}
        </div>
      </div>
    )
  }
  return <AdvField field={field} value={value} write={write} />
}

function AdvField({
  field,
  value,
  write,
}: {
  field: BuildSchemaField
  value: unknown
  write: (key: string, v: unknown) => void
}) {
  const { key, label } = field
  const note = formatHint(key, field.hint)
  const full = field.type === 'file'
  return (
    <div className={`ax-field ${full ? 'ax-field-full' : ''}`} title={field.hint || `参数键: ${key}`}>
      <span className="ax-field-lbl">{label}</span>

      {field.type === 'select' ? (
        <div className="ax-seg-track">
          {(field.options || []).map((o) => {
            const cur = typeof value === 'string' ? value : undefined
            const isSel = cur === o
            return (
              <button
                key={o}
                type="button"
                className={`ax-seg-btn ${isSel ? 'active' : ''}`}
                onClick={() => write(key, isSel ? undefined : o)}
                title={isSel ? '再次点击恢复默认' : o}
              >
                {o}
              </button>
            )
          })}
        </div>
      ) : field.type === 'file' ? (
        <FileRow field={field} write={write} />
      ) : field.type === 'number' ? (
        <input
          type="number"
          className="mono"
          min={field.min}
          max={field.max}
          value={value === undefined || value === null ? '' : String(value)}
          placeholder={field.placeholder}
          onChange={(e) => write(key, e.target.value === '' ? undefined : Number(e.target.value))}
        />
      ) : (
        <input
          className="mono"
          value={value === undefined || value === null ? '' : String(value)}
          placeholder={field.placeholder}
          onChange={(e) => write(key, e.target.value === '' ? undefined : e.target.value)}
        />
      )}

      {note && <div className="ax-field-hint">{note}</div>}
    </div>
  )
}

const MAX_FILE = 16 << 20

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
      setErr(`文件 ${fmtBytes(file.size)} 超过上限 ${fmtBytes(MAX_FILE)}`)
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
    <div className="ax-file-box">
      <div className="ax-file-row">
        <input
          type="file"
          accept={field.accept || undefined}
          onChange={(e) => pick(e.target.files?.[0])}
          title={field.key}
        />
        {picked && (
          <>
            <span className="ax-pill mono">
              {picked.name} · {fmtBytes(picked.size)}
            </span>
            <button type="button" className="ax-btn ax-btn-sm" onClick={() => pick(undefined)}>
              清除
            </button>
          </>
        )}
      </div>
      {err && <div className="ax-warn-callout">{err}</div>}
    </div>
  )
}

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

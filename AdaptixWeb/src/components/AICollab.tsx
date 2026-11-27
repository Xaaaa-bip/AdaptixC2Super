import { Fragment, useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { copyText, fmtRelative, fmtTime, fmtTimeShort, isAgentOnline, useOnlineClock5s } from '../utils'

type Message = { id: string; role: 'assistant' | 'user' | 'error'; text: string; ts: number }
type ChatThread = { id: string; title: string; targetId: string; createdAt: number; updatedAt: number; messages: Message[] }
type AIConfig = { provider: string; baseUrl: string; model: string; apiKey: string }

const ROOMS_KEY = 'adaptix.ai.battle.messages.v1'
const CONFIG_KEY = 'adaptix.ai.config.v1'
const PROVIDERS = [
  { id: 'openai', label: 'OpenAI', desc: '官方 API / 兼容代理', baseUrl: 'https://api.openai.com/v1', model: 'gpt-4o-mini' },
  { id: 'deepseek', label: 'DeepSeek', desc: '高性价比中文推理', baseUrl: 'https://api.deepseek.com/v1', model: 'deepseek-chat' },
  { id: 'ollama', label: 'Ollama (本地)', desc: '内网离线大模型', baseUrl: 'http://127.0.0.1:11434/v1', model: 'llama3.1' },
  { id: 'compatible', label: '自定义兼容', desc: 'OpenAI Chat 接口', baseUrl: '', model: '' },
]

function uid(prefix: string) {
  return prefix + '-' + Date.now() + '-' + Math.random().toString(36).slice(2, 7)
}

function makeThread(targetId = '', title = '新对话'): ChatThread {
  const now = Date.now()
  return { id: uid('thread'), title, targetId, createdAt: now, updatedAt: now, messages: [] }
}

function normMessages(raw: any): Message[] {
  if (!Array.isArray(raw)) return []
  return raw
    .filter((m: any) => m && m.id !== 'welcome')
    .map((m: any) => ({
      id: String(m?.id || uid('msg')),
      role: (m?.role === 'user' ? 'user' : m?.role === 'error' ? 'error' : 'assistant') as Message['role'],
      text: String(m?.text || ''),
      ts: Number(m?.ts) || Date.now(),
    }))
}

function loadThreads(): ChatThread[] {
  try {
    const parsed = JSON.parse(localStorage.getItem(ROOMS_KEY) || 'null')
    if (Array.isArray(parsed) && parsed.length) {
      return parsed.slice(0, 12).map((r: any, idx: number) => ({
        id: String(r?.id || uid('thread' + idx)),
        title: String(r?.title || '对话 ' + (idx + 1)),
        targetId: String(r?.targetId || ''),
        createdAt: Number(r?.createdAt) || Date.now(),
        updatedAt: Number(r?.updatedAt || r?.createdAt) || Date.now(),
        messages: normMessages(r?.messages),
      }))
    }
  } catch { /* ignore */ }
  return [makeThread('', '当前态势分析')]
}

function loadConfig(): AIConfig {
  try {
    const saved = JSON.parse(localStorage.getItem(CONFIG_KEY) || 'null')
    if (saved && typeof saved === 'object') {
      return {
        provider: String(saved.provider || 'compatible'),
        baseUrl: String(saved.baseUrl || ''),
        model: String(saved.model || ''),
        apiKey: String(saved.apiKey || ''),
      }
    }
  } catch { /* ignore */ }
  return { provider: 'compatible', baseUrl: '', model: '', apiKey: '' }
}

function configured(config: AIConfig) {
  if (config.provider === 'ollama') return Boolean(config.baseUrl.trim() && config.model.trim())
  return Boolean(config.baseUrl.trim() && config.model.trim() && config.apiKey.trim())
}

function endpoint(baseUrl: string) {
  const base = baseUrl.trim().replace(/\/+$/, '')
  return base.endsWith('/chat/completions') ? base : base + '/chat/completions'
}

function buildContext(
  target: any,
  targetTasks: any[],
  online: number,
  total: number,
  logs: any[],
  includeTasks: boolean,
  includeLogs: boolean,
) {
  const targetName = target?.a_computer || t('整个工作区')
  const targetState = target
    ? `${isAgentOnline(target) ? t('在线') : t('离线')} · ${target.a_name || 'agent'} · ${target.a_internal_ip || target.a_external_ip || '无地址'}`
    : '工作区级分析'
  const lines = [
    `目标: ${targetName}`,
    `目标状态: ${targetState}`,
    `工作区在线主机: ${online}/${total}`,
  ]
  if (includeTasks) {
    const taskText = targetTasks.slice(0, 8)
      .map((task) => `- [${task.a_completed ? t('已完成') : t('执行中')}] ${task.a_cmdline || '未命名任务'} -> ${(task.a_message || task.a_text || '暂无回显').slice(0, 160)}`)
      .join('\n')
    lines.push('目标任务:\n' + (taskText || t('暂无任务')))
  }
  if (includeLogs) {
    const eventText = logs.slice(0, 6)
      .map((log) => `- [${log.level || 'info'}] ${log.text || ''}`)
      .join('\n')
    lines.push('最近事件:\n' + (eventText || '暂无事件'))
  }
  return lines.join('\n')
}

function localReply(
  prompt: string,
  target: any,
  agentList: any[],
  targetTasks: any[],
  online: number,
  total: number,
  logs: any[],
  includeTasks: boolean,
  includeLogs: boolean,
) {
  const name = target?.a_computer || t('整个工作区')
  const state = target
    ? `${isAgentOnline(target) ? t('在线') : t('离线')} · ${target.a_name || 'agent'} (${target.a_os_desc || 'OS'}) · ${target.a_internal_ip || target.a_external_ip || '-'}`
    : `工作区在线 ${online}/${total} 台主机`
  const running = targetTasks.filter((task) => !task.a_completed)
  const failed = targetTasks.filter((task) => /err|fail|denied|timeout|失败|异常/i.test(String(task.a_message || task.a_text || '')))

  const sections: string[] = [
    `### ${name} · 上下文分析`,
    `- **范围**：${state}`,
    `- **问题**：${prompt}`,
  ]
  if (!target && agentList.length > 0) {
    const summaryRows = agentList.slice(0, 5).map((a) =>
      `${isAgentOnline(a) ? '[ONLINE]' : '[OFFLINE]'} ${a.a_computer || a.a_id.slice(0, 8)} (${a.a_name || '-'} · ${a.a_internal_ip || '-'})`,
    )
    sections.push('#### 在线主机快照\n```text\n' + summaryRows.join('\n') + '\n```')
  }
  if (includeTasks) {
    sections.push(`- **任务统计**：共 ${targetTasks.length} 条记录，执行中 **${running.length}** 条，异常/失败 **${failed.length}** 条`)
    if (failed.length) {
      sections.push('#### 异常线索\n' + failed.slice(0, 4).map((x) => `- \`${x.a_cmdline || '-'}\`：${String(x.a_message || x.a_text || '').slice(0, 120)}`).join('\n'))
    }
  }
  if (includeLogs && logs.length) {
    sections.push('#### 最近系统事件\n' + logs.slice(0, 4).map((l) => `- \`${(l.level || 'info').toUpperCase()}\` ${l.text || ''}`).join('\n'))
  }
  sections.push('> 当前运行在**本地上下文模式**。点右上角「AI 设置」接入 OpenAI / DeepSeek / Ollama 后即可基于以上实时上下文生成深度排查与命令建议。')
  return sections.join('\n\n')
}

async function ask(config: AIConfig, prompt: string, history: Message[], context: string, externalAbort?: AbortController) {
  const controller = externalAbort || new AbortController()
  const timer = window.setTimeout(() => controller.abort('timeout'), 45_000)
  try {
    const system = '你是 AdaptixC2 的实战协同助手。回答需精准、结构清晰、可执行；涉及命令时使用代码块并注明适用平台；先核对证据，不捏造未观察到的回显。'
      + (context ? '\n\n当前实时工作区上下文：\n' + context : '')
    const messages = [
      { role: 'system', content: system },
      ...history.filter((m) => m.role === 'user' || m.role === 'assistant').slice(-12).map((m) => ({ role: m.role, content: m.text })),
      { role: 'user', content: prompt },
    ]
    const headers: Record<string, string> = { 'Content-Type': 'application/json' }
    if (config.apiKey.trim()) headers.Authorization = 'Bearer ' + config.apiKey.trim()
    const response = await fetch(endpoint(config.baseUrl), {
      method: 'POST',
      headers,
      body: JSON.stringify({ model: config.model.trim(), messages, temperature: 0.2, stream: false }),
      signal: controller.signal,
    })
    const data = await response.json().catch(() => ({}))
    if (!response.ok) throw new Error(data?.error?.message || data?.message || 'HTTP ' + response.status)
    const text = data?.choices?.[0]?.message?.content || data?.choices?.[0]?.text || data?.message?.content
    if (!text) throw new Error('接口返回中没有可读取的回答内容')
    return String(text).trim()
  } finally {
    window.clearTimeout(timer)
  }
}

function renderInline(line: string): React.ReactNode {
  const parts = line.split(/(`[^`]+`|\*\*[^*]+\*\*)/g)
  return parts.map((p, idx) => {
    if (p.startsWith('`') && p.endsWith('`') && p.length >= 2) {
      return <code key={idx} className="qt-ai-inline-code">{p.slice(1, -1)}</code>
    }
    if (p.startsWith('**') && p.endsWith('**') && p.length >= 4) {
      return <strong key={idx}>{p.slice(2, -2)}</strong>
    }
    return <Fragment key={idx}>{p}</Fragment>
  })
}

function AiMarkdown({
  text,
  onCopy,
  onOpenConsole,
}: {
  text: string
  onCopy: (s: string) => void
  onOpenConsole?: () => void
}) {
  const blocks = useMemo(() => {
    const out: Array<{ kind: 'text'; body: string } | { kind: 'code'; lang: string; body: string }> = []
    const re = /```([a-zA-Z0-9_-]*)\n([\s\S]*?)```/g
    let last = 0
    let m: RegExpExecArray | null
    while ((m = re.exec(text)) !== null) {
      if (m.index > last) out.push({ kind: 'text', body: text.slice(last, m.index) })
      out.push({ kind: 'code', lang: m[1] || 'text', body: m[2].replace(/\n$/, '') })
      last = m.index + m[0].length
    }
    if (last < text.length) out.push({ kind: 'text', body: text.slice(last) })
    return out
  }, [text])

  return (
    <div className="qt-ai-md">
      {blocks.map((blk, i) => {
        if (blk.kind === 'code') {
          return (
            <div key={i} className="qt-ai-codeblock">
              <div className="qt-ai-code-hd">
                <span>{blk.lang}</span>
                <div style={{ display: 'inline-flex', gap: 6 }}>
                  {onOpenConsole && (
                    <button type="button" className="qt-ai-mini-btn" onClick={onOpenConsole}>
                      打开终端
                    </button>
                  )}
                  <button type="button" className="qt-ai-mini-btn" onClick={() => onCopy(blk.body)}>
                    复制代码
                  </button>
                </div>
              </div>
              <pre><code>{blk.body}</code></pre>
            </div>
          )
        }
        const lines = blk.body.split('\n')
        return (
          <div key={i} className="qt-ai-md-text">
            {lines.map((ln, li) => {
              const trimmed = ln.trim()
              if (!trimmed) return <div key={li} className="qt-ai-br" />
              if (trimmed.startsWith('#### ')) return <h5 key={li}>{renderInline(trimmed.slice(5))}</h5>
              if (trimmed.startsWith('### ')) return <h4 key={li}>{renderInline(trimmed.slice(4))}</h4>
              if (trimmed.startsWith('## ')) return <h3 key={li}>{renderInline(trimmed.slice(3))}</h3>
              if (trimmed.startsWith('> ')) return <blockquote key={li}>{renderInline(trimmed.slice(2))}</blockquote>
              if (/^[-*]\s+/.test(trimmed)) {
                return <div key={li} className="qt-ai-li"><span className="qt-ai-li-dot">•</span><span>{renderInline(trimmed.replace(/^[-*]\s+/, ''))}</span></div>
              }
              if (/^\d+\.\s+/.test(trimmed)) {
                const num = trimmed.match(/^(\d+\.)\s+/)?.[1] || '1.'
                return <div key={li} className="qt-ai-li"><span className="qt-ai-li-dot">{num}</span><span>{renderInline(trimmed.replace(/^\d+\.\s+/, ''))}</span></div>
              }
              return <p key={li}>{renderInline(ln)}</p>
            })}
          </div>
        )
      })}
    </div>
  )
}

function CopilotEmblem({ size = 16 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.9" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M12 3l2.1 5.4L19.5 10.5l-5.4 2.1L12 18l-2.1-5.4L4.5 10.5l5.4-2.1L12 3z" />
      <path d="M19 16l.9 2.1 2.1.9-2.1.9-.9 2.1-.9-2.1-2.1-.9 2.1-.9.9-2.1z" />
    </svg>
  )
}

export function AICollab({
  mode = 'modal',
  onToggleMode,
  onClose,
}: {
  mode?: 'modal' | 'drawer'
  onToggleMode?: () => void
  onClose?: () => void
} = {}) {
  const agents = useStore((state) => state.agents) || {}
  const tasks = useStore((state) => state.tasks) || []
  const logs = useStore((state) => state.logs) || []
  const selectedAgentId = useStore((state) => state.selectedAgentId)
  const selectAgent = useStore((state) => state.selectAgent)
  const openConsole = useStore((state) => state.openConsole)
  const openUiTab = useStore((state) => state.openUiTab)
  const pushToast = useStore((state) => state.pushToast)

  const onlineClock = useOnlineClock5s()
  void onlineClock

  const [threads, setThreads] = useState<ChatThread[]>(loadThreads)
  const [activeThreadId, setActiveThreadId] = useState<string>(() => loadThreads()[0]?.id || '')
  const [sideTab, setSideTab] = useState<'threads' | 'context'>('threads')
  const [targetId, setTargetId] = useState(selectedAgentId || '')
  const [draft, setDraft] = useState('')
  const [config, setConfig] = useState<AIConfig>(loadConfig)
  const [includeTasks, setIncludeTasks] = useState(true)
  const [includeLogs, setIncludeLogs] = useState(true)
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [showKey, setShowKey] = useState(false)
  const [sending, setSending] = useState(false)
  const [testing, setTesting] = useState(false)
  const [notice, setNotice] = useState('')
  const endRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLTextAreaElement>(null)
  const abortRef = useRef<AbortController | null>(null)

  const activeThread = useMemo(
    () => threads.find((th) => th.id === activeThreadId) || threads[0],
    [threads, activeThreadId],
  )
  const messages = activeThread?.messages || []

  const target = targetId ? agents[targetId] : null
  const targetLive = !!(target && isAgentOnline(target))
  const targetTasks = useMemo(
    () => (targetId ? tasks.filter((task) => task.a_id === targetId) : tasks).slice(0, 12),
    [tasks, targetId],
  )
  const agentList = useMemo(() => Object.values(agents), [agents])
  const online = agentList.filter((a) => isAgentOnline(a)).length
  const total = agentList.length
  const hasAI = configured(config)
  const runningCount = targetTasks.filter((task) => !task.a_completed).length

  const starterCards = useMemo(() => {
    if (target) {
      const host = target.a_computer || target.a_id.slice(0, 8)
      return [
        { title: '现状诊断与下一步', desc: `梳理 ${host} 权限、网络与推荐排查顺序`, prompt: `分析 ${host} 当前在线状态、上下文信息并给出下一步排查建议` },
        { title: '异常任务排查', desc: '聚合该主机近期失败/超时任务回显', prompt: `排查 ${host} 最近执行任务中的异常或错误输出` },
        { title: '心跳与健康评估', desc: '核对 Sleep / 抖动及进程上下文风险', prompt: `核对 ${host} 回连周期、当前进程及潜在稳定性问题` },
        { title: '生成战情简报', desc: '整理主机关键资产参数供团队交接', prompt: `整理 ${host} 的完整态势摘要` },
      ]
    }
    return [
      { title: '全局战情汇总', desc: `汇总 ${online}/${total} 在线主机与重点推进目标`, prompt: t('总结当前战情') },
      { title: '批量任务排障', desc: '扫描近期全部任务失败、超时与拒绝记录', prompt: t('整理最近任务中的异常') },
      { title: '资产优先级排序', desc: '按系统、权限与在线状态排序处置建议', prompt: '梳理当前全部在线主机并给出优先级建议' },
      { title: '事件流风险核查', desc: '对照系统日志检查上线/通道抖动线索', prompt: '结合近期系统事件分析是否存在回连或稳定性问题' },
    ]
  }, [target, online, total])

  useEffect(() => {
    try { localStorage.setItem(ROOMS_KEY, JSON.stringify(threads.slice(0, 12))) } catch { /* ignore */ }
  }, [threads])

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'auto', block: 'end' })
  }, [messages.length, sending])

  useEffect(() => {
    if (targetId && agents[targetId]) return
    if (selectedAgentId && agents[selectedAgentId]) setTargetId(selectedAgentId)
  }, [agents, selectedAgentId, targetId])

  useEffect(() => {
    if (!settingsOpen) return
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault()
        e.stopPropagation()
        setSettingsOpen(false)
      }
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [settingsOpen])

  function chooseTarget(value: string) {
    setTargetId(value)
    if (value) selectAgent(value)
  }

  function updateThreadMessages(threadId: string, updater: (prev: Message[]) => Message[], firstUserTitle?: string) {
    setThreads((prev) => prev.map((th) => {
      if (th.id !== threadId) return th
      const nextMsgs = updater(th.messages).slice(-80)
      const isDefault = th.title === '新对话' || th.title === '当前态势分析' || th.title === t('对战助手')
      return {
        ...th,
        title: firstUserTitle && isDefault ? firstUserTitle : th.title,
        targetId,
        updatedAt: Date.now(),
        messages: nextMsgs,
      }
    }))
  }

  function createThread() {
    const label = target ? (target.a_computer || target.a_id.slice(0, 8)) : '新对话'
    const nt = makeThread(targetId, label)
    setThreads((prev) => [nt, ...prev].slice(0, 12))
    setActiveThreadId(nt.id)
    setTimeout(() => inputRef.current?.focus(), 0)
  }

  function removeThread(tid: string) {
    setThreads((prev) => {
      if (prev.length <= 1) {
        const fresh = makeThread(targetId, '新对话')
        setActiveThreadId(fresh.id)
        return [fresh]
      }
      const next = prev.filter((x) => x.id !== tid)
      if (activeThreadId === tid) setActiveThreadId(next[0].id)
      return next
    })
  }

  function clearCurrentMessages() {
    if (!activeThread) return
    updateThreadMessages(activeThread.id, () => [])
  }

  function saveConfig() {
    try { localStorage.setItem(CONFIG_KEY, JSON.stringify(config)) } catch { /* ignore */ }
    setNotice('')
    setSettingsOpen(false)
    pushToast('ok', 'AI 模型配置已保存')
  }

  function clearConfig() {
    const empty = { provider: 'compatible', baseUrl: '', model: '', apiKey: '' }
    setConfig(empty)
    try { localStorage.removeItem(CONFIG_KEY) } catch { /* ignore */ }
    setNotice(t('已清除 AI 配置，恢复本地上下文模式。'))
  }

  async function testConnection() {
    if (!configured(config)) {
      setNotice('请先填写 API Base URL、模型名称（以及所需 API Key）。')
      return
    }
    setTesting(true)
    setNotice('正在测试模型接口连通性…')
    const t0 = performance.now()
    try {
      await ask(config, '只回复：OK', [], '')
      const ms = Math.round(performance.now() - t0)
      setNotice(`连接成功（延迟 ${ms} ms），可以开始协同分析。`)
    } catch (error: any) {
      setNotice('连接失败：' + (error?.message || '未知错误'))
    } finally {
      setTesting(false)
    }
  }

  function stopSending() {
    abortRef.current?.abort('user_stop')
    abortRef.current = null
    setSending(false)
  }

  async function send(text = draft) {
    const prompt = text.trim()
    if (!prompt || sending || !activeThread) return
    const tid = activeThread.id
    const userMessage: Message = { id: uid('user'), role: 'user', text: prompt, ts: Date.now() }
    const nextHistory = [...messages, userMessage]
    const autoTitle = prompt.length > 20 ? prompt.slice(0, 20) + '…' : prompt
    updateThreadMessages(tid, (prev) => [...prev, userMessage], autoTitle)
    setDraft('')
    setSending(true)
    const ctrl = new AbortController()
    abortRef.current = ctrl
    try {
      const ctxStr = buildContext(target, targetTasks, online, total, logs, includeTasks, includeLogs)
      const reply = hasAI
        ? await ask(config, prompt, nextHistory, ctxStr, ctrl)
        : localReply(prompt, target, agentList, targetTasks, online, total, logs, includeTasks, includeLogs)
      const assistantMsg: Message = { id: uid('assistant'), role: 'assistant', text: reply, ts: Date.now() }
      updateThreadMessages(tid, (prev) => [...prev, assistantMsg])
    } catch (error: any) {
      if (ctrl.signal.aborted && ctrl.signal.reason === 'user_stop') return
      const errMsg: Message = {
        id: uid('error'),
        role: 'error',
        text: 'AI 请求失败：' + (error?.message || '未知错误') + '\n\n请核对 Base URL、模型名与 Key；清空配置即可退回本地上下文模式。',
        ts: Date.now(),
      }
      updateThreadMessages(tid, (prev) => [...prev, errMsg])
    } finally {
      if (abortRef.current === ctrl) abortRef.current = null
      setSending(false)
    }
  }

  function onCopyBlock(s: string) {
    void copyText(s).then((ok) => pushToast(ok ? 'ok' : 'err', ok ? '已复制到剪贴板' : t('复制失败')))
  }

  function jumpToUi(id: 'chat' | 'tasks' | 'bofs') {
    if (mode === 'modal') onClose?.()
    openUiTab(id)
  }

  function openTargetConsole(id: string) {
    if (mode === 'modal') onClose?.()
    openConsole(id)
  }

  const lastUserPrompt = [...messages].reverse().find((m) => m.role === 'user')?.text || ''

  return (
    <div className={`qt-ai-shell ${mode}`}>
      {/* ── Window Header Bar ── */}
      <header className="qt-ai-hd">
        <div className="qt-ai-hd-left">
          <span className="qt-ai-emblem">
            <CopilotEmblem size={15} />
          </span>
          <h2>{t('对战助手')}</h2>
          <button
            type="button"
            className={`qt-ai-conn-pill ${hasAI ? 'on' : ''}`}
            onClick={() => setSettingsOpen(true)}
            title={hasAI ? `点击管理模型连接 (${config.baseUrl})` : '当前为本地上下文模式 · 点击接入 OpenAI / DeepSeek / Ollama'}
          >
            <span className={`qt-ai-dot ${hasAI ? 'on' : ''}`} />
            <span>{hasAI ? (config.model || config.provider) : '本地上下文 · 配置模型'}</span>
          </button>
        </div>

        <div className="qt-ai-hd-right">
          {mode === 'modal' && (
            <button type="button" className="qt-ai-btn" onClick={() => jumpToUi('chat')}>
              {t('团队聊天')}
            </button>
          )}
          <button type="button" className="qt-ai-btn" onClick={() => setSettingsOpen(true)}>
            {t('AI 设置')}
          </button>
          {onToggleMode && (
            <button
              type="button"
              className="qt-ai-btn"
              onClick={onToggleMode}
              title={mode === 'modal' ? '停靠到屏幕右侧，一边看主机/终端一边问 AI' : '展开为居中大窗 Copilot 视图'}
            >
              {mode === 'modal' ? '◧ 贴靠右侧' : '⤢ 大窗模式'}
            </button>
          )}
          {onClose && (
            <button type="button" className="qt-ai-close-btn" title="关闭 (Esc)" onClick={onClose}>
              ×
            </button>
          )}
        </div>
      </header>

      {mode === 'drawer' && (
        <div className="qt-ai-drawer-bar">
          <div className="qt-ai-drawer-threads">
            <select
              className="qt-ai-drawer-select"
              aria-label="切换对话"
              value={activeThread?.id || ''}
              onChange={(e) => setActiveThreadId(e.target.value)}
            >
              {threads.map((th) => (
                <option key={th.id} value={th.id}>
                  {th.title} ({th.messages.length})
                </option>
              ))}
            </select>
            <button type="button" className="qt-ai-mini-btn" onClick={createThread} title="新建独立对话">
              + 新对话
            </button>
          </div>
          <div className="qt-ai-drawer-links">
            {target && (
              <button type="button" className="qt-ai-mini-btn" onClick={() => openTargetConsole(target.a_id)}>
                终端
              </button>
            )}
            <button type="button" className="qt-ai-mini-btn" onClick={() => jumpToUi('tasks')}>任务</button>
            <button type="button" className="qt-ai-mini-btn" onClick={() => jumpToUi('bofs')}>BOF</button>
            <button type="button" className="qt-ai-mini-btn" onClick={() => jumpToUi('chat')}>{t('团队聊天')}</button>
          </div>
        </div>
      )}

      {/* ── Studio Workspace Body ── */}
      <div className="qt-ai-body">
        {mode === 'modal' && (
          <aside className="qt-ai-side">
            <div className="qt-ai-side-tabs" role="tablist">
              <button
                type="button"
                className={`qt-ai-side-tab ${sideTab === 'threads' ? 'active' : ''}`}
                onClick={() => setSideTab('threads')}
              >
                对话 ({threads.length})
              </button>
              <button
                type="button"
                className={`qt-ai-side-tab ${sideTab === 'context' ? 'active' : ''}`}
                onClick={() => setSideTab('context')}
              >
                实时上下文
              </button>
            </div>

            {sideTab === 'threads' ? (
              <div className="qt-ai-side-panel">
                <button type="button" className="qt-ai-new-btn" onClick={createThread}>
                  + 新建对话
                </button>
                <div className="qt-ai-thread-list">
                  {threads.map((th) => {
                    const isCur = th.id === activeThread?.id
                    return (
                      <div key={th.id} className={`qt-ai-thread-row ${isCur ? 'active' : ''}`}>
                        <button type="button" className="qt-ai-thread-btn" onClick={() => setActiveThreadId(th.id)}>
                          <span className="qt-ai-thread-title">{th.title}</span>
                          <span className="qt-ai-thread-time">{fmtRelative(th.updatedAt)}</span>
                        </button>
                        {threads.length > 1 && (
                          <button
                            type="button"
                            className="qt-ai-thread-del"
                            title="删除会话"
                            onClick={() => removeThread(th.id)}
                          >
                            ×
                          </button>
                        )}
                      </div>
                    )
                  })}
                </div>

                <div className="qt-ai-target-foot">
                  <div className="qt-ai-target-foot-top">
                    <span className={`qt-ai-dot ${target ? (targetLive ? 'on' : '') : (online > 0 ? 'on' : '')}`} />
                    <b className="mono">{target ? (target.a_computer || target.a_id.slice(0, 8)) : `工作区 (${online}/${total})`}</b>
                  </div>
                  <div className="qt-ai-target-foot-sub">
                    {target
                      ? `${target.a_name || '-'} · ${target.a_internal_ip || target.a_external_ip || '-'} · 进行中 ${runningCount}`
                      : `任务 ${targetTasks.length} · 事件 ${logs.length}`}
                  </div>
                  <div className="qt-ai-target-btns">
                    {target && (
                      <button type="button" className="qt-ai-mini-btn" onClick={() => openTargetConsole(target.a_id)}>终端</button>
                    )}
                    <button type="button" className="qt-ai-mini-btn" onClick={() => jumpToUi('tasks')}>任务</button>
                    <button type="button" className="qt-ai-mini-btn" onClick={() => jumpToUi('bofs')}>BOF</button>
                  </div>
                </div>
              </div>
            ) : (
              <div className="qt-ai-side-panel qt-ai-ctx-scroll">
                <div className="qt-ai-ctx-blk">
                  <div className="qt-ai-ctx-lbl">目标作用域</div>
                  <select className="qt-ai-select" value={targetId} onChange={(e) => chooseTarget(e.target.value)}>
                    <option value="">{t('整个工作区')} ({online}/{total} 在线)</option>
                    {agentList.map((a) => (
                      <option key={a.a_id} value={a.a_id}>
                        {isAgentOnline(a) ? '● ' : '○ '}{a.a_computer || a.a_id} ({a.a_name || 'agent'})
                      </option>
                    ))}
                  </select>
                  <div className="qt-ai-ctx-switches">
                    <label className="qt-ai-chk">
                      <input type="checkbox" checked={includeTasks} onChange={(e) => setIncludeTasks(e.target.checked)} />
                      <span>附带任务 ({targetTasks.length})</span>
                    </label>
                    <label className="qt-ai-chk">
                      <input type="checkbox" checked={includeLogs} onChange={(e) => setIncludeLogs(e.target.checked)} />
                      <span>附带事件 ({logs.length})</span>
                    </label>
                  </div>
                </div>

                <div className="qt-ai-ctx-blk">
                  <div className="qt-ai-ctx-lbl">近期任务（点击填入提问）</div>
                  {targetTasks.length === 0 ? (
                    <div className="qt-ai-empty-sub">{t('暂无任务')}</div>
                  ) : (
                    <div className="qt-ai-feed">
                      {targetTasks.slice(0, 6).map((task) => {
                        const cmd = task.a_cmdline || '未命名任务'
                        const done = !!task.a_completed
                        return (
                          <button
                            key={task.a_task_id || cmd + task.a_start_time}
                            type="button"
                            className="qt-ai-task-btn"
                            onClick={() => {
                              setDraft(`请分析任务「${cmd}」的执行结果与异常：\n回显：${String(task.a_message || task.a_text || '无').slice(0, 220)}`)
                              inputRef.current?.focus()
                            }}
                          >
                            <span className={`qt-ai-dot ${done ? 'on' : 'warn'}`} />
                            <span className="qt-ai-task-cmd mono">{cmd}</span>
                            <span className="qt-ai-task-st">{done ? '完成' : '进行'}</span>
                          </button>
                        )
                      })}
                    </div>
                  )}
                </div>

                <div className="qt-ai-ctx-blk">
                  <div className="qt-ai-ctx-lbl">最近事件</div>
                  {logs.length === 0 ? (
                    <div className="qt-ai-empty-sub">暂无事件</div>
                  ) : (
                    <div className="qt-ai-feed">
                      {logs.slice(0, 5).map((l, idx) => (
                        <div key={idx} className="qt-ai-ev-row">
                          <span className={`qt-ai-ev-lvl ${l.level || 'info'}`}>{(l.level || 'info').toUpperCase()}</span>
                          <span className="qt-ai-ev-txt">{l.text || ''}</span>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              </div>
            )}
          </aside>
        )}

        {/* Main Conversation Stage */}
        <main className="qt-ai-stage">
          <div className="qt-ai-scroll">
            {messages.length === 0 && !sending ? (
              <div className="qt-ai-hero">
                <div className="qt-ai-hero-icon">
                  <CopilotEmblem size={24} />
                </div>
                <h3>围绕「{target ? (target.a_computer || target.a_id.slice(0, 8)) : '当前工作区'}」开始协同分析</h3>
                <p>
                  {hasAI
                    ? `已就绪外部模型 ${config.model} · 自动附带在线主机、任务与事件实时上下文`
                    : '当前工作在本地上下文模式 · 无外部请求，随时可接入 OpenAI / DeepSeek / Ollama'}
                </p>
                <div className="qt-ai-hero-stats">
                  <span><b>{online}</b>/{total} 在线主机</span>
                  <i>·</i>
                  <span><b>{targetTasks.length}</b> 条任务 ({runningCount} 运行中)</span>
                  <i>·</i>
                  <span><b>{logs.length}</b> 条事件</span>
                </div>
                <div className="qt-ai-starter-grid">
                  {starterCards.map((c) => (
                    <button
                      key={c.title}
                      type="button"
                      className="qt-ai-starter-card"
                      onClick={() => void send(c.prompt)}
                    >
                      <b>{c.title}</b>
                      <span>{c.desc}</span>
                    </button>
                  ))}
                </div>
              </div>
            ) : (
              <div className="qt-ai-stream">
                {messages.map((message) => {
                  if (message.role === 'user') {
                    return (
                      <div key={message.id} className="qt-ai-msg-user-wrap">
                        <div className="qt-ai-msg-user" title={fmtTime(message.ts)}>
                          <div className="qt-ai-plain-text">{message.text}</div>
                        </div>
                      </div>
                    )
                  }
                  if (message.role === 'error') {
                    return (
                      <article key={message.id} className="qt-ai-msg-err">
                        <div className="qt-ai-msg-meta">
                          <span className="qt-ai-avatar error">!</span>
                          <b>模型调用失败</b>
                          <span className="qt-ai-msg-time mono">{fmtTimeShort(message.ts)}</span>
                          <span style={{ flex: 1 }} />
                          <button type="button" className="qt-ai-mini-btn" onClick={() => setSettingsOpen(true)}>
                            检查设置
                          </button>
                          {lastUserPrompt && (
                            <button type="button" className="qt-ai-mini-btn" onClick={() => void send(lastUserPrompt)}>
                              重试
                            </button>
                          )}
                        </div>
                        <div className="qt-ai-plain-text">{message.text}</div>
                      </article>
                    )
                  }
                  return (
                    <article key={message.id} className="qt-ai-msg-ai">
                      <div className="qt-ai-msg-meta">
                        <span className="qt-ai-avatar">AI</span>
                        <b>{t('对战助手')}</b>
                        <span className="qt-ai-msg-time mono">{fmtTimeShort(message.ts)}</span>
                        <span style={{ flex: 1 }} />
                        <button type="button" className="qt-ai-mini-btn" onClick={() => onCopyBlock(message.text)}>
                          复制全文
                        </button>
                      </div>
                      <AiMarkdown
                        text={message.text}
                        onCopy={onCopyBlock}
                        onOpenConsole={target ? () => openTargetConsole(target.a_id) : undefined}
                      />
                    </article>
                  )
                })}

                {sending && (
                  <div className="qt-ai-thinking">
                    <i />
                    <span>{t('正在分析当前上下文…')}</span>
                    <button type="button" className="qt-ai-mini-btn" onClick={stopSending}>停止生成</button>
                  </div>
                )}
                <div ref={endRef} />
              </div>
            )}
          </div>

          {/* Unified Grounded Composer Card */}
          <div className="qt-ai-composer-wrap">
            <div className="qt-ai-composer-card">
              <textarea
                ref={inputRef}
                value={draft}
                placeholder={target ? `围绕 ${target.a_computer || target.a_id} 提问或粘贴回显…` : t('输入你要分析的目标、异常或下一步…')}
                onChange={(e) => setDraft(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter' && !e.shiftKey) {
                    e.preventDefault()
                    void send()
                  }
                }}
              />
              <div className="qt-ai-composer-bar">
                <select
                  className="qt-ai-target-pill"
                  value={targetId}
                  onChange={(e) => chooseTarget(e.target.value)}
                  title="切换对话上下文作用域"
                >
                  <option value="">@ 全工作区 ({online}/{total})</option>
                  {agentList.map((a) => (
                    <option key={a.a_id} value={a.a_id}>
                      @ {a.a_computer || a.a_id.slice(0, 8)} {isAgentOnline(a) ? '●' : '○'}
                    </option>
                  ))}
                </select>

                <button
                  type="button"
                  className={`qt-ai-ctx-pill ${includeTasks ? 'on' : ''}`}
                  onClick={() => setIncludeTasks((v) => !v)}
                  title="切换是否附带最近任务上下文"
                >
                  任务 {targetTasks.length}
                </button>
                <button
                  type="button"
                  className={`qt-ai-ctx-pill ${includeLogs ? 'on' : ''}`}
                  onClick={() => setIncludeLogs((v) => !v)}
                  title="切换是否附带系统事件上下文"
                >
                  事件 {logs.length}
                </button>

                <span style={{ flex: 1 }} />

                {messages.length > 0 && (
                  <button type="button" className="qt-ai-mini-btn" onClick={clearCurrentMessages} title={t('清空对话')}>
                    清空
                  </button>
                )}

                {sending ? (
                  <button type="button" className="qt-ai-btn danger" onClick={stopSending}>停止</button>
                ) : (
                  <button
                    type="button"
                    className="qt-ai-send-btn"
                    disabled={!draft.trim()}
                    onClick={() => void send()}
                  >
                    <span>{t('发送')}</span>
                    <span className="mono" style={{ opacity: 0.8 }}>↵</span>
                  </button>
                )}
              </div>
            </div>
          </div>
        </main>
      </div>

      {/* ── Model Connection Settings Modal ── */}
      {settingsOpen && (
        <div
          className="qt-ai-modal-mask"
          onClick={(e) => { if (e.target === e.currentTarget) setSettingsOpen(false) }}
        >
          <section className="qt-ai-modal" role="dialog" aria-modal="true" aria-labelledby="ai-settings-title">
            <div className="qt-ai-modal-hd">
              <div>
                <span className="qt-ai-kicker">MODEL CONNECTION</span>
                <h3 id="ai-settings-title">{t('AI 设置')}</h3>
              </div>
              <button type="button" className="qt-ai-close-btn" onClick={() => setSettingsOpen(false)}>×</button>
            </div>

            <div className="qt-ai-modal-bd">
              <p className="qt-ai-modal-intro">
                {t('填写后，点击发送才会把当前对话和工作区上下文发给该接口。配置只保存在当前浏览器。')}
              </p>

              <div className="qt-ai-prov-grid">
                {PROVIDERS.map((p) => (
                  <button
                    key={p.id}
                    type="button"
                    className={`qt-ai-prov-card ${config.provider === p.id ? 'active' : ''}`}
                    onClick={() => setConfig((cur) => ({
                      ...cur,
                      provider: p.id,
                      baseUrl: p.baseUrl || cur.baseUrl,
                      model: p.model || cur.model,
                    }))}
                  >
                    <b>{p.label}</b>
                    <span>{p.desc}</span>
                  </button>
                ))}
              </div>

              <div className="qt-ai-form-grid">
                <label className="qt-ai-field">
                  <span>API Base URL</span>
                  <input
                    value={config.baseUrl}
                    onChange={(e) => setConfig((cur) => ({ ...cur, baseUrl: e.target.value }))}
                    placeholder="https://api.openai.com/v1"
                  />
                </label>
                <label className="qt-ai-field">
                  <span>{t('模型')}</span>
                  <input
                    value={config.model}
                    onChange={(e) => setConfig((cur) => ({ ...cur, model: e.target.value }))}
                    placeholder="gpt-4o-mini / deepseek-chat"
                  />
                </label>
              </div>

              <label className="qt-ai-field">
                <span>API Key {config.provider === 'ollama' ? '(Ollama 本地可留空)' : ''}</span>
                <div className="qt-ai-key-row">
                  <input
                    type={showKey ? 'text' : 'password'}
                    value={config.apiKey}
                    onChange={(e) => setConfig((cur) => ({ ...cur, apiKey: e.target.value }))}
                    placeholder="sk-…"
                    autoComplete="off"
                  />
                  <button type="button" className="qt-ai-btn" onClick={() => setShowKey((v) => !v)}>
                    {showKey ? t('隐藏') : t('显示')}
                  </button>
                </div>
              </label>

              <div className="qt-ai-modal-hint">
                {t('支持 OpenAI 兼容的')} <code>/chat/completions</code> {t('接口。API Key 会存入浏览器 localStorage；共享电脑请使用短期 Key。')}
              </div>

              {notice && <div className="qt-ai-modal-status">{notice}</div>}
            </div>

            <div className="qt-ai-modal-ft">
              <button type="button" className="qt-ai-btn" onClick={clearConfig}>{t('清除配置')}</button>
              <span style={{ flex: 1 }} />
              <button type="button" className="qt-ai-btn" disabled={testing} onClick={() => void testConnection()}>
                {testing ? '测试中…' : '测试连接'}
              </button>
              <button type="button" className="qt-ai-send-btn" onClick={saveConfig}>{t('保存配置')}</button>
            </div>
          </section>
        </div>
      )}
    </div>
  )
}

import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { fmtTime, isAgentOnline } from '../utils'

type Message = { id: string; role: 'assistant' | 'user' | 'error'; text: string; ts: number }
type AIConfig = { provider: string; baseUrl: string; model: string; apiKey: string }

const ROOMS_KEY = 'adaptix.ai.battle.messages.v1'
const CONFIG_KEY = 'adaptix.ai.config.v1'
const PROVIDERS = [
  { id: 'openai', label: 'OpenAI', baseUrl: 'https://api.openai.com/v1', model: 'gpt-4o-mini' },
  { id: 'deepseek', label: 'DeepSeek', baseUrl: 'https://api.deepseek.com/v1', model: 'deepseek-chat' },
  { id: 'ollama', label: 'Ollama（本地）', baseUrl: 'http://127.0.0.1:11434/v1', model: 'llama3.1' },
  { id: 'compatible', label: 'OpenAI 兼容接口', baseUrl: '', model: '' },
]
const SUGGESTIONS = ['总结当前战情', '分析目标主机的下一步', '整理最近任务中的异常', '检查 Linux Agent 上线问题']

function id(prefix: string) { return prefix + '-' + Date.now() + '-' + Math.random().toString(36).slice(2, 7) }
function welcome(): Message {
  return { id: 'welcome', role: 'assistant', ts: Date.now(), text: '我是对战助手。选择目标主机后，直接告诉我你想分析什么。未配置 API 时，我只读取当前工作区的主机、任务和事件；配置后，发送消息才会调用你填写的模型。' }
}
function loadMessages(): Message[] {
  try {
    const parsed = JSON.parse(localStorage.getItem(ROOMS_KEY) || 'null')
    const saved = Array.isArray(parsed) ? parsed.find((room) => Array.isArray(room?.messages))?.messages : null
    if (Array.isArray(saved) && saved.length) return saved.map((message: any) => ({
      id: String(message.id || id('msg')),
      role: message.role === 'user' ? 'user' : message.role === 'error' ? 'error' : 'assistant',
      text: String(message.text || ''),
      ts: Number(message.ts) || Date.now(),
    }))
  } catch { /* ignore */ }
  return [welcome()]
}
function loadConfig(): AIConfig {
  try {
    const saved = JSON.parse(localStorage.getItem(CONFIG_KEY) || 'null')
    if (saved && typeof saved === 'object') return {
      provider: String(saved.provider || 'compatible'),
      baseUrl: String(saved.baseUrl || ''),
      model: String(saved.model || ''),
      apiKey: String(saved.apiKey || ''),
    }
  } catch { /* ignore */ }
  return { provider: 'compatible', baseUrl: '', model: '', apiKey: '' }
}
function configured(config: AIConfig) { return Boolean(config.baseUrl.trim() && config.model.trim() && config.apiKey.trim()) }
function endpoint(baseUrl: string) {
  const base = baseUrl.trim().replace(/\/+$/, '')
  return base.endsWith('/chat/completions') ? base : base + '/chat/completions'
}
function contextOf(target: any, targetTasks: any[], online: number, total: number, logs: any[]) {
  const targetName = target?.a_computer || '整个工作区'
  const targetState = target
    ? (isAgentOnline(target) ? '在线' : '离线') + ' · ' + (target.a_name || '未知 agent') + ' · ' + (target.a_internal_ip || target.a_external_ip || '无地址')
    : '工作区级分析'
  const taskText = targetTasks.slice(0, 6).map((task) => '- ' + (task.a_cmdline || '未命名任务') + '：' + (task.a_completed ? '已完成' : '执行中') + '，' + (task.a_message || task.a_text || '暂无回显')).join('\n')
  const eventText = logs.slice(0, 5).map((log) => '- [' + (log.level || 'info') + '] ' + (log.text || '')).join('\n')
  return '目标：' + targetName + '\n目标状态：' + targetState + '\n工作区在线主机：' + online + '/' + total + '\n目标任务：\n' + (taskText || '暂无任务') + '\n最近事件：\n' + (eventText || '暂无事件')
}
function localReply(target: any, targetTasks: any[], online: number, total: number, logs: any[]) {
  const name = target?.a_computer || '整个工作区'
  const state = target ? (isAgentOnline(target) ? '在线' : '离线') + ' · ' + (target.a_name || '未知 agent') : '工作区级分析'
  const running = targetTasks.filter((task) => !task.a_completed).length
  return '当前目标：' + name + '（' + state + '）\n工作区状态：' + online + '/' + total + ' 台在线，' + logs.length + ' 条事件。\n目标任务：' + targetTasks.length + ' 条，' + running + ' 条仍在执行。\n\n建议先核对最近一次回连、任务回显和 agent 类型，再决定下一步。命令下发前请确认目标和影响范围。\n\n当前未配置外部 AI，以上内容来自本地工作区数据。'
}
async function ask(config: AIConfig, prompt: string, history: Message[], context: string) {
  const controller = new AbortController()
  const timer = window.setTimeout(() => controller.abort(), 45_000)
  try {
    const system = '你是 AdaptixC2 的对战助手。回答简洁、具体、可执行；先分析证据，再给出下一步；不要自动执行命令，不要把建议描述成已经完成。' + (context ? '\n\n当前工作区上下文：\n' + context : '')
    const messages = [
      { role: 'system', content: system },
      ...history.filter((message) => message.role === 'user' || message.role === 'assistant').slice(-12).map((message) => ({ role: message.role, content: message.text })),
      { role: 'user', content: prompt },
    ]
    const response = await fetch(endpoint(config.baseUrl), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + config.apiKey.trim() },
      body: JSON.stringify({ model: config.model.trim(), messages, temperature: 0.2, stream: false }),
      signal: controller.signal,
    })
    const data = await response.json().catch(() => ({}))
    if (!response.ok) throw new Error(data?.error?.message || data?.message || 'HTTP ' + response.status)
    const text = data?.choices?.[0]?.message?.content || data?.choices?.[0]?.text || data?.message?.content
    if (!text) throw new Error('接口返回中没有可读取的回答内容')
    return String(text).trim()
  } finally { window.clearTimeout(timer) }
}

export function AICollab() {
  const agents = useStore((state) => state.agents) || {}
  const tasks = useStore((state) => state.tasks) || []
  const logs = useStore((state) => state.logs) || []
  const selectedAgentId = useStore((state) => state.selectedAgentId)
  const selectAgent = useStore((state) => state.selectAgent)
  const openConsole = useStore((state) => state.openConsole)
  const openUiTab = useStore((state) => state.openUiTab)
  const [messages, setMessages] = useState<Message[]>(loadMessages)
  const [targetId, setTargetId] = useState(selectedAgentId || '')
  const [draft, setDraft] = useState('')
  const [config, setConfig] = useState<AIConfig>(loadConfig)
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [showKey, setShowKey] = useState(false)
  const [sending, setSending] = useState(false)
  const [testing, setTesting] = useState(false)
  const [notice, setNotice] = useState('')
  const endRef = useRef<HTMLDivElement>(null)
  const target = targetId ? agents[targetId] : null
  const targetTasks = useMemo(() => targetId ? tasks.filter((task) => task.a_id === targetId).slice(0, 12) : tasks.slice(0, 12), [tasks, targetId])
  const online = Object.values(agents).filter((agent) => isAgentOnline(agent)).length
  const total = Object.keys(agents).length
  const hasAI = configured(config)
  const mode = hasAI ? (config.provider === 'compatible' ? 'AI' : config.provider.toUpperCase()) + ' · ' + config.model : 'LOCAL CONTEXT'

  useEffect(() => {
    try { localStorage.setItem(ROOMS_KEY, JSON.stringify([{ id: 'battle', title: '对战助手', summary: '单一对话上下文', createdAt: Date.now(), messages: messages.slice(-80) }])) } catch { /* ignore */ }
    endRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages])
  useEffect(() => {
    if (targetId && agents[targetId]) return
    if (selectedAgentId && agents[selectedAgentId]) setTargetId(selectedAgentId)
    else setTargetId(Object.keys(agents)[0] || '')
  }, [agents, selectedAgentId, targetId])

  function chooseTarget(value: string) { setTargetId(value); if (value) selectAgent(value) }
  function saveConfig() {
    try { localStorage.setItem(CONFIG_KEY, JSON.stringify(config)) } catch { /* ignore */ }
    setNotice('AI 配置已保存，仅保存在当前浏览器。')
    setSettingsOpen(false)
  }
  function clearConfig() {
    const empty = { provider: 'compatible', baseUrl: '', model: '', apiKey: '' }
    setConfig(empty)
    try { localStorage.removeItem(CONFIG_KEY) } catch { /* ignore */ }
    setNotice('已清除 AI 配置，恢复本地上下文模式。')
  }
  async function testConnection() {
    if (!configured(config)) { setNotice('请先填写 API Base URL、模型和 API Key。'); return }
    setTesting(true); setNotice('正在测试连接…')
    try { await ask(config, '请只回复“连接成功”。', [], ''); setNotice('连接成功，可以开始使用 AI。') }
    catch (error: any) { setNotice('连接失败：' + (error?.message || '未知错误')) }
    finally { setTesting(false) }
  }
  async function send(text = draft) {
    const prompt = text.trim()
    if (!prompt || sending) return
    const userMessage: Message = { id: id('user'), role: 'user', text: prompt, ts: Date.now() }
    const next = [...messages, userMessage]
    setMessages(next); setDraft(''); setSending(true)
    try {
      const reply = hasAI ? await ask(config, prompt, next, contextOf(target, targetTasks, online, total, logs)) : localReply(target, targetTasks, online, total, logs)
      setMessages((current) => [...current, { id: id('assistant'), role: 'assistant', text: reply, ts: Date.now() }].slice(-80))
    } catch (error: any) {
      setMessages((current) => [...current, { id: id('error'), role: 'error', text: 'AI 请求失败：' + (error?.message || '未知错误') + '\n\n请检查 API 地址、模型和 Key；清空 AI 配置即可恢复本地模式。', ts: Date.now() }].slice(-80))
    } finally { setSending(false) }
  }

  return (
    <div className="ai-page ai-battle-page">
      <header className="ai-header">
        <div className="ai-title-block"><span className="ai-kicker">AI BATTLE ASSISTANT</span><h2>对战助手</h2><p>围绕一个目标主机分析现状、任务和下一步。</p></div>
        <div className="ai-header-actions">
          <span className={'ai-mode ' + (hasAI ? 'configured' : '')}><i /> {mode}</span>
          <button className="ghost" onClick={() => openUiTab('chat')}>团队聊天</button>
          <button className="ghost" onClick={() => setSettingsOpen(true)}>AI 设置</button>
          <button className="ghost" onClick={() => setMessages([welcome()])}>清空对话</button>
        </div>
      </header>
      <main className="ai-battle-main">
        <div className="ai-battle-context">
          <label className="ai-target-select"><span>目标主机</span><select value={targetId} onChange={(event) => chooseTarget(event.target.value)}><option value="">整个工作区</option>{Object.values(agents).map((agent) => <option key={agent.a_id} value={agent.a_id}>{agent.a_computer || agent.a_id}</option>)}</select></label>
          <span className="ai-context-stat"><b>{target ? (isAgentOnline(target) ? '在线' : '离线') : online + '/' + total}</b>{target ? ' · ' + (target.a_name || 'agent') : ' 台在线'}</span>
          <span className="ai-context-stat"><b>{targetTasks.filter((task) => !task.a_completed).length}</b> 个任务执行中</span>
          <span className="ai-context-stat"><b>{logs.length}</b> 条事件</span>
          {target && <button className="ghost ai-target-action" onClick={() => openConsole(target.a_id)}>打开终端 →</button>}
          <button className="ghost ai-target-action" onClick={() => openUiTab('tasks')}>任务中心 →</button>
        </div>
        <section className="ai-assistant-card">
          <div className="ai-assistant-head"><div><b>对战助手</b><span>{hasAI ? '已连接外部模型' : '本地上下文模式'}</span></div>{notice && <span className="ai-notice">{notice}</span>}</div>
          <div className="ai-message-list">
            {messages.map((message) => <article key={message.id} className={'ai-message ' + message.role}><div className="ai-message-avatar">{message.role === 'user' ? '你' : message.role === 'error' ? '!' : 'AI'}</div><div className="ai-message-body"><div className="ai-message-meta"><b>{message.role === 'user' ? '你' : message.role === 'error' ? 'AI 请求' : '对战助手'}</b><span>{fmtTime(message.ts)}</span></div><div className="ai-message-text">{message.text}</div></div></article>)}
            {sending && <div className="ai-thinking"><i /> 正在分析当前上下文…</div>}
            <div ref={endRef} />
          </div>
          <div className="ai-suggestions">{SUGGESTIONS.map((item) => <button key={item} disabled={sending} onClick={() => void send(item)}>{item}</button>)}</div>
          <div className="ai-composer"><textarea value={draft} placeholder="输入你要分析的目标、异常或下一步…" onChange={(event) => setDraft(event.target.value)} onKeyDown={(event) => { if (event.key === 'Enter' && !event.shiftKey) { event.preventDefault(); void send() } }} /><div className="ai-composer-foot"><span>Enter 发送 · Shift+Enter 换行 · {hasAI ? '会调用已配置模型' : '仅使用本地数据'}</span><button className="primary" disabled={!draft.trim() || sending} onClick={() => void send()}>{sending ? '分析中…' : '发送'}</button></div></div>
        </section>
        <p className="ai-safety-note">助手只提供分析和建议，命令执行仍需你在终端或任务中心确认。</p>
      </main>
      {settingsOpen && <div className="ai-settings-backdrop" onClick={(event) => { if (event.target === event.currentTarget) setSettingsOpen(false) }}><section className="ai-settings" role="dialog" aria-modal="true" aria-labelledby="ai-settings-title">
        <div className="ai-settings-head"><div><span className="ai-kicker">MODEL CONNECTION</span><h3 id="ai-settings-title">AI 设置</h3></div><button className="ghost" onClick={() => setSettingsOpen(false)}>关闭</button></div>
        <p className="ai-settings-intro">填写后，点击发送才会把当前对话和工作区上下文发给该接口。配置只保存在当前浏览器。</p>
        <label>服务类型<select value={config.provider} onChange={(event) => { const provider = PROVIDERS.find((item) => item.id === event.target.value); setConfig((current) => ({ ...current, provider: event.target.value, baseUrl: provider?.baseUrl || '', model: provider?.model || '' })) }}>{PROVIDERS.map((provider) => <option key={provider.id} value={provider.id}>{provider.label}</option>)}</select></label>
        <label>API Base URL<input value={config.baseUrl} onChange={(event) => setConfig((current) => ({ ...current, baseUrl: event.target.value }))} placeholder="https://api.openai.com/v1" /></label>
        <label>模型<input value={config.model} onChange={(event) => setConfig((current) => ({ ...current, model: event.target.value }))} placeholder="gpt-4o-mini / deepseek-chat" /></label>
        <label>API Key<div className="ai-key-field"><input type={showKey ? 'text' : 'password'} value={config.apiKey} onChange={(event) => setConfig((current) => ({ ...current, apiKey: event.target.value }))} placeholder="sk-…" autoComplete="off" /><button className="ghost" onClick={() => setShowKey((value) => !value)}>{showKey ? '隐藏' : '显示'}</button></div></label>
        <div className="ai-settings-hint">支持 OpenAI 兼容的 <span className="mono">/chat/completions</span> 接口。API Key 会存入浏览器 localStorage；共享电脑请使用短期 Key。</div>
        {notice && <div className="ai-settings-status">{notice}</div>}
        <div className="ai-settings-actions"><button className="ghost" onClick={clearConfig}>清除配置</button><button className="ghost" disabled={testing} onClick={() => void testConnection()}>{testing ? '测试中…' : '测试连接'}</button><button className="primary" onClick={saveConfig}>保存配置</button></div>
      </section></div>}
    </div>
  )
}

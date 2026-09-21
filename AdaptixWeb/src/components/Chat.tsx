import { useEffect, useMemo, useRef, useState } from 'react'
import { useStore } from '../store'
import { fmtTime } from '../utils'
import { PageTips } from './PageTips'

export function Chat() {
  const api = useStore((s) => s.api)
  const username = useStore((s) => s.username)
  const messages = useStore((s) => s.chatMessages) || []
  const pushLog = useStore((s) => s.pushLog)
  const pushToast = useStore((s) => s.pushToast)
  const [text, setText] = useState('')
  const [busy, setBusy] = useState(false)
  const endRef = useRef<HTMLDivElement>(null)

  const sorted = useMemo(
    () => [...messages].sort((a, b) => (a.date || 0) - (b.date || 0)),
    [messages],
  )

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [sorted.length])

  async function send() {
    const msg = text.trim()
    if (!msg || busy) return
    setBusy(true)
    try {
      await api.chatSend(msg)
      // 乐观显示：即使 WS 未回包也能看到自己发的消息（store 会去重）
      useStore.getState().pushChat({
        username: username || 'me',
        message: msg,
        date: Math.floor(Date.now() / 1000),
      })
      setText('')
    } catch (e: any) {
      pushLog('err', e.message || String(e))
      pushToast('err', e.message || String(e))
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="page chat-page">
      <div className="toolbar">
        <strong>团队聊天</strong>
        <span className="dim">{sorted.length} 条 · 当前 {username || '-'}</span>
        <div className="stats"><span className="dim">sync chat_history / chat_realtime</span></div>
        <PageTips id="chat">
          <p>团队实时聊天，消息经团队服务器广播给所有在线操作员。</p>
          <p>Enter 发送；自己的消息靠右显示。</p>
        </PageTips>
      </div>
      <div className="chat-body">
        {sorted.length === 0 ? (
          <div className="empty">
            <div className="big">💬</div>
            <div>暂无消息</div>
            <div className="dim" style={{ marginTop: 6 }}>消息经 teamserver 广播给所有在线操作员</div>
          </div>
        ) : (
          <div className="chat-list">
            {sorted.map((m) => {
              const mine = m.username === username
              return (
                <div key={m.id} className={`chat-row ${mine ? 'mine' : ''}`}>
                  <div className="chat-meta">
                    <b>{m.username || '?'}</b>
                    <span className="dim mono">{fmtTime(m.date)}</span>
                  </div>
                  <div className="chat-bubble">{m.message}</div>
                </div>
              )
            })}
            <div ref={endRef} />
          </div>
        )}
      </div>
      <div className="chat-compose">
        <input
          value={text}
          placeholder="输入消息，Enter 发送"
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
              e.preventDefault()
              void send()
            }
          }}
        />
        <button className="primary" disabled={busy || !text.trim()} onClick={() => void send()}>
          {busy ? '…' : '发送'}
        </button>
      </div>
    </div>
  )
}

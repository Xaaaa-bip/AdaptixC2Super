import { useCallback, useEffect, useRef, useState } from 'react'
import type { ReactNode } from 'react'
import { useStore } from '../store'
import type { RelayStatus } from '../api/http'
import { copyText } from '../utils'
import { PageTips } from './PageTips'

/**
 * libp2p 中转（axrelay）进程的面板。
 *
 * 为什么需要这一页：axrelay 是**独立于 teamserver 的进程**，它死了这一页之外的
 * 任何地方都看不出来 —— 页面照常开、监听器照常列着，只有 libp2p 载荷会静默失联。
 * 2026-09-20 实测撞上过一次：relay 已经没了、4446 零监听、日志文件都不存在，
 * 没人发现，而且它**永远不会自己回来**。
 *
 * ★ 按钮不做「自己拉一个进程」这件事，一律转发 systemctl（见 extenders/relay/pl_main.go）。
 *   否则会和 systemd 抢 4446，症状是「时好时坏」。
 * ★ 状态是**轮询**的（5s），没有走 WS 推送：这一页的价值在「看一眼真实状态」，
 *   而轮询读的是 systemd 的当前值，比推一份可能过期的快照更可信。
 */

const POLL_MS = 5000

type Tone = 'ok' | 'warn' | 'err' | 'info'

function describe(st: RelayStatus | null): { text: string; tone: Tone } {
  if (!st) return { text: '读取中…', tone: 'info' }
  if (!st.systemctl_present) return { text: '本机无 systemd', tone: 'err' }
  if (!st.unit_present) return { text: '未托管', tone: 'err' }
  switch (st.active) {
    case 'active': return { text: '运行中', tone: 'ok' }
    case 'activating': return { text: '启动中…', tone: 'warn' }
    case 'deactivating': return { text: '停止中…', tone: 'warn' }
    case 'failed': return { text: '已失败', tone: 'err' }
    case 'inactive': return { text: '已停止', tone: 'warn' }
    default: return { text: st.active || '未知', tone: 'warn' }
  }
}

function Fact({ k, v, title }: { k: string; v: ReactNode; title?: string }) {
  return (
    <div style={{ display: 'flex', gap: 8, alignItems: 'baseline', minWidth: 0 }}>
      <span className="dim" style={{ flex: '0 0 auto', fontSize: 12 }}>{k}</span>
      <span
        className="mono"
        title={title}
        style={{ minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}
      >
        {v}
      </span>
    </div>
  )
}

export function Relay() {
  const api = useStore((s) => s.api)
  const pushToast = useStore((s) => s.pushToast)

  const [st, setSt] = useState<RelayStatus | null>(null)
  const [err, setErr] = useState('')
  // 接口回了 200 空 body ⇒ 插件确实没注册。与"请求失败"是**两回事**，
  // 分开存才知道该给哪条指引（见 refresh 里的注释）。
  const [missing, setMissing] = useState(false)
  const [busy, setBusy] = useState('')
  // 轮询可能在上一次还没回来时又触发；用一个 ref 挡住，避免请求叠起来。
  const inflight = useRef(false)

  const refresh = useCallback(async () => {
    if (!api || inflight.current) return
    inflight.current = true
    try {
      const s = await api.relayStatus()
      // ★★ 「插件没注册」**不是异常**。未注册的 /api/* 路径，服务端回的是
      //    **HTTP 200 + 空 body**（实测，见 api/http.ts 末尾那段注释），
      //    解析出来就是 null。以前这里直接把 null 当成功写进 state ⇒
      //    顶部状态永远停在「读取中…」，而下面那句"若为 404…去 profile 注册"
      //    又只在 404 时才成立 —— 两头都对不上，把操作员往"去改 profile"上带。
      if (!s) { setMissing(true); setSt(null); setErr(''); return }
      setSt(s)
      setMissing(false)
      setErr('')
    } catch (e: any) {
      // ★★ relay/status 用 `ok:false` 表达「relay 未被托管」—— 那是**合法的诊断结果**
      //    （unit 没装就是没装），负载是完整的一份状态。但 api/http.ts 把一切
      //    `ok:false` 一律抛出来，于是整份状态被丢掉，页面只剩一句
      //    `request failed`，所有字段显示 `—`，「unit 文件 未找到」也成了默认值而不是事实。
      //    好在 http.ts 把响应体挂在 `err.payload` 上（见那里的注释），接回来当状态用。
      const p = e?.payload
      if (p && typeof p === 'object' && 'unit_present' in p) {
        setSt(p as RelayStatus)
        setMissing(false)
        setErr('')
        return
      }
      // 到这里的才是真失败：网络中断、401、服务端 ok:false 且没带状态体。
      setErr(e?.message || String(e))
    } finally {
      inflight.current = false
    }
  }, [api])

  useEffect(() => {
    void refresh()
    const t = setInterval(() => { void refresh() }, POLL_MS)
    return () => clearInterval(t)
  }, [refresh])

  const act = async (a: 'start' | 'stop' | 'restart') => {
    if (!api) return
    const label = a === 'start' ? '启动' : a === 'stop' ? '停止' : '重启'
    setBusy(a)
    try {
      const r = await api.relayAction(a)
      setSt(r)
      setErr('')
      pushToast(r.ok ? 'ok' : 'err', r.ok ? `已${label} relay` : (r.hint || `${label}失败`))
    } catch (e: any) {
      pushToast('err', `${label}失败：${e?.message || e}`)
      void refresh()
    } finally {
      setBusy('')
    }
  }

  // 插件没注册时 st 恒为 null，describe() 只会一直回「读取中…」——
  // 那是个永远转不完的假状态，得单独标出来。
  const { text: stText, tone } = missing ? { text: '插件未注册', tone: 'err' as Tone } : describe(st)
  const running = st?.active === 'active'
  const known = !!st && st.unit_present && st.systemctl_present
  const disabled = !!busy || !known

  return (
    <div className="page">
      <div className="toolbar">
        <span className={`lv ${tone}`}>{stText}</span>
        {st?.n_restarts && st.n_restarts !== '0' ? (
          <span className="dim" style={{ fontSize: 12 }}>已自动重启 {st.n_restarts} 次</span>
        ) : null}
        <span className="spacer" />
        <button onClick={() => act('start')} disabled={disabled || running}>
          {busy === 'start' ? '启动中…' : '启动'}
        </button>
        <button onClick={() => act('restart')} disabled={disabled}>
          {busy === 'restart' ? '重启中…' : '重启'}
        </button>
        <button className="danger" onClick={() => act('stop')} disabled={disabled || !running}>
          {busy === 'stop' ? '停止中…' : '停止'}
        </button>
        <button onClick={() => void refresh()} disabled={!!busy}>刷新</button>
        <PageTips id="relay">
          <p>这一页管的是 <b>libp2p 中转进程（axrelay）本身</b>，不是监听器。</p>
          <p>按钮转发给 systemd（<span className="mono">{st?.unit || 'axrelay.service'}</span>），
            不会另起一个进程 —— relay 的生死只能有一个 owner。</p>
          <p>载荷要连的地址来自<b>监听器</b>的 <span className="mono">l2p_relays</span>
            （gopher_listener_tcp 的字段，不是本插件）—— 本页只管 relay 进程本身。</p>
          <p>改 <span className="mono">/etc/axrelay/relaynode.key</span> 会换 peer id，
            于是 <span className="mono">l2p_relays</span> 与<b>所有已发出的 agent 地址一起失效</b>。</p>
          <p>状态每 5 秒自动读一次；读的是 systemd 的当前值，不是缓存。</p>
        </PageTips>
      </div>

      {/* ★ 三种「看不到状态」必须分开说，否则会把操作员指向错误的方向：
            · missing   插件没注册（200 空响应）→ 去 profile；这是 2026-09-21 实测踩的坑，
                        当时页面把这条说成"404 才可能"，而真实原因（unit 没装）压根没显示
            · err       请求本身失败           → 多半 teamserver 在重启 / 链接断，刷新
            · st.hint   插件答了但它自己有问题 → **服务端的权威诊断**，照它做 */}
      {missing ? (
        <div style={{ padding: '10px 14px 0' }}>
          <div className="card" style={{ flex: '0 0 auto', padding: '10px 12px' }}>
            <div className="lv err">relay 插件没注册</div>
            <div className="dim" style={{ marginTop: 6, fontSize: 12 }}>
              接口回了<b>空响应</b> —— 说明 <span className="mono">extenders/relay</span> 没在
              profile 里列出，或 <span className="mono">relay.so</span> 没落位。改完要重启 teamserver。
            </div>
          </div>
        </div>
      ) : null}

      {err ? (
        <div style={{ padding: '10px 14px 0' }}>
          <div className="card" style={{ flex: '0 0 auto', padding: '10px 12px' }}>
            <div className="lv err">读不到 relay 状态</div>
            <div className="dim" style={{ marginTop: 6, fontSize: 12 }}>{err}</div>
            <div className="dim" style={{ marginTop: 6, fontSize: 12 }}>
              这是<b>请求失败</b>，与「插件没注册」是两回事（后者是 200 空响应，不会抛异常）。
              多半是 teamserver 正在重启或链接中断，刷新即可。
            </div>
          </div>
        </div>
      ) : null}

      {st?.hint ? (
        <div style={{ padding: '10px 14px 0' }}>
          <div className="card" style={{ flex: '0 0 auto', padding: '10px 12px' }}>
            <div className="lv warn">relay 无法被托管</div>
            <div className="dim" style={{ marginTop: 6, fontSize: 12 }}>{st.hint}</div>
          </div>
        </div>
      ) : null}

      <div style={{ padding: '10px 14px 0' }}>
        {/* ★ 必须显式 flex:0 0 auto —— .page > .card 有一条 flex:1，
            不加就会被拉满整屏，把下面的日志区挤没。 */}
        <div className="card" style={{ flex: '0 0 auto', padding: '10px 12px' }}>
          <div style={{
            display: 'grid',
            gridTemplateColumns: 'repeat(auto-fit, minmax(260px, 1fr))',
            gap: '8px 20px',
          }}>
            <Fact k="unit" v={st?.unit || '—'} />
            <Fact k="自启" v={st ? (st.enabled === 'enabled' ? '已启用' : st.enabled) : '—'} />
            <Fact k="监听" v={st?.listen || '—'} />
            <Fact k="PID" v={st?.main_pid || (running ? '—' : '(未运行)')} />
            {/* ★ systemd 的 ActiveEnterTimestamp 在停止后**仍保留上次激活时间**
                （真正的停止时刻在 StateChangeTimestamp）。直接写「启动于」会让一个
                陈旧值读起来像当前事实 —— 正是这一页要防的那类误读，所以按状态换标签。 */}
            <Fact k={running ? '启动于' : '上次启动'} v={st?.since || '—'} />
            <Fact k="unit 文件" v={st?.unit_file || '未找到'} title={st?.unit_file} />
            <Fact
              k="peer id"
              v={st?.peer_id || '—'}
              title={st?.peer_id}
            />
            <Fact k="日志" v={st?.log_file || '—'} title={st?.log_file} />
          </div>
          {st?.peer_id ? (
            <div style={{ marginTop: 8 }}>
              <button
                className="ghost"
                onClick={async () => {
                  const ok = await copyText(st.peer_id)
                  pushToast(ok ? 'ok' : 'err', ok ? 'peer id 已复制' : '复制失败')
                }}
              >
                复制 peer id
              </button>
            </div>
          ) : null}
        </div>
      </div>

      {/* .page > .log-page 自带 flex:1 / min-height:0，所以这里不用写高度 */}
      <div className="log-page" style={{ margin: '10px 14px 12px', padding: '8px 10px' }}>
        {st?.log_tail?.length ? (
          st.log_tail.map((line, i) => (
            <div key={i} className="it" style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-all' }}>{line}</div>
          ))
        ) : (
          <div className="dim">没有日志内容{st?.log_file ? `（${st.log_file} 不存在或为空）` : ''}</div>
        )}
      </div>
    </div>
  )
}

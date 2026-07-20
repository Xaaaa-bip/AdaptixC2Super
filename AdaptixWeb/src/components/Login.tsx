import { useEffect, useState } from 'react'
import { useStore } from '../store'
import { loadLoginPrefs, loadRuntimeConfig } from '../utils'

export function Login() {
  const loginAndConnect = useStore((s) => s.loginAndConnect)
  const pushLog = useStore((s) => s.pushLog)
  const prefs = loadLoginPrefs()
  const [baseUrl, setBaseUrl] = useState(prefs.baseUrl)
  const [username, setUsername] = useState(prefs.username || 'admin')
  const [password, setPassword] = useState('')
  const [busy, setBusy] = useState(false)
  const [err, setErr] = useState('')
  const [showPass, setShowPass] = useState(false)
  const [advanced, setAdvanced] = useState(!!prefs.baseUrl)

  // Product mode: server may ship /config.json (or {base}config.json) with api_base.
  useEffect(() => {
    let cancelled = false
    ;(async () => {
      if (prefs.baseUrl) return
      const cfg = await loadRuntimeConfig()
      if (cancelled || !cfg) return
      const api = (cfg.api_base || '').trim()
      if (api) {
        setBaseUrl(api)
        setAdvanced(true)
      }
      // empty api_base => same-origin /api (leave baseUrl empty)
    })()
    return () => { cancelled = true }
  }, [])

  async function onSubmit(e: React.FormEvent) {
    e.preventDefault()
    setBusy(true)
    setErr('')
    try {
      await loginAndConnect({ baseUrl: baseUrl.trim(), username, password })
    } catch (ex: any) {
      const msg = ex?.message || String(ex)
      const nice = /429|too many|rate/i.test(msg) ? '尝试过于频繁，请约一分钟后再试' : msg
      setErr(nice)
      pushLog?.('err', nice, false)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="lx">
      <div className="lx-ambient" aria-hidden />
      <div className="ink-scape lx-scape" aria-hidden>
        <div className="ink-wash ink-wash-a" />
        <div className="ink-wash ink-wash-b" />
        <div className="ink-ridge" />
        <div className="ink-mist" />
      </div>
      <div className="lx-card">
        <div className="lx-brand">
          <div className="lx-mark" aria-hidden>
            <span />
          </div>
          <div>
            <div className="lx-name">Adaptix</div>
            <div className="lx-sub">控制台</div>
          </div>
        </div>

        <form className="lx-form" onSubmit={onSubmit}>
          <label className="lx-label">用户名</label>
          <input
            className="lx-input"
            value={username}
            onChange={(e) => setUsername(e.target.value)}
            autoComplete="username"
            autoFocus
            spellCheck={false}
          />

          <label className="lx-label">密码</label>
          <div className="lx-pass">
            <input
              className="lx-input"
              type={showPass ? 'text' : 'password'}
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              autoComplete="current-password"
              placeholder="••••••••"
            />
            <button type="button" className="lx-textbtn" onClick={() => setShowPass((v) => !v)}>
              {showPass ? '隐藏' : '显示'}
            </button>
          </div>

          <button type="button" className="lx-textbtn lx-adv" onClick={() => setAdvanced((v) => !v)}>
            {advanced ? '收起服务器设置' : '服务器设置'}
          </button>
          {advanced && (
            <div className="lx-advbox">
              <input
                className="lx-input"
                value={baseUrl}
                onChange={(e) => setBaseUrl(e.target.value)}
                placeholder="留空则使用当前站点 /api"
                autoComplete="url"
                spellCheck={false}
              />
            </div>
          )}

          <button className="lx-submit" type="submit" disabled={busy || !username || !password}>
            {busy ? '连接中…' : '继续'}
          </button>

          {err ? <div className="lx-err">{err}</div> : <div className="lx-err spacer" />}
        </form>

        <div className="lx-foot">
          <kbd>⌘</kbd><kbd>K</kbd>
          <span>登录后可快速跳转</span>
        </div>
      </div>
    </div>
  )
}

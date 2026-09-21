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
        {/* 品牌行：`> Console▊` 整行由光标动画逐字打出
            （样式见 hacker.css 的 .lx-brandline*）。
            ★ 多一层 `.lx-brandline-typing` 是必需的：光标是它的 ::after，
            **不能**挂在做 clip-path 的 `.lx-brandline-text` 上 ——
            clip-path 会连子孙一起裁掉，光标会整段消失。
            ★ 刻意**不用** `.lx-mark`：hacker.css 里有一组针对 `.lx-mark span`
            的 `display:none !important`（旧太极子元素留下的守卫），而本行要按
            span 分段配色，套那个类会被整条藏掉。
            ★ 名字就是 `Console`，不再带任何前缀/后缀 —— 刻意留白的品牌字符串
            容易被当成指纹（原来是 `4-T34M OPS CONSOLE`）。 */}
        <div className="lx-brand">
          <div className="lx-brandline" role="img" aria-label="Console">
            <span className="lx-brandline-prompt">{'>'}</span>
            <span className="lx-brandline-typing">
              <span className="lx-brandline-text">
                <span className="lx-brandline-name">Console</span>
              </span>
            </span>
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

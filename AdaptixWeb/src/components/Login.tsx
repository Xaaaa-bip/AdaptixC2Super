import { useEffect, useRef, useState } from 'react'
import { useStore } from '../store'
import { t } from '../i18n'
import { loadLoginPrefs, loadRuntimeConfig } from '../utils'
import { QT_THEMES, applyQtTheme, loadQtTheme, qtAsset, type QtThemeId } from '../design/qt-themes'

type SavedProfile = {
  id: string
  project: string
  username: string
  baseUrl: string
}

const PROFILES_KEY = 'adaptix.qt.profiles.v1'

function loadProfiles(): SavedProfile[] {
  try {
    const raw = localStorage.getItem(PROFILES_KEY)
    if (!raw) return []
    const parsed = JSON.parse(raw)
    if (!Array.isArray(parsed)) return []
    return parsed.filter((x) => x && typeof x.project === 'string')
  } catch {
    return []
  }
}

function saveProfiles(list: SavedProfile[]) {
  try { localStorage.setItem(PROFILES_KEY, JSON.stringify(list)) } catch {}
}

/**
 * 1:1 Qt DialogConnect (AdaptixClient/Source/UI/Dialogs/DialogConnect.cpp)
 * 720×470 two-column desktop dialog:
 *  - Left: QGroupBox("User Info") + QGroupBox("Server Details") + QGroupBox("Project") + Connect
 *  - Right: Saved Profiles CardListWidget + New Profile / Load / Save
 */
export function Login() {
  const loginAndConnect = useStore((s) => s.loginAndConnect)
  const pushLog = useStore((s) => s.pushLog)
  const prefs = loadLoginPrefs()

  const [profiles, setProfiles] = useState<SavedProfile[]>(() => loadProfiles())
  const [activeProfId, setActiveProfId] = useState<string | null>(null)
  const [project, setProject] = useState(() => localStorage.getItem('adaptix.qt.project') || 'Adaptix')
  const [baseUrl, setBaseUrl] = useState(prefs.baseUrl || '')
  const [username, setUsername] = useState(prefs.username || '')
  const [password, setPassword] = useState('')
  const [showPass, setShowPass] = useState(false)
  const [busy, setBusy] = useState(false)
  const [err, setErr] = useState('')
  const [qtTheme, setQtTheme] = useState<QtThemeId>(() => loadQtTheme())
  const fileRef = useRef<HTMLInputElement>(null)

  function switchTheme(id: QtThemeId) {
    setQtTheme(id)
    applyQtTheme(id)
  }

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      if (prefs.baseUrl) return
      const cfg = await loadRuntimeConfig()
      if (cancelled || !cfg) return
      const api = (cfg.api_base || '').trim()
      if (api) setBaseUrl(api)
    })()
    return () => { cancelled = true }
  }, [])

  function selectProfile(p: SavedProfile) {
    setActiveProfId(p.id)
    setProject(p.project)
    setUsername(p.username)
    setBaseUrl(p.baseUrl)
    setErr('')
  }

  function newProfile() {
    setActiveProfId(null)
    setProject('Adaptix')
    setUsername('')
    setPassword('')
    setBaseUrl('')
    setErr('')
  }

  function upsertCurrentProfile(projName: string, user: string, url: string) {
    const cleanProj = projName.trim() || 'Adaptix'
    const cleanUser = user.trim()
    const cleanUrl = url.trim()
    if (!cleanUser) return
    setProfiles((prev) => {
      const existing = prev.find((x) => x.project === cleanProj && x.username === cleanUser && x.baseUrl === cleanUrl)
      const next: SavedProfile[] = existing
        ? [existing, ...prev.filter((x) => x.id !== existing.id)]
        : [{ id: String(Date.now()), project: cleanProj, username: cleanUser, baseUrl: cleanUrl }, ...prev].slice(0, 20)
      saveProfiles(next)
      return next
    })
  }

  function removeProfile(id: string) {
    setProfiles((prev) => {
      const next = prev.filter((x) => x.id !== id)
      saveProfiles(next)
      return next
    })
    if (activeProfId === id) setActiveProfId(null)
  }

  function saveProfileToFile() {
    const data = {
      project: project.trim() || 'Adaptix',
      username: username.trim(),
      url: baseUrl.trim(),
    }
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
    const a = document.createElement('a')
    a.href = URL.createObjectURL(blob)
    a.download = `${data.project.replace(/[^\w.-]/g, '_')}.profile.json`
    a.click()
    URL.revokeObjectURL(a.href)
  }

  function loadProfileFromFile(e: React.ChangeEvent<HTMLInputElement>) {
    const f = e.target.files?.[0]
    if (!f) return
    f.text().then((txt) => {
      const j = JSON.parse(txt)
      if (j.project) setProject(String(j.project))
      if (j.username || j.user) setUsername(String(j.username || j.user))
      if (j.url || j.baseUrl) setBaseUrl(String(j.url || j.baseUrl))
      setErr('')
    }).catch(() => setErr(t('无法解析配置文件')))
    e.target.value = ''
  }

  async function onSubmit(e: React.FormEvent) {
    e.preventDefault()
    setBusy(true)
    setErr('')
    try {
      const proj = project.trim() || 'Adaptix'
      try { localStorage.setItem('adaptix.qt.project', proj) } catch {}
      upsertCurrentProfile(proj, username, baseUrl)
      await loginAndConnect({ baseUrl: baseUrl.trim(), username, password })
    } catch (ex: any) {
      const msg = ex?.message || String(ex)
      const nice = /429|too many|rate/i.test(msg) ? t('尝试过于频繁，请约一分钟后再试') : msg
      setErr(nice)
      pushLog?.('err', nice, false)
    } finally {
      setBusy(false)
    }
  }

  const hostHint = window.location.origin

  return (
    <div className="qt-login-screen">
      <div className="qt-connect-win" role="dialog" aria-label="Connect">
        <div className="qt-connect-titlebar">
          <div className="qt-connect-brand">
            <img src={qtAsset('Logo.png')} alt="" />
            <span>Connect</span>
          </div>
          <select
            style={{ height: 22, padding: '0 6px', fontSize: 11 }}
            value={qtTheme}
            onChange={(e) => switchTheme(e.target.value as QtThemeId)}
            aria-label="Theme"
          >
            {QT_THEMES.map((th) => (
              <option key={th.id} value={th.id}>{th.name}</option>
            ))}
          </select>
        </div>

        <div className="qt-connect-body">
          {/* Left Form Column: User Info · Server Details · Project · Connect */}
          <form className="qt-connect-left" onSubmit={onSubmit}>
            <div className="qt-groupbox">
              <span className="qt-groupbox-title">User Info</span>
              <div className="qt-form-grid">
                <label htmlFor="qt-login-user">User:</label>
                <input
                  id="qt-login-user"
                  value={username}
                  onChange={(e) => setUsername(e.target.value)}
                  autoComplete="username"
                  autoFocus
                  spellCheck={false}
                  placeholder="operator"
                />

                <label htmlFor="qt-login-pass">Password:</label>
                <div className="qt-pass-wrap">
                  <input
                    id="qt-login-pass"
                    type={showPass ? 'text' : 'password'}
                    value={password}
                    onChange={(e) => setPassword(e.target.value)}
                    autoComplete="current-password"
                    placeholder="••••••••"
                  />
                  <button type="button" onClick={() => setShowPass((v) => !v)}>
                    {showPass ? t('隐藏') : t('显示')}
                  </button>
                </div>
              </div>
            </div>

            <div className="qt-groupbox">
              <span className="qt-groupbox-title">Server Details</span>
              <div className="qt-form-grid">
                <label htmlFor="qt-login-url">URL:</label>
                <input
                  id="qt-login-url"
                  className="mono"
                  value={baseUrl}
                  onChange={(e) => setBaseUrl(e.target.value)}
                  placeholder={`${hostHint} (${t('留空使用当前站点')})`}
                  autoComplete="url"
                  spellCheck={false}
                />
              </div>
            </div>

            <div className="qt-groupbox">
              <span className="qt-groupbox-title">Project</span>
              <div className="qt-form-grid">
                <label htmlFor="qt-login-proj">Name:</label>
                <input
                  id="qt-login-proj"
                  value={project}
                  onChange={(e) => setProject(e.target.value)}
                  placeholder="Adaptix"
                  spellCheck={false}
                />
              </div>
            </div>

            {err ? <div className="qt-connect-err" role="alert">{err}</div> : null}

            <div className="qt-connect-actions">
              <button
                className="primary qt-connect-submit"
                type="submit"
                disabled={busy || !username || !password}
              >
                {busy ? t('连接中…') : 'Connect'}
              </button>
            </div>
          </form>

          <div className="qt-connect-vline" />

          {/* Right Profiles Column (DialogConnect::cardWidget) */}
          <div className="qt-connect-right">
            <div className="qt-profiles-head">Profiles</div>
            <div className="qt-profiles-list">
              {profiles.length === 0 ? (
                <div className="qt-profiles-empty">
                  {t('暂无保存的连接档案\n连接成功后自动保存')}
                </div>
              ) : (
                profiles.map((p) => (
                  <div
                    key={p.id}
                    className={`qt-profile-card ${activeProfId === p.id ? 'active' : ''}`}
                    onClick={() => selectProfile(p)}
                  >
                    <div className="qt-profile-name">{p.project}</div>
                    <div className="qt-profile-sub">
                      {p.username} @ {(p.baseUrl || window.location.host).replace(/^https?:\/\//, '')}
                    </div>
                    <button
                      type="button"
                      className="qt-profile-del"
                      title="Remove"
                      onClick={(e) => { e.stopPropagation(); removeProfile(p.id) }}
                    >
                      ×
                    </button>
                  </div>
                ))
              )}
            </div>

            <div className="qt-profiles-btns">
              <button type="button" className="new-btn" onClick={newProfile}>
                New Profile
              </button>
              <button
                type="button"
                className="ico-btn"
                title="Load profile from file"
                onClick={() => fileRef.current?.click()}
              >
                <img className="qt-ico qt-ico-mono" src={qtAsset('icons/file_open_64dp.png')} alt="" />
              </button>
              <button
                type="button"
                className="ico-btn"
                title="Save profile to file"
                onClick={saveProfileToFile}
              >
                <img className="qt-ico qt-ico-mono" src={qtAsset('icons/save_as_64dp.png')} alt="" />
              </button>
              <input ref={fileRef} type="file" accept=".json" style={{ display: 'none' }} onChange={loadProfileFromFile} />
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}

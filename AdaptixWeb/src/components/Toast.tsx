import { useEffect } from 'react'
import { useStore } from '../store'

export function ToastHost() {
  const toasts = useStore((s) => s.toasts)
  const dismissToast = useStore((s) => s.dismissToast)

  useEffect(() => {
    if (!toasts.length) return
    const timers = toasts.map((t) =>
      window.setTimeout(() => dismissToast(t.id), t.ttl || 3200),
    )
    return () => timers.forEach((id) => clearTimeout(id))
  }, [toasts, dismissToast])

  if (!toasts.length) return null
  return (
    <div className="toast-host" aria-live="polite">
      {toasts.map((t) => (
        <div key={t.id} className={`toast ${t.level}`} onClick={() => dismissToast(t.id)}>
          <span className="toast-ico">
            {t.level === 'ok' ? '✓' : t.level === 'err' ? '!' : t.level === 'warn' ? '⚠' : 'i'}
          </span>
          <span className="toast-text">{t.text}</span>
          <span className="toast-x">×</span>
        </div>
      ))}
    </div>
  )
}

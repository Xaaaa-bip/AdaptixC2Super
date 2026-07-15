/** Load xterm + fit addon with multi-CDN fallback (jsDelivr often blocked in CN). */

declare global {
  interface Window {
    Terminal?: any
    FitAddon?: { FitAddon: any }
  }
}

let loading: Promise<void> | null = null

const XTERM_CSS = [
  'https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css',
  'https://unpkg.com/xterm@5.3.0/css/xterm.min.css',
  'https://cdnjs.cloudflare.com/ajax/libs/xterm/5.3.0/xterm.min.css',
]
const XTERM_JS = [
  'https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js',
  'https://unpkg.com/xterm@5.3.0/lib/xterm.min.js',
  'https://cdnjs.cloudflare.com/ajax/libs/xterm/5.3.0/xterm.min.js',
]
const FIT_JS = [
  'https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js',
  'https://unpkg.com/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js',
]

function loadScript(src: string) {
  return new Promise<void>((resolve, reject) => {
    const exists = Array.from(document.scripts).some((s) => s.src === src || s.src.includes('xterm'))
    // only skip exact src; different CDN URLs must still load
    if (Array.from(document.scripts).some((s) => s.src === src)) { resolve(); return }
    const s = document.createElement('script')
    s.src = src
    s.async = true
    s.onload = () => resolve()
    s.onerror = () => reject(new Error(`failed to load ${src}`))
    document.head.appendChild(s)
  })
}

function loadCss(href: string) {
  if (document.querySelector(`link[href="${href}"]`)) return
  const l = document.createElement('link')
  l.rel = 'stylesheet'
  l.href = href
  document.head.appendChild(l)
}

async function loadFirst(urls: string[], kind: 'js' | 'css') {
  let last = ''
  for (const url of urls) {
    try {
      if (kind === 'css') {
        loadCss(url)
        return url
      }
      await loadScript(url)
      return url
    } catch (e: any) {
      last = e?.message || String(e)
    }
  }
  throw new Error(last || `all ${kind} sources failed`)
}

export async function ensureXterm(): Promise<{ Terminal: any; FitAddon: any }> {
  if (window.Terminal && (window as any).FitAddon) {
    const Fit = (window as any).FitAddon.FitAddon || (window as any).FitAddon
    return { Terminal: window.Terminal, FitAddon: Fit }
  }
  if (!loading) {
    loading = (async () => {
      await loadFirst(XTERM_CSS, 'css')
      await loadFirst(XTERM_JS, 'js')
      await loadFirst(FIT_JS, 'js')
    })().catch((e) => {
      loading = null
      throw e
    })
  }
  await loading
  const Terminal = window.Terminal
  const FitAddon = (window as any).FitAddon?.FitAddon || (window as any).FitAddon
  if (!Terminal || !FitAddon) throw new Error('xterm 加载失败（CDN 可能被拦截，请换网络或本地托管 xterm）')
  return { Terminal, FitAddon }
}

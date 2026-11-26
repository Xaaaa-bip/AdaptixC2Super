/** Load xterm + fit addon.
 *
 * ★ 2026-11-17：改为**打进 bundle**（vite 副作用 import → assets/ 哈希文件，
 *   同源一定可达）。此前运行时从三个公网 CDN 注入 script —— 目标网络全被墙，
 *   PTY 打不开（failed to load cdnjs.../xterm.min.js）。CDN 列表保留为兜底
 *   （bundle 已就位时 window.Terminal 直接命中快路径，不会走到注入）。 */

// UMD 构建无类型；副作用 import 挂 window.Terminal / window.FitAddon
import '../vendor/xterm.min.js'
import '../vendor/xterm-addon-fit.min.js'
import '../vendor/xterm.min.css'


declare global {
  interface Window {
    Terminal?: any
    FitAddon?: { FitAddon: any }
  }
}

let loading: Promise<void> | null = null

// ★ 2026-11-17：本地 vendor 优先（public/vendor/，随构建打进同源 dist），
//   CDN 仅兜底 —— 三个 CDN 在目标网络全不可达，PTY 打不开。本地路径必须
//   带 vite base（部署在 Web.path 子路径下，绝对 /vendor 会 404）。
const VENDOR = `${import.meta.env.BASE_URL}vendor/`
const XTERM_CSS = [
  `${VENDOR}xterm.min.css`,
  'https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css',
  'https://unpkg.com/xterm@5.3.0/css/xterm.min.css',
  'https://cdnjs.cloudflare.com/ajax/libs/xterm/5.3.0/xterm.min.css',
]
const XTERM_JS = [
  `${VENDOR}xterm.min.js`,
  'https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js',
  'https://unpkg.com/xterm@5.3.0/lib/xterm.min.js',
  'https://cdnjs.cloudflare.com/ajax/libs/xterm/5.3.0/xterm.min.js',
]
const FIT_JS = [
  `${VENDOR}xterm-addon-fit.min.js`,
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

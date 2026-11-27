import { LANG } from './index'
import { translateEn } from './translateEn'

const ZH_RE = /[\u4e00-\u9fff]/

const SKIP_ELEMENT_SELECTOR = [
  '[data-no-i18n]',
  'script',
  'style',
  'noscript',
  'template',
  '.xterm',
  '.pty-host',
  '.qt-console-out .qt-c-line',
  '.cmd-exec-body .ln',
].join(', ')

const TRANSLATABLE_ATTRS = ['placeholder', 'title', 'aria-label', 'alt'] as const

const textSourceMap = new WeakMap<Text, string>()
const attrSourceMap = new WeakMap<Element, Record<string, string>>()

let applying = false
let observer: MutationObserver | null = null
let pendingNodes = new Set<Node>()
let flushScheduled = false

function shouldSkipElement(el: Element | null): boolean {
  if (!el) return true
  try {
    return !!el.closest(SKIP_ELEMENT_SELECTOR)
  } catch {
    return false
  }
}

function translateTextNode(node: Text) {
  const val = node.nodeValue
  if (!val) return
  if (shouldSkipElement(node.parentElement)) return

  const prevSrc = textSourceMap.get(node)
  if (!ZH_RE.test(val)) {
    // Preserve clean English updates if React changed the source text
    if (prevSrc !== undefined && translateEn(prevSrc) !== val) {
      textSourceMap.delete(node)
    }
    return
  }

  textSourceMap.set(node, val)
  const translated = translateEn(val)
  if (translated !== val) {
    node.nodeValue = translated
  }
}

function translateElementAttrs(el: Element) {
  if (shouldSkipElement(el)) return
  let map = attrSourceMap.get(el)

  for (const attr of TRANSLATABLE_ATTRS) {
    const val = el.getAttribute(attr)
    if (!val) continue
    if (!ZH_RE.test(val)) {
      if (map && map[attr] && translateEn(map[attr]) !== val) {
        delete map[attr]
      }
      continue
    }
    if (!map) {
      map = {}
      attrSourceMap.set(el, map)
    }
    map[attr] = val
    const translated = translateEn(val)
    if (translated !== val) {
      el.setAttribute(attr, translated)
    }
  }
}

function walkSubtree(root: Node) {
  if (root.nodeType === Node.TEXT_NODE) {
    translateTextNode(root as Text)
    return
  }
  if (root.nodeType !== Node.ELEMENT_NODE && root.nodeType !== Node.DOCUMENT_FRAGMENT_NODE) return

  if (root.nodeType === Node.ELEMENT_NODE) {
    const el = root as Element
    if (shouldSkipElement(el)) return
    translateElementAttrs(el)
  }

  const walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT)
  let cur: Node | null = walker.nextNode()
  while (cur) {
    if (cur.nodeType === Node.TEXT_NODE) {
      translateTextNode(cur as Text)
    } else if (cur.nodeType === Node.ELEMENT_NODE) {
      const el = cur as Element
      if (!shouldSkipElement(el)) {
        translateElementAttrs(el)
      }
    }
    cur = walker.nextNode()
  }
}

function flushPending() {
  flushScheduled = false
  if (LANG !== 'en' || applying) return
  applying = true
  try {
    if (document.title && ZH_RE.test(document.title)) {
      document.title = translateEn(document.title)
    }
    const batch = Array.from(pendingNodes)
    pendingNodes.clear()
    for (const node of batch) {
      if (node.isConnected) walkSubtree(node)
    }
  } finally {
    applying = false
  }
}

function scheduleNode(node: Node | null) {
  if (!node) return
  pendingNodes.add(node)
  if (!flushScheduled) {
    flushScheduled = true
    Promise.resolve().then(flushPending)
  }
}

/**
 * Installs a lightweight DOM fallback translator exclusively in English mode so any
 * remaining static text nodes, SVG `<text>`/`<title>`, tooltips, placeholders, or
 * dynamic server-returned `.axs` BuildSchema strings render in English with zero flicker.
 */
export function installEnDomTranslator() {
  if (LANG !== 'en' || typeof document === 'undefined' || !document.body) return
  if (observer) return

  applying = true
  try {
    if (document.title && ZH_RE.test(document.title)) {
      document.title = translateEn(document.title)
    }
    walkSubtree(document.body)
  } finally {
    applying = false
  }

  observer = new MutationObserver((mutations) => {
    if (applying) return
    for (const m of mutations) {
      if (m.type === 'characterData') {
        scheduleNode(m.target)
      } else if (m.type === 'attributes') {
        scheduleNode(m.target)
      } else if (m.type === 'childList') {
        m.addedNodes.forEach((n) => scheduleNode(n))
      }
    }
  })

  observer.observe(document.body, {
    childList: true,
    subtree: true,
    characterData: true,
    attributes: true,
    attributeFilter: [...TRANSLATABLE_ATTRS],
  })
}

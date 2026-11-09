/* 极简 i18n：键 = 中文原文，字典缺失回退中文。
 *
 * 为什么键用中文原文而不是英文 slug：
 *  - 半翻译状态下组件不改也能跑（t() 缺键返回原串）——长尾页面可以
 *    后续只往 dict 里加条目，不用再动组件；
 *  - 组件改造就是把文案包一层 t()，不引入起名负担。
 *
 * 语言来源（先到先得）：URL ?lang= / #lang= > localStorage(adaptix.lang) > zh。
 * 切换由 setLang() 落 localStorage 后整页 reload —— 简单可靠，React 树
 * 无需订阅语言状态；README 的 /?lang=en 链接也天然工作。
 */

export type Lang = 'zh' | 'en'

import { EN_DICT } from './dict.en'

const LS_KEY = 'adaptix.lang'

function detectLang(): Lang {
  try {
    const u = new URL(location.href)
    const q = (u.searchParams.get('lang') || '').toLowerCase()
    if (q === 'en' || q === 'zh') {
      localStorage.setItem(LS_KEY, q)
      return q
    }
    const h = (u.hash.match(/lang=(en|zh)/i) || [])[1]
    if (h) {
      localStorage.setItem(LS_KEY, h.toLowerCase())
      return h.toLowerCase() as Lang
    }
    const saved = localStorage.getItem(LS_KEY)
    if (saved === 'en' || saved === 'zh') return saved
  } catch { /* localStorage 不可用 → 默认中文 */ }
  return 'zh'
}

export let LANG: Lang = detectLang()

export function setLang(lang: Lang) {
  try { localStorage.setItem(LS_KEY, lang) } catch { /* 忽略 */ }
  location.reload()
}

/** 翻译：en 字典命中 → 英文；否则原样返回中文。 */
export function t(s: string): string {
  if (LANG === 'zh') return s
  return EN_DICT[s] ?? s
}

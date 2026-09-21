/**
 * IP → 归属地（**服务端查询**）。
 *
 * ★ 2026-09-21 改：原先是浏览器本地查库 —— 控制台要把 qqwry.ipdb（36MB）整个拉下来
 *   在内存里做二分。两个问题：① 每开一次控制台一次 36MB 下行；② 取库路径写成绝对的
 *   `/qqwry.ipdb`，Web 一挂到子路径（profile 的 Web.path）就打到站点根、拿到诱饵 404，
 *   而失败只在 console.warn 留一行 —— 界面上表现为「归属地」整列空白、完全看不出原因。
 *
 *   库本来就在服务器上（dist/web/qqwry.ipdb），挪到服务端查之后前端一次批量问一屏
 *   主机的 IP，回包几十字节。接口：POST /api/ip/geo（见 core/connector/tc_ipdb.go）。
 *
 * 本模块对外仍是**同步读缓存**的形态（`lookupIpGeoSync`），因为主机表是按行同步渲染的；
 * 取数走 `preloadIpGeo()`，拿回新结果后由调用方 bump 一次版本号触发重渲染。
 */
import type { ApiClient } from '../api/http'

const cache = new Map<string, string>()
let inflight: Promise<boolean> | null = null

/** 同步读缓存；还没查到就返回空串（调用方照原样显示 "…"） */
export function lookupIpGeoSync(ip: string): string {
  return cache.get(ip) || ''
}

/**
 * 批量预取。只问缓存里没有的 IP；同一时刻只允许一个在飞（主机表 tick 很密，
 * 不串行化会一屏几十个并发请求打到服务端）。
 * 返回 true 表示**有新结果落地**，调用方据此触发一次重渲染。
 */
export async function preloadIpGeo(api: ApiClient, ips: string[]): Promise<boolean> {
  const todo = [...new Set(ips.map((s) => (s || '').trim()).filter((ip) => ip && !cache.has(ip)))]
  if (!todo.length) return false
  if (inflight) { await inflight.catch(() => {}); return false }
  inflight = (async () => {
    try {
      const res = await api.ipGeo(todo)
      const items = res?.items || {}
      for (const ip of todo) cache.set(ip, items[ip] || '')
      return true
    } finally {
      inflight = null
    }
  })()
  return inflight.catch(() => false)
}

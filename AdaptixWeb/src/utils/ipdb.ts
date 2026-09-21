/**
 * MaxMind-style IPDB reader（对齐 Qt IpdbReader / client/qqwry.ipdb）
 * 仅 IPv4；字段：country_name region_name city_name …
 */

type IpdbMeta = {
  node_count: number
  total_size: number
  fields: string[]
  languages: Record<string, number>
  v4node?: number
}

let tried = false
let loaded = false
let data = new Uint8Array(0)
let nodeCount = 0
let v4Offset = 0
let langOff = 0
let fieldCount = 0
const cache = new Map<string, string>()

function readNodeU32(node: number, index: number): number {
  const off = node * 8 + index * 4
  if (off + 4 > data.length) return 0
  return ((data[off] << 24) | (data[off + 1] << 16) | (data[off + 2] << 8) | data[off + 3]) >>> 0
}

function computeV4Entry(): number {
  let node = 0
  for (let i = 0; i < 96 && node < nodeCount; i++) {
    const bit = i >= 80 ? 1 : 0
    node = readNodeU32(node, bit)
  }
  return node
}

function resolvePayload(node: number): Uint8Array | null {
  const resolved = node - nodeCount + nodeCount * 8
  if (resolved < 0 || resolved + 2 > data.length) return null
  const sz = (data[resolved] << 8) | data[resolved + 1]
  if (sz < 0 || resolved + 2 + sz > data.length) return null
  return data.subarray(resolved + 2, resolved + 2 + sz)
}

function searchIPv4(nb: number[]): number {
  if (!loaded) return -1
  let node = v4Offset
  for (let i = 0; i < 32; i++) {
    if (node > nodeCount) break
    const bit = (nb[i >> 3] >> (7 - (i & 7))) & 1
    node = readNodeU32(node, bit)
  }
  return node > nodeCount ? node : -1
}

function formatRecord(raw: Uint8Array | null): string {
  if (!raw || !raw.length) return ''
  const s = new TextDecoder('utf-8').decode(raw)
  const all = s.split('\t')
  if (langOff + fieldCount > all.length) return ''
  const parts: string[] = []
  for (let i = 0; i < fieldCount; i++) {
    const t = (all[langOff + i] || '').trim()
    if (t) parts.push(t)
  }
  // 精简：国家 省 市 ISP，去掉空与重复
  const uniq: string[] = []
  for (const p of parts) {
    if (!uniq.includes(p) && p !== '中国') uniq.push(p)
    else if (p === '中国' && uniq.length === 0) uniq.push(p)
  }
  // 优先 country/region/city/isp
  return uniq.slice(0, 4).join(' ')
}

async function ensureLoaded(): Promise<boolean> {
  if (loaded) return true
  if (tried) return false
  tried = true
  try {
    // ★ 必须走 Vite 的 base，不能用绝对 `/qqwry.ipdb`：
    //   Web 挂在 profile 的 Web.path 下（现网是 `/bf21a14d8b0eb60f8b95ae0df61162b8/`），
    //   绝对路径会打到站点根 → 根路径返回的是**诱饵 404 页** → 库里一个字节读不到，
    //   表现就是主机列表「归属地」整列空白，而控制台只在 console.warn 里留一行，
    //   界面上完全看不出是库没加载。BASE_URL 由构建时的 --base 决定，两种部署都对。
    const res = await fetch(`${import.meta.env.BASE_URL}qqwry.ipdb`, { cache: 'force-cache' })
    if (!res.ok) throw new Error(`qqwry.ipdb HTTP ${res.status} (${import.meta.env.BASE_URL}qqwry.ipdb)`)
    const buf = new Uint8Array(await res.arrayBuffer())
    if (buf.length < 8) throw new Error('ipdb too small')
    const metaLen = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]
    if (metaLen <= 0 || 4 + metaLen > buf.length) throw new Error('bad meta len')
    const meta = JSON.parse(new TextDecoder().decode(buf.subarray(4, 4 + metaLen))) as IpdbMeta
    nodeCount = Number(meta.node_count) || 0
    const totalSize = Number(meta.total_size) || 0
    if (nodeCount <= 0 || totalSize <= 0 || 4 + metaLen + totalSize !== buf.length) {
      throw new Error('ipdb size mismatch')
    }
    const langs = meta.languages || {}
    langOff = langs.CN != null ? Number(langs.CN) : Number(Object.values(langs)[0] || 0)
    fieldCount = Array.isArray(meta.fields) ? meta.fields.length : 0
    if (fieldCount <= 0) throw new Error('no fields')
    data = buf.subarray(4 + metaLen, 4 + metaLen + totalSize)
    v4Offset = meta.v4node != null ? Number(meta.v4node) : computeV4Entry()
    loaded = true
    return true
  } catch (e) {
    console.warn('[ipdb] load failed', e)
    loaded = false
    return false
  }
}

function parseIPv4(ip: string): number[] | null {
  let s = String(ip || '').trim()
  if (!s) return null
  // strip port
  const colon = s.indexOf(':')
  if (colon > 0 && s.indexOf('.') > 0) s = s.slice(0, colon)
  const pts = s.split('.')
  if (pts.length !== 4) return null
  const nb: number[] = []
  for (const p of pts) {
    const v = Number(p)
    if (!Number.isInteger(v) || v < 0 || v > 255) return null
    nb.push(v)
  }
  return nb
}

/** 同步查询（库未加载时返回空） */
export function lookupIpGeoSync(ip: string): string {
  const nb = parseIPv4(ip)
  if (!nb || !loaded) return ''
  const key = nb.join('.')
  if (cache.has(key)) return cache.get(key) || ''
  const node = searchIPv4(nb)
  if (node < 0) return ''
  const result = formatRecord(resolvePayload(node))
  if (cache.size < 8192) cache.set(key, result)
  return result
}

/** 确保加载后查询 */
export async function lookupIpGeo(ip: string): Promise<string> {
  await ensureLoaded()
  return lookupIpGeoSync(ip)
}

export function isIpdbReady() {
  return loaded
}

export function preloadIpdb() {
  return ensureLoaded()
}

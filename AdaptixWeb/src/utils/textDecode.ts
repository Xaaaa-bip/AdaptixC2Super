/**
 * Best-effort decode for Windows text that may be GBK/GB18030/UTF-8/Latin1-mojibake.
 * Used by file preview with Auto + explicit encoding picker.
 */

export type TextEncodingId =
  | 'auto'
  | 'utf-8'
  | 'gbk'
  | 'gb18030'
  | 'big5'
  | 'shift_jis'
  | 'windows-1252'
  | 'latin1'

export const TEXT_ENCODING_OPTIONS: { id: TextEncodingId; label: string }[] = [
  { id: 'auto', label: '自动' },
  { id: 'utf-8', label: 'UTF-8' },
  { id: 'gbk', label: 'GBK (简体)' },
  { id: 'gb18030', label: 'GB18030' },
  { id: 'big5', label: 'Big5 (繁体)' },
  { id: 'shift_jis', label: 'Shift_JIS' },
  { id: 'windows-1252', label: 'Windows-1252' },
  { id: 'latin1', label: 'Latin-1 (原始字节)' },
]

function scoreText(s: string): number {
  if (!s) return -1e9
  let score = 0
  let bad = 0
  let cjk = 0
  let printable = 0
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i)
    if (c === 0xfffd) bad += 3
    else if (c < 9 || (c > 13 && c < 32)) bad += 2
    else printable++
    if (c >= 0x4e00 && c <= 0x9fff) cjk++
  }
  score += printable
  score += cjk * 4
  score -= bad * 8
  if (/Ã.|Â.|å.|æ.|ä.|é./.test(s)) score -= 20
  return score
}

export function bytesFromBinaryString(s: string): Uint8Array {
  const out = new Uint8Array(s.length)
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i) & 0xff
  return out
}

function tryDecode(bytes: Uint8Array, label: string): string | null {
  try {
    const dec = new TextDecoder(label as any, { fatal: false })
    return dec.decode(bytes)
  } catch {
    return null
  }
}

function labelForId(id: TextEncodingId): string | null {
  switch (id) {
    case 'utf-8': return 'utf-8'
    case 'gbk': return 'gbk'
    case 'gb18030': return 'gb18030'
    case 'big5': return 'big5'
    case 'shift_jis': return 'shift_jis'
    case 'windows-1252': return 'windows-1252'
    case 'latin1': return 'latin1' // handled specially
    default: return null
  }
}

/** Force-decode raw binary-ish string with a specific encoding. */
export function decodeWithEncoding(raw: string, encoding: TextEncodingId): string {
  if (!raw) return ''
  if (encoding === 'auto') {
    return decodeWindowsText(raw, { acp: 936 })
  }
  if (encoding === 'latin1') {
    // keep code units as-is (byte-preserving display)
    return raw
  }
  const label = labelForId(encoding)
  if (!label) return raw
  const bytes = bytesFromBinaryString(raw)
  return tryDecode(bytes, label) ?? raw
}

/** Decode raw binary-ish string (char codes 0-255) using agent code page hints. */
export function decodeWindowsText(
  raw: string,
  opts?: { acp?: number; oemcp?: number },
): string {
  if (!raw) return ''
  if (!/[-ÿ]/.test(raw) && !raw.includes('�')) return raw

  const bytes = bytesFromBinaryString(raw)
  const candidates: string[] = [raw]

  const labels: string[] = ['utf-8']
  const acp = Number(opts?.acp || 0)
  const oem = Number(opts?.oemcp || 0)
  const map: Record<number, string> = {
    936: 'gbk',
    54936: 'gb18030',
    950: 'big5',
    932: 'shift_jis',
    949: 'euc-kr',
    1252: 'windows-1252',
    65001: 'utf-8',
  }
  if (acp && map[acp]) labels.push(map[acp])
  if (oem && map[oem]) labels.push(map[oem])
  labels.push('gb18030', 'gbk', 'windows-1252')

  const seen = new Set<string>()
  for (const label of labels) {
    if (seen.has(label)) continue
    seen.add(label)
    const d = tryDecode(bytes, label)
    if (d != null) candidates.push(d)
  }

  let best = raw
  let bestScore = scoreText(raw)
  for (const c of candidates) {
    const sc = scoreText(c)
    if (sc > bestScore) {
      bestScore = sc
      best = c
    }
  }
  return best
}

/** Guess best encoding id among common ones (for UI default label). */
export function detectEncodingId(
  raw: string,
  opts?: { acp?: number; oemcp?: number },
): TextEncodingId {
  if (!raw) return 'utf-8'
  if (!/[-ÿ]/.test(raw) && !raw.includes('�')) return 'utf-8'

  const bytes = bytesFromBinaryString(raw)
  const trials: { id: TextEncodingId; label: string }[] = [
    { id: 'utf-8', label: 'utf-8' },
    { id: 'gbk', label: 'gbk' },
    { id: 'gb18030', label: 'gb18030' },
    { id: 'big5', label: 'big5' },
    { id: 'shift_jis', label: 'shift_jis' },
    { id: 'windows-1252', label: 'windows-1252' },
  ]
  const acp = Number(opts?.acp || 0)
  if (acp === 936) {
    // prefer chinese first for Chinese ACP
    trials.sort((a, b) => (a.id === 'gbk' ? -1 : b.id === 'gbk' ? 1 : 0))
  }

  let bestId: TextEncodingId = 'utf-8'
  let bestScore = -1e12
  for (const t of trials) {
    const d = tryDecode(bytes, t.label)
    if (d == null) continue
    const sc = scoreText(d)
    if (sc > bestScore) {
      bestScore = sc
      bestId = t.id
    }
  }
  // if nothing beats raw much, still return best trial
  return bestId
}
